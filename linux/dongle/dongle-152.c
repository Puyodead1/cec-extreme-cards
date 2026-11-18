/*
 * Parallel-port protection dongle emulator for build 152:
 *   "Foto Fantasy 5.370.92.152 pay vend (sherman)", Jul 12 2007 (vjmnupns-152.elf),
 *   Chuck E. Cheese "Extreme Cards" (1014v1), Evolis card printer.
 * Ported from the Sketch Book recomp's dongle.c (v8mnu22b).  Same design: the dongle is a
 * 64 x 16-bit serial EEPROM (93C46-style) on LPT behind a login/challenge API; this LD_PRELOAD
 * patches the two word-level primitives so the game's own API logic runs unchanged against an
 * in-memory image, and makes ioperm() succeed.  Built -nostdlib (raw syscalls only).
 *
 * Addresses (build 152; found in IDA database ec152):
 *   0x807F930  read word (port, addr)         } masked byte-signature match vs v8mnu22b
 *   0x807FA50  write word (port, addr, val)    } (0x806B4B0 / 0x806B5D0 there)
 *   0x807FAB0  dongle API dispatcher (cmd, a, b, c); only caller is wrapper sub_8064C61,
 *              which usleeps then calls it.  cmd is taken as a BYTE (movzx edx, byte [arg_4]
 *              in sub_807FCE0), jump table 0x80DC7A0:
 *                0 sub_807FFF0 challenge response   1 sub_807FD70 login
 *                2 sub_80800F0 unlock (read perm)   3 sub_80801A0 read w62
 *                4 sub_80801E0 read word (<56)      5 sub_8080220 write permission
 *                6 sub_80802E0 write word (<56)     7 sub_8080330 decrement word
 *              cmds >7 (0x18, 0x24, 0x46 are used as decoys) just return random().
 *   0x807FD70  login (port, p1, p2, p3).  Derives word_80E52D2/D4/D6 from w60, w61 (w62 also
 *              read) and accepts iff they equal p1/p2/p3.  On failure sets the API error
 *              global dword_80E52AC = 0x20000004 (cleared after solving below).
 *              main_init calls the login through sub_8064CD6(x, 0xEB36, 0xC9F4, 0x57E2) with
 *              cmd = (byte)("LPT1"[3] << 13) - 0x60000 + 1 = 1, then immediately answers the
 *              challenge with cmd 0 (cmd = swap(D6^0x172C)^0xCE40 must be 0 -> D6 = 0x57E2);
 *              result ^ 0xF2DB must equal 0x16980000, which works out to D2 = 0xEB36,
 *              D4 = 0xC9F4.  Otherwise it calls sub_80654F4, which smashes the stack and jumps
 *              into the fatal-exit routine with no file/line -> "Exit (10): Missing/Incorrect
 *              protection block" reported as v4cksum.c:241.  (Same values as v8mnu22b.)
 *              The other login call (0x57E2, 0x812A, 0xEB36) is a decoy expected to fail.
 *   0x80533F1  fatal exit (code, line, file); line==0 && file==0 -> code forced to 10,
 *              "v4cksum.c", 241.  sub_804E402(23) (bad w12:w13) and sub_80654F4 (bad login)
 *              both get there with garbage args, so both show up as error 10.
 *   0x804F239  main_init.  main (0x804C5DE) calls ((main_init() ^ 0x804D852) ^ 0x8BE2D272)(),
 *              so main_init must return 0x8BE2D272 -> main loop at 0x804D852.
 *              Return value = dword_80D9650 (init 0xB2567230) ^ 0x74362355 ^ w11 ^ (w10<<16)
 *              ^ CRC32(w42..49 string)    => w10:w11 = 0x4D828317 with an empty string.
 *              Protection block: dword_80D9654 (init 0xF6D36F22) ^ CRC ^ (w12<<16|w13) must be
 *              0xCB4739DE, else sub_804E402(23)  => w12:w13 = 0x3D9456FC.
 *              CRC is sub_80C10F4 (table CRC, init 0): empty string -> 0.
 *              w14 must be nonzero (else init error 20 after the dict check).
 *              w2: read and nibble-folded into an index into the function table 0x80DAC60;
 *              fold^2 == 2 (w2 == 0) selects sub_80654F4 (error 10).  w2 = 1 -> no-op entry.
 *              w3: timeout seconds (main_init rewrites 0x348).  w5: rewritten 0x12C.
 *              w6:w7: function pointer, called as fn("v4.c",1960) from sub_804C7B1; main_init
 *              itself writes w7 = hi(sub_804BA98), w6 = lo(sub_804BA98) -> 0x0804:0xBA98.
 *   0x8072FDF  prn_init  (found via "Printer %d successfully initialized"; globals at the same
 *              relative offsets as v8mnu22b: inited 0x80DC284, primary 0x80DC288,
 *              online count 0x80DC290, port words 0x80DC29E[1..8])
 *   0x8073347  prn_status(pr): if !inited prn_init(); return sub_8072B1C(pr ? pr : primary).
 *              Same shape as v8mnu22b's sub_8067CCB.
 *   0x809C3D3  dict key from string (sketch sub_8083C33); calls flush 0x809B55A; key buffer
 *              byte_80DE7C0 (32 bytes), key pointer global dword_80DE710.
 *   0x80D9301  "(#)Foto Fantasy 5.370.92.152 ..." id string, used by the load guard (pages probed
 *              with mincore first, so the shim stays inert in children: sh, mplayer, uprn).
 *   0x80B3162  DIO ("UIB card", ISA) setup, called from setUpSystem with dioBase (0x80F3888).
 *              Stubbed so the engine uses the serial UIB box (uibemu.py) instead; see below.
 *
 * Constants agree with build 170 (same main gate 0x4D828317, protect block 0x3D9456FC, login
 * 0xEB36/0xC9F4/0x57E2).
 *
 * EEPROM layout:
 *   w2 = 0x0001, w3 = 0x0348, w5 = 0x012C, w6:w7 = 0xBA98:0x0804, w8 = 0x0100,
 *   w10:w11 = 0x4D82:0x8317, w12:w13 = 0x3D94:0x56FC, w14 = 0x0001, w42..49 = "" (zeros),
 *   w60/w61 = login seed (solved at load), w62 = 0x0001, w63 = 0xA000 (presence signature,
 *   checked by sub_807FBD0 before every API call).
 * Writes are persisted to /vend/dongle.img; log in /vend/tmp/dongle.log.
 *
 * dict.db key: hashed from the w42..49 string in the real dongle; recovered instead by known
 * plaintext with sketch's dictkey.py ("camera.wav" from entry 294, plus hints
 *   0:4:0="OUT OF FILM"  0:5:0="OUT OF PAPER"  0:10:1="ff00s001.wav"  0:2:0="INSERT %$ TO START").
 * (0:3:0 turns out to be "Press any Key to Start", capital K.)  All sections decode cleanly.
 */
