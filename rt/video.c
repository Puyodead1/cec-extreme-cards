/* Attract videos. The game plays its .wmv files with `/bin/sh -c "mplayer -quiet -slave <file> < /dev/null"`
 * as a child and waits for it (or kills it, to cut the video short). Here a play request is a DirectShow
 * graph rendered into a child window over our game window (video and sound), registered as a child
 * process like the sound players in snd.c: an odd pseudo-pid (never a Windows pid) whose handle is
 * signalled when playback ends, so waitpid()/kill() keep working. */
#include "rt.h"

int rt_kid_add(int pid, HANDLE h);   /* proc.c */
HWND x11_hwnd(void);                 /* x11.c */
void x11_view_rect(int w, int h, RECT *v);
char *rt_strtok_r(char *s, const char *d, char **save);   /* proc.c */
__declspec(dllimport) HRESULT WINAPI CoInitializeEx(void *, DWORD);
__declspec(dllimport) void WINAPI CoUninitialize(void);
__declspec(dllimport) HRESULT WINAPI CoCreateInstance(const GUID *, void *, DWORD, const GUID *, void **);

/* DirectShow interfaces by vtable slot (MinGW.org's headers don't declare them) */
static const GUID CLSID_FilterGraph_ = { 0xe436ebb3, 0x524f, 0x11ce, { 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID IID_IGraphBuilder_ = { 0x56a868a9, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID IID_IMediaControl_ = { 0x56a868b1, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID IID_IMediaEvent_ = { 0x56a868b6, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID IID_IVideoWindow_ = { 0x56a868b4, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
#define VT(o, i) ((*(void ***)(o))[i])
#define REL(o) do { if (o) ((ULONG (WINAPI *)(void *))VT(o, 2))(o); } while (0)
typedef HRESULT (WINAPI *QIf)(void *, const GUID *, void **);
typedef HRESULT (WINAPI *RenderFilef)(void *, const WCHAR *, const WCHAR *);   /* IGraphBuilder 13 */
typedef HRESULT (WINAPI *Voidf)(void *);                                       /* IMediaControl 7 Run, 9 Stop */
typedef HRESULT (WINAPI *GetEventf)(void *, long *, LONG_PTR *, LONG_PTR *, long);   /* IMediaEvent 8 */
typedef HRESULT (WINAPI *FreeEventf)(void *, long, LONG_PTR, LONG_PTR);             /* IMediaEvent 12 */
typedef HRESULT (WINAPI *Longf)(void *, long);                                 /* IVideoWindow put_* */
typedef HRESULT (WINAPI *Ptrf)(void *, LONG_PTR);                              /* put_Owner 29, put_MessageDrain 31 */
typedef HRESULT (WINAPI *Posf)(void *, long, long, long, long);                /* SetWindowPosition 39 */
#define EC_COMPLETE 1
#define EC_USERABORT 2
#define EC_ERRORABORT 3

typedef struct {
    int pid;
    HANDLE done;          /* manual-reset: playback over */
    volatile LONG stop;
    char path[MAX_PATH];
} Vid;

#define MAXVID 4
static Vid *vids[MAXVID];
static CRITICAL_SECTION vcs;
static int vinit;
static LONG vnext = 800001;   /* odd pseudo-pids, below the sound players' 900001.. */

/* All videos play on one long-lived worker thread, in request order, and the filter graph is kept between
   plays (one per file, up to NGRAPH): replaying a file (the attract loop's rotation.wmv, over and over) rewinds
   it instead of building a new graph. Every graph built and released leaked ~3.5 MB, 3 mutexes and 4 sections inside the
   system's WMV/DirectShow filters, about 140 MB an hour of attract. */
static Vid *vq[MAXVID];          /* pending, oldest first */
static HANDLE vq_ev;             /* auto-reset: something queued */
static const GUID IID_IMediaPosition_ = { 0x56a868b2, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
typedef HRESULT (WINAPI *PutPosf)(void *, double);   /* IMediaPosition 8 put_CurrentPosition */

/* the active graph, and parked ones per file (worker thread only): build 152 alternates rotation.wmv in attract
   with eov.wmv after each game */
static void *gb, *mc, *me, *vw, *mp;
static char gpath[MAX_PATH];
static HWND gowner;
#define NGRAPH 4
static struct { void *gb, *mc, *me, *vw, *mp; char path[MAX_PATH]; HWND owner; DWORD used; } parked[NGRAPH];

static void graph_free(void) {
    if (vw) { ((Longf)VT(vw, 19))(vw, 0); ((Ptrf)VT(vw, 29))(vw, 0); }   /* put_Visible(false), put_Owner(0) */
    REL(mp); REL(vw); REL(me); REL(mc); REL(gb);
    mp = vw = me = mc = gb = 0;
    gpath[0] = 0;
}

/* park the active graph (evicting the least recently used parked one if all slots are taken) */
static void graph_park(void) {
    int i, slot = 0;
    if (!gb) return;
    for (i = 0; i < NGRAPH; i++) {
        if (!parked[i].gb) { slot = i; break; }
        if (parked[i].used < parked[slot].used) slot = i;
    }
    if (parked[slot].gb) {   /* evict */
        void *a = gb, *b = mc, *e = me, *w = vw, *p = mp;
        gb = parked[slot].gb; mc = parked[slot].mc; me = parked[slot].me; vw = parked[slot].vw; mp = parked[slot].mp;
        graph_free();
        gb = a; mc = b; me = e; vw = w; mp = p;
    }
    parked[slot].gb = gb; parked[slot].mc = mc; parked[slot].me = me; parked[slot].vw = vw; parked[slot].mp = mp;
    snprintf(parked[slot].path, MAX_PATH, "%s", gpath);
    parked[slot].owner = gowner; parked[slot].used = GetTickCount();
    gb = mc = me = vw = mp = 0; gpath[0] = 0;
}

/* a graph for path, rewound to the start: 0 ok */
static int graph_get(const char *path, HWND owner) {
    WCHAR wp[MAX_PATH];
    int i;
    if (!(gb && !strcmp(gpath, path) && owner == gowner)) {
        graph_park();
        for (i = 0; i < NGRAPH; i++)
            if (parked[i].gb && !strcmp(parked[i].path, path) && parked[i].owner == owner) {
                gb = parked[i].gb; mc = parked[i].mc; me = parked[i].me; vw = parked[i].vw; mp = parked[i].mp;
                snprintf(gpath, sizeof gpath, "%s", path); gowner = owner;
                parked[i].gb = 0;
                break;
            }
    }
    if (gb) {
        if (mp && SUCCEEDED(((PutPosf)VT(mp, 8))(mp, 0.0))) return 0;
        graph_free();   /* can't rewind: build afresh */
    }
    MultiByteToWideChar(CP_ACP, 0, path, -1, wp, MAX_PATH);
    if (FAILED(CoCreateInstance(&CLSID_FilterGraph_, 0, 1 /* CLSCTX_INPROC_SERVER */, &IID_IGraphBuilder_, &gb))) {
        gb = 0; rt_logf("video: no DirectShow\n"); return -1;
    }
    if (FAILED(((RenderFilef)VT(gb, 13))(gb, wp, 0))) { rt_logf("video: cannot render %s\n", path); graph_free(); return -1; }
    ((QIf)VT(gb, 0))(gb, &IID_IMediaControl_, &mc);
    ((QIf)VT(gb, 0))(gb, &IID_IMediaEvent_, &me);
    ((QIf)VT(gb, 0))(gb, &IID_IVideoWindow_, &vw);
    ((QIf)VT(gb, 0))(gb, &IID_IMediaPosition_, &mp);
    if (!mc || !me) { graph_free(); return -1; }
    if (vw && owner) {
        SetWindowLongA(owner, GWL_STYLE, GetWindowLongA(owner, GWL_STYLE) | WS_CLIPCHILDREN);   /* our painting stays off the video */
        ((Ptrf)VT(vw, 29))(vw, (LONG_PTR)owner);                         /* put_Owner */
        ((Longf)VT(vw, 9))(vw, WS_CHILD | WS_CLIPSIBLINGS);               /* put_WindowStyle */
        ((Ptrf)VT(vw, 31))(vw, (LONG_PTR)owner);                         /* put_MessageDrain: keys still reach the game */
    }
    snprintf(gpath, sizeof gpath, "%s", path);
    gowner = owner;
    return 0;
}

static void vid_play(Vid *v) {
    HWND owner = x11_hwnd();
    RECT last = { -1, -1, -1, -1 };
    long code; LONG_PTR p1, p2;
    DWORD t0;
    int i;
    if (v->stop || graph_get(v->path, owner)) goto out;   /* killed before it got its turn, or no graph */
    while (((GetEventf)VT(me, 8))(me, &code, &p1, &p2, 0) == S_OK) ((FreeEventf)VT(me, 12))(me, code, p1, p2);   /* last play's */
    if (vw && owner) ((Longf)VT(vw, 19))(vw, -1);   /* put_Visible(OATRUE) */
    if (FAILED(((Voidf)VT(mc, 7))(mc))) { rt_logf("video: cannot play %s\n", v->path); graph_free(); goto out; }
    rt_logf("video: playing %s (pid %d)\n", v->path, v->pid);
    t0 = GetTickCount();
    for (;;) {
        int over = 0;
        if (vw && owner) {   /* follow the game window's size (fullscreen, resizing): same 4:3 box as the game picture */
            RECT rc, r;
            GetClientRect(owner, &rc);
            x11_view_rect(rc.right, rc.bottom, &r);
            if (memcmp(&r, &last, sizeof r)) {
                ((Posf)VT(vw, 39))(vw, r.left, r.top, r.right - r.left, r.bottom - r.top);
                last = r;
            }
        }
        while (((GetEventf)VT(me, 8))(me, &code, &p1, &p2, 0) == S_OK) {
            ((FreeEventf)VT(me, 12))(me, code, p1, p2);
            if (code == EC_COMPLETE || code == EC_USERABORT || code == EC_ERRORABORT) over = 1;
        }
        if (over || v->stop) break;
        Sleep(20);
    }
    ((Voidf)VT(mc, 9))(mc);   /* Stop */
    if (vw) ((Longf)VT(vw, 19))(vw, 0);   /* put_Visible(false): the graph stays for the next play */
    rt_logf("video: %s %s after %lu ms\n", v->path, v->stop ? "stopped" : "finished", GetTickCount() - t0);
out:
    if (owner) InvalidateRect(owner, 0, FALSE);
    EnterCriticalSection(&vcs);
    for (i = 0; i < MAXVID; i++) if (vids[i] == v) vids[i] = 0;
    LeaveCriticalSection(&vcs);
    SetEvent(v->done);   /* the child table owns its own duplicate of the handle */
    CloseHandle(v->done);
    free(v);
}

static DWORD WINAPI vid_worker(void *arg) {
    (void)arg;
    CoInitializeEx(0, 0 /* COINIT_MULTITHREADED */);
    for (;;) {
        Vid *v;
        int i;
        WaitForSingleObject(vq_ev, INFINITE);
        for (;;) {
            EnterCriticalSection(&vcs);
            v = vq[0];
            for (i = 0; i < MAXVID - 1; i++) vq[i] = vq[i + 1];
            vq[MAXVID - 1] = 0;
            LeaveCriticalSection(&vcs);
            if (!v) break;
            vid_play(v);
        }
    }
    return 0;
}

/* start playing a guest path; returns the pseudo-pid or -1 */
int rt_video_play(const char *gpath) {
    Vid *v;
    HANDLE dup;
    int i;
    if (!vinit) {
        vinit = 1; InitializeCriticalSection(&vcs);
        vq_ev = CreateEventA(0, FALSE, FALSE, 0);
        CloseHandle(CreateThread(0, 0, vid_worker, 0, 0, 0));
    }
    if (!(v = (Vid *)calloc(1, sizeof *v))) return -1;
    if (rt_map_path(gpath, v->path, sizeof v->path) || GetFileAttributesA(v->path) == INVALID_FILE_ATTRIBUTES) {
        rt_logf("video: %s not found\n", gpath); free(v); return -1;
    }
    v->pid = (int)InterlockedExchangeAdd(&vnext, 2);
    v->done = CreateEventA(0, TRUE, FALSE, 0);
    DuplicateHandle(GetCurrentProcess(), v->done, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS);
    EnterCriticalSection(&vcs);
    for (i = 0; i < MAXVID; i++) if (!vids[i]) { vids[i] = v; break; }
    LeaveCriticalSection(&vcs);
    if (i == MAXVID) { rt_logf("video: too many videos\n"); CloseHandle(dup); CloseHandle(v->done); free(v); return -1; }
    rt_kid_add(v->pid, dup);
    i = v->pid;
    EnterCriticalSection(&vcs);
    { int k; for (k = 0; k < MAXVID; k++) if (!vq[k]) { vq[k] = v; break; } }   /* vq can't be fuller than vids */
    LeaveCriticalSection(&vcs);
    SetEvent(vq_ev);
    return i;
}

/* kill(pid, sig) for a video pseudo-child: 0 if it was one */
int rt_video_kill(int pid) {
    int i, r = -1;
    if (!vinit) return -1;
    EnterCriticalSection(&vcs);
    for (i = 0; i < MAXVID; i++) if (vids[i] && vids[i]->pid == pid) { vids[i]->stop = 1; r = 0; }
    LeaveCriticalSection(&vcs);
    return r;
}

/* `mplayer [options] file` (the part of a /bin/sh -c command line before any redirection):
   play the file; returns pseudo-pid, -1 failure, -2 not an mplayer command */
int rt_video_shell(const char *cmd) {
    char buf[1024], *tok, *file = 0, *save = 0;
    snprintf(buf, sizeof buf, "%s", cmd);
    if ((tok = strpbrk(buf, "<>|;&"))) *tok = 0;
    tok = rt_strtok_r(buf, " \t", &save);
    if (!tok) return -2;
    { const char *b = strrchr(tok, '/'); if (strcmp(b ? b + 1 : tok, "mplayer")) return -2; }
    while ((tok = rt_strtok_r(0, " \t", &save))) {
        if (tok[0] == '-') {   /* options that take a value */
            static const char *withval[] = { "-vo", "-ao", "-geometry", "-x", "-y", "-vf", "-af", "-ss", "-loop", "-volume", "-display", 0 };
            int k;
            for (k = 0; withval[k]; k++) if (!strcmp(tok, withval[k])) { rt_strtok_r(0, " \t", &save); break; }
            continue;
        }
        file = tok;
    }
    if (!file) return -1;
    if ((file[0] == '"' || file[0] == '\'') && strlen(file) > 1) { file++; file[strlen(file) - 1] = 0; }
    return rt_video_play(file);
}
