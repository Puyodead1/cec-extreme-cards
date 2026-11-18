#!/bin/bash
# Build the two /vend trees that run_vend.sh bind-mounts into the rootfs.
# Run as root in WSL after mkrootfs.sh:
#   ISO=... OLD_VEND=<old disc vend> ROOTFS=... EC_WORK=/dir bash mkvend.sh [old|new|both]
#
#  new: the vendor RPMs the kickstart installs (core, core_control, core_util_files, relid,
#       1014v1, ...), unpacked and then put through the bits of their %post scripts and the
#       kickstart %post that matter.
#  old: the /vend folder pulled out of the Ghost image with Ghost Explorer. That drops every
#       symlink, so they are recreated from the new RPMs' symlink list (where the target
#       exists in the old tree), then by the game's own makelinks.sh and setlinks.
set -eu
F=${ROOTFS:?set ROOTFS}
W=${EC_WORK:?set EC_WORK to the work dir (vend-old/ and vend-new/ go there)}
MNT=${ISO_MNT:-/mnt/ecnew}
WHICH=${1:-both}
RPMS="core-61-5 core_control-61-4 core_util_files-61-4 locale-61-2 relid-70-5 uprn-61-1.gb
  pager-4-4.08 marsreset-2-0 usbreset-1-1 showink-1-2 1014v1-1.0-12"

if [ ! -d "$MNT/Fedora" ]; then mkdir -p "$MNT"; mount -o loop,ro "${ISO:?set ISO}" "$MNT"; fi
mkdir -p "$W"
while mountpoint -q "$F/vend"; do umount -l "$F/vend"; done   # never rm a tree that is bound in

# symlinks the vendor RPMs carry, as "path<TAB>target" (path without the leading /)
linklist() {
  for p in $RPMS; do
    rpm -qp --nosignature --qf '[%{FILENAMES}\t%{FILELINKTOS}\n]' "$MNT/Fedora/RPMS/$p".i386.rpm 2>/dev/null
  done | awk -F'\t' '$2 != "" { sub(/^\//, "", $1); print }'
}

inchroot() {  # run a command in the rootfs with $1 bound on /vend
  local v=$1; shift
  mkdir -p "$F/vend"
  mount --bind "$v" "$F/vend"
  mountpoint -q "$F/proc" || mount -t proc proc "$F/proc"
  chroot "$F" /usr/bin/env -i PATH=/vend/bin:/bin:/usr/bin:/sbin:/usr/sbin HOME=/vend /bin/sh -c "$*" || true
  umount "$F/vend"
}

postinst() {  # new: what the %post scripts and the kickstart %post do, minus the hardware bits
  local v=$1
  inchroot "$v" '
    mkdir -p /vend/tmp
    cd /vend/lib && sh /vend/lib/makelinks.sh
    /sbin/ldconfig
    [ -f /vend/.rel ] || echo 61 > /vend/.rel
    [ -f /vend/.base ] || cp /vend/.rel /vend/.base
    /vend/bin/relid > /vend/relid.id 2>/dev/null
    cd /vend/pics && PATH=$PATH:/vend/bin sh /vend/pics/setlinks 1014v1
    [ -f /vend/etc/fbus.conf ] && cp /vend/etc/fbus.conf /etc/fbus.conf
    true'
}

oldlinks() {  # old: already an installed machine; only put back the links (setlinks'
  local v=$1  # setProduct/setThemeLinks and makelinks.sh), leave its config files alone
  inchroot "$v" '
    mkdir -p /vend/tmp
    cd /vend/lib && sh /vend/lib/makelinks.sh
    /sbin/ldconfig
    P=$(cat /vend/.theme); d=/vend/pics; t=$d/product/theme
    rm -f $d/product; ln -s $d/$P $d/product
    mkdir -p $d/cat; [ -e $d/cat/$P.ini ] || ln -s $d/product/$P.ini $d/cat/$P.ini
    for l in studio:product/studio lang:product/lang workpics:product/workpics; do
      n=${l%%:*}; [ -e /vend/$n ] || ln -s $d/${l#*:} /vend/$n; done
    for f in ppaper.tga wpaper.tga wframe.tga pattr.ini; do [ -e /vend/$f ] || ln -s $t/$f /vend/$f; done
    for f in $t/num?.tga $t/pnum?.tga; do b=${f##*/}; [ -e /vend/lang/$b ] || ln -s $f /vend/lang/$b; done
    for f in hcs.dat hcs2.dat; do [ -e /vend/$f ] || ln -s pics/product/$f /vend/$f; done
    true'
}

if [ "$WHICH" = new ] || [ "$WHICH" = both ]; then
  V=$W/vend-new; S=$W/stage-new
  rm -rf "$V" "$S"; mkdir -p "$S"
  for p in $RPMS; do (cd "$S" && rpm2cpio "$MNT/Fedora/RPMS/$p".i386.rpm | cpio -idmu --quiet --no-absolute-filenames); done
  echo "new: files outside /vend:"; (cd "$S" && find . -path ./vend -prune -o -type f -print)
  mv "$S/vend" "$V"; rm -rf "$S"
  postinst "$V"
  echo "new vend: $(du -sh "$V" | cut -f1) in $V"
fi

if [ "$WHICH" = old ] || [ "$WHICH" = both ]; then
  V=$W/vend-old
  rm -rf "$V"; mkdir -p "$V"
  rsync -a "${OLD_VEND:?set OLD_VEND}/" "$V/"
  # Ghost Explorer wrote everything as plain files; restore exec bits on ELF/scripts
  find "$V" -type f \( -path '*/bin/*' -o -name '*.sh' -o -path '*/sys/rc.*' \) -exec chmod 755 {} +
  find "$V" -type f -exec sh -c 'head -c4 "$1" | grep -q "ELF" && chmod 755 "$1"' _ {} \;
  n=0
  while IFS=$'\t' read -r path target; do
    rel=${path#vend/}; [ "$rel" = "$path" ] && continue
    dst=$V/$rel
    [ -e "$dst" ] || [ -L "$dst" ] && continue
    [ -d "$(dirname "$dst")" ] || continue
    ln -s "$target" "$dst"; n=$((n+1))
  done < <(linklist)
  echo "old: recreated $n symlinks from the RPM list"
  oldlinks "$V"
  echo "old vend: $(du -sh "$V" | cut -f1) in $V"
fi

# pristine copies; run_vend.sh resets vend-X from these unless VEND_KEEP=1
for x in old new; do
  if [ -d "$W/vend-$x" ] && { [ "$WHICH" = both ] || [ "$WHICH" = $x ]; }; then
    rm -rf "$W/vend-$x.pristine"; cp -a "$W/vend-$x" "$W/vend-$x.pristine"
  fi
done
