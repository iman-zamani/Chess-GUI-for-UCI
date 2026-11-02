#!/usr/bin/env python3
"""Recorded searches of calibration engines, for repeating them on another computer.

speed_replay.py --set replays these searches (the same position and node count,
or depth) and compares the time with the one recorded on the calibration machine:
how much faster that computer runs each engine. speed_proxy.py then checks how
well the reference engines' ratio stands for the others'.

The times of the model v1 run (responses.csv, 2026-10-22) are used as recorded;
the reference engines' searches the estimator replays come from that run too. The
model v2 run (responses_v2.csv, 2026-11-01) recorded the speed of the machine
against those searches with every answer (column "speed": 0.97 = 3% slower than
when they were recorded), so its times are scaled by it to the same footing.

Picks, per engine, up to N searches of 200-520 ms (long enough to time well) in a
fixed order (a hash of the position id), like speed_reference.py.

Usage: replay_set.py --data DIR --engines ID ... [--per-engine 40] --out replay_set.json
DIR holds responses.csv, responses_v2.csv (optional) and reference.jsonl."""
import argparse, csv, hashlib, json, os, sys

MIN_MS, MAX_MS = 200, 520


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--engines", nargs="+", required=True)
    ap.add_argument("--per-engine", type=int, default=40)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    fens = {}
    for line in open(os.path.join(a.data, "reference.jsonl")):
        if line.strip():
            j = json.loads(line)
            fens[j["id"]] = j["fen"]
    rows = {e: [] for e in a.engines}
    for name in ("responses.csv", "responses_v2.csv"):
        path = os.path.join(a.data, name)
        if not os.path.exists(path):
            continue
        for r in csv.DictReader(open(path)):
            e = r["engine"]
            if e not in rows or r["error"] or not r["nodes"] or not r["time_ms"] or not r["depth"]:
                continue
            ms = float(r["time_ms"]) * float(r.get("speed") or 1)      # v2: on the v1 footing
            if MIN_MS <= ms <= MAX_MS and r["position"] in fens:
                rows[e].append({"pos": r["position"], "fen": fens[r["position"]], "calNodes": int(r["nodes"]),
                                "calMs": round(ms, 1), "calDepth": int(r["depth"]), "source": name})
    out = {"description": __doc__.split("\n\n")[0], "engines": {}}
    for e in a.engines:
        if len({x["source"] for x in rows[e]}) > 1:
            sys.exit("%s is in both runs" % e)
        pick = sorted(rows[e], key=lambda x: hashlib.sha256(x["pos"].encode()).hexdigest())[:a.per_engine]
        if len(pick) < 10:
            sys.exit("%s: only %d usable searches" % (e, len(pick)))
        out["engines"][e] = pick
        print("%s: %d searches (%s)" % (e, len(pick), pick[0]["source"]), file=sys.stderr)
    json.dump(out, open(a.out, "w"), indent=0)


if __name__ == "__main__":
    main()
