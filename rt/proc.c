/* Child processes. The game runs its helper programs (tgascale, voronoi, ...) with
 * vfork()+execvp() inside run_in_child(). Those helpers are recompiled to <name>.exe next to
 * their ELF in /vend/bin, so a guest exec becomes CreateProcess of the matching .exe. */
#include "rt.h"
#include "syms.h"
#include <stdarg.h>
#include <io.h>
#include <ctype.h>
#include <math.h>

#define MAXCHILD 64
static struct { int pid; HANDLE h; int code, proc; } kids[MAXCHILD];   /* code: exit code of a non-process child */
static CRITICAL_SECTION kcs;
static int kinit;

static void kids_init(void) {
    if (kinit) return;
    kinit = 1;
    InitializeCriticalSection(&kcs);
    SetEnvironmentVariableA("VEND_ROOT", rt_root);   /* children find the same data root */
}

static int file_exists(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

/* guest program name -> host .exe path; 0 ok */
static int resolve(const char *file, int search, char *out, size_t n) {
    static const char *dirs[] = { ".", "/vend/bin", "/bin", "/usr/bin", 0 };
    char g[512], h[MAX_PATH];
    int i;
    if (strchr(file, '/') || !search) {
        if (rt_map_path(file, h, sizeof h)) return -1;
        snprintf(out, n, "%s.exe", h);
        return file_exists(out) ? 0 : -1;
    }
    for (i = 0; dirs[i]; i++) {
        snprintf(g, sizeof g, "%s/%s", dirs[i], file);
        if (rt_map_path(g, h, sizeof h)) continue;
        snprintf(out, n, "%s.exe", h);
        if (file_exists(out)) return 0;
    }
    return -1;
}

/* MSVCRT command-line quoting */
static void append_arg(char *cl, size_t n, const char *a) {
    size_t L = strlen(cl);
    int quote = !*a || strpbrk(a, " \t\"") != 0, bs = 0;
    if (L && L + 1 < n) cl[L++] = ' ';
    if (quote && L + 1 < n) cl[L++] = '"';
    for (; *a && L + 4 < n; a++) {
        if (*a == '\\') { bs++; cl[L++] = '\\'; continue; }
        if (*a == '"') { while (bs-- >= 0 && L + 2 < n) cl[L++] = '\\'; }
        bs = 0;
        cl[L++] = *a;
    }
    if (quote) { while (bs-- > 0 && L + 2 < n) cl[L++] = '\\'; if (L + 1 < n) cl[L++] = '"'; }
    cl[L] = 0;
}

/* an inheritable handle for the child's stdio slot i: guest fd i, or NUL */
static HANDLE std_handle(int i) {
    HANDLE h = INVALID_HANDLE_VALUE, d = 0;
    SECURITY_ATTRIBUTES sa = { sizeof sa, 0, TRUE };
    if (rt_fds[i].kind == FD_HOST) h = (HANDLE)_get_osfhandle(rt_fds[i].hfd);
    else if (rt_fds[i].kind == FD_TTY) h = GetStdHandle(i == 0 ? STD_INPUT_HANDLE : i == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE && DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &d, 0, TRUE, DUPLICATE_SAME_ACCESS))
        return d;
    return CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, 0);
}

/* CreateProcess with the given (inheritable) stdio handles, which it closes. 0 ok */
static int create_proc(const char *exe, char *cl, HANDLE *std, PROCESS_INFORMATION *pi) {
    STARTUPINFOA si;
    BOOL ok;
    int i;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = std[0]; si.hStdOutput = std[1]; si.hStdError = std[2];
    fflush(0);
    ok = CreateProcessA(exe, cl, 0, 0, TRUE, 0, 0, 0, &si, pi);
    for (i = 0; i < 3; i++) if (std[i] && std[i] != INVALID_HANDLE_VALUE) CloseHandle(std[i]);
    if (!ok) { rt_logf("exec %s: CreateProcess failed (%lu)\n", exe, GetLastError()); return -1; }
    CloseHandle(pi->hThread);
    return 0;
}

