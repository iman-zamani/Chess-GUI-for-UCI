#!/usr/bin/env python3
"""Pick the final test positions and write the estimator's model.

From items.json (fit_irt.py): drop items with a <= A_MIN or a poor fit
(FIT_MAX), then irt.select_items balances categories and spreads difficulty.
Writes Resources/elo/positions.epd (id, category, source in opcodes) and
Resources/elo/model.json (everything the C++ estimator needs: per-position a, b
and acceptable moves, threshold, theta -> Elo line, prior, protocol, the
cross-validation results and the calibration engine list).

Usage: select_positions.py --data DIR --items items.json --cv cv.json --calib-nps N
                           [--n 1000] [--movetime 500] --out-dir Resources/elo
"""
import argparse, datetime, json, os, sys

import numpy as np

import irt

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINES = os.path.join(HERE, "..", "..", "data", "elo", "engines.json")
FIT_MAX = 2.0           # mean squared standardised residual; ~1 for an item that fits
A_MIN = 0.25


def elo_scaled(a, b, intercept, slope):
    """a, b re-expressed on the theta = (Elo - CENTER) / SCALE scale used by the selection bins."""
    k = slope / irt.SCALE
    return a / k, irt.theta_of(intercept + slope * b)


def choose(items, n):
    a = np.array([x["a"] for x in items["items"]])
    b = np.array([x["b"] for x in items["items"]])
    fit = np.array([x["fit"] for x in items["items"]])
    a_s, b_s = elo_scaled(a, b, items["intercept"], items["slope"])
    cats = [x["category"] for x in items["items"]]
    return irt.select_items(a_s, b_s, cats, n=n, a_min=A_MIN, exclude=fit > FIT_MAX)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, help="dir with candidates.jsonl, reference.jsonl, responses.csv")
    ap.add_argument("--items", required=True)
    ap.add_argument("--cv", required=True, help="validate.py output; the entry matching items.json is used")
    ap.add_argument("--calib-nps", type=float, required=True, help="chess-cli bench on the calibration machine")
    ap.add_argument("--movetime", type=int, default=500)
    ap.add_argument("--n", type=int, default=1000)
    ap.add_argument("--version", type=int, default=1)
    ap.add_argument("--engines", default=ENGINES)
    ap.add_argument("--out-dir", required=True)
    a = ap.parse_args()
    items = json.load(open(a.items))
    refs = {json.loads(l)["id"]: json.loads(l) for l in open(os.path.join(a.data, "reference.jsonl")) if l.strip()}
    cands = {json.loads(l)["id"]: json.loads(l) for l in open(os.path.join(a.data, "candidates.jsonl")) if l.strip()}
    cv = json.load(open(a.cv))
    row = next((r for r in cv["results"] if r["threshold"] == items["threshold"] and r["model"] == items["model"]
                and r["items"] == a.n), None)
    if row is None:
        sys.exit("no cross-validation result for threshold %g, model %s, %d items" % (items["threshold"], items["model"], a.n))
    sel = choose(items, a.n)
    T = items["threshold"]
    out_items, epd = [], []
    for i in sel:
        it = items["items"][i]
        ref = refs[it["id"]]
        table, best = irt.position_table(ref)
        ok = sorted(m for m, w in table.items() if best - w <= T)
        c = cands.get(it["id"], {})
        out_items.append({"id": it["id"], "fen": ref["fen"], "category": it["category"],
                          "group": irt.CATEGORY_GROUPS.get(it["category"], "positional"),
                          "a": round(it["a"], 6), "b": round(it["b"], 6), "ok": ok})
        f = ref["fen"].split()
        epd.append('%s id "%s"; c0 "%s"; c1 "%s"; c2 "%s"; hmvc %s; fmvn %s;'
                   % (" ".join(f[:4]), it["id"], it["category"], c.get("source", ""), c.get("url", ""), f[4], f[5]))
    eng = {e["id"]: e for e in json.load(open(a.engines))["engines"]}
    calib = [{"name": "%s %s" % (eng[e]["family"], eng[e]["version"]), "elo": eng[e]["ccrl"]["rating"],
              "family": eng[e]["family"]} for e in items["engines"]]
    lo, lf = row["loeo"], row["lofo"]
    text = ("leave-one-engine-out on %d engines: MAE %.0f, RMSE %.0f, max %.0f Elo; "
            "leave-one-family-out (unseen engine styles): MAE %.0f, RMSE %.0f Elo"
            % (lo["n"], lo["mae"], lo["rmse"], lo["max"], lf["mae"], lf["rmse"]))
    model = {
        "format": "chessgui-elo-model", "version": a.version,
        "created": datetime.date.today().isoformat(),
        "threshold": T, "model": items["model"],
        "protocol": {"baseMovetimeMs": a.movetime, "calibNps": a.calib_nps, "factorMin": 0.25, "factorMax": 4,
                     "hashMb": 64, "threads": 1, "moveOverheadMs": 10, "graceMs": 50},
        "elo": {"intercept": items["intercept"], "slope": items["slope"]},
        "prior": {"mean": 0.0, "sd": 3.0},
        "adaptive": {"seTargetElo": 50.0, "maxItems": 300, "minItems": 20},
        "groupShare": irt.GROUP_SHARE,
        "errorRmse": lf["rmse"],            # unseen-family error: the honest one for a new engine
        "validation": {"loeo": lo, "lofo": lf},
        "validationText": text,
        "calibration": calib,
        "items": out_items,
    }
    os.makedirs(a.out_dir, exist_ok=True)
    json.dump(model, open(os.path.join(a.out_dir, "model.json"), "w"), indent=1)
    with open(os.path.join(a.out_dir, "positions.epd"), "w") as f:
        f.write("# Elo estimator test positions (Lichess, CC0). Opcodes: id, c0 category, c1 source, c2 URL.\n")
        f.write("\n".join(epd) + "\n")
    groups = {}
    for x in out_items:
        groups[x["group"]] = groups.get(x["group"], 0) + 1
    print("selected %d positions %s; model written to %s" % (len(out_items), groups, a.out_dir), file=sys.stderr)


if __name__ == "__main__":
    main()
