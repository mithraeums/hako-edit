#!/usr/bin/env python3
"""Drive `hako --pipe` exactly the way hake's Rei pane does, and assert the
contract between them.

This exists because the two halves drifted apart once already and nobody
noticed for a month: the agent emitted `tool_request`/`name` while the editor
sent `permission_response`/`decision` and listened for `permission_request`.
Zero overlap, so every write tool in the Rei pane waited forever with no
prompt. Inspection did not catch it; only running both ends does.

No model, no network, no credentials: the agent is pointed at the fake engine
from hako-code/tools/tty/fakehakm.py via $HAKO_HAKM.

    python3 tools/pipe-conformance.py [path/to/hako]

Exits non-zero on the first broken expectation.
"""
import json, os, queue, shutil, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
EDIT = os.path.dirname(HERE)
ROOT = os.path.dirname(EDIT)
FAKE = os.path.join(ROOT, "hako-code", "tools", "tty", "fakehakm.py")
HAKO = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "hako-code", "hako")

import tempfile
_WORK = tempfile.mkdtemp(prefix="hakopipe-")
HOME = os.path.join(_WORK, "home")
PROJ = os.path.join(_WORK, "proj")

fails = []
def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond: fails.append(what)

def setup():
    shutil.rmtree(HOME, ignore_errors=True); shutil.rmtree(PROJ, ignore_errors=True)
    os.makedirs(os.path.join(HOME, ".hako", "models", "hako-sho"))
    os.makedirs(PROJ)
    open(os.path.join(HOME, ".hako", "models", "hako-sho", "hako-sho.mlf2"), "w").write("fake")
    # tool gate ON: the approval round trip is the whole point of this script
    open(os.path.join(HOME, ".hakorc"), "w").write(
        "ai_provider=mithraeum\nai_model=hako-sho\nshow_splash=0\nai_auto_approve=0\n")
    open(os.path.join(PROJ, "alpha.txt"), "w").write("a\n")
    # the pane runs against a trusted project; trust is a tty prompt otherwise
    pdir = os.path.join(HOME, ".hako", "projects")
    os.makedirs(pdir, exist_ok=True)

