#!/usr/bin/env python3
"""A stand-in for `hako --pipe`, for tools/smoke.py. Speaks just enough of the
protocol to put one event on the wire that is longer than 64 KB, which the Rei
pane's reader used to split in two (the first half parsed with its fields
missing). Answers every line it is sent with that event and a done."""
import sys, json
def emit(o):
    sys.stdout.write(json.dumps(o, separators=(",", ":")) + "\n"); sys.stdout.flush()
emit({"type": "init", "session": "smoke", "resumed": 0, "turns": 0, "provider": "fake", "model": "fake", "protocol": 1})
emit({"type": "message", "role": "ai", "pad": "x" * 70000, "text": "BIGLINE_OK"})
for line in sys.stdin:
    emit({"type": "message", "role": "ai", "pad": "y" * 70000, "text": "BIGREPLY_OK"})
    emit({"type": "done", "turn": 1})
