#!/usr/bin/env python3
"""Speed replay experiment: how well the reference engines' speed stands for other engines'.

Calibration engines built on this computer repeat searches they made on the calibration
machine: "go nodes N" (N = the node count recorded there) or, for engines that report at
iteration ends, "go depth d" when that gives the same node count. The time ratio
(recorded / here) is each engine's speed relative to the calibration machine;
speed_proxy.py compares the engines' ratios with the reference engines'.
The result for model v2 is data/elo/speed_replay_m4_2026-11-02.json (speed_constants.json).

Usage: speed_replay.py OUT.json --status build_status.json (--data DIR | --set SET.json) [--repeats 2]
                       [--positions 40] [--policy normal|bg] --engines ID ...
DIR holds the calibration run's responses.csv and reference.jsonl (the same positions for
every engine); SET.json is replay_set.py output (each engine its own recorded searches).
--status maps engine ids to {"binary": path}."""
import argparse, csv, json, os, queue, random, subprocess, sys, threading, time


FORCE = {"blunder-8.0.0": "depth"}   # per-iteration node counts and history carried over: replay by depth


class Eng:
    def __init__(self, cmd):
        self.p = subprocess.Popen(cmd, cwd=os.path.dirname(cmd[-1]), stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1,
                                  encoding="utf-8", errors="replace")
        self.q = queue.Queue()
        self.limit = 120
        threading.Thread(target=self._r, daemon=True).start()
        self.send("uci"); self.wait("uciok")
        self.send("setoption name Hash value 64"); self.send("setoption name Threads value 1")
        self.send("isready"); self.wait("readyok")

    def _r(self):
        for l in self.p.stdout:
            self.q.put(l.strip())
        self.q.put(None)

    def send(self, s):
        self.p.stdin.write(s + "\n"); self.p.stdin.flush()

    def wait(self, pref, timeout=60):
        end = time.monotonic() + timeout
        while True:
            l = self.q.get(timeout=max(0.01, end - time.monotonic()))
            if l is None:
                raise RuntimeError("engine exited")
            if l.startswith(pref):
                return l
            if l.startswith("info") and " nodes " in l and " time " in l:
                self.last = l

    def search(self, fen, go):
        self.send("ucinewgame"); self.send("isready"); self.wait("readyok")
        self.last = ""; self.stopped = False
        self.send("position fen " + fen)
        t0 = time.monotonic()
        self.send("go " + go)
        try:
            self.wait("bestmove", self.limit)
        except Exception:
            self.send("stop"); self.wait("bestmove", 10)
            self.stopped = True
        wall = (time.monotonic() - t0) * 1000
        t = self.last.split()
        nodes = int(t[t.index("nodes") + 1]) if "nodes" in t else 0
        ms = int(t[t.index("time") + 1]) if "time" in t else 0
        return nodes, ms, wall

    def close(self):
        try:
            self.send("quit"); self.p.wait(timeout=3)
        except Exception:
            self.p.kill()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--status", required=True, help="build_engines.py output on this computer")
    ap.add_argument("--data", help="dir with the calibration run's responses.csv and reference.jsonl")
    ap.add_argument("--set", help="replay_set.py output")
    ap.add_argument("--policy", default="normal")
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--positions", type=int, default=40)
    ap.add_argument("--engines", nargs="+", required=True)
    a = ap.parse_args()
    st = json.load(open(a.status))
    fens, cal, posOf = {}, {}, {}
    if a.set:
        rs = json.load(open(a.set))["engines"]
        for e in a.engines:
            for x in rs[e][:a.positions]:
                fens[x["pos"]] = x["fen"]
                cal[(e, x["pos"])] = (x["calNodes"], x["calMs"], x["calDepth"])
            posOf[e] = [x["pos"] for x in rs[e][:a.positions]]
        pos = sorted({p for v in posOf.values() for p in v})
    else:
        RESP, REF = os.path.join(a.data, "responses.csv"), os.path.join(a.data, "reference.jsonl")
        for l in open(REF):
            if l.strip():
                j = json.loads(l); fens[j["id"]] = j["fen"]
        for r in csv.DictReader(open(RESP)):
            if r["engine"] in a.engines and r["nodes"] and r["time_ms"] and not r["error"]:
                if int(r["time_ms"]) >= 200:
                    cal[(r["engine"], r["position"])] = (int(r["nodes"]), int(r["time_ms"]), int(r["depth"] or 0))
        rnd = random.Random(1)
        common = sorted(set(p for (e, p) in cal if all((x, p) in cal for x in a.engines)))
        pos = rnd.sample(common, min(a.positions, len(common)))
        posOf = {e: pos for e in a.engines}
    out = {"policy": a.policy, "positions": pos, "runs": [], "errors": []}
    methods = {}
    for rep in range(a.repeats):
        for e in a.engines:
            if e not in st:
                print("%s: not built, skipped" % e, file=sys.stderr)
                continue
            cmd = [st[e]["binary"]]
            if a.policy == "bg":
                cmd = ["/usr/sbin/taskpolicy", "-b"] + cmd
            try:
                one(a, e, cmd, rep, cal, fens, posOf, methods, out)
            except Exception as ex:                         # an engine that fails is reported, not fatal
                out["errors"].append({"engine": e, "repeat": rep, "error": "%s: %s" % (type(ex).__name__, ex)})
                print("%s: failed: %s" % (e, ex), file=sys.stderr, flush=True)
            json.dump(out, open(a.out, "w"))


def one(a, e, cmd, rep, cal, fens, posOf, methods, out):
    eng = Eng(cmd)
    try:
        run_engine(a, eng, e, rep, cal, fens, posOf, methods, out)
    finally:
        eng.close()


def run_engine(a, eng, e, rep, cal, fens, posOf, methods, out):
    rows = []
    # replay method: "depth d" when the engine reports at iteration ends (same nodes come back),
    # else "nodes N"; decided on the first 3 positions
    method = methods.get(e) or FORCE.get(e)
    if method is None:
        hits = 0
        for p in posOf[e][:3]:
            n, ms, d = cal[(e, p)]
            got, gms, wall = eng.search(fens[p], "depth %d" % d)
            hits += abs(got - n) <= 0.01 * n
        method = "depth" if hits >= 2 else None
        if method is None:                         # does it stop at a node count?
            n, ms, d = cal[(e, posOf[e][0])]
            eng.limit = max(3.0, ms / 100.0)
            got, gms, wall = eng.search(fens[posOf[e][0]], "nodes %d" % n)
            method = "nodes" if not eng.stopped and abs(got - n) <= 0.05 * n else "depth"
            eng.limit = 120
        methods[e] = method
    for p in posOf[e]:
        n, ms, d = cal[(e, p)]
        got, gms, wall = eng.search(fens[p], "depth %d" % d if method == "depth" else "nodes %d" % n)
        rows.append({"pos": p, "calNodes": n, "calMs": ms, "nodes": got, "ms": gms, "wall": round(wall, 1)})
    out["runs"].append({"engine": e, "repeat": rep, "method": method, "rows": rows})
    print(a.policy, rep, e, "done", file=sys.stderr, flush=True)


if __name__ == "__main__":
    main()
