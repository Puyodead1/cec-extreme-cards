# Extreme Cards port

Chuck E. Cheese "Extreme Cards" is a Foto Fantasy photo-ID badge kiosk (product `1014v1`, Evolis card printer). This folder is for getting both known builds of the game running without the cabinet: first under WSL, then as a Win32 static recompilation, following earlier recomps of the same engine family (Sketch Book).

The game binaries, data and disc images are **not** part of this folder.

## The two builds

| | Old | New |
|---|---|---|
| Source | Old disc image: Norton Ghost image `1.gho`. `/vend` was pulled out with Ghost Explorer, which drops every symlink. | New disc image: Fedora Core 5 install DVD with a kickstart (`isolinux/ks.cfg`) that adds the vendor RPMs `core`, `core_control`, `core_util_files`, `1014v1`, … |
| Engine | `/vend/bin/fbus/vjmnupns`, Foto Fantasy 5.370.92.**152** "sherman", built Jul 12 2007 | same path, 5.370.92.**170** "peabody", Jul 2008 |
| Here as | `bin/vjmnupns-152.elf` | `bin/vjmnupns-170.elf` |
| `hwconfig` | `D:FB P:U:Evol CA:TOK IO:UIB` | `D:BL P:U:EvolisBadge` (after `setlinks 1014v1`) |

Both are stripped 32-bit Linux ELFs (V4.14 codebase, "(C) 1999"), using X11 + XFree86-DGA/VidMode, libfbus (capture card), WinDriver `/dev/windrvr`, a UIB coin/button box on USB serial, and a parallel-port EEPROM dongle. Startup chain on the cabinet: `inittab` → `/vend/sys/rc.start` → `foto` → `rc.vend` → `/vend/bin/vend` (shell) → `/vend/bin/fbus/vjmnupns`.

## Running under WSL (`linux/`)

All scripts run as root in WSL (`wsl -u root -e bash script.sh`). PowerShell mangles inline quoting, so put commands in a file.

| Script | What |
|---|---|
| `mkrootfs.sh` | Builds a minimal FC5 rootfs from the new DVD's RPMs. `resolve.py` resolves the package closure from `Fedora/repodata/primary.xml.gz`. |
| `mkvend.sh` | Builds `vend-new/` (vendor RPMs plus their `%post` steps) and `vend-old/` (the Ghost tree with its symlinks put back), plus a `.pristine` copy of each. |
| `run_vend.sh old\|new [secs]` | Resets the tree from its pristine copy, bind-mounts it on `/vend` in the rootfs, and runs the engine with `-vt1` (plain XImage video). |
| `uibemu.py` | UIB box emulator (from the Sketch Book recomp), on a pty bound at `rootfs/dev/uib`. |
| `xgrab.py` | Converts an Xvfb `-fbdir` screen file to PNG. |
| `dongle/` | `LD_PRELOAD` dongle emulator per build (`dongle-152.c`, `dongle-170.c`); also stubs the printer and sets the dict.db key. `run_vend.sh` loads the right one by default. |
| `x11relay.py` | Relays the X socket into the chroot when WSLg only offers abstract sockets. |
| `exportvend.sh <src> <dst>` | Copies a WSL `/vend` tree to a Windows folder: file symlinks become copies, directory symlinks are listed in `.junctions`. |

Typical setup:

```bash
export ISO=/path/to/new-disc.iso
export OLD_VEND=/path/to/old-disc/vend   # /vend extracted from the Ghost image
export ROOTFS=$HOME/ec/rootfs EC_WORK=$HOME/ec
bash mkrootfs.sh && bash mkvend.sh both
bash dongle/build.sh          # builds dongle-*.so into $ROOTFS/lib
bash run_vend.sh old          # attract appears after ~50 s
```

Notes:
- Use a **16-bit** X server. The engine's depth probe only accepts 16 bpp TrueColor (finding from the Sketch Book recomp).
- `exiterr.dat` counts failed starts. After two in a row the engine parks on the blue OUT OF FILM screen ("Too many reboots in a row"), which is why every run starts from the pristine tree.
- `mkdict` (run by `setlinks`) reports "has expired!": the vendor tools carry a date check.

## Win32 recompilation

Both builds run natively on Windows: coin and buttons on the keyboard, the webcam as the booth camera, DirectShow for the attract videos, and prints saved as PNGs under `prints/`. MinGW (`C:\MinGW\bin`) and Python are needed.

| Step | Command |
|---|---|
| Export each `/vend` tree from WSL into `vend-152/`, `vend-170/` | `linux/exportvend.sh`, then `fixlinks.ps1` (recreates the directory symlinks as junctions) |
| Translate the engine to C | `python recomp.py bin\vjmnupns-<b>.elf gen<b>` |
| Build `vjmnupns-<b>.exe` | `build.ps1 [152] [170]` |
| Build the helper programs (`tgaconv`, `tgamerge`, …) into `vend-<b>\bin\*.exe` | `buildtool.ps1 [-Builds 152,170] [tools…]` |
| Package a ready-to-run release zip | `release.ps1` (see below) |

