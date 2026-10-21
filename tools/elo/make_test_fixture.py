#!/usr/bin/env python3
"""Writes tests/data/elo_fixture.json: a small synthetic model and response sets,
with the ability estimates computed by irt.estimate_theta. The C++ estimator
must reproduce them (tests/test_elo.cpp). The model alone also goes to
tests/data/elo_test_model.json for the chess-cli test."""
import json, os, sys

import numpy as np

import irt

rng = np.random.default_rng(2026)
N = 60
a = np.round(rng.lognormal(0.2, 0.4, N), 6)
b = np.round(rng.normal(0.2, 1.0, N), 6)
groups = ["tactics", "positional", "endgame"]
items = [{"id": "syn-%d" % i, "fen": "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", "category": groups[i % 3],
          "group": groups[i % 3], "a": float(a[i]), "b": float(b[i]), "ok": ["e2e4"]} for i in range(N)]
prior_mean, prior_sd = 0.1, 3.0
cases = []
for true_theta in (-1.5, 0.3, 2.2):
    y = (rng.random(N) < irt.sigmoid(a * (true_theta - b))).astype(float)
    ask = sorted(rng.choice(N, size=40, replace=False).tolist())
    m = np.zeros(N); m[ask] = 1
    th, se = irt.estimate_theta(y, m, a, b, prior_mean=prior_mean, prior_sd=prior_sd)
    cases.append({"responses": [[i, bool(y[i])] for i in ask], "theta": th, "se": se})
# all solved / none solved: the prior keeps the estimate finite
for val in (1.0, 0.0):
    y = np.full(N, val); m = np.ones(N)
    th, se = irt.estimate_theta(y, m, a, b, prior_mean=prior_mean, prior_sd=prior_sd)
    cases.append({"responses": [[i, bool(val)] for i in range(N)], "theta": th, "se": se})
model = {"format": "chessgui-elo-model", "version": 0, "threshold": 5.0,
         "protocol": {"baseMovetimeMs": 100, "calibNps": 0},
         "elo": {"intercept": irt.CENTER, "slope": irt.SCALE}, "prior": {"mean": prior_mean, "sd": prior_sd},
         "adaptive": {"seTargetElo": 60.0, "maxItems": 50, "minItems": 10},
         "groupShare": {"tactics": 0.4, "positional": 0.35, "endgame": 0.25}, "items": items}
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tests", "data", "elo_fixture.json")
json.dump({"generator": "tools/elo/make_test_fixture.py", "model": model, "cases": cases}, open(out, "w"), indent=1)
json.dump(model, open(os.path.join(os.path.dirname(out), "elo_test_model.json"), "w"), indent=1)
print("wrote", out, file=sys.stderr)
