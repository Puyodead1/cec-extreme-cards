#!/bin/bash
# Build dongle-<ver>.so from each dongle-<ver>.c in this directory (run in WSL/Linux).
rc=0
cd "$(dirname "$0")"
for c in dongle-*.c; do
  so=${c%.c}.so
  gcc -m32 -shared -fPIC -nostdlib -ffreestanding -fno-stack-protector \
      -fno-asynchronous-unwind-tables -O2 -Wl,--hash-style=sysv -Wl,-z,norelro \
      -o "$so" "$c" || { echo "FAILED $c"; rc=1; continue; }
  echo "built $so"
done
exit $rc
