/* Per-build guest addresses the runtime needs (globals and helper functions of the game).
 * The values are emitted by recomp.py into gen<build>/syms.c from the BUILDS table there;
 * a name that is unknown for a build is 0. recomp.py reads this file for the list of names,
 * so every name here must be `extern const u32 S_<name>;` on its own line. */
#pragma once
#include "cpu.h"

/* dongle (93C46 EEPROM behind a login API) */
extern const u32 S_login_fn;        /* login(port, p1, p2, p3) */
extern const u32 S_login_words;     /* u16[3] the login derives from w60/w61 (expect EB36 C9F4 57E2) */
extern const u32 S_dongle_err;      /* API error global, left nonzero by trial logins */
extern const u32 S_ee_fnptr;        /* function pointer the game stores in w6:w7 */

/* printers */
extern const u32 S_prn_inited;
extern const u32 S_prn_primary;
extern const u32 S_prn_online;
extern const u32 S_prn_types;       /* u16[9]: [i] nonzero = printer i installed */
extern const u32 S_uprn_open_flag;  /* set by uprn_server_open() once the server is up */

/* dict.db */
extern const u32 S_dict_flush;      /* flush the dictionary cache */
extern const u32 S_dict_keybuf;     /* 32-byte key buffer */
extern const u32 S_dict_keyptr;     /* pointer to the active key */

/* I/O: ISA "UIB card" base address (nonzero = use the card instead of the serial UIB box) */
extern const u32 S_dio_base;

/* camera / video driver */
extern const u32 S_cam_live;        /* driver's global "live" flag */
extern const u32 S_vt1_drvobj;      /* returns the vt1 driver object */
extern const u32 S_vt1_fillops;     /* fills the vt1 driver ops (obj) */
extern const u32 S_vt1_register;    /* registers a driver (obj, a, b, c) */
extern const u32 S_cam_adjstore;    /* int[7] picture adjustments */
extern const u32 S_vt1_setup;       /* 170: setup(a, b, c) fills the ops inline (no fillops); ovr_vt1_init wraps it */
extern const u32 S_vt1_flag;        /* 170: global the vt1 init wrapper sets to 1 */

/* shell commands: 152's movie player forks `sh -c <cmd>` itself and runs this loop around it */
extern const u32 S_mov_enter;       /* () before playing */
extern const u32 S_mov_grab;        /* (on) input grab around the movie */
extern const u32 S_mov_busy;        /* () nonzero: S_mov_service() wants a call */
extern const u32 S_mov_service;     /* () */
extern const u32 S_mov_key;         /* (2) nonzero: a key cuts the movie short */
extern const u32 S_mov_stop;        /* () nonzero: something else cuts it short */
extern const u32 S_mov_leave;       /* () after playing; its value is returned */

/* sound */
extern const u32 S_snd_on;
extern const u32 S_snd_hook;
extern const u32 S_snd_fxpid;
extern const u32 S_snd_muspid;
extern const u32 S_snd_wait;        /* wait for the effect child (ms); 0 = none (170): wait here */

/* malloc wrapper: return addresses of the two screen-banner allocations */
extern const u32 S_banner_ret1;
extern const u32 S_banner_ret2;

/* build identity */
extern const char guest_build[];    /* "152", "170", or "" for the helper tools */
extern const char guest_path[];     /* argv[0] the game expects, e.g. /vend/bin/fbus/vjmnupns */
extern const u32 guest_text_lo, guest_text_hi;
