#!/usr/bin/env python3
"""Writes the results section of docs/elo-estimator.md from the pipeline's output,
so every number in the documentation comes straight from the files.

Usage: report.py --cv cv.json --model Resources/elo/model.json
                 [--holdout-json estimate.json ...] > results.md
--holdout-json: `chess-cli elo-estimate --json` output for engines kept out of the fit.
"""
import argparse, json, os, sys

import irt

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINES = os.path.join(HERE, "..", "..", "data", "elo", "engines.json")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cv", required=True)
    ap.add_argument("--model", required=True)
    ap.add_argument("--holdout-json", nargs="*", default=[])
    ap.add_argument("--holdout-ids", nargs="*", default=[], help="engine ids, in the order of --holdout-json")
    ap.add_argument("--engines", default=ENGINES)
    a = ap.parse_args()
    cv = json.load(open(a.cv))
    model = json.load(open(a.model))
    eng = {e["id"]: e for e in json.load(open(a.engines))["engines"]}
    chosen = next(r for r in cv["results"] if r["threshold"] == model["threshold"] and r["model"] == model["model"]
                  and r["items"] == model.get("itemsRequested", len(model["items"])))
    out = []
    w = out.append
    w("Calibration: %d engines from %d families, CCRL Blitz %d-%d. Model: %s fit, solved = a move within "
      "%g win-%% points of the best, %d positions."
      % (len(cv["engines"]), len({eng[e]["family"] for e in cv["engines"]}),
         min(eng[e]["ccrl"]["rating"] for e in cv["engines"]), max(eng[e]["ccrl"]["rating"] for e in cv["engines"]),
         model["model"], model["threshold"], len(model["items"])))
    w("")
    w("### Cross-validation")
    w("")
    w("Every fold refits the positions and reselects them without the engines it predicts. "
      "*Leave-one-engine-out* (LOEO) predicts each engine from the others; *leave-one-family-out* "
      "(LOFO) predicts every version of an engine family from the other families only, the "
      "honest test for an engine the model has never seen.")
    w("")
    w("| T (win %) | model | LOEO MAE | LOEO RMSE | LOEO max | Spearman | LOFO MAE | LOFO RMSE | LOFO max |")
    w("|---|---|---|---|---|---|---|---|---|")
    for r in cv["results"]:
        mark = " **(shipped)**" if r is chosen else ""
        l, f = r["loeo"], r["lofo"]
        w("| %g | %s%s | %.0f | %.0f | %.0f | %.3f | %.0f | %.0f | %.0f |"
          % (r["threshold"], r["model"], mark, l["mae"], l["rmse"], l["max"], l["spearman"], f["mae"], f["rmse"], f["max"]))
    w("")
    ratings = {e: eng[e]["ccrl"]["rating"] for e in cv["engines"]}
    lo_b = irt.band_errors({e: v[0] for e, v in chosen["loeo_pred"].items()}, ratings)
    lf_b = irt.band_errors({e: v[0] for e, v in chosen["lofo_pred"].items()}, ratings)
    w("By rating band (shipped configuration; bias > 0 means estimates too high):")
    w("")
    w("| CCRL Blitz | engines | LOEO MAE | LOEO bias | LOFO MAE | LOFO bias |")
    w("|---|---|---|---|---|---|")
    for band in irt.BANDS:
        if band in lo_b:
            n, m1, b1 = lo_b[band]; _, m2, b2 = lf_b[band]
            w("| %s | %d | %.0f | %+.0f | %.0f | %+.0f |" % (irt.band_name(*band), n, m1, b1, m2, b2))
    w("")
    if model.get("errorBands"):
        w("The 95% interval uses the LOFO RMSE of the engines *estimated* in the same range, "
          "since that is what is known when a new engine is tested:")
        w("")
        w("| Estimate | engines | LOFO RMSE | model error in the 95% interval |")
        w("|---|---|---|---|")
        for b in model["errorBands"]:
            w("| %s | %d | %.0f | +/- %.0f |" % (irt.band_name(b["from"], b["to"]), b["engines"], b["rmse"], 1.96 * b["rmse"]))
        w("")
    if model.get("reliableFromElo"):
        w("Estimates below %d get a warning in the GUI and in `chess-cli`." % model["reliableFromElo"])
        w("")
    w("The configuration with the lowest LOEO MAE is shipped. Choosing it on the LOEO numbers makes "
      "them slightly optimistic; the LOFO numbers and the held-out engine below are the checks that "
      "were not used for any choice.")
    w("")
    w("### Per engine (shipped configuration)")
    w("")
    w("| Engine | CCRL Blitz | LOEO estimate | error | LOFO estimate | error |")
    w("|---|---|---|---|---|---|")
    rows = sorted(cv["engines"], key=lambda e: eng[e]["ccrl"]["rating"])
    for e in rows:
        r = eng[e]["ccrl"]["rating"]
        lo = chosen["loeo_pred"][e][0]
        lf = chosen["lofo_pred"][e][0]
        w("| %s %s | %d | %.0f | %+.0f | %.0f | %+.0f |" % (eng[e]["family"], eng[e]["version"], r, lo, lo - r, lf, lf - r))
    w("")
    if a.holdout_json:
        w("### Held-out engine")
        w("")
        w("Kept out of every fit and of the cross-validation, then estimated with the shipped model:")
        w("")
        w("| Engine | CCRL Blitz | Mode | Estimate | 95% interval | Inside? |")
        w("|---|---|---|---|---|---|")
        for path, eid in zip(a.holdout_json, a.holdout_ids):
            j = json.load(open(path))
            r = eng[eid]["ccrl"]["rating"]
            inside = abs(j["elo"] - r) <= j["ci95"]
            w("| %s %s | %d | %s, %d positions | %d | +/- %d | %s |"
              % (eng[eid]["family"], eng[eid]["version"], r, j["mode"], j["positions"], j["elo"], j["ci95"],
                 "yes" if inside else "**no** (error %+d)" % (j["elo"] - r)))
        w("")
    print("\n".join(out))


if __name__ == "__main__":
    sys.exit(main())
