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
# :login asks for the key with input_request, as hako does over --pipe; the line that
# comes back must be an answer carrying exactly what was typed (smoke.py types sk-FAKE-rei)
for line in iter(sys.stdin.readline, ""):
    if '"cmd":":login' in line:
        emit({"type": "input_request", "prompt": "paste API key (input hidden): ", "hidden": 1})
        try: ev = json.loads(sys.stdin.readline())
        except ValueError: ev = {}
        good = ev.get("type") == "answer" and ev.get("answer") == "sk-FAKE-rei"
        emit({"type": "message", "role": "system", "text": "ANSWER_OK" if good else "ANSWER_BAD " + str(ev.get("type"))})
        emit({"type": "done", "turn": 1})
        continue
    # a turn that runs until the pane steers it: said back as hako does (a user message
    # where the model read it), then the turn ends; what the pane queued comes after
    if "LONGTURN" in line:
        for more in iter(sys.stdin.readline, ""):
            try: ev = json.loads(more)
            except ValueError: continue
            if ev.get("type") == "steer":
                emit({"type": "message", "role": "user", "text": ev.get("text", "")})
                break
            emit({"type": "message", "role": "system", "text": "EARLY " + str(ev.get("type"))})
        emit({"type": "message", "role": "ai", "text": "LONG_DONE"})
        emit({"type": "done", "turn": 1})
        continue
    if "QUEUED_LINE" in line:
        emit({"type": "message", "role": "ai", "text": "QUEUED_SENT"})
        emit({"type": "done", "turn": 1})
        continue
    emit({"type": "message", "role": "ai", "pad": "y" * 70000, "text": "BIGREPLY_OK"})
    emit({"type": "done", "turn": 1})
