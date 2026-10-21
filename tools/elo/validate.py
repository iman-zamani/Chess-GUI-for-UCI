#!/usr/bin/env python3
"""Cross-validation of the Elo estimator.

For every configuration (threshold T, model, number of positions) and every
fold, the item parameters are refitted and the positions re-selected WITHOUT
the held-out engines, then the held-out engines' Elo is estimated from their
answers on the selected positions. Folds:
  * leave-one-engine-out (LOEO): one calibration engine at a time;
  * leave-one-family-out (LOFO): every version of one engine family at once
    (the harder test: the model never saw that engine's style).
Synthetic engines (Stockfish UCI_Elo levels) are only ever used for training
and are never scored. Reports MAE, RMSE, max error and Spearman rho.

Usage: validate.py --data DIR [--thresholds 2 3 5 7 10] [--models anchored joint]
                   [--items 1000] [--holdout ID ...] [--out cv.json]
DIR holds reference.jsonl and responses.csv; engines come from data/elo/engines.json.
Engines given with --holdout are left out of everything (kept for the final check).
"""
import argparse, json, os, sys

import numpy as np
from scipy.stats import spearmanr

import irt

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINES = os.path.join(HERE, "..", "..", "data", "elo", "engines.json")


def run_folds(d, T, model, n_items, folds):
    """Refit and reselect without the test engines, then estimate them.
    folds: list of (train mask, test mask) over engines. Returns (Elo, SE) per engine."""
    from fit_irt import fit_all
    from select_positions import choose
    pred = np.full(len(d["engines"]), np.nan)
    se = np.full(len(d["engines"]), np.nan)
    for train, test in folds:
        f = fit_all(d, T, model, train)
        items = {"intercept": f["intercept"], "slope": f["slope"],
                 "items": [{"a": f["a"][i], "b": f["b"][i], "fit": f["fit"][i], "category": d["categories"][i]}
                           for i in range(len(d["positions"]))]}
        sel = choose(items, n_items, irt.unusable_positions(d, T))
        Y, M = f["Y"], f["M"]
        for k in np.where(test)[0]:
            t, s = irt.estimate_theta(Y[k, sel], M[k, sel], f["a"][sel], f["b"][sel])
            pred[k] = f["intercept"] + f["slope"] * t
            se[k] = s * f["slope"]
    return pred, se


def metrics(pred, true, mask):
    e = pred[mask] - true[mask]
    rho = spearmanr(pred[mask], true[mask]).correlation if mask.sum() > 2 else float("nan")
    return {"n": int(mask.sum()), "mae": float(np.mean(np.abs(e))), "rmse": float(np.sqrt(np.mean(e * e))),
            "max": float(np.max(np.abs(e))), "bias": float(np.mean(e)), "spearman": float(rho)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--thresholds", nargs="+", type=float, default=[2, 3, 5, 7, 10])
    ap.add_argument("--models", nargs="+", default=["anchored", "joint"])
    ap.add_argument("--items", nargs="+", type=int, default=[1000])
    ap.add_argument("--holdout", nargs="*", default=[])
    ap.add_argument("--out", default=None)
    ap.add_argument("--engines", default=ENGINES)
    a = ap.parse_args()
    d = irt.load_dataset(a.engines, os.path.join(a.data, "reference.jsonl"), os.path.join(a.data, "responses.csv"))
    keep = np.array([e not in a.holdout for e in d["engines"]])
    have = ~np.all(np.isnan(d["L"]), axis=1)
    use = keep & have
    real = use & ~d["synthetic"]
    fam = np.array(d["families"])
    print("%d engines with answers (%d real, %d held out entirely), %d positions"
          % (use.sum(), real.sum(), (~keep).sum(), len(d["positions"])), file=sys.stderr)
    loeo = [(use & (np.arange(len(use)) != k), np.arange(len(use)) == k) for k in np.where(real)[0]]
    lofo = [(use & (fam != f), real & (fam == f)) for f in sorted(set(fam[real]))]
    results = []
    for T in a.thresholds:
        for model in a.models:
            for n in a.items:
                row = {"threshold": T, "model": model, "items": n}
                for name, folds in (("loeo", loeo), ("lofo", lofo)):
                    pred, se = run_folds(d, T, model, n, folds)
                    row[name] = metrics(pred, d["elo"], real)
                    row[name + "_pred"] = {d["engines"][k]: [round(float(pred[k]), 1), round(float(se[k]), 1)]
                                           for k in np.where(real)[0]}
                results.append(row)
                print("T=%-4g %-8s n=%-5d LOEO MAE %6.1f RMSE %6.1f max %6.1f rho %.3f | LOFO MAE %6.1f RMSE %6.1f max %6.1f"
                      % (T, model, n, row["loeo"]["mae"], row["loeo"]["rmse"], row["loeo"]["max"],
                         row["loeo"]["spearman"], row["lofo"]["mae"], row["lofo"]["rmse"], row["lofo"]["max"]),
                      file=sys.stderr, flush=True)
    if a.out:
        json.dump({"engines": [e for e, u in zip(d["engines"], real) if u], "holdout": a.holdout,
                   "results": results}, open(a.out, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