static int kid_put(int pid, HANDLE h, int code, int proc);

/* guest argv (from argv[1]; argv[0] may be NULL) -> command line for exe */
static void build_cl(char *cl, size_t n, const char *exe, u32 gargv, int from) {
    int i;
    cl[0] = 0;
    append_arg(cl, n, exe);
    if (gargv && M32(gargv))
        for (i = from; M32(gargv + 4 * i); i++) append_arg(cl, n, (const char *)(uintptr_t)M32(gargv + 4 * i));
}

/* /vend/bin/inplace (a shell script): `inplace file cmd args...` = run cmd with stdout to a temp file,
   then copy that over file. Done synchronously; returns a pseudo pid that is already over. */
static int inplace(u32 gargv) {
    static LONG next = 700001;
    char exe[MAX_PATH], cl[8192], tmp[MAX_PATH], dst[MAX_PATH];
    const char *file = (const char *)(uintptr_t)M32(gargv + 4), *prog = file ? (const char *)(uintptr_t)M32(gargv + 8) : 0;
    HANDLE std[3];
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa = { sizeof sa, 0, TRUE };
    DWORD ec = 1;
    int pid = (int)InterlockedExchangeAdd(&next, 2);
    if (!prog || rt_map_path(file, dst, sizeof dst) || resolve(prog, 1, exe, sizeof exe)) {
        rt_logf("inplace %s %s: no recompiled program\n", file ? file : "?", prog ? prog : "?");
        return -1;
    }
    build_cl(cl, sizeof cl, exe, gargv, 3);
    snprintf(tmp, sizeof tmp, "%s\\tmp\\inplace%lu_%d.tmp", rt_root, GetCurrentProcessId(), pid);
    std[0] = std_handle(0);   /* the game points stdin at file */
    std[1] = CreateFileA(tmp, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0, 0);
    std[2] = std_handle(2);
    if (!create_proc(exe, cl, std, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &ec);
        CloseHandle(pi.hProcess);
        if (ec == 0 && !CopyFileA(tmp, dst, FALSE)) ec = 1;
    }
    DeleteFileA(tmp);
    rt_logf("inplace %s: %s -> %lu\n", file, cl, ec);
    kid_put(pid, CreateEventA(0, TRUE, TRUE, 0), (int)ec, 0);
    return pid;
}

/* run a recompiled tool to completion: stdin/stdout from/to guest paths (0 = the guest's own fds) */
static DWORD run_tool(const char *prog, const char *const *args, const char *in, const char *out) {
    char exe[MAX_PATH], cl[8192], h[MAX_PATH];
    HANDLE std[3];
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa = { sizeof sa, 0, TRUE };
    DWORD ec = 1;
    int i;
    if (resolve(prog, 1, exe, sizeof exe)) { rt_logf("%s: no recompiled program\n", prog); return 127; }
    cl[0] = 0;
    append_arg(cl, sizeof cl, exe);
    for (i = 0; args[i]; i++) append_arg(cl, sizeof cl, args[i]);
    std[0] = in && !rt_map_path(in, h, sizeof h)
        ? CreateFileA(h, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, 0) : std_handle(0);
    std[1] = out && !rt_map_path(out, h, sizeof h)
        ? CreateFileA(h, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, 0) : std_handle(1);
    std[2] = std_handle(2);
    if (!create_proc(exe, cl, std, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &ec);
        CloseHandle(pi.hProcess);
    }
    if (ec) rt_logf("  %s -> %lu\n", cl, ec);
    return ec;
}

