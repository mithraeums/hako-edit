#!/usr/bin/env python3
"""pty test of what the 10-29 overnight pass added: the layered rc (global, then
the project's, with its denylist), theme writes that keep the rc's comments,
custom themes, the start screen, nano and emacs keys, the console (and its
sandbox on macOS), the Rei pane's command menu, and the terminal theme painting
no background. usage: tools/modes.py ./hake  (an ASan build makes it a gate)"""
import os, pty, sys, time, select, struct, fcntl, termios, signal, shutil, tempfile, glob
import pyte

HERE = os.path.dirname(os.path.abspath(__file__))
HAKE = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(HERE, "..", "hake")
WORK = tempfile.mkdtemp(prefix="hake-modes-")
HOME, PROJ, SANLOG = os.path.join(WORK, "home"), os.path.join(WORK, "proj"), os.path.join(WORK, "san")
COLS, ROWS = 100, 30
ok = True

def check(cond, what, run=None):
    global ok
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond and run is not None:
        for i, l in enumerate(run.screen.display):
            if l.strip(): print(f"       {i+1:3} {l.rstrip()[:100]}")
    ok = ok and bool(cond)

def fresh(global_rc="", project_rc=None):
    shutil.rmtree(HOME, ignore_errors=True); shutil.rmtree(PROJ, ignore_errors=True)
    os.makedirs(HOME); os.makedirs(PROJ)
    open(os.path.join(HOME, ".hakorc"), "w").write(global_rc)
    if project_rc is not None: open(os.path.join(PROJ, ".hakorc"), "w").write(project_rc)

class Run:
    def __init__(self, *args, binary=None, env_extra=None):
        self.screen = pyte.Screen(COLS, ROWS); self.stream = pyte.ByteStream(self.screen); self.raw = b""
        pid, fd = pty.fork()
        if pid == 0:
            os.chdir(PROJ)
            env = dict(os.environ, HOME=HOME, TERM="xterm-256color", ASAN_OPTIONS="detect_leaks=0:log_path=" + SANLOG,
                       UBSAN_OPTIONS="print_stacktrace=1:log_path=" + SANLOG)
            env.pop("HAKORC", None); env.pop("XDG_CONFIG_HOME", None); env.pop("COLORTERM", None)
            env.update(env_extra or {})
            b = binary or HAKE
            os.execve(b, [b] + list(args), env)
        self.pid, self.fd = pid, fd
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
        self.pump(0.8)
    def pump(self, secs):
        end = time.time() + secs
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.02)
            if r:
                try: data = os.read(self.fd, 65536)
                except OSError: return
                if not data: return
                self.raw += data; self.stream.feed(data)
    def keys(self, s, gap=0.05):
        for ch in s: os.write(self.fd, ch.encode()); self.pump(gap)
        self.pump(0.3)
    def text(self): return "\n".join(self.screen.display)
    def wait_for(self, needle, secs=4):
        end = time.time() + secs
        while time.time() < end:
            if needle in self.text(): return True
            self.pump(0.1)
        return False
    def done(self, secs=4):
        end = time.time() + secs
        while time.time() < end:
            self.pump(0.05)
            p, st = os.waitpid(self.pid, os.WNOHANG)
            if p: return st
        os.kill(self.pid, signal.SIGKILL); os.waitpid(self.pid, 0); return None

def rc(): return open(os.path.join(HOME, ".hakorc")).read()
def proj(name): return open(os.path.join(PROJ, name)).read()

