#!/usr/bin/env python3
"""Reference analysis of every candidate position.

For each position, Stockfish evaluates every legal move (MultiPV = number of
legal moves, capped at 40; moves beyond the cap count as "bad" later) with a
fixed node budget, single-threaded. Results go to a JSON-lines file, one
position per line, appended as they finish, so an interrupted run resumes
where it stopped. Runs one engine per worker process.

  python reference_eval.py --engine /path/to/stockfish --candidates candidates.jsonl \
      --out reference.jsonl --nodes 10000000 --workers 9
"""
import argparse
import json
import multiprocessing as mp
import os
import sys
import time

import chess
import chess.engine

ENGINE = None


def init_worker(path, hash_mb):
    global ENGINE
    ENGINE = chess.engine.SimpleEngine.popen_uci(path)
    ENGINE.configure({"Threads": 1, "Hash": hash_mb})


def score_json(score):
    if score.is_mate():
        return {"mate": score.mate()}
    return {"cp": score.score()}


def analyse(job):
    rec, nodes, cap = job
    board = chess.Board(rec["fen"])
    n = min(cap, board.legal_moves.count())
    t0 = time.time()
    ENGINE.configure({"Clear Hash": None})
    infos = ENGINE.analyse(board, chess.engine.Limit(nodes=nodes), multipv=n, game=object())
    moves = []
    for info in infos:
        if "pv" not in info or not info["pv"]:
            continue
        moves.append({"move": info["pv"][0].uci(), "depth": info.get("depth"),
                      **score_json(info["score"].pov(board.turn))})
    return {"id": rec["id"], "fen": rec["fen"], "category": rec["category"], "legal": board.legal_moves.count(),
            "multipv": n, "nodes": infos[0].get("nodes") if infos else None, "seconds": round(time.time() - t0, 2),
            "moves": moves}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", required=True)
    ap.add_argument("--candidates", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--nodes", type=int, default=10_000_000)
    ap.add_argument("--multipv-cap", type=int, default=40)
    ap.add_argument("--hash", type=int, default=64)
    ap.add_argument("--workers", type=int, default=max(1, os.cpu_count() - 1))
    ap.add_argument("--limit", type=int, default=0, help="only the first N positions (for a trial run)")
    a = ap.parse_args()

    cands = [json.loads(l) for l in open(a.candidates)]
    if a.limit:
        cands = cands[:a.limit]
    done = set()
    if os.path.exists(a.out):
        for l in open(a.out):
            try:
                done.add(json.loads(l)["id"])
            except (ValueError, KeyError):
                pass                      # a line cut off by an interruption
    todo = [c for c in cands if c["id"] not in done]
    print("%d candidates, %d already analysed, %d to go, %d workers, %d nodes"
          % (len(cands), len(done), len(todo), a.workers, a.nodes), file=sys.stderr, flush=True)
    if not todo:
        return
    t0 = time.time()
    with mp.Pool(a.workers, initializer=init_worker, initargs=(a.engine, a.hash)) as pool, open(a.out, "a") as out:
        for i, res in enumerate(pool.imap_unordered(analyse, [(c, a.nodes, a.multipv_cap) for c in todo]), 1):
            out.write(json.dumps(res) + "\n")
            out.flush()
            if i % 20 == 0 or i == len(todo):
                el = time.time() - t0
                eta = el / i * (len(todo) - i)
                print("%d/%d done, %.1f s elapsed, ETA %.0f min" % (i, len(todo), el, eta / 60),
                      file=sys.stderr, flush=True)


if __name__ == "__main__":
    main()