/* `key=value` from an ini-ish file (guest path); 0 if absent */
static int spec_int(const char *gpath, const char *key, int def) {
    char h[MAX_PATH], line[256];
    size_t k = strlen(key);
    int v = def;
    FILE *f;
    if (rt_map_path(gpath, h, sizeof h) || !(f = fopen(h, "r"))) return def;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, k) && line[k] == '=' && atoi(line + k + 1) > 0) v = atoi(line + k + 1);
    fclose(f);
    return v;
}

/* /vend/pics/product/cec-badge.sh PIC p1|p2: the CEC badge card filter (a shell script on the booth), same steps:
   greyscale, crop the 416x416 centre, mask it round, scale to the circle size, paste onto the card, scale to print width */
static DWORD cec_badge(const char *pic, const char *mode) {
    static const char base[] = "/vend/pics/product/card_base.tga", mask[] = "/vend/pics/product/crop-photo.tga";
    char t1[64], t2[64], d[3][24], xc[24], yc[24], hdir[MAX_PATH];
    int n = (int)GetCurrentProcessId() % 100000;
    int diam = spec_int("/vend/fx.spec", "diameter", 422);
    int x = spec_int("/vend/fx.spec", "xCenter", 253), y = spec_int("/vend/fx.spec", "yCenter", 319);
    DWORD ec;
    snprintf(t1, sizeof t1, "/vend/tmp/%d-1.tga", n);
    snprintf(t2, sizeof t2, "/vend/tmp/%d-2.tga", n);
    snprintf(d[0], sizeof d[0], "-w%d", diam); snprintf(d[1], sizeof d[1], "-h%d", diam);
    snprintf(xc, sizeof xc, "-xc%d", x); snprintf(yc, sizeof yc, "-yc%d", y);
    {
        const char *a1[] = { "-m", 0 }, *a2[] = { "-x112", "-y26", "-w416", "-h416", t1, 0 };
        const char *a3[] = { "-xc208", "-yc208", "-B", mask, t2, 0 }, *a4[] = { d[0], d[1], t1, 0 };
        const char *a5[] = { xc, yc, "-C", base, t2, 0 };
        const char *a6[] = { "-A", mode && !strcmp(mode, "p1") ? "-w1016" : "-w1015", "-I", pic, 0 };
        ec = run_tool("tgaconv", a1, pic, t1);
        if (!ec) ec = run_tool("tgacrop", a2, 0, t2);
        if (!ec) ec = run_tool("tgapaste", a3, 0, t1);
        if (!ec) ec = run_tool("tgascale", a4, 0, t2);
        if (!ec) ec = run_tool("tgapaste", a5, 0, pic);
        if (!ec) run_tool("tgascale", a6, 0, 0);   /* the script ignores this one's status */
    }
    if (!rt_map_path(t1, hdir, sizeof hdir)) DeleteFileA(hdir);
    if (!rt_map_path(t2, hdir, sizeof hdir)) DeleteFileA(hdir);
    if (!ec && !rt_map_path("/vend/save", hdir, sizeof hdir) && (GetFileAttributesA(hdir) & FILE_ATTRIBUTE_DIRECTORY)
        && GetFileAttributesA(hdir) != INVALID_FILE_ATTRIBUTES) {   /* "save to thumb drive" */
        char src[MAX_PATH], dst[MAX_PATH];
        SYSTEMTIME st;
        GetLocalTime(&st);
        if (!rt_map_path(pic, src, sizeof src)) {
            snprintf(dst, sizeof dst, "%s/%02d-%02d-%02d_%02d.%02d.tga", hdir, st.wYear % 100, st.wMonth, st.wDay, st.wHour, st.wMinute);
            CopyFileA(src, dst, FALSE);
        }
    }
    rt_logf("cec-badge %s %s -> %lu\n", pic, mode ? mode : "", ec);
    return ec;
}

