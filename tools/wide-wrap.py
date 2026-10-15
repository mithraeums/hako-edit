#!/usr/bin/env python3
# wide characters on a wrapped line: the cell the cursor is drawn on must hold the
# character x deletes, and backspace must remove the one drawn left of it. the wrap
# was measured in bytes while the cursor counted cells, and x removed one byte of a
# katakana (the file came out invalid UTF-8). usage: tools/wide-wrap.py ./hake
import os, pty, sys, time, select, struct, fcntl, termios, pyte, tempfile
HAKE = os.path.abspath(sys.argv[1])
W = tempfile.mkdtemp(); HOME = W + "/home"; os.makedirs(HOME)
open(HOME + "/.hakorc", "w").write("show_splash=0\nword_wrap=1\nshow_line_numbers=0\npane_titles=1\ntheme=mithraeum\n")
LINE = "abc " + "カタカナ" * 9 + " middle " + "ひらがな" * 9 + " xyz end"
open(W + "/t.txt", "w").write(LINE + "\nsecond line\n")
COLS, ROWS = 40, 20
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
def under():
    x, y = scr.cursor.x, scr.cursor.y
    return scr.buffer[y][x].data
pump(1.5)
fails = 0; checks = 0
text = LINE
# walk right through the line, deleting nothing, then test x at several points
for steps in (3, 9, 17, 26, 33, 44, 52, 60):
    key("0"); key(str(steps) + "l")
    shown = under()
    key("x"); pump(0.2)
    expect_removed = text[steps] if steps < len(text) else text[-1]
    key(":w\r"); pump(0.4)
    now = open(W + "/t.txt").read().split("\n")[0]
    removed = None
    for i in range(min(len(now), len(text))):
        if now[i] != text[i]: removed = text[i]; break
    if removed is None and len(now) < len(text): removed = text[len(now)]
    checks += 1
    ok = removed == shown and removed == expect_removed
    if not ok: fails += 1
    print(("ok  " if ok else "FAIL"), "col %2d: cursor drawn on %r, x removed %r (expected %r)" % (steps, shown, removed, expect_removed))
    text = now
# down/up inside the wrapped line keep the cursor on a real character
key("0"); key("j"); s1 = under(); key("x"); key(":w\r"); pump(0.4)
now = open(W + "/t.txt").read().split("\n")[0]
d = next((text[i] for i in range(len(now)) if now[i] != text[i]), None)
print(("ok  " if d == s1 else "FAIL"), "after j inside the wrap: cursor on %r, x removed %r" % (s1, d)); fails += d != s1
# insert-mode backspace (the report): the character left of the drawn cursor is the one removed
def left_of_cursor():
    x, y = scr.cursor.x, scr.cursor.y
    if x == 0: y -= 1; x = COLS
    for xx in range(x - 1, -1, -1):
        d = scr.buffer[y][xx].data
        if d.strip(): return d
        if d == " " and xx == x - 1 and x < COLS: return d
for steps in (12, 25, 41):
    key("0"); key(str(steps) + "l"); key("i"); pump(0.2)
    shown = left_of_cursor()
    os.write(fd, b"\x7f"); pump(0.3); key("\x1b"); key(":w\r"); pump(0.4)
    now = open(W + "/t.txt").read().split("\n")[0]
    gone = next((text[i] for i in range(len(now)) if now[i] != text[i]), None)
    ok = gone == shown == text[steps - 1]
    fails += not ok
    print(("ok  " if ok else "FAIL"), "backspace at col %d: left of cursor %r, removed %r (expected %r)" % (steps, shown, gone, text[steps - 1]))
    text = now
os.kill(pid, 9)
print("RESULT:", "PASS" if not fails else "FAIL (%d)" % fails)
sys.exit(1 if fails else 0)
