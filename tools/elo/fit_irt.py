#!/usr/bin/env python3
"""Fit the 2PL item parameters on all calibration engines.

Writes items.json: for every candidate position its a, b (theta scale), the
theta -> Elo line, a fit statistic (mean squared standardised residual, ~1 for
a well-fitting item) and the solve rate. select_positions.py picks the final
set from it.

Usage: fit_irt.py --data DIR --threshold T [--model anchored|joint] --out items.json
"""
import argparse, json, os, sys

import numpy as np

import irt

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINES = os.path.join(HERE, "..", "..", "data", "elo", "engines.json")


def fit_all(d, threshold, model, mask=None):
    """Item parameters from the engines in `mask` (default: all with answers).
    Returns dict with a, b, intercept, slope, theta (per engine), fit."""
    Y, M = irt.responses(d["L"], threshold)
    use = ~np.all(np.isnan(d["L"]), axis=1) if mask is None else mask
    elo = d["elo"][use]
    if model == "anchored":
        theta = irt.theta_of(elo)
        a, b, _, _ = irt.fit_items_anchored(Y[use], M[use], theta)
        intercept, slope = irt.CENTER, irt.SCALE
    else:
        theta, a, b = irt.fit_joint(Y[use], M[use])
        slope, intercept = np.polyfit(theta, elo, 1)
    p = irt.sigmoid(a[None, :] * (theta[:, None] - b[None, :]))
    w = M[use]
    z2 = w * (Y[use] - p) ** 2 / np.clip(p * (1 - p), 1e-6, None)
    fit = z2.sum(0) / np.maximum(w.sum(0), 1)
    rate = (Y[use] * w).sum(0) / np.maximum(w.sum(0), 1)
    return {"a": a, "b": b, "intercept": float(intercept), "slope": float(slope), "theta": theta,
            "fit": fit, "rate": rate, "Y": Y, "M": M}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--threshold", type=float, required=True)
    ap.add_argument("--model", default="anchored", choices=["anchored", "joint"])
    ap.add_argument("--holdout", nargs="*", default=[])
    ap.add_argument("--engines", default=ENGINES)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    d = irt.load_dataset(a.engines, os.path.join(a.data, "reference.jsonl"), os.path.join(a.data, "responses.csv"))
    mask = ~np.all(np.isnan(d["L"]), axis=1) & np.array([e not in a.holdout for e in d["engines"]])
    f = fit_all(d, a.threshold, a.model, mask)
    out = {"threshold": a.threshold, "model": a.model, "intercept": f["intercept"], "slope": f["slope"],
           "engines": [e for e, m in zip(d["engines"], mask) if m], "holdout": a.holdout,
           "items": [{"id": d["positions"][i], "category": d["categories"][i], "a": float(f["a"][i]),
                      "b": float(f["b"][i]), "fit": float(f["fit"][i]), "rate": float(f["rate"][i])}
                     for i in range(len(d["positions"]))]}
    json.dump(out, open(a.out, "w"), indent=1)
    print("%d items fitted on %d engines" % (len(out["items"]), mask.sum()), file=sys.stderr)


if __name__ == "__main__":
    main()