/* shell scripts the game runs (`sh script args`), done natively; -1 if unknown */
static int builtin_script(u32 gargv) {
    static LONG next = 600001;
    const char *script = M32(gargv + 4) ? (const char *)(uintptr_t)M32(gargv + 4) : 0, *b;
    const char *a1, *a2;
    DWORD ec;
    int pid;
    if (!script) return -1;
    b = strrchr(script, '/'); b = b ? b + 1 : script;
    if (strcmp(b, "cec-badge.sh")) return -1;
    a1 = (const char *)(uintptr_t)M32(gargv + 8);
    a2 = a1 ? (const char *)(uintptr_t)M32(gargv + 12) : 0;
    if (!a1) return -1;
    ec = cec_badge(a1, a2);
    pid = (int)InterlockedExchangeAdd(&next, 2);
    kid_put(pid, CreateEventA(0, TRUE, TRUE, 0), (int)ec, 0);
    return pid;
}

/* /vend/bin/uibfind (a Perl script): prints the UIB box's serial port and exits 0. Our box is /dev/uib (uib.c). */
static int uibfind(void) {
    static LONG next = 500001;
    HANDLE out = std_handle(1);
    DWORD n;
    int pid = (int)InterlockedExchangeAdd(&next, 2);
    WriteFile(out, "/dev/uib\n", 9, &n, 0);
    CloseHandle(out);
    kid_put(pid, CreateEventA(0, TRUE, TRUE, 0), 0, 0);
    return pid;
}

/* returns pid, or -1 (errno ENOENT) */
static int spawn(const char *file, u32 gargv, int search) {
    char exe[MAX_PATH], cl[8192];
    PROCESS_INFORMATION pi;
    HANDLE std[3];
    const char *base = strrchr(file, '/');
    kids_init();
    if (!strcmp(base ? base + 1 : file, "inplace") && gargv) return inplace(gargv);
    if (!strcmp(base ? base + 1 : file, "uibfind")) return uibfind();
    if (!strcmp(base ? base + 1 : file, "sh") && gargv) {
        int pid = builtin_script(gargv);
        if (pid > 0) return pid;
    }
    if (resolve(file, search, exe, sizeof exe)) {
        build_cl(cl, sizeof cl, file, gargv, 1);
        rt_logf("exec %s: no recompiled program (%s)\n", file, cl);
        return -1;
    }
    build_cl(cl, sizeof cl, exe, gargv, 1);
    /* the child's stdio is the guest's fds 0-2 (the game redirects them to files around a spawn) */
    std[0] = std_handle(0); std[1] = std_handle(1); std[2] = std_handle(2);
    if (create_proc(exe, cl, std, &pi)) return -1;
    rt_logf("exec %s -> pid %lu\n", cl, pi.dwProcessId);
    if (kid_put((int)pi.dwProcessId, pi.hProcess, 0, 1)) CloseHandle(pi.hProcess);
    return (int)pi.dwProcessId;
}

static int kid_put(int pid, HANDLE h, int code, int proc) {
    int i;
    EnterCriticalSection(&kcs);
    for (i = 0; i < MAXCHILD; i++)
        if (!kids[i].h) { kids[i].pid = pid; kids[i].h = h; kids[i].code = code; kids[i].proc = proc; break; }
    LeaveCriticalSection(&kcs);
    return i == MAXCHILD ? -1 : 0;
}

/* a non-process child (snd.c): h is signalled when it is over; the table closes h when reaped. 0 ok */
int rt_kid_add(int pid, HANDLE h) {
    kids_init();
    return kid_put(pid, h, 0, 0);
}

