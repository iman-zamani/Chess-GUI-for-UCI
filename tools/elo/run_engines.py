#!/usr/bin/env python3
"""Run engines on the candidate positions and record their answers.

Protocol (the same as the C++ estimator uses for a user's engine): Threads=1,
Hash=64 MB and Move Overhead=10 ms (when the engine has these options; the
overhead option is matched by name ignoring case and spaces), `ucinewgame` + `isready`
before every position, then `go movetime T` with T = base x hardware factor,
and `stop` if there is no bestmove after T + 50 ms (some engines ignore movetime).
For calibration the factor is 1 by definition and the machine's speed is
recorded with `chess-cli bench` (see docs/elo-estimator.md).

Work is split into batches of (engine, positions); a pool of worker processes
runs one single-threaded engine each. Results are appended to the CSV after
every batch, so an interrupted run resumes where it stopped.

Usage:
  run_engines.py --status BUILD_STATUS.json --positions candidates.jsonl --out responses.csv
                 [--movetime 500] [--factor 1.0] [--workers 4] [--only ID ...]
"""
import argparse, csv, json, multiprocessing as mp, os, sys, threading, time

import chess
import chess.engine

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINES = os.path.join(HERE, "..", "..", "data", "elo", "engines.json")
MOVE_OVERHEAD_MS = 10   # value for an engine's "Move Overhead" option, when it has one
GRACE_MS = 50      # "stop" is sent this long after the move time if the engine is still thinking
FIELDS = ["engine", "position", "move", "depth", "seldepth", "nodes", "time_ms", "wall_ms", "score_cp", "score_mate",
          "movetime_ms", "error"]


def open_engine(binary, hash_mb):
    eng = chess.engine.SimpleEngine.popen_uci(binary, cwd=os.path.dirname(binary), timeout=30)
    opts = {}
    if "Threads" in eng.options:
        opts["Threads"] = 1
    if "Hash" in eng.options:
        opts["Hash"] = hash_mb
    # "Move Overhead" style safety margins (meant for laggy GUIs) would cut into the move time
    for name, o in eng.options.items():
        if name.lower().replace(" ", "").replace("_", "") == "moveoverhead" and o.type == "spin":
            opts[name] = max(o.min or 0, min(MOVE_OVERHEAD_MS, o.max if o.max is not None else MOVE_OVERHEAD_MS))
    if opts:
        eng.configure(opts)
    return eng


def run_batch(job):
    """job = (engine id, binary, [(position id, fen)], movetime ms, hash MB) -> rows"""
    eid, binary, positions, movetime, hash_mb = job
    rows, eng = [], None
    for pid, fen in positions:
        row = {"engine": eid, "position": pid, "movetime_ms": movetime, "error": ""}
        for attempt in range(2):                      # one restart after a crash or a hang
            try:
                if eng is None:
                    eng = open_engine(binary, hash_mb)
                board = chess.Board(fen)
                # a new `game` object makes python-chess send ucinewgame, then isready.
                # Engines that ignore movetime get "stop" once it has run out (+ GRACE_MS).
                with eng.analysis(board, chess.engine.Limit(time=movetime / 1000.0), game=object(),
                                  info=chess.engine.INFO_ALL) as an:
                    timer = threading.Timer((movetime + GRACE_MS) / 1000.0, an.stop)
                    t0 = time.monotonic()
                    timer.start()
                    try:
                        best = an.wait()
                        wall = int((time.monotonic() - t0) * 1000)
                    finally:
                        timer.cancel()
                    info = dict(an.info)
                move = best.move
                sc = info.get("score")
                pov = sc.pov(board.turn) if sc is not None else None
                row.update(move=move.uci() if move else "",
                           depth=info.get("depth", ""), seldepth=info.get("seldepth", ""),
                           nodes=info.get("nodes", ""),
                           time_ms=int(info["time"] * 1000) if "time" in info else "", wall_ms=wall,
                           score_cp=pov.score() if pov is not None and not pov.is_mate() else "",
                           score_mate=pov.mate() if pov is not None and pov.is_mate() else "",
                           error="")
                if not move:
                    row["error"] = "no move"
                break
            except Exception as e:                    # engine died, timed out or sent garbage
                row.update(move="", error=("%s: %s" % (type(e).__name__, e))[:200])
                try:
                    if eng is not None:
                        eng.close()
                except Exception:
                    pass
                eng = None
        rows.append(row)
    if eng is not None:
        try:
            eng.quit()
        except Exception:
            pass
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--status", required=True, help="build_status.json written by build_engines.py")
    ap.add_argument("--positions", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--movetime", type=int, default=500, help="base move time in ms")
    ap.add_argument("--factor", type=float, default=1.0, help="hardware factor (1 for calibration)")
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--batch", type=int, default=50)
    ap.add_argument("--hash", type=int, default=64)
    ap.add_argument("--only", nargs="*")
    a = ap.parse_args()

    engines = json.load(open(ENGINES))["engines"]
    status = json.load(open(a.status))
    positions = [json.loads(l) for l in open(a.positions) if l.strip()]
    movetime = int(round(a.movetime * max(0.25, min(4.0, a.factor))))   # factor clamped to [0.25, 4]
    todo_engines = []
    for e in engines:
        if a.only and e["id"] not in a.only:
            continue
        st = status.get(e["id"])
        if not st or not st.get("ok"):
            print("skipping %s: not built" % e["id"], file=sys.stderr)
            continue
        todo_engines.append((e["id"], st["binary"]))

    done = set()
    if os.path.exists(a.out):
        with open(a.out) as f:
            for r in csv.DictReader(f):
                done.add((r["engine"], r["position"]))
    # position chunks outside, engines inside: a partial run covers every engine
    jobs = []
    for i in range(0, len(positions), a.batch):
        chunk = positions[i:i + a.batch]
        for eid, binary in todo_engines:
            left = [(p["id"], p["fen"]) for p in chunk if (eid, p["id"]) not in done]
            if left:
                jobs.append((eid, binary, left, movetime, a.hash))
    total = sum(len(j[2]) for j in jobs)
    print("%d engines, %d positions, %d answers already recorded, %d to go in %d batches, %d workers, movetime %d ms"
          % (len(todo_engines), len(positions), len(done), total, len(jobs), a.workers, movetime),
          file=sys.stderr, flush=True)
    if not jobs:
        return 0
    new = not os.path.exists(a.out)
    f = open(a.out, "a", newline="")
    w = csv.DictWriter(f, fieldnames=FIELDS)
    if new:
        w.writeheader()
    t0, n, errors = time.time(), 0, 0
    with mp.Pool(a.workers) as pool:
        for k, rows in enumerate(pool.imap_unordered(run_batch, jobs), 1):
            for r in rows:
                w.writerow({k2: r.get(k2, "") for k2 in FIELDS})
                errors += 1 if r["error"] else 0
            f.flush()
            n += len(rows)
            if k % 10 == 0 or k == len(jobs):
                el = time.time() - t0
                print("%d/%d answers, %d errors, %.0f s elapsed, ETA %.0f min"
                      % (n, total, errors, el, el / n * (total - n) / 60), file=sys.stderr, flush=True)
    f.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
