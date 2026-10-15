#!/usr/bin/env python3
# closing windows out of a stack of splits: the focus must land on a real window.
# it landed on the split's container (no rows, not in the ctrl-w list), so j did
# nothing and ctrl-w w did nothing until kami was opened. usage: tools/close-pane.py ./hake
import os, pty, sys, time, select, struct, fcntl, termios, pyte, tempfile
HAKE = os.path.abspath(sys.argv[1])
W = tempfile.mkdtemp(); HOME = W + "/home"; os.makedirs(HOME)
open(HOME + "/.hakorc", "w").write("show_splash=0\nshow_line_numbers=1\npane_titles=1\n")
open(W + "/t.txt", "w").write("".join("line %d\n" % i for i in range(1, 60)))
COLS, ROWS = 120, 40
scr = pyte.Screen(COLS, ROWS); st = pyte.ByteStream(scr)
pid, fd = pty.fork()
if pid == 0:
    os.chdir(W); os.execve(HAKE, ["hake", "t.txt"], dict(os.environ, HOME=HOME, TERM="xterm-256color"))
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
def pump(t=0.35):
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.03)
        if r:
            try: st.feed(os.read(fd, 65536))
            except OSError: return
def key(s): os.write(fd, s.encode()); pump()
def cur(): return (scr.cursor.x, scr.cursor.y)
pump(1.5)
# A | B, then B over C; then split B again: the tree is A | ((B over D) over C).
# Closing C leaves (B over D) in its place: a split, not a window.
key(":vs\r"); key(":sp\r")
key("\x172"); key(":sp\r")             # window 2 is B
key("\x174"); key("\x17c")             # window 4 is C: close it
fails = 0
pump(0.5)
a = cur(); key("j"); key("j"); b = cur()
ok = b[1] == a[1] + 2
fails += not ok
print(("ok  " if ok else "FAIL"), "after closing a window beside a split, j moves the cursor (%s -> %s)" % (a, b))
key("\x17w"); c1 = cur(); key("j"); c2 = cur()
ok = c1 != b and c2[1] == c1[1] + 1
fails += not ok
print(("ok  " if ok else "FAIL"), "ctrl-w w moves to another window and j works there (%s -> %s -> %s)" % (b, c1, c2))
os.kill(pid, 9)
print("RESULT:", "PASS" if not fails else "FAIL (%d)" % fails)
sys.exit(1 if fails else 0)
