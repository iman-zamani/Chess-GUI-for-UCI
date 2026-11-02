#!/usr/bin/env python3
"""Recorded searches of the reference engines, for measuring a computer's speed.

The estimator measures how much faster a computer is than the calibration machine
by replaying searches the reference engines (calibration engines that ship with
the program) made during calibration: the same position, the same amount of
search ("go nodes N" when the engine stops exactly at a node count, else
"go depth d"), and the time the engine reported for it then. This picks those
searches from responses.csv and writes the "speed" block of model.json
(select_positions.py --speed).

  Stockfish 11: reports the node count at the moment it stops, so its searches
    replay exactly with "go nodes N" (verified: identical node counts).
  Blunder 8.0.0: reports per-iteration node counts and keeps history between
    searches, so it replays to the same depth ("go depth d") and its reported
    time for completing that depth is compared.

Usage: speed_reference.py --data DIR --constants speed_constants.json --out speed.json [--per-engine 40]
--constants: measured values that are not in responses.csv (sigmaLog2, eloPerDoubling, with sources).
"""
import argparse, csv, hashlib, json, os, sys

REFERENCES = [
    # id in engines.json, file name in Resources/reference, start of "id name", replay method
    {"id": "stockfish-11", "file": "stockfish-11", "idName": "Stockfish 11", "replay": "nodes"},
    {"id": "blunder-8.0.0", "file": "blunder-8.0.0", "idName": "Blunder 8.0.0", "replay": "depth"},
]
MIN_MS, MAX_MS = 250, 520        # long enough to time well, no longer than the 500 ms protocol


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, help="dir with reference.jsonl and responses.csv")
    ap.add_argument("--constants", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--per-engine", type=int, default=40)
    a = ap.parse_args()
    fens = {}
    for line in open(os.path.join(a.data, "reference.jsonl")):
        if line.strip():
            j = json.loads(line)
            fens[j["id"]] = j["fen"]
    rows = {r["id"]: [] for r in REFERENCES}
    with open(os.path.join(a.data, "responses.csv")) as f:
        for r in csv.DictReader(f):
            if r["engine"] in rows and not r["error"] and r["nodes"] and r["time_ms"] and r["depth"]:
                ms = int(r["time_ms"])
                if MIN_MS <= ms <= MAX_MS and r["position"] in fens:
                    rows[r["engine"]].append(r)
    refs = []
    for spec in REFERENCES:
        # a fixed, well-mixed order (hash of the position id), so every build picks the same searches
        cand = sorted(rows[spec["id"]], key=lambda r: hashlib.sha256(r["position"].encode()).hexdigest())
        items = []
        for r in cand[:a.per_engine]:
            it = {"id": r["position"], "fen": fens[r["position"]], "calMs": int(r["time_ms"])}
            if spec["replay"] == "nodes":
                it["nodes"] = int(r["nodes"])
            else:
                it["depth"] = int(r["depth"])
            items.append(it)
        if len(items) < a.per_engine:
            sys.exit("%s: only %d usable searches" % (spec["id"], len(items)))
        refs.append({"id": spec["id"], "file": spec["file"], "idName": spec["idName"], "replay": spec["replay"],
                     "items": items})
    const = json.load(open(a.constants))
    out = dict(const)
    out["references"] = refs
    json.dump(out, open(a.out, "w"), indent=1)
    print("%s: %s" % (a.out, ", ".join("%s %d searches" % (r["id"], len(r["items"])) for r in refs)), file=sys.stderr)


if __name__ == "__main__":
    main()