typedef unsigned short u16;
typedef unsigned int u32;

#define RD_FN    0x807F930
#define WR_FN    0x807FA50
#define LOGIN_FN 0x807FD70
#define D2 (*(volatile u16 *)0x80E52D2)
#define D4 (*(volatile u16 *)0x80E52D4)
#define D6 (*(volatile u16 *)0x80E52D6)
#define API_ERR (*(volatile u32 *)0x80E52AC)
#define ID_STR ((const char *)0x80D9301)

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
#define SYS_mincore 218

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

/* no printer hardware: prn_init() -> mark inited / online, no ports */
#define PRN_INIT_FN 0x8072FDF
int __attribute__((cdecl)) prn_init_stub(void) {
    int i;
    *(volatile u32 *)0x80DC284 = 1;   /* prn inited */
    *(volatile u32 *)0x80DC288 = 1;   /* primary printer */
    *(volatile u32 *)0x80DC290 = 1;   /* printers online */
    for (i = 1; i <= 8; i++) ((volatile u16 *)0x80DC29E)[i] = 0;   /* no ports: skip driver polls */
    put("prn_init stubbed\n");
    return 0;
}

/* prn_status(pr): 0 = ready */
#define PRN_STATUS_FN 0x8073347
int __attribute__((cdecl)) prn_status_stub(int pr) { (void)pr; return 0; }

/* dict.db string key (see header).  Replaces sub_809C3D3(keystring); only caller is main_init. */
#define DICTKEY_FN 0x809C3D3
#define DICT_FLUSH_FN 0x809B55A
#define DICT_KEYBUF 0x80DE7C0
#define DICT_KEYPTR (*(volatile u32 *)0x80DE710)
static const unsigned char dict_key[16] = {
    0x34, 0x88, 0x8d, 0x9a, 0xf7, 0x7c, 0xa0, 0x77, 0x45, 0x2a, 0xb7, 0xbc, 0x61, 0x17, 0x86, 0x7a };
int __attribute__((cdecl)) dictkey_stub(const char *s) {
    int i;
    (void)s;
    ((void (*)(void))DICT_FLUSH_FN)();             /* flush dictionary cache, as original */
    for (i = 0; i < 32; i++) ((volatile unsigned char *)DICT_KEYBUF)[i] = i < 16 ? dict_key[i] : 0;
    DICT_KEYPTR = DICT_KEYBUF;
    put("dict key installed\n");
    return 0;
}

