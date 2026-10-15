#!/usr/bin/env python3
"""pty smoke test for the editor itself (the --pipe wire has pipe-conformance.py).
Drives hake in a pseudo-terminal and checks what a user would lose data over:
save writes the buffer, undo, a clean buffer reloads when the file changes on
disk (even within the same second), a dirty buffer refuses to overwrite a file
changed on disk until :w!, :e! reloads, a resize storm, a clean quit.

Needs python3 + `pip install pyte`. Usage: python3 tools/smoke.py [path/to/hake]
Build with -fsanitize=address,undefined to make it a sanitizer gate: reports go
to a log file here, and any report fails the run."""
import os, pty, sys, time, select, struct, fcntl, termios, signal, shutil, tempfile, glob
try:
    import pyte
except ImportError:
    sys.exit("pip install pyte")

HERE = os.path.dirname(os.path.abspath(__file__))
HAKE = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(HERE, "..", "hake")
WORK = tempfile.mkdtemp(prefix="hake-smoke-")
HOME, PROJ = os.path.join(WORK, "home"), os.path.join(WORK, "proj")
SANLOG = os.path.join(WORK, "san")
COLS, ROWS = 100, 30
ok = True

def check(cond, what, run=None):
    global ok
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond and run is not None:
        for i, l in enumerate(run.screen.display):
            if l.strip(): print(f"       {i+1:3} {l.rstrip()[:100]}")
    ok = ok and bool(cond)

def fresh():
    shutil.rmtree(HOME, ignore_errors=True); shutil.rmtree(PROJ, ignore_errors=True)
    os.makedirs(HOME); os.makedirs(PROJ)
    open(os.path.join(HOME, ".hakorc"), "w").write("show_splash=0\n")

class Run:
    def __init__(self, *args):
        self.screen = pyte.Screen(COLS, ROWS)
        self.stream = pyte.ByteStream(self.screen)
        pid, fd = pty.fork()
        if pid == 0:
            os.chdir(PROJ)
            env = dict(os.environ, HOME=HOME, TERM="xterm-256color",
                       ASAN_OPTIONS="detect_leaks=0:log_path=" + SANLOG,
                       UBSAN_OPTIONS="print_stacktrace=1:log_path=" + SANLOG)
            os.execve(HAKE, [HAKE] + list(args), env)
        self.pid, self.fd = pid, fd
        self.size(ROWS, COLS)
    def size(self, rows, cols):
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.screen.resize(rows, cols)
        try: os.kill(self.pid, signal.SIGWINCH)
        except ProcessLookupError: pass
    def pump(self, secs):
        end = time.time() + secs
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.02)
            if r:
                try: data = os.read(self.fd, 65536)
                except OSError: return False
                if not data: return False
                self.clears = getattr(self, "clears", 0) + data.count(b"\x1b[2J")
                self.stream.feed(data)
        return True
    def keys(self, s, gap=0.05):
        for ch in s:
            os.write(self.fd, ch.encode()); self.pump(gap)
        self.pump(0.3)
    def cmd(self, c):
        self.keys("\x1b"); self.keys(":" + c + "\r")
    def text(self):
        return "\n".join(self.screen.display)
    def wait_for(self, needle, secs=5):
        end = time.time() + secs
        while time.time() < end:
            if needle in self.text(): return True
            self.pump(0.1)
        return False
    def exit_status(self, secs=5):
        end = time.time() + secs
        while time.time() < end:
            self.pump(0.05)
            p, st = os.waitpid(self.pid, os.WNOHANG)
            if p: return st
        os.kill(self.pid, signal.SIGKILL); os.waitpid(self.pid, 0)
        return None

def read(name):
    return open(os.path.join(PROJ, name)).read()

def main():
    global HAKE
    if not os.path.exists(HAKE):
        print("missing hake binary:", HAKE); return 2
    fresh()
    f = os.path.join(PROJ, "a.txt")
    # hake finds its agent beside its own binary: run a copy with the fake next to it
    bindir = os.path.join(WORK, "bin"); os.makedirs(bindir)
    shutil.copy2(HAKE, os.path.join(bindir, "hake"))
    shutil.copy2(os.path.join(HERE, "fakehako.py"), os.path.join(bindir, "hako"))
    HAKE = os.path.join(bindir, "hake")

    print("1  edit, save, undo")
    r = Run("a.txt"); r.wait_for(":help for commands", 10); r.pump(0.3)
    r.keys("ihello world\x1b"); r.cmd("w")
    check(read("a.txt") == "hello world\n", "save writes the buffer", r)
    r.keys("ddu"); r.pump(0.3)
    check("hello world" in r.text(), "undo restores a deleted line", r)
    r.cmd("w")

    print("2  on-disk changes")
    open(f, "w").write("from the agent\n")            # same second as the save above
    check(r.wait_for("from the agent", 4), "a clean buffer reloads a same-second change on disk", r)
    r.keys("Aedited\x1b")                               # dirty now
    open(f, "w").write("agent again\n")
    r.pump(1.5); r.cmd("w")
    check(read("a.txt") == "agent again\n", ":w refuses to overwrite a file changed on disk", r)
    check("changed on disk" in r.text(), "and says why", r)
    r.cmd("w!")
    check(read("a.txt").startswith("from the agent"), ":w! overwrites deliberately", r)
    open(f, "w").write("third\n")
    r.keys("Ax\x1b"); r.cmd("e!")
    check(r.wait_for("third", 3), ":e! reloads and discards the buffer", r)

    print("3  resize storm")
    for c in (60, 140, 40, 120, 80, 33, 100):
        r.size(ROWS, c); r.pump(0.08)
    r.size(ROWS, COLS); r.pump(0.5)
    check("third" in r.text(), "the buffer survives ten resizes", r)

    print("4  the Rei pane reads events longer than 64 KB")
    r.cmd("rei"); r.pump(1.5)
    check(r.wait_for("BIGLINE_OK", 6), "a 70 KB event from the agent arrives whole", r)
    r.keys("\x1b"); r.keys("\x17r"); r.pump(0.4)          # ctrl-w r: focus Rei
    # every agent event used to force a full-screen clear: the editor flashed several
    # times per reply. The grid diff repaints only what changed.
    r.clears = 0
    r.keys("iping\x1b"); r.keys("\r"); r.pump(0.2)
    check(r.wait_for("BIGREPLY_OK", 6), "a reply arrives in the Rei pane", r)
    check(r.clears == 0, "and the screen is not cleared for it (%d full clears)" % r.clears, r)
    check("AI NORMAL" in r.text() or "press 'i'" in r.text(), "ctrl-w r focuses Rei", r)
    r.keys(":help\r"); r.pump(0.5)
    check(r.wait_for("Help — commands & keys", 3), ":help with Rei focused opens the editor's help (it used to vanish)", r)
    r.keys("\x1b"); r.pump(0.3)
    r.cmd("rei"); r.pump(0.3)

    print("5  quit")
    r.cmd("q")
    st = r.exit_status()
    check(st is not None and os.WIFEXITED(st) and os.WEXITSTATUS(st) == 0, ":q exits 0")

    reports = [p for p in glob.glob(SANLOG + "*") if os.path.getsize(p)]
    for p in reports:
        print(open(p).read()[:3000])
    check(not reports, "no sanitizer reports")
    shutil.rmtree(WORK, ignore_errors=True)
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