def main():
    print("1  the start screen and the terminal theme")
    fresh()
    r = Run()
    check(r.wait_for("the hako editor"), "no file: the intro shows", r)
    check(b"\x1b[48;" not in r.raw, "terminal theme paints no background")
    r.keys("i")
    check(r.wait_for("-- INSERT --") and "the hako editor" not in r.text(), "the first key clears it", r)
    r.keys("\x1b:q!\r"); r.done()

    print("2  the rc: global, then project, the denylist, edit. keys")
    fresh("# my settings\ntheme=nord\nedit.theme=gruvbox\nshow_line_numbers=0\n",
          "ai_endpoint=http://evil\nshow_line_numbers=1\nconsole_sandbox=0\nbuild_cmd=echo built\n")
    open(os.path.join(PROJ, "a.txt"), "w").write("one\ntwo\n")
    r = Run("a.txt")
    check(r.wait_for("project .hakorc: ai_endpoint ignored"), "a refused project key says so once", r)
    check(r.wait_for("1 one"), "a project key layers over the global rc (line numbers on)", r)
    r.keys(":theme washi\r")
    check("edit.theme=washi" in rc() and "theme=nord" in rc() and rc().startswith("# my settings"),
          ":theme writes edit.theme in place and keeps the comments", r)
    r.keys(":config tab_stop=4\r")
    check("tab_stop=4" in rc(), ":config key=value appends a new key")
    r.keys(":build\r")
    check(r.wait_for("built"), "build_cmd from a project rc still runs (it is allowed)", r)
    r.keys(":q!\r"); r.done()

    print("3  custom themes")
    fresh("theme=mine\ntheme.mine.base=nord\ntheme.mine.bg=#102030\n")
    r = Run("x.txt")
    check(b"48;5;" in r.raw or b"48;2;16;32;48" in r.raw, "theme.mine.bg paints the custom background")
    r.keys(":theme\r")
    check(r.wait_for("mine"), "the custom theme is in the picker", r)
    r.keys("\x1b:q!\r"); r.done()

    print("4  nano")
    fresh("edit.keys=emacs\n")
    r = Run("n.txt", "--nano")
    check(r.wait_for("^G Help") and r.wait_for("^X Exit"), "--nano beats the rc and shows the shortcut rows", r)
    r.keys("hello nano\rsecond line")
    r.keys("\x0f"); r.keys("\r")
    check(proj("n.txt") == "hello nano\nsecond line\n", "^O writes out", r)
    r.keys("\x0b"); r.keys("\x10"); r.keys("\x15")
    r.keys("\x0f\r")
    check(proj("n.txt") == "second line\nhello nano\n", "^K cuts a line, ^U pastes it above", r)
    r.keys("\x14help\r")
    check(r.wait_for("Nano keys"), "^T reaches the : commands", r)
    r.keys("\x1b"); r.keys("zz"); r.keys("\x18"); r.keys("n")
    st = r.done()
    check(st is not None and os.WIFEXITED(st), "^X asks about the change and exits on n")
    r = Run("p.md", "--nano")
    check("LineM-U" not in r.text(), "the shortcut rows keep a space between columns", r)
    r.keys("a note:\rnext line\x0f\r")
    check(proj("p.md") == "a note:\nnext line\n", "prose (markdown) gets no smart indent after a ':'", r)
    r.keys("\x18"); r.done()

    print("5  emacs")
    fresh()
    r = Run("e.txt", "--emacs")
    r.keys("alpha beta\rgamma")
    r.keys("\x18\x13")
    check(proj("e.txt") == "alpha beta\ngamma\n", "typing goes straight in; C-x C-s saves", r)
    r.keys("\x1b<"); r.keys("\x0b"); r.keys("\x05"); r.keys("\x19"); r.keys("\x19")
    r.keys("\x18\x13")
    check(proj("e.txt").split("\n")[0] == "alpha betaalpha beta", "M-< then C-k kills, C-y yanks it twice", r)
    r.keys("\x13gam")
    check(r.wait_for("I-search: gam"), "C-s searches as you type", r)
    r.keys("\r"); r.keys("\x1bx"); r.keys("help\r")
    check(r.wait_for("Emacs keys"), "M-x runs a : command", r)
    r.keys("\x1b"); r.keys("\x18\x03")
    check(r.done() is not None, "C-x C-c quits")

    print("6  the console")
    fresh()
    r = Run("c.txt")
    r.keys(":console\r"); r.keys("echo hi-$((40+2))\r")
    check(r.wait_for("hi-42"), ":console runs a line in the project dir", r)
    r.keys("\x1b[A"); r.pump(0.2)
    check("$ echo hi-$((40+2))" in r.text(), "up recalls the last line", r)
    r.keys("\x15exit\r"); r.keys(":q!\r"); r.done()
    if sys.platform == "darwin":
        fresh("console_sandbox=1\n", "word_wrap=1\n")   # a project rc too: it once freed the sandbox root
        outside = os.path.join(WORK, "outside.txt")
        box = os.path.join(WORK, "tmpbox"); os.makedirs(box, exist_ok=True)
        r = Run("c.txt", env_extra={"TMPDIR": box})
        r.keys(":console\r"); r.keys("echo in > inside.txt; echo out > " + outside + "; echo done\r")
        r.wait_for("done", 6)
        check(os.path.exists(os.path.join(PROJ, "inside.txt")) and not os.path.exists(outside),
              "console_sandbox=1: the project is writable, outside it is not", r)
        r.keys(":q!\r"); r.done()

    print("6b :split and :vsplit take a file, as in vim")
    fresh()
    open(os.path.join(PROJ, "left.c"), "w").write("int left;\n")
    open(os.path.join(PROJ, "right.md"), "w").write("# right side\n")
    r = Run("left.c")
    r.keys(":vsplit right.md\r"); r.pump(0.6)
    check(r.wait_for("# right side") and "int left;" in r.text(), ":vsplit file opens it beside the buffer", r)
    r.keys(":q!\r"); r.keys(":q!\r"); r.done()

    print("7  the Rei pane: hako's commands as a menu")
    fresh()
    bindir = os.path.join(WORK, "bin"); os.makedirs(bindir, exist_ok=True)
    shutil.copy2(HAKE, os.path.join(bindir, "hake")); shutil.copy2(os.path.join(HERE, "fakehako.py"), os.path.join(bindir, "hako"))
    global ROWS
    r = Run("r.txt", binary=os.path.join(bindir, "hake"))
    r.keys(":rei\r"); r.pump(1.0); r.keys("\x17r")
    check(r.wait_for("notes"), "the chat's name shows in the pane header", r)
    words = "alpha bravo charlie delta echo foxtrot golf hotel india juliet kilo lima".split()
    check(r.wait_for("lima") and all(w in r.text() for w in words), "a long Rei line wraps between words, never inside one", r)
    r.keys("i:th")
    check(r.wait_for(":theme"), "typing :th opens the menu with :theme", r)
    r.keys("\x1b")
    check(":theme" not in r.text() and ":th" in r.text(), "esc closes the menu and keeps the line", r)
    r.keys("\x1b"); r.keys("\x17w"); r.keys(":q!\r"); r.done()

    reports = [p for p in glob.glob(SANLOG + "*") if os.path.getsize(p)]
    for p in reports: print(open(p).read()[:3000])
    check(not reports, "no sanitizer reports")
    shutil.rmtree(WORK, ignore_errors=True)
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
