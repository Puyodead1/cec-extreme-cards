/*
 * Parallel-port protection dongle emulator for build 170:
 *   "Foto Fantasy 5.370.92.170 pay vend (peabody)", Jul 2008 (Chuck E. Cheese Extreme Cards,
 *   1014v1, Evolis card printer).  LD_PRELOAD into /vend/bin/fbus/vjmnupns.
 * Port of the Sketch Book recomp's dongle.c (v8mnu22b).  Addresses below are for build 170
 * only; found in IDA (database ec170) as noted.
 *
 * Dongle = 93C46-style serial EEPROM, 64 x 16-bit words (dword_80E9DF0 = 6 address bits,
 * dword_80E9DF4 = 64 words), behind a login/challenge API.
 *   0x8087D10  read word (port, addr)                 [masked signature match vs v8mnu22b]
 *   0x8087E30  write word (port, addr, val)            [signature match]
 *   0x8087E90  API dispatcher (cmd, a, b, c), probes LPT 0x378/0x278/0x3BC, needs w63==0xA000
 *              (sub_8087FB0); the game calls it via sub_8068712 (usleep + dispatch)
 *   0x80880C0  cmd switch, jump table at 0x80E9E20:
 *              0 sub_80883D0 challenge reply   1 sub_8088150 login   2 sub_80884D0 unlock
 *              3 sub_8088580 read w62          4 sub_80885C0 read word (<56)
 *              5 sub_8088600 write unlock      6 sub_80886C0 write word   7 sub_8088710 decrement
 *              cmd>2 needs cmds 0 and 2 done (dword_80F0DB4/DB8), writes need cmd 5 (DBC).
 *   0x8088150  login(port, p1, p2, p3): w60/w61 -> challenge words
 *              word_80F0DD2 / word_80F0DD4 / word_80F0DD6, must equal p1/p2/p3.
 *              The real login in main is at 0x80525DE: sub_8068775(1, 0xEB36, 0xC9F4, 0x57E2)
 *              (the 0x805247F call with 0x57E2,0x812A,0xEB36 and the cmd 2/5 calls with
 *              mismatched words just before it are decoys).  Consistency check: cmd 0 must
 *              arrive with cmd number 0, which forces DD6 = 0x57E2; cmd 2 (AAD1,D2D1,CE40) and
 *              cmd 5 (2697,812A,7893) calls elsewhere in main decode to DD2=EB36 DD4=C9F4
 *              DD6=57E2 as well.  DD4 depends only on w61, DD6 only on DD2 and w61, so the
 *              seed is solved at load time by brute force through the game's own login.
 *   dword_80F0DAC  dongle API error code; failed trial logins leave 0x20000004 -> cleared.
 *
 * Checks in main (0x80516BA, main_init is inlined into it here):
 *   w14 != 0 (0x8053049, cmd 4, else init error 20 "dongle").
 *   w42..49: 16-char string, read big-endian bytes (0x8052D38 loop), CRC32 via sub_80D428C
 *     (init 0, table 0x80E6000) -> var_18.  We leave it empty: CRC = 0.
 *   Protection block (0x8052DD5..0x8052E1A):
 *     (w12<<16|w13) ^ CRC ^ dword_80E74AC(init 0xF6D36F22) == 0xCB4739DE, else
 *     sub_804C161(0x17) smashes the stack.  -> (w12<<16|w13) ^ CRC == 0x3D9456FC
 *   Main loop gate: dword_80E74B0 (init 0xB2567230) ^= 0x74362355 (0x8052340), ^= w11
 *     (0x8052CE6), ^= w10<<16 (0x8052E3F), ^= CRC (0x80530DC); at 0x80531C2 main calls
 *     (dword_80E74B0 ^ 0x08053C09 ^ 0x8BE2D272), so it must end as 0x8BE2D272 to reach the
 *     main loop sub_8053C09.  -> (w10<<16|w11) ^ CRC == 0x4D828317
 *   The game itself writes w3=0x348, w5=0x12C and w6:w7 = sub_805344A (0x8053144..0x8053199,
 *     0x8052D07) and calls that pointer later; we preload the same values.
 *   0x8051B37 issues cmd 0x18 (invalid -> random()) and dispatches through the 16-entry table
 *     at 0x80E87C0 by its nibbles; the original does the same with real hardware.
 *   hwconfig (vsystem.cfg) is only written back by sub_804B79B as a report; protect_blk is a
 *   config key listed in sub_8064C76; neither changes the checks.
 *
 * dict.db: string key derived from the (scrambled) w42..49 string by sub_80AA761 into the
 *   32-byte buffer byte_80F6B60, pointer in dword_80F4B40 (used by sub_80AA441, cipher
 *   sub_80AA3AF/sub_80AA3F8, 16-byte key).  We replace sub_80AA761: call the dictionary
 *   cache flush sub_80A9E33 (as the original does), install the 16 key bytes recovered
 *   by known plaintext (dictkey.py, camera.wav + "Press any Key to Start", "OUT OF FILM",
 *   "OUT OF PAPER"), point dword_80F4B40 at them.  Wrong key -> init error 25.
 *
 * No printer hardware:
 *   0x807890B prn_init(): stubbed; inited dword_80EF9D8=1, primary printer dword_80EF9DC=1,
 *     printers counted dword_80EFDAC=1, port table word_80E9AE4[1..8]=0.
 *   0x807A163 get_status(pr in eax) (called by prn_status sub_807A5A6, prn_advance ...):
 *     returns 0 = ready.
 *
 * Init error codes (main): 4/9 printer, 11 video, 12 comm, 14/21/22 camera, 20 dongle, 25 dict.
 *
 * Built -nostdlib (raw syscalls only).  Image persisted in /vend/dongle.img, log in
 * /vend/tmp/dongle.log.
 */
