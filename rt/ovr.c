/* Replacements for guest functions that talk to hardware we don't have:
 * the parallel-port EEPROM dongle, the card printer, the dict.db key derived from the dongle, and the ISA I/O card.
 * Guest addresses come from syms.h (per build, see BUILDS in recomp.py). */
#include "rt.h"
#include "syms.h"

static u16 ee[64];
static int solving;
static const char IMG[] = "dongle.img";   /* relative to /vend */

static void ee_save(void) {
    FILE *f = fopen(IMG, "wb");
    if (f) { fwrite(ee, sizeof ee, 1, f); fclose(f); }
}

/* sub_806B4B0(port, addr) -> word */
void ovr_dongle_read(CPU *c) {
    int a = ARG(1) & 63;
    if (!solving && a < 60) rt_logf("dongle R w%02x = %04x\n", a, ee[a]);
    RET(ee[a]);
}

/* sub_806B5D0(port, addr, val) */
void ovr_dongle_write(CPU *c) {
    int a = ARG(1) & 63;
    ee[a] = (u16)ARG(2);
    rt_logf("dongle W w%02x = %04x\n", a, ee[a]);
    ee_save();
    RET(0);
}

/* prn_init(): no printer hardware */
void ovr_prn_init(CPU *c) {
    int i;
    M32(S_prn_inited) = 1;
    M32(S_prn_primary) = 1;
    M32(S_prn_online) = 1;
    /* S_prn_types[i] is printer i's type (nonzero = installed); the real prn_init clears it for
       printers that fail to come up. Only printer 1 exists here. It must stay nonzero, or every
       ready-wait on it fails ("wait error/timeout"), it gets removed, and the next print reports
       "out of paper" and exits. */
    if (!M16(S_prn_types + 2)) M16(S_prn_types + 2) = 1;
    for (i = 2; i <= 8; i++) M16(S_prn_types + 2 * i) = 0;
    RET(0);
}

/* prn_status(pr): 0 = ready */
void ovr_prn_status(CPU *c) { RET(0); }

/* uprn print-server stand-in: the game normally forks /vend/bin/uprn and talks
 * "cmd args\n" / "<int> ...\n" over pipes. There is no printer, so answer locally. */
void ovr_uprn_open(CPU *c) { if (S_uprn_open_flag) M32(S_uprn_open_flag) = 1; RET(0); }
static int vsend_cmd(const char *cmd, u32 va, char *reply, int n);
void ovr_vsend_cmd(CPU *c) { RET(vsend_cmd(ARGS(0), ARG(2), (char *)ARGP(3), (int)ARG(4))); }
/* regparm(3) (build 170): cmd=eax, fmt=edx, va=ecx, reply and n on the stack */
void ovr_vsend_cmd_rp(CPU *c) { RET(vsend_cmd((const char *)(uintptr_t)c->r[0].d, c->r[1].d, (char *)ARGP(0), (int)ARG(1))); }
static int vsend_cmd(const char *cmd, u32 va, char *reply, int n) {
    int v = 0;
    if (!strcmp(cmd, "apiversion")) v = 1002;   /* <=1002: no spool, no prn_errno queries */
    if (!strcmp(cmd, "print_generate")) {
        /* "%d %s %s": render <src> into the printer-ready <dst>; prn_generate() then renames dst over src.
           No printer to render for, so dst is a plain copy of src. */
        char hs[MAX_PATH], hd[MAX_PATH];
        const char *src = (const char *)(uintptr_t)M32(va + 4), *dst = (const char *)(uintptr_t)M32(va + 8);
        if (rt_map_path(src, hs, sizeof hs) || rt_map_path(dst, hd, sizeof hd) || !CopyFileA(hs, hd, FALSE)) {
            rt_logf("uprn: print_generate %s -> %s failed\n", src, dst);
            v = -1;
        } else {
            extern void rt_save_print(const char *host_src);
            rt_save_print(hs);   /* the finished print goes to prints\ next to the exe */
        }
    }
    if (reply && n > 0) snprintf(reply, n, "%d;", v);
    rt_logf("uprn: %s -> %d\n", cmd, v);
    return v;
}

/* dict.db key (recovered by known plaintext; same for builds 152 and 170) */
static const u8 dict_key[16] = { 0x34, 0x88, 0x8d, 0x9a, 0xf7, 0x7c, 0xa0, 0x77, 0x45, 0x2a, 0xb7, 0xbc, 0x61, 0x17, 0x86, 0x7a };
void ovr_dict_key(CPU *c) {
    int i;
    rt_call_guest(c, S_dict_flush, 0, 0);   /* flush dictionary cache, as the original does */
    for (i = 0; i <= 32; i++) M8(S_dict_keybuf + i) = i < 16 ? dict_key[i] : 0;
    M32(S_dict_keyptr) = S_dict_keybuf;
    rt_logf("dict key installed\n");
    RET(0);
}

#define LOGIN_FN S_login_fn
#define LW(i) M16(S_login_words + 2 * (i))
static int solve_login(CPU *c) {
    u32 e2, e0;
    u32 args[4] = { 0x378, 0, 0, 0 };
    solving = 1;
    for (e2 = 0; e2 < 0x10000; e2++) {
        ee[61] = (u16)e2; ee[60] = 0;
        rt_call_guest(c, LOGIN_FN, 4, args);
        if (LW(1) != 0xC9F4) continue;
        for (e0 = 0; e0 < 0x10000; e0++) {
            ee[60] = (u16)e0;
            rt_call_guest(c, LOGIN_FN, 4, args);
            if (LW(0) == 0xEB36) {
                if (LW(2) == 0x57E2) { solving = 0; return 1; }
                break;
            }
        }
    }
    solving = 0;
    return 0;
}

static void defaults(void) {
    memset(ee, 0, sizeof ee);
    ee[2] = 0x0001; ee[3] = 0x0348; ee[5] = 0x012C;
    ee[6] = (u16)S_ee_fnptr; ee[7] = (u16)(S_ee_fnptr >> 16);   /* the game writes these itself too */
    ee[8] = 0x0100;
    /* w42..49 (dongle string) empty -> CRC 0, so these are the check constants themselves:
       main gate (w10<<16|w11) ^ CRC = 0x4D828317, protection block (w12<<16|w13) ^ CRC = 0x3D9456FC */
    ee[10] = 0x4D82; ee[11] = 0x8317;
    ee[12] = 0x3D94; ee[13] = 0x56FC;
    ee[14] = 0x0001; ee[62] = 0x0001; ee[63] = 0xA000;
}

void ovr_init(CPU *c) {
    FILE *f = fopen(IMG, "rb");
    int loaded = 0;
    if (f) { loaded = fread(ee, sizeof ee, 1, f) == 1; fclose(f); }
    if (!loaded) {
        defaults();
        if (solve_login(c)) rt_logf("dongle: solved login seed w60=%04x w61=%04x\n", ee[60], ee[61]);
        else rt_logf("dongle: FAILED to solve login seed\n");
        M32(S_dongle_err) = 0;   /* trial logins leave an error code behind */
        ee_save();
    }
    rt_logf(loaded ? "dongle: loaded image\n" : "dongle: fresh image\n");
}

/* dio_setup(base): this game's old tree sets dioBase=0x300 (ISA "UIB card", iopl + in/out on 0x300-0x305).
   No card here: clear dioBase so the engine uses the serial UIB box, and fail as the original does for base 0. */
void ovr_dio_setup(CPU *c) {
    rt_logf("dio: dioBase=%x ignored, using the UIB box\n", ARG(0));
    if (S_dio_base) M32(S_dio_base) = 0;
    RET(-1);
}
