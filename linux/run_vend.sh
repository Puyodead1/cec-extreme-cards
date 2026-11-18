#!/bin/bash
# Run the Extreme Cards vend app (old or new build) in the FC5 rootfs under WSL, as root.
# Usage: ROOTFS=... EC_WORK=... bash run_vend.sh old|new [timeout_seconds]   (0 = no timeout)
#   VEND_ARGS   extra engine args (default -vt1: plain X11/XImage video; vsystem.cfg's
#               DGA + /dev/mem framebuffer mode can't work here)
#   VEND_BIN    engine path inside /vend (default bin/fbus/vjmnupns, what /vend/bin/vend runs)
#   VEND_PRE    command prefix inside the chroot
#   VEND_PRELOAD  LD_PRELOAD for the engine (default: /lib/dongle-152.so or -170.so)
set -u
F=${ROOTFS:?set ROOTFS to the FC5 rootfs from mkrootfs.sh}
W=${EC_WORK:?set EC_WORK to the dir holding vend-old/ and vend-new/}
VER=${1:?old or new}
case $VER in old) DEF_PRE=/lib/dongle-152.so;; new) DEF_PRE=/lib/dongle-170.so;; esac
VEND_PRELOAD=${VEND_PRELOAD-$DEF_PRE}   # set VEND_PRELOAD= (empty) to run without the shim
T=${2:-0}
V=$W/vend-$VER
[ -d "$V" ] || { echo "no $V (run mkvend.sh)"; exit 1; }

# every run starts from the pristine tree (logs, exiterr.dat reboot counter, dongle.img...),
# except what VEND_KEEP_FILES lists; VEND_KEEP=1 keeps the tree as the last run left it
while mountpoint -q "$F/vend"; do umount -l "$F/vend"; done
if [ "${VEND_KEEP:-0}" != 1 ] && [ -d "$V.pristine" ]; then
  rsync -a --delete $(for k in ${VEND_KEEP_FILES:-}; do echo --exclude=/$k; done) "$V.pristine/" "$V/"
fi
while mountpoint -q "$F/vend"; do umount -l "$F/vend"; done
mkdir -p "$F/vend"
mount --bind "$V" "$F/vend"
mountpoint -q "$F/proc" || mount -t proc proc "$F/proc"
mountpoint -q "$F/dev/pts" || { mkdir -p "$F/dev/pts"; mount -t devpts devpts "$F/dev/pts"; }
mkdir -p "$F/tmp/.X11-unix"
XN=${VEND_DISPLAY:-:0}; XN=${XN#*:}; XN=${XN%%.*}
if [ ! -S "/tmp/.X11-unix/X$XN" ] && grep -q "@/tmp/.X11-unix/X$XN\$" /proc/net/unix; then
  # WSLg: host /tmp/.X11-unix is read-only, Xvfb only has the abstract socket, which the
  # chroot's libX11 can't use -> writable tmpfs in the rootfs + relay (x11relay.py)
  if ! touch "$F/tmp/.X11-unix/.w" 2>/dev/null; then
    while mountpoint -q "$F/tmp/.X11-unix"; do umount -l "$F/tmp/.X11-unix"; done
    mount -t tmpfs -o mode=1777 tmpfs "$F/tmp/.X11-unix"
  fi
  RP=/tmp/x11relay-$(echo "$F" | tr / _)-$XN.pid
  if ! { [ -f "$RP" ] && kill -0 "$(cat "$RP")" 2>/dev/null && [ -S "$F/tmp/.X11-unix/X$XN" ]; }; then
    [ -f "$RP" ] && kill "$(cat "$RP")" 2>/dev/null
    setsid python3 -I "$(cd "$(dirname "$0")" && pwd)/x11relay.py" "$F/tmp/.X11-unix/X$XN" "$XN" </dev/null >/dev/null 2>&1 &
    echo $! > "$RP"; sleep 0.5
  fi
else
  mountpoint -q "$F/tmp/.X11-unix" || mount --bind /tmp/.X11-unix "$F/tmp/.X11-unix"
fi
for n in "null c 1 3" "zero c 1 5" "random c 1 8" "urandom c 1 9" "tty c 5 0"; do
  set -- $n; [ -e "$F/dev/$1" ] || mknod -m 666 "$F/dev/$1" $2 $3 $4
done
mkdir -p "$V/tmp"

CMD="cd /vend && LD_PRELOAD=${VEND_PRELOAD:-} exec ${VEND_PRE:-} /vend/${VEND_BIN:-bin/fbus/vjmnupns} ${VEND_ARGS--vt1}"
ENV="DISPLAY=${VEND_DISPLAY:-:0} HOME=/vend TMPDIR=/vend/tmp PATH=.:/vend/bin:/bin:/usr/bin:/usr/X11R6/bin"
# HOST_PRE: host-side wrapper around the chroot, e.g. "strace -f -tt -o /tmp/vend.st"
if [ "$T" != 0 ]; then
  timeout -s KILL "$T" ${HOST_PRE:-} chroot "$F" /usr/bin/env -i $ENV /bin/sh -c "$CMD"
else
  ${HOST_PRE:-} chroot "$F" /usr/bin/env -i $ENV /bin/sh -c "$CMD"
fi
RC=$?
umount -l "$F/vend" 2>/dev/null
echo "exit $RC"
