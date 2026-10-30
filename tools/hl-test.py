#!/usr/bin/env python3
"""Pins the syntax highlighting: runs `hake --hl-dump` over the samples in
tools/hl/ and checks the class of chosen pieces of text. One letter per byte:
. plain  c comment  k keyword  t type  s string  e escape  n number/constant
p preproc  v variable  y key  g tag  a attribute  L label  o operator  b bracket
h heading  B strong  I emphasis  X both  ~ strike  x code  l link  u url
m list marker  q quote  d done task  - delimiter  + added  ! removed
usage: tools/hl-test.py ./hake"""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
HAKE = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(HERE, "..", "hake")

# (file, piece of a line, the class every byte of that piece must have)
CASES = [
    ("sample.md", "# Heading one", "h"), ("sample.md", "Setext heading", "h"), ("sample.md", "======", "h"),
    ("sample.md", "*emph*", "I"), ("sample.md", "**strong**", "B"), ("sample.md", "***both***", "X"),
    ("sample.md", "`code`", "x"), ("sample.md", "~~gone~~", "~"), ("sample.md", "snake_case_word", "."),
    ("sample.md", "[link]", "l"), ("sample.md", "(https://example.com \"t\")", "u"), ("sample.md", "![img]", "l"),
    ("sample.md", "<https://auto.link>", "u"), ("sample.md", "[ ]", "m"), ("sample.md", "done task", "d"),
    ("sample.md", "1.", "m"), ("sample.md", "> a", "q.."), ("sample.md", "|-------|:-----:|", "-"),
    ("sample.md", "col a", "B"), ("sample.md", "return", "k"), ("sample.md", "/* c */", "c"),
    ("sample.md", "plain fence", "x"), ("sample.md", "comment -->", "c"), ("sample.md", "title", "y"),
    ("sample.md", "https://example.org/x", "u"), ("sample.md", "\\*not", "-...."),
    ("sample.c", "#include", "p"), ("sample.c", "<stdio.h>", "s"), ("sample.c", "   comment */", "c"),
    ("sample.c", "\\\"q\\\"", "eesee"), ("sample.c", "\\x41", "e"), ("sample.c", "'\\''", "sees"),
    ("sample.c", "1.5e-3f", "n"), ("sample.c", "0x1Fu", "n"), ("sample.c", "// tail", "c"), ("sample.c", "static", "k"),
    ("sample.py", "@decorator", "p"), ("sample.py", "    string\"\"\"", "s"), ("sample.py", "f\"x={x}\"", "s"),
    ("sample.py", "1_000.5", "n"), ("sample.py", "None", "n"),
    ("sample.sh", "$HOME", "v"), ("sample.sh", "${PATH}", "v"), ("sample.sh", "'raw $x'", "s"),
    ("sample.sh", "a#b", "."), ("sample.sh", "# tail", "c"), ("sample.sh", "then", "k"),
    ("sample.rs", "#[derive(Debug)]", "p"), ("sample.rs", "'a", "L"), ("sample.rs", "'x'", "s"),
    ("sample.rs", "println!", "p"), ("sample.rs", "/* outer /* nested */ still */", "c"), ("sample.rs", "3u32", "n"),
    ("sample.go", "over lines`", "s"), ("sample.go", "0b1010", "n"),
    ("sample.js", "c`", "s"), ("sample.js", "10n", "n"), ("sample.js", "// c", "c"),
    ("sample.json", "\"key\"", "y"), ("sample.json", "\"value\"", "s"), ("sample.json", "1.5e3", "n"), ("sample.json", "null", "n"),
    ("sample.yaml", "name", "y"), ("sample.yaml", "it's plain", "."), ("sample.yaml", "&a", "L"), ("sample.yaml", "yes", "n"),
    ("sample.toml", "[server]", "k"), ("sample.toml", "\"a#b\"", "s"), ("sample.toml", "# c", "c"), ("sample.toml", "port", "y"),
    ("Makefile", "all", "k"), ("Makefile", "CC", "y"), ("Makefile", "$(CC)", "v"), ("Makefile", "$@", "v"),
    ("sample.html", "<a", "g"), ("sample.html", "href", "a"), ("sample.html", "\"x.html\"", "s"), ("sample.html", "&amp;", "e"),
    ("sample.html", "\"multi\"", "s"), ("sample.html", "<!-- c -->", "c"),
    ("sample.css", ".cls", "k"), ("sample.css", "color", "y"), ("sample.css", "#fff", "n"), ("sample.css", "12px", "n"), ("sample.css", "@media", "p"),
    ("sample.lua", "string]]", "s"), ("sample.lua", "comment ]]", "c"),
    ("sample.diff", "-old", "!"), ("sample.diff", "+new", "+"), ("sample.diff", "@@ -1 +1 @@", "p"),
    ("sample.hakorc", "theme.mine.bg", "y"), ("sample.hakorc", "# comment", "c"),
    ("sample.sql", "select", "k"), ("sample.sql", "'it''s'", "s"), ("sample.sql", "-- c", "c"),
]

def dump(name):
    out = subprocess.run([HAKE, "--hl-dump", os.path.join(HERE, "hl", name)], capture_output=True, text=True, check=True).stdout
    rows = out.split("\n")[1:]
    return [(rows[i], rows[i + 1]) for i in range(0, len(rows) - 1, 2)]

def main():
    cache, fails = {}, 0
    for name, piece, want in CASES:
        lines = cache.setdefault(name, dump(name))
        hit = None
        for text, cls in lines:
            b = text.encode()
            at = b.find(piece.encode())
            if at >= 0:
                hit = cls[at:at + len(piece.encode())]
                break
        if hit is None:
            ok, got = False, "(text not found)"
        else:
            exp = want if len(want) == len(hit) else want * len(hit)
            ok, got = hit == exp, hit
        if not ok:
            fails += 1
            print("  FAIL %-12s %-34r want %-6s got %s" % (name, piece, want, got))
    print("  ok   %d of %d pieces" % (len(CASES) - fails, len(CASES)))
    print("RESULT:", "PASS" if not fails else "FAIL")
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