/* wait for pid (-1: any). returns pid, 0 (WNOHANG, still running) or -1 (no such child) */
static int reap(CPU *c, int pid, int nohang, int *code) {
    for (;;) {
        int i, found = 0;
        EnterCriticalSection(&kcs);
        for (i = 0; i < MAXCHILD; i++) {
            DWORD ec;
            if (!kids[i].h || (pid > 0 && kids[i].pid != pid)) continue;
            found = 1;
            if (WaitForSingleObject(kids[i].h, 0) == WAIT_OBJECT_0) {
                int p = kids[i].pid;
                if (!kids[i].proc || !GetExitCodeProcess(kids[i].h, &ec)) ec = (DWORD)kids[i].code;   /* sound/video/inplace child: an event */
                CloseHandle(kids[i].h);
                kids[i].h = 0;
                LeaveCriticalSection(&kcs);
                *code = (int)ec;
                return p;
            }
        }
        LeaveCriticalSection(&kcs);
        if (!found) return -1;
        if (nohang) return 0;
        if (rt_wait_ms(c, 5)) return -2;   /* interrupted by a signal */
    }
}

/* Linux wait status from a Windows exit code */
static u32 wstatus(int code) {
    if ((u32)code >= 0xC0000000u && (u32)code < 0xD0000000u) return 11;   /* crashed (NTSTATUS): "killed by SIGSEGV" */
    return ((u32)code & 0xff) << 8;
}

DECL_IMP(waitpid) {
    int pid = (s32)ARG(0), code = 0, r;
    u32 st = ARG(1), opt = ARG(2);
    if (!kinit) ERR(10);
    r = reap(c, pid, opt & 1, &code);
    if (r == -1) ERR(10);
    if (r == -2) ERR(4);
    if (r > 0 && st) M32(st) = wstatus(code);
    RET(r);
}
DECL_IMP(wait) {
    int code = 0, r;
    u32 st = ARG(0);
    if (!kinit) ERR(10);
    r = reap(c, -1, 0, &code);
    if (r == -1) ERR(10);
    if (r == -2) ERR(4);
    if (st) M32(st) = wstatus(code);
    RET(r);
}

static const char *plt_name(u32 a) {
    int i;
    for (i = 0; i < guest_got_n; i++)
        if (guest_got[i].stub == a) return guest_got[i].name;
    return 0;
}

/* run_in_child(mode, fn, a, b): mode 0 = run fn(a,b) in a child and wait (returns exit code),
 * 1 = run it in the background (returns 0), 2 = run fn(a,b) in this process (an exec replaces us) */
static void run_child(CPU *c, int mode, u32 fn, u32 a, u32 b);
void ovr_run_child(CPU *c) { run_child(c, (int)ARG(0), ARG(1), ARG(2), ARG(3)); }
/* the same with gcc regparm(3): mode=eax, fn=edx, a=ecx, b on the stack (build 170) */
void ovr_run_child_rp(CPU *c) { run_child(c, (int)c->r[0].d, c->r[2].d, c->r[1].d, ARG(0)); }
static void run_child(CPU *c, int mode, u32 fn, u32 a, u32 b) {
    int pid, code = 0, r;
    const char *name = plt_name(fn);
    int search;
    if (!name || (strcmp(name, "execvp") && strcmp(name, "execv"))) {
        rt_logf("run_in_child: unsupported child function %08x (%s)\n", fn, name ? name : "?");
        RET(mode == 1 ? 0 : -1);
    }
    search = !strcmp(name, "execvp");
    pid = spawn((const char *)(uintptr_t)a, b, search);
    if (mode == 1) RET(0);
    if (pid < 0) {
        if (mode == 2) RET(-1);
        RET(255);   /* child: exec failed -> _exit(-1) */
    }
    while ((r = reap(c, pid, 0, &code)) == -2) rt_deliver(c);
    if (mode == 2) rt_exit(c, code);
    if ((u32)code >= 0xC0000000u && (u32)code < 0xD0000000u) {
        rt_logf("child %d crashed (%08x)\n", pid, (u32)code);
        RET(-2);
    }
    if (code) rt_logf("child %d (%s) exit %d\n", pid, (const char *)(uintptr_t)a, code);
    RET(code & 0xff);
}
DECL_IMP(__gmon_start__) { RET_VOID(); }