def main():
    if not os.path.exists(FAKE):
        print("missing fake engine:", FAKE); return 2
    if not os.path.exists(HAKO):
        print("missing hako binary:", HAKO, "(build hako-code first)"); return 2
    setup()
    # SSH_CONNECTION: :login below must not open a browser
    flog = os.path.join(HOME, "fake.log")      # what the model was given
    env = dict(os.environ, HOME=HOME, HAKO_HAKM=FAKE, FAKE_LOG=flog, TERM="dumb", SSH_CONNECTION="127.0.0.1 1 127.0.0.1 2")
    env.pop("OPENAI_API_KEY", None)
    p = subprocess.Popen([HAKO, "--pipe"], cwd=PROJ, env=env,
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True, bufsize=1)

    seen, raw = [], []
    def send(obj):
        p.stdin.write(json.dumps(obj) + "\n"); p.stdin.flush()

    # a reader thread, so a quiet agent times out instead of blocking readline forever
    lines = queue.Queue()
    threading.Thread(target=lambda: [lines.put(l) for l in iter(p.stdout.readline, "")] + [lines.put(None)],
                     daemon=True).start()

    def pump(pred, secs=25):
        """Read events until pred(event) is true; returns it or None."""
        end = time.time() + secs
        while time.time() < end:
            try: line = lines.get(timeout=max(0.05, end - time.time()))
            except queue.Empty: return None
            if line is None: return None
            line = line.strip()
            if not line: continue
            try: ev = json.loads(line)
            except Exception: raw.append(line); continue
            seen.append(ev)
            if pred(ev): return ev
        return None

    # 1. every line the agent writes must be one JSON object with a "type"
    init = pump(lambda e: e.get("type") in ("init", "message", "system"), 20)
    check(init is not None, "the agent emits a typed JSON event on startup")
    first_init = next((e for e in seen if e.get("type") == "init"), None)
    check(first_init is not None and first_init.get("protocol") == 2,
          "init declares protocol 2 — the pane reads it (got %r)" % (first_init or {}).get("protocol"))

    # 2. trust, so tools are allowed at all
    send({"type": "slash", "cmd": ":trust"})
    time.sleep(0.6)

    # 3. a prompt that makes the fake engine call a tool
    send({"type": "prompt", "text": "TOOL please"})
    t_ask = time.time()

    req = pump(lambda e: e.get("type") == "tool_request", 25)
    check(req is not None, "a gated tool produces tool_request (not permission_request)")
    # It used to wait 5 s for a keyboard reader that --pipe does not have.
    check(time.time() - t_ask < 3.0, "the approval request arrives promptly (%.1fs)" % (time.time() - t_ask))
    if req:
        check("id" in req, "tool_request carries an id to answer")
        check("name" in req, "tool_request carries name (not 'tool')")
        for f in ("kind", "subject"):
            check(f in req, "tool_request carries %s, which the pane renders" % f)

        # 3b. said while the approval waits: a steer goes into this turn after the tool,
        # a prompt waits for the next (the approval loop used to drop both)
        send({"type": "steer", "text": "STEERED in pipe"})
        send({"type": "prompt", "text": "AFTER the turn"})

        # 4. answer it the way hake does
        send({"type": "approve", "id": req["id"], "answer": "y"})

        end = pump(lambda e: e.get("type") in ("tool_end", "done"), 25)
        check(end is not None and end.get("type") == "tool_end",
              "approving runs the tool and reports tool_end")

    done = pump(lambda e: e.get("type") in ("done", "message", "ai"), 25)
    check(done is not None, "the turn completes after the tool")
    dones = lambda: sum(1 for e in seen if e.get("type") == "done")
    said = lambda t: os.path.exists(flog) and ("[user] %s\n" % t) in open(flog).read()
    pump(lambda e: e.get("type") == "done" and said("AFTER the turn"), 25)
    fl = open(flog).read() if os.path.exists(flog) else ""
    check("(sent while you were working) STEERED in pipe" in fl, "a steer said mid-turn reaches the model in that turn")
    check(any(e.get("type") == "message" and e.get("role") == "user" and e.get("text") == "STEERED in pipe" for e in seen),
          "and the pane hears it said, where the turn read it")
    check(said("AFTER the turn"), "a prompt sent while the approval waited runs next, not lost")
    send({"type": "steer", "text": "IDLE steer"})
    pump(lambda e: e.get("type") == "done" and said("IDLE steer"), 25)
    check(said("IDLE steer"), "a steer with no turn running is a prompt")

    # 5. a picker over the pipe: :provider must emit a choice_request the pane can
    # render, and an answer must resolve it (this is how :model/:provider/:sessions
    # work in the Rei pane — hako cannot draw a tty popup over --pipe).
    send({"type": "slash", "cmd": ":provider"})
    cr = pump(lambda e: e.get("type") == "choice_request", 20)
    check(cr is not None, "a picker command emits choice_request")
    if cr:
        check("id" in cr, "choice_request carries an id to answer")
        check(isinstance(cr.get("options"), list) and len(cr["options"]) > 0,
              "choice_request carries a non-empty options list")
        # answer with a 1-based index; the turn should settle (done/message)
        send({"type": "choice", "id": cr["id"], "answer": "1"})
        settled = pump(lambda e: e.get("type") in ("done", "message", "system"), 20)
        check(settled is not None, "answering a choice_request resolves the command")

    # 6. a pasted key (:login in the pane): hako asks with input_request and takes the
    # answer. It printed a raw prompt onto the wire and took the next JSON line as the key
    send({"type": "slash", "cmd": ":login openai"})
    ir = pump(lambda e: e.get("type") == "input_request", 20)
    check(ir is not None, ":login asks for the key with input_request")
    if ir:
        check(ir.get("hidden") == 1 and "API key" in ir.get("prompt", ""), "the ask names what to paste and says it is hidden")
        send({"type": "answer", "answer": "sk-FAKE-conformance"})
        saved = pump(lambda e: e.get("type") == "message" and "key saved" in e.get("text", ""), 20)
        check(saved is not None, "the answer is taken as the key")
        st = pump(lambda e: e.get("type") == "state", 10)
        check(st is not None and st.get("provider") == "openai", "and the provider moves to openai")
    pump(lambda e: e.get("type") == "done", 10)
    check(not raw, "every line on the wire is JSON (%s)" % (raw[:2] or "none other"))

    send({"type": "quit"})
    try: p.wait(timeout=10)
    except subprocess.TimeoutExpired: p.kill(); check(False, "quit exits the agent")

    kinds = sorted({e.get("type", "?") for e in seen})
    print("  events seen:", ", ".join(kinds))
    shutil.rmtree(HOME, ignore_errors=True); shutil.rmtree(PROJ, ignore_errors=True)
    print("RESULT:", "PASS" if not fails else "FAIL (%d)" % len(fails))
    return 0 if not fails else 1

if __name__ == "__main__":
    sys.exit(main())