typedef unsigned short u16;
typedef unsigned int u32;

#define RD_FN       0x8087D10
#define WR_FN       0x8087E30
#define LOGIN_FN    0x8088150
#define DD2 (*(volatile u16 *)0x80F0DD2)
#define DD4 (*(volatile u16 *)0x80F0DD4)
#define DD6 (*(volatile u16 *)0x80F0DD6)
#define DONGLE_ERR  (*(volatile u32 *)0x80F0DAC)
#define VERSTR_AT   0x80D904F        /* "5.370.92.170" in .rodata */

static u16 ee[64];
static int logfd = -1;
static const char IMG[] = "/vend/dongle.img";
static const char LOG[] = "/vend/tmp/dongle.log";

static inline int sc3(int n, int a, int b, int c) {
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_getpid 20
#define SYS_mprotect 125

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void put(const char *s) { if (logfd >= 0) sc3(SYS_write, logfd, (int)s, slen(s)); }
static void puthex(u32 v, int digits) {
    char b[9]; int i;
    for (i = digits - 1; i >= 0; i--) { b[i] = "0123456789abcdef"[v & 15]; v >>= 4; }
    b[digits] = 0; put(b);
}

static int quiet_read(int a) { return a == 63 || a == 60 || a == 61 || a == 62; }

static void save(void) {
    int fd = sc3(SYS_open, (int)IMG, 01 | 0100 | 01000, 0644);  /* O_WRONLY|O_CREAT|O_TRUNC */
    if (fd >= 0) { sc3(SYS_write, fd, (int)ee, sizeof ee); sc3(SYS_close, fd, 0, 0); }
}

static int solving;

int __attribute__((cdecl)) dongle_read(int port, int addr) {
    (void)port;
    int v = ee[addr & 63];
    if (!solving && !quiet_read(addr & 63)) {
        put("["); puthex(sc3(SYS_getpid, 0, 0, 0), 4); put("] R w"); puthex(addr & 63, 2);
        put(" = "); puthex(v, 4); put(" from "); puthex((u32)__builtin_return_address(0), 8); put("\n");
    }
    return v;
}

int __attribute__((cdecl)) dongle_write(int port, int addr, int val) {
    (void)port;
    ee[addr & 63] = (u16)val;
    put("W w"); puthex(addr & 63, 2); put(" = "); puthex((u16)val, 4); put("\n");
    save();
    return 0;
}

/* game calls ioperm(port, 3, on) before touching the LPT port */
int ioperm(unsigned long from, unsigned long num, int on) { (void)from; (void)num; (void)on; return 0; }

static void hook(u32 at, void *to) {
    u32 page = at & ~0xFFFu;
    sc3(SYS_mprotect, page, 0x2000, 7);
    unsigned char *p = (unsigned char *)at;
    int rel = (int)((u32)to - (at + 5));
    p[0] = 0xE9;
    p[1] = rel; p[2] = rel >> 8; p[3] = rel >> 16; p[4] = rel >> 24;
}

/* prn_init() (sub_807890B): no printer hardware -> mark inited, one printer, primary = 1 */
#define PRN_INIT_FN 0x807890B
int __attribute__((cdecl)) prn_init_stub(void) {
    int i;
    *(volatile u32 *)0x80EF9D8 = 1;   /* prn inited */
    *(volatile u32 *)0x80EF9DC = 1;   /* primary printer */
    *(volatile u32 *)0x80EFDAC = 1;   /* printers counted/online */
    for (i = 1; i <= 8; i++) ((volatile u16 *)0x80E9AE4)[i] = 0;   /* no ports: skip driver polls */
    put("prn_init stubbed\n");
    return 0;
}

/* get_status(pr) (sub_807A163, pr in eax): 0 = ready */
#define PRN_STATUS_FN 0x807A163
int __attribute__((cdecl)) prn_status_stub(void) { return 0; }

/* dict.db string key, replaces sub_80AA761(keystring); only caller is main (0x8052DC0). */
#define DICTKEY_FN   0x80AA761
#define DICT_FLUSH   0x80A9E33
#define DICT_KEYBUF  0x80F6B60
#define DICT_KEYPTR  (*(volatile u32 *)0x80F4B40)
static const unsigned char dict_key[16] = {
    0x34, 0x88, 0x8d, 0x9a, 0xf7, 0x7c, 0xa0, 0x77, 0x45, 0x2a, 0xb7, 0xbc, 0x61, 0x17, 0x86, 0x7a };
int __attribute__((cdecl)) dictkey_stub(const char *s) {
    int i;
    (void)s;
    ((void (*)(void))DICT_FLUSH)();
    for (i = 0; i < 32; i++) ((volatile unsigned char *)DICT_KEYBUF)[i] = i < 16 ? dict_key[i] : 0;
    *(volatile unsigned char *)(DICT_KEYBUF + 32) = 0;   /* byte_80F6B80, cleared by the original */
    DICT_KEYPTR = DICT_KEYBUF;
    put("dict key installed\n");
    return 0;
}

typedef int (*login_t)(int, int, int, int);

static int solve_login(void) {
    login_t login = (login_t)LOGIN_FN;
    u32 e1, e0;
    solving = 1;
    for (e1 = 0; e1 < 0x10000; e1++) {
        ee[61] = e1; ee[60] = 0;
        login(0x378, 0, 0, 0);
        if (DD4 != 0xC9F4) continue;
        for (e0 = 0; e0 < 0x10000; e0++) {
            ee[60] = e0;
            login(0x378, 0, 0, 0);
            if (DD2 == 0xEB36) {
                if (DD6 == 0x57E2) { solving = 0; return 1; }
                break;  /* DD6 depends only on DD2 and w61 */
            }
        }
    }
    solving = 0;
    return 0;
}

static void defaults(void) {
    int i;
    for (i = 0; i < 64; i++) ee[i] = 0;
    ee[2] = 0x0001;
    ee[3] = 0x0348;
    ee[5] = 0x012C;
    ee[6] = 0x344A; ee[7] = 0x0805;   /* sub_805344A */
    ee[8] = 0x0100;
    ee[10] = 0x4D82; ee[11] = 0x8317; /* ^ CRC32("") == 0x4D828317 */
    ee[12] = 0x3D94; ee[13] = 0x56FC; /* ^ CRC32("") == 0x3D9456FC */
    ee[14] = 0x0001;
    ee[62] = 0x0001;
    ee[63] = 0xA000;
}

/* only act inside build 170 (LD_PRELOAD is inherited by uprn, uibfind, sh...): mincore()
 * fails with ENOMEM on unmapped pages, so check both pages are mapped before reading. */
#define SYS_mincore 218
static int mapped(u32 a) {
    unsigned char v[2];
    return sc3(SYS_mincore, a & ~0xFFFu, 0x2000, (int)v) == 0;
}
static int is_target(void) {
    static const char want[] = "5.370.92.170";
    int i;
    if (!mapped(VERSTR_AT) || !mapped(RD_FN)) return 0;
    for (i = 0; i < (int)sizeof want; i++) if (((const char *)VERSTR_AT)[i] != want[i]) return 0;
    return *(volatile u32 *)RD_FN == 0x56E58955u;   /* push ebp; mov ebp,esp; push esi */
}

__attribute__((constructor)) static void init(void) {
    logfd = sc3(SYS_open, (int)LOG, 01 | 0100 | 02000, 0644);  /* O_WRONLY|O_CREAT|O_APPEND */
    if (!is_target()) {   /* uprn, sh, mplayer...: stay silent and inactive */
        if (logfd >= 0) sc3(SYS_close, logfd, 0, 0);
        logfd = -1;
        return;
    }
    int fd = sc3(SYS_open, (int)IMG, 0, 0), loaded = 0;
    if (fd >= 0) { loaded = sc3(SYS_read, fd, (int)ee, sizeof ee) == sizeof ee; sc3(SYS_close, fd, 0, 0); }
    if (!loaded) defaults();
    hook(RD_FN, dongle_read);
    hook(WR_FN, dongle_write);
    hook(PRN_INIT_FN, prn_init_stub);
    hook(PRN_STATUS_FN, prn_status_stub);
    hook(DICTKEY_FN, dictkey_stub);
    if (!loaded) {
        if (solve_login()) { put("dongle: solved login seed w60="); puthex(ee[60], 4); put(" w61="); puthex(ee[61], 4); put("\n"); }
        else put("dongle: FAILED to solve login seed\n");
        DONGLE_ERR = 0;   /* clear the error code the trial logins left */
        save();
    }
    put(loaded ? "dongle: loaded /vend/dongle.img\n" : "dongle: fresh image\n");
}
