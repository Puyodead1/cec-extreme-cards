"""Relay a filesystem X11 socket to the X server's abstract-namespace socket.

Under WSLg /tmp/.X11-unix is a read-only tmpfs, so Xvfb :N can only listen on the abstract
socket @/tmp/.X11-unix/XN, which the FC5 chroot's old libX11 doesn't know about.
run_vend.sh mounts a tmpfs on $ROOTFS/tmp/.X11-unix and runs this to bridge them.
Usage: python3 -I x11relay.py <listen-path> <display-number>
"""
import os, socket, sys, threading

path, num = sys.argv[1], sys.argv[2]
target = "\0/tmp/.X11-unix/X" + num

def pump(a, b):
    try:
        while True:
            d = a.recv(65536)
            if not d:
                break
            b.sendall(d)
    except OSError:
        pass
    for s in (a, b):
        try:
            s.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass

def serve(c):
    u = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        u.connect(target)
    except OSError:
        c.close(); return
    threading.Thread(target=pump, args=(c, u), daemon=True).start()
    threading.Thread(target=pump, args=(u, c), daemon=True).start()

try:
    os.unlink(path)
except FileNotFoundError:
    pass
l = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
l.bind(path)
os.chmod(path, 0o777)
l.listen(16)
while True:
    c, _ = l.accept()
    serve(c)
