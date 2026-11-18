#!/bin/bash
# Build a minimal Fedora Core 5 rootfs from the RPMs on the "new" install DVD.
# Run as root in WSL:  ISO=/path/to/new.iso ROOTFS=/path/to/rootfs bash mkrootfs.sh
# The game's own /vend trees are installed separately (see mkvend.sh).
set -eu
ISO=${ISO:?set ISO to the new disc image (.iso)}
F=${ROOTFS:?set ROOTFS to the directory to build the rootfs in}
HERE=$(cd "$(dirname "$0")" && pwd)
MNT=${ISO_MNT:-/mnt/ecnew}

mkdir -p "$MNT" "$F"
mountpoint -q "$MNT" || mount -o loop,ro "$ISO" "$MNT"

PKGS="filesystem setup basesystem glibc glibc-common bash coreutils sed grep gawk findutils
  util-linux procps which less tar gzip shadow-utils
  libgcc libstdc++ compat-libstdc++-296 compat-libstdc++-33 ncurses termcap
  libX11 libXext libXxf86dga libXxf86vm libXau libXdmcp xorg-x11-utils
  netpbm-progs mplayer mplayer-codecs aumix alsa-lib alsa-utils"

python3 -I "$HERE/resolve.py" "$MNT/Fedora/repodata/primary.xml.gz" $PKGS > "$F/.rpmlist" 2> "$F/.unresolved" || true
echo "$(wc -l < "$F/.rpmlist") packages; unresolved:"; cat "$F/.unresolved"

cd "$F"
while read -r href; do   # hrefs are relative to Fedora/, e.g. RPMS/glibc-2.4-4.i686.rpm
  rpm2cpio "$MNT/Fedora/$href" | cpio -idmu --quiet --no-absolute-filenames
done < "$F/.rpmlist"

# things normally made by %post scripts / the installer
mkdir -p "$F/proc" "$F/sys" "$F/dev/pts" "$F/tmp/.X11-unix" "$F/var/tmp" "$F/root"
chmod 1777 "$F/tmp" "$F/var/tmp"
[ -e "$F/etc/ld.so.conf" ] || echo "include ld.so.conf.d/*.conf" > "$F/etc/ld.so.conf"
mkdir -p "$F/etc/ld.so.conf.d"; echo "/vend/lib" > "$F/etc/ld.so.conf.d/vend.conf"
grep -q '^vend:' "$F/etc/group" 2>/dev/null || echo "vend:x:600:" >> "$F/etc/group"
grep -q '^vend:' "$F/etc/passwd" 2>/dev/null || echo "vend:x:0:0::/vend:/bin/bash" >> "$F/etc/passwd"
for n in "null c 1 3" "zero c 1 5" "random c 1 8" "urandom c 1 9" "tty c 5 0"; do
  set -- $n; [ -e "$F/dev/$1" ] || mknod -m 666 "$F/dev/$1" $2 $3 $4
done
chroot "$F" /sbin/ldconfig
echo "rootfs ready in $F"
