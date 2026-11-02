#!/usr/bin/env python3
"""Speed proxy: how well the reference engines' speed ratio (this computer / calibration machine)
stands for other engines'. Input: speed_replay.py output.
Rows whose node count differs from the recorded one by more than 5% are not the same search and
are dropped (except Blunder 8.0.0, which the estimator replays by depth: it carries history between
searches, so its node counts differ). Families are weighted equally in the second figure: 15 Stash
versions are not 15 independent engines.
Usage: speed_proxy.py data/elo/speed_replay_m4_2026-11-02.json"""
import json, math, statistics as st, sys, collections

REFS = ("stockfish-11", "blunder-8.0.0")


def family(e):
    return e.rsplit("-", 1)[0]


def analyse(d):
    """{"speed": engine -> median ratio, "spread": engine -> repeat spread, "kept"/"total": counts,
    "method": engine -> replay method, "dev": engine -> log2(ratio / references' geometric mean),
    "summary": [(name, rms, n, family rms, families, max |dev|, engine)]}"""
    per = collections.defaultdict(list)       # engine -> per-repeat median speed ratio
    kept = collections.Counter(); total = collections.Counter()
    for run in d["runs"]:
        same = lambda x: run["engine"] == "blunder-8.0.0" or abs(x["nodes"] - x["calNodes"]) <= 0.05 * x["calNodes"]
        rows = [x for x in run["rows"] if x.get("ms") and x["calNodes"] and same(x)]
        total[run["engine"]] += len(run["rows"]); kept[run["engine"]] += len(rows)
        if len(rows) >= 10:
            per[run["engine"]].append(st.median(x["calMs"] / x["ms"] for x in rows))
    S = {e: st.median(v) for e, v in per.items()}
    out = {"speed": S, "kept": kept, "total": total, "summary": [], "dev": {},
           "spread": {e: max(v) / min(v) - 1 for e, v in per.items()},
           "method": {r["engine"]: r["method"] for r in d["runs"]}}
    if not all(r in S for r in REFS):
        return out
    sf, bl = S["stockfish-11"], S["blunder-8.0.0"]
    others = [e for e in S if e not in REFS]
    for name, ref in (("both (geometric mean)", math.sqrt(sf * bl)), ("stockfish-11 only", sf), ("blunder-8.0.0 only", bl)):
        dev = {e: math.log2(S[e] / ref) for e in others}
        if not dev:
            continue
        if name.startswith("both"):
            out["dev"] = dev
        rms = math.sqrt(sum(v * v for v in dev.values()) / len(dev))
        fams = collections.defaultdict(list)
        for e, v in dev.items(): fams[family(e)].append(v * v)
        rmsf = math.sqrt(st.mean(st.mean(v) for v in fams.values()))
        worst = max(dev, key=lambda e: abs(dev[e]))
        out["summary"].append((name, rms, len(dev), rmsf, len(fams), abs(dev[worst]), worst))
    return out


def main():
    d = json.load(open(sys.argv[1]))
    a = analyse(d)
    for e in sorted(a["total"]):
        if e not in a["speed"]:
            print("not used (fewer than 10 repeatable searches): %s, %d of %d" % (e, a["kept"][e], a["total"][e]))
    print("engine            method  kept   speed   repeat spread")
    for e in sorted(a["speed"], key=lambda e: a["speed"][e]):
        print("%-16s %-6s %3d/%3d %6.3f  %5.2f%%" % (e, a["method"][e], a["kept"][e], a["total"][e], a["speed"][e],
                                                    100 * a["spread"][e]))
    for name, rms, n, rmsf, nf, mx, worst in a["summary"]:
        print("%-22s RMS log2 %.3f over %d engines; family-weighted %.3f over %d families; max |dev| %.3f (%s)"
              % (name, rms, n, rmsf, nf, mx, worst))


if __name__ == "__main__":
    main()