Running: start `vjmnupns-152.exe` or `vjmnupns-170.exe` from this folder. **C** = coin, **1-8** = UIB buttons (hold Ctrl to type those keys into the game instead), **F11** / Alt+Enter = fullscreen. Logs go to `vend-<b>\tmp\rt.log`. If 170 shows "Too many reboots", delete `vend-170\exiterr.dat`.

How it fits together:
- `recomp.py` turns the ELF into C over a CPU context. Per-build addresses of overridden functions (`ovr_*`) and data (`S_*`) are in its `BUILDS` table. Build 170 is compiled with regparm(3) in places, and those overrides have `_rp` variants.
- `rt/` is the runtime. It provides libc, X11 as a GDI window (`x11.c`), the UIB box (`uib.c`), the dongle, the printer (`ovr.c`, saving to `prints/`), the camera (`cam.c`), sound, and video (`video.c`, used for the game's `mplayer` calls).
- **Child programs:** an exec runs `<name>.exe` next to the original. Its stdio is the guest's fds 0-2, which the game redirects to files around each spawn. Shell and Perl scripts the game needs are built in: `inplace`, `uibfind` (prints `/dev/uib`), and `pics/product/cec-badge.sh` (the card filter: greyscale, round crop, paste onto `card_base.tga`). Other `sh -c` commands are logged as "not run".
- **Camera:** the game paints the photo screen's `background` (`theme/liveframe.tga`) over the live picture. On the booth, the capture card's overlay showed through its hole. Here, pixels inside the camera window that the game draws black or white (`0xFFDF`) show the webcam.

Environment variables:
- **Camera:** `RT_CAMERA=<image>` uses a still picture. `RT_CAMERA_DEVICE=<name part>` picks a specific camera. `RT_CAMERA_MIRROR`, `RT_CAMERA_FLIP` mirror or flip the picture.
- **Window:** `VEND_FULLSCREEN=1` starts fullscreen.
- **Logging:** `RT_TOOLLOG=1` makes helper programs log to `tmp\rt-<name>.log` (`RT_FILELOG` adds file opens). `RT_XDBG=1` logs camera overlay state. `RT_HEAPDUMP=<secs>` logs live guest-heap bytes and the biggest allocation sites every `<secs>`, for leak hunting.

Leak fixes:
- The game's keyboard init only finishes once `tcgetattr(0)` succeeds, and until then it re-runs and leaks on every poll. Fds 0-2 therefore always count as a tty.
- Building a new DirectShow graph for every attract replay leaked inside the system's WMV filters. Videos now play on one worker thread, which keeps up to four graphs (one per file) and rewinds one when its file plays again.

Threads: the game starts helper threads with `__clone` and passes the thread's argument block on its own stack, then returns after a short `usleep`. On Linux the child had long since read it; here it could read a dead frame (a FATAL indirect call in 170). `__clone` now copies the block onto the child's stack first. A slot's previous thread handle is closed when the slot is reused.

**Operator tools.** The engine has no on-screen operator menu. Setup is done with separate console programs, which `buildtool.ps1 -Builds <b> nsyscfg wps iniconfig` builds into `vend-<b>\bin\`. Run them from a console with the `vend-<b>` folder as the current directory:
- `bin\nsyscfg.exe`: the System Configure form, which edits `vsystem.cfg`. Arrows/Enter move between fields, F2 lists the choices, **Esc** then `y` leaves without saving, **F10** then `y` saves. Saving rewrites `vsystem.cfg` (the shipped file is read-only; the runtime writes it anyway, like root on the cabinet) and mirrors it to `vsystem.alt`. It also rewrites `billvals`/`tokenvals`/`coinvals` to 8 slots, which the original does too.
- `bin\iniconfig.exe -d<form.ini> -c<config>` (170 only): the same editor for any form/config pair, for example `-dnsyscfg.ini -cvsystem.cfg`.
- `wps` is the wireless pager daemon, not a menu. Nothing uses it while the config says `Wireless Pager = No`, and the 152 build's daemon mode needs `fork()`, which the port doesn't support.

How the console side works: once a program sets termios, reads on stdin/`/dev/tty` follow ICANON/VMIN/VTIME, and `select()` and `FIONREAD` report keys that are waiting, so ESC and arrow-key sequences can be told apart. `TIOCGWINSZ` gives the console size. The engine never sets termios, so its non-blocking tty poll is unchanged.

Expected log noise: `tgarot -w432 -h480 0.000` exits -1 in both builds. The game asks it for a still of each print-rotation entry, the entry is `rotation.wmv`, and `tgarot` rejects non-TGA input ("input file: No error"). The original does the same. `snd: cannot play lang\eng\ff00s016.wav` (also `s017`, `s039`) appears once per print in both builds. Those files aren't in either original tree, so the cabinet was silent there too.

## Release

`release.ps1 -Pristine152 <dir> -Pristine170 <dir>` builds `dist\ExtremeCards-win32.zip`. The arguments are fresh `linux/exportvend.sh` exports of the `.pristine` trees (not the working `vend-*` folders, which collect logs, counters and camera frames). The zip holds both exes, both data trees with the helper exes from `vend-<b>\bin`, and `Play-<b>.cmd` launchers (from `release/`) that make the junctions listed in `.junctions` and clear `exiterr.dat` before each start.