/* I/O board.  This tree's vsystem.cfg has dioBase=0x300 (with hwconfig IO:UIB = the ISA
 * "UIB card" driver, "@(#)UIB card" at 0x80E0320), so setUpSystem (sub_804E759) calls
 * sub_80B3162(dioBase) and then drives ports 0x300..0x305 with iopl()+in/out
 * (sub_80C34F9 / sub_80C34D4), which SIGSEGVs under WSL (iopl -> ENOSYS).  The engine only
 * uses the serial UIB box (/dev/uib, what uibemu.py emulates) when dioBase == 0
 * (sub_804BD4A).  sub_80B3162 is called only from there: replace it with "no DIO card":
 * clear dioBase (dword_80F3888) and return -1, as the original does for base 0. */
#define DIO_SETUP_FN 0x80B3162
#define DIO_BASE (*(volatile u32 *)0x80F3888)
int __attribute__((cdecl)) dio_setup_stub(int base) {
    put("dio: dioBase="); puthex(base, 4); put(" ignored, using UIB box\n");
    DIO_BASE = 0;
    return -1;
}

typedef int (*login_t)(int, int, int, int);

static int solve_login(void) {
    login_t login = (login_t)LOGIN_FN;
    u32 e1, e0;
    solving = 1;
    for (e1 = 0; e1 < 0x10000; e1++) {
        ee[61] = e1; ee[60] = 0;
        login(0x378, 0, 0, 0);
        if (D4 != 0xC9F4) continue;           /* D4 depends only on w61 */
        for (e0 = 0; e0 < 0x10000; e0++) {
            ee[60] = e0;
            login(0x378, 0, 0, 0);
            if (D2 == 0xEB36) {
                if (D6 == 0x57E2) { solving = 0; return 1; }
                break;  /* D6 depends only on D2 and w61 */
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
    ee[6] = 0xBA98; ee[7] = 0x0804;   /* sub_804BA98 */
    ee[8] = 0x0100;
    ee[10] = 0x4D82; ee[11] = 0x8317;
    ee[12] = 0x3D94; ee[13] = 0x56FC;
    ee[14] = 0x0001;
    ee[62] = 0x0001;
    ee[63] = 0xA000;
}

/* nonzero if [a, a+4096) is mapped.  The preload is inherited by every child the game
 * spawns (sh, helpers), where these addresses may be unmapped: probe before reading. */
static int mapped(u32 a) {
    unsigned char vec[2];
    return sc3(SYS_mincore, a & ~0xFFFu, 0x1000, (int)vec) == 0;
}

static int is_152(void) {
    static const char want[] = "(#)Foto Fantasy 5.370.92.152 ";
    int i;
    if (!mapped(RD_FN) || !mapped((u32)ID_STR) || !mapped((u32)ID_STR + 0x40)) return 0;
    if (*(volatile u32 *)RD_FN != 0x56E58955u) return 0;   /* push ebp; mov ebp,esp; push esi */
    for (i = 0; want[i]; i++) if (ID_STR[i] != want[i]) return 0;
    return 1;
}

__attribute__((constructor)) static void init(void) {
    logfd = sc3(SYS_open, (int)LOG, 01 | 0100 | 02000, 0644);  /* O_WRONLY|O_CREAT|O_APPEND */
    /* only act inside the build-152 game binary (preload is inherited by children) */
    if (!is_152()) { put("dongle-152: not build 152, inactive\n"); return; }
    int fd = sc3(SYS_open, (int)IMG, 0, 0), loaded = 0;
    if (fd >= 0) { loaded = sc3(SYS_read, fd, (int)ee, sizeof ee) == sizeof ee; sc3(SYS_close, fd, 0, 0); }
    if (!loaded) defaults();
    hook(RD_FN, dongle_read);
    hook(WR_FN, dongle_write);
    hook(PRN_INIT_FN, prn_init_stub);
    hook(PRN_STATUS_FN, prn_status_stub);
    hook(DICTKEY_FN, dictkey_stub);
    hook(DIO_SETUP_FN, dio_setup_stub);
    if (!loaded) {
        if (solve_login()) { put("dongle: solved login seed w60="); puthex(ee[60], 4); put(" w61="); puthex(ee[61], 4); put("\n"); }
        else put("dongle: FAILED to solve login seed\n");
        API_ERR = 0;   /* clear the error code the trial logins left */
        save();
    }
    put(loaded ? "dongle: loaded /vend/dongle.img\n" : "dongle: fresh image\n");
}
