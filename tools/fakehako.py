#!/usr/bin/env python3
"""A stand-in for `hako --pipe`, for tools/smoke.py. Speaks just enough of the
protocol to put one event on the wire that is longer than 64 KB, which the Rei
pane's reader used to split in two (the first half parsed with its fields
missing). Answers every line it is sent with that event and a done."""
import sys, json
def emit(o):
    sys.stdout.write(json.dumps(o, separators=(",", ":")) + "\n"); sys.stdout.flush()
CMDS = [{"n": "theme", "p": ":", "c": "settings", "a": "[name]", "d": "pick a theme"},
        {"n": "model", "p": ":", "c": "model", "a": "[id]", "d": "pick a model"},
        {"n": "compact", "p": "/", "c": "session", "a": "", "d": "shrink the context"}]
emit({"type": "init", "session": "smoke", "resumed": 0, "turns": 0, "provider": "fake", "model": "fake",
      "protocol": 1, "session_name": "notes", "commands": CMDS})
emit({"type": "message", "role": "ai", "pad": "x" * 70000, "text": "BIGLINE_OK"})
# a line longer than the pane: it must wrap between words (tools/modes.py checks every word is whole)
emit({"type": "message", "role": "ai", "text": "WRAP alpha bravo charlie delta echo foxtrot golf hotel india juliet kilo lima"})
for line in sys.stdin:
    emit({"type": "message", "role": "ai", "pad": "y" * 70000, "text": "BIGREPLY_OK"})
    emit({"type": "done", "turn": 1})
