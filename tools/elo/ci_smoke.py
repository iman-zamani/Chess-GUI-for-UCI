#!/usr/bin/env python3
"""End-to-end check of the Elo estimator on a CI runner (.github/workflows/ci.yml).

Runs `chess-cli elo-estimate --quick` on Blunder 8.0.0 (a reference engine and a
calibration engine, CCRL Blitz 2654), with the real reference-engine speed
measurement and CPU placement of this runner, and checks that:
  * the speed was measured with the reference engines (not the fallback),
  * the run passed its checks (status "ok": no speed drift, the engine used
    its time, one CPU's worth of CPU time, ...),
  * the estimate is within its 95% interval (+ 50 Elo of slack, since a
    shared runner is noisy) of the CCRL rating.
A failed run is tried once more before failing (shared runners can be busy).

Usage: ci_smoke.py --cli build/chess-cli --engine Resources/reference/blunder-8.0.0 [--rating 2654]
"""
import argparse, json, subprocess, sys


def once(a):
    p = subprocess.run([a.cli, "elo-estimate", "--engine", "cmd=" + a.engine, "--quick", "--json"],
                       capture_output=True, text=True)
    sys.stderr.write(p.stderr[-3000:])
    try:
        j = json.loads(p.stdout)
    except ValueError:
        return False, "no JSON from chess-cli (exit %d)" % p.returncode
    d = j["diagnostics"]
    speeds = ", ".join("%s %.2fx" % (e["id"], e["speed"]) for e in d["speedStart"]["engines"])
    checks = ", ".join("%ds ref %s own %s" % (c["atSec"], "-" if c["reference"] is None else "%+.3f" % c["reference"],
                                               "-" if c["own"] is None else "%+.3f" % c["own"]) for c in d["speedChecks"])
    line = ("status %s, %s; move time %d ms; speed: %s; checks: %s; probe: %s; time use %.2f; cpu %.2f; placement: %s"
            % (j["status"], j["headline"], j["movetimeMs"], speeds, checks, d["ownProbe"] or "-", d["timeUse"],
               d["cpuRatio"], d["placement"]))
    print(line)
    for w in d["warnings"] + d["failures"]:
        print("  " + w)
    # as an annotation too, so the result can be read without the job log
    print("::notice title=Elo estimate::" + "%0A".join([line] + d["warnings"] + d["failures"]))
    if not d["referenceSpeed"]:
        return False, "the reference engines were not used"
    if j["status"] != "ok":
        return False, "status " + j["status"]
    if abs(j["elo"] - a.rating) > j["ci95"] + 50:
        return False, "estimate %d is not within %d of %d" % (j["elo"], j["ci95"] + 50, a.rating)
    return True, ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", required=True)
    ap.add_argument("--engine", required=True)
    ap.add_argument("--rating", type=int, default=2654)
    a = ap.parse_args()
    for attempt in (1, 2):
        ok, why = once(a)
        if ok:
            print("OK")
            return 0
        print("::warning title=Elo smoke attempt %d::%s" % (attempt, why))
    return 1


if __name__ == "__main__":
    sys.exit(main())