/* strtok_r (not in MinGW.org's CRT) */
char *rt_strtok_r(char *s, const char *d, char **save) {
    if (!s) s = *save;
    s += strspn(s, d);
    if (!*s) { *save = s; return 0; }
    *save = s + strcspn(s, d);
    if (**save) *(*save)++ = 0;
    return s;
}

/* kill() for a child in the table: video players stop, real processes are terminated. 0 = was ours */
int rt_kid_kill(int pid, int sig) {
    extern int rt_video_kill(int pid);
    int i, r = -1;
    if (!kinit) return -1;
    if (rt_video_kill(pid) == 0) return 0;
    EnterCriticalSection(&kcs);
    for (i = 0; i < MAXCHILD; i++)
        if (kids[i].h && kids[i].pid == pid) {
            r = 0;
            if (sig && sig != 18) TerminateProcess(kids[i].h, 128 + sig);
        }
    LeaveCriticalSection(&kcs);
    return r;
}

/* `/bin/sh -c <cmd>` as a child: returns its pid, or -1.
 * mplayer lines play in the game window (video.c); a plain `prog args...` runs prog's recompiled .exe;
 * anything needing a real shell (pipes, redirection) is not supported. */
int rt_shell(const char *cmd) {
    extern int rt_video_shell(const char *cmd);
    static int nlog;
    char buf[2048], *tok, *save = 0;
    u32 argv[64];
    int n = 0, pid;
    kids_init();
    if ((pid = rt_video_shell(cmd)) != -2) return pid;
    snprintf(buf, sizeof buf, "%s", cmd);
    if ((tok = strpbrk(buf, "<>|;&`$"))) *tok = 0;   /* drop redirections and the rest */
    for (tok = rt_strtok_r(buf, " \t", &save); tok && n < 63; tok = rt_strtok_r(0, " \t", &save)) argv[n++] = (u32)(uintptr_t)tok;
    argv[n] = 0;
    if (!n) return -1;
    pid = spawn((const char *)(uintptr_t)argv[0], (u32)(uintptr_t)argv, 1);
    if (pid < 0 && nlog++ < 5) rt_logf("sh -c \"%s\": not run\n", cmd);
    return pid;
}

/* spawn_sh(stdin_file, stdout_file, stderr_file, fmt, va_list): fork + `sh -c` with redirections; returns the pid.
   The redirections are dropped (the game points them at /dev/null or log files). */
void ovr_shell_spawn(CPU *c) {
    char cmd[10000];
    const char *fmt = ARGS(3);
    if (ARG(4)) vsnprintf(cmd, sizeof cmd, fmt, (va_list)ARGP(4));   /* guest va_list = pointer to the stacked args, as here */
    else snprintf(cmd, sizeof cmd, "%s", fmt);
    { int pid = rt_shell(cmd); if (pid < 0) { c->err = 11; RET(-1); } RET(pid); }
}

/* 152 movie player play(cmd): fork `sh -c cmd`, then wait for it while servicing input; a key or
   S_mov_stop() kills it (SIGTERM, then SIGKILL after 500 ms). Same loop as the original. */
void ovr_shell_movie(CPU *c) {
    const char *cmd = ARGS(0);
    u32 zero = 0, one = 1, two = 2;
    int pid, code;
    pid = rt_shell(cmd);
    rt_call_guest(c, S_mov_enter, 0, 0);
    rt_call_guest(c, S_mov_grab, 1, &zero);
    if (pid > 0) {
        for (;;) {
            int r = reap(c, pid, 1, &code), cut = 0;
            if (r == pid || r == -1) break;
            rt_call_guest(c, S_mov_busy, 0, 0);
            if (c->r[0].d) rt_call_guest(c, S_mov_service, 0, 0);
            rt_call_guest(c, S_mov_key, 1, &two);
            if (c->r[0].d) cut = 1;
            if (!cut) { rt_call_guest(c, S_mov_stop, 0, 0); cut = c->r[0].d != 0; }
            if (cut) {
                rt_kid_kill(pid, 15);
                while (reap(c, pid, 0, &code) == -2) rt_deliver(c);
                break;
            }
            if (rt_wait_ms(c, 10)) rt_deliver(c);
        }
    }
    rt_call_guest(c, S_mov_grab, 1, &one);
    rt_call_guest(c, S_mov_leave, 0, 0);
    RET(c->r[0].d);
}

/* qsort with a guest comparator: merge sort (stable, O(n log n) guest calls) */
static void msort(CPU *c, u8 *b, u8 *tmp, u32 n, u32 sz, u32 cmp) {
    u32 h, i, j, k;
    if (n < 2) return;
    h = n / 2;
    msort(c, b, tmp, h, sz, cmp);
    msort(c, b + h * sz, tmp, n - h, sz, cmp);
    i = 0; j = h; k = 0;
    while (i < h && j < n) {
        u32 args[2] = { (u32)(uintptr_t)(b + i * sz), (u32)(uintptr_t)(b + j * sz) };
        rt_call_guest(c, cmp, 2, args);
        if ((s32)c->r[0].d <= 0) memcpy(tmp + k++ * sz, b + i++ * sz, sz);
        else memcpy(tmp + k++ * sz, b + j++ * sz, sz);
    }
    while (i < h) memcpy(tmp + k++ * sz, b + i++ * sz, sz);
    while (j < n) memcpy(tmp + k++ * sz, b + j++ * sz, sz);
    memcpy(b, tmp, (size_t)n * sz);
}
DECL_IMP(qsort) {
    u8 *b = (u8 *)ARGP(0);
    u32 n = ARG(1), sz = ARG(2), cmp = ARG(3);
    u8 *tmp = (u8 *)malloc((size_t)n * sz + 1);
    msort(c, b, tmp, n, sz, cmp);
    free(tmp);
    RET_VOID();
}

/* ---------------- odds and ends the helper programs import */
DECL_IMP(fileno) {
    FILE *f = (FILE *)ARGP(0);
    int h = _fileno(f), i;
    if (f == stdin) RET(0);
    if (f == stdout) RET(1);
    if (f == stderr) RET(2);
    for (i = 3; i < MAXFD; i++)
        if (rt_fds[i].kind == FD_HOST && rt_fds[i].hfd == h) RET(i);
    i = rt_fd_alloc(3);
    if (i < 0) ERR(24);
    rt_fds[i].kind = FD_HOST; rt_fds[i].hfd = h; rt_fds[i].flags = 2;
    RET(i);
}
DECL_IMP(tmpfile) {
    static volatile LONG seq;
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s\\tmp\\tf%lu_%ld.tmp", rt_root, GetCurrentProcessId(), InterlockedIncrement(&seq));
    RET(fopen(p, "w+bD"));
}
DECL_IMP(scanf) {
    extern int rt_fwd_scanf(FILE *f, const char *str, const char *fmt0, const u32 *a);
    RET(rt_fwd_scanf(stdin, 0, ARGS(0), (const u32 *)(uintptr_t)(c->r[4].d + 8)));
}
static double argd(CPU *c, int i) { double d; memcpy(&d, (void *)(uintptr_t)(c->r[4].d + 4 + 4 * i), 8); return d; }
DECL_IMP(pow) { FPUSH((long double)pow(argd(c, 0), argd(c, 2))); RET_VOID(); }
DECL_IMP(sqrt) { FPUSH((long double)sqrt(argd(c, 0))); RET_VOID(); }
DECL_IMP(sin) { FPUSH((long double)sin(argd(c, 0))); RET_VOID(); }
DECL_IMP(floor) { FPUSH((long double)floor(argd(c, 0))); RET_VOID(); }
DECL_IMP(ceil) { FPUSH((long double)ceil(argd(c, 0))); RET_VOID(); }
DECL_IMP(cos) { FPUSH((long double)cos(argd(c, 0))); RET_VOID(); }
