#!/usr/bin/env python3
"""Run engines on the candidate positions and record their answers.

Protocol (the same as the C++ estimator uses for a user's engine): Threads=1,
Hash=64 MB and Move Overhead=10 ms (when the engine has these options; the
overhead option is matched by name ignoring case and spaces), `ucinewgame` + `isready`
before every position, then `go movetime T` with T = base x hardware factor,
and `stop` if there is no bestmove after T + 50 ms (some engines ignore movetime).
For calibration the factor is 1 by definition and the machine's speed is
recorded with `chess-cli bench` (see docs/elo-estimator.md).

An illegal bestmove is recorded as such (it counts as not solved); a crash or a
hang (no bestmove 5 s after "stop") restarts the engine and asks again.

Work is split into batches of (engine, positions); a pool of worker processes
runs one single-threaded engine each. Results are appended to the CSV after
every batch, so an interrupted run resumes where it stopped.

Usage:
  run_engines.py --status BUILD_STATUS.json --positions candidates.jsonl --out responses.csv
                 [--movetime 500] [--factor 1.0] [--workers 4] [--only ID ...]
"""
import argparse, csv, json, multiprocessing as mp, os, queue, re, subprocess, sys, threading, time

import chess

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINES = os.path.join(HERE, "..", "..", "data", "elo", "engines.json")
MOVE_OVERHEAD_MS = 10   # value for an engine's "Move Overhead" option, when it has one
GRACE_MS = 50      # "stop" is sent this long after the move time if the engine is still thinking
STOP_WAIT_S = 5    # after "stop", an engine with no bestmove this long is treated as hung and restarted
FIELDS = ["engine", "position", "move", "depth", "seldepth", "nodes", "time_ms", "wall_ms", "score_cp", "score_mate",
          "movetime_ms", "error"]


def sleep_clock():
    """Seconds the system has spent suspended (a clock that counts sleep minus one
    that doesn't); 0 when the OS has no such pair."""
    if hasattr(time, "CLOCK_UPTIME_RAW"):                          # macOS
        return time.clock_gettime(time.CLOCK_MONOTONIC) - time.clock_gettime(time.CLOCK_UPTIME_RAW)
    if hasattr(time, "CLOCK_BOOTTIME"):                            # Linux
        return time.clock_gettime(time.CLOCK_BOOTTIME) - time.clock_gettime(time.CLOCK_MONOTONIC)
    return 0.0


class EngineGone(Exception):
    """The engine crashed, hung or could not be started."""


class Uci:
    """A minimal UCI driver: every wait has a timeout, so a misbehaving engine can
    cost an answer but never hang the run (python-chess waits forever when an
    engine sends an illegal bestmove). It speaks the same protocol as the C++
    estimator."""

    def __init__(self, binary, hash_mb):
        self.p = subprocess.Popen([binary], cwd=os.path.dirname(binary), stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.q = queue.Queue()
        threading.Thread(target=self._reader, daemon=True).start()
        self.options = {}                                   # name -> (type, min, max)
        self.send("uci")
        for line in self._until(lambda l: l == "uciok", 30):
            m = re.match(r"option name (.+?) type (\S+)(.*)$", line)
            if m:
                lo = re.search(r"\bmin (-?\d+)", m.group(3)); hi = re.search(r"\bmax (-?\d+)", m.group(3))
                self.options[m.group(1)] = (m.group(2), int(lo.group(1)) if lo else None, int(hi.group(1)) if hi else None)
        if "Threads" in self.options:
            self.send("setoption name Threads value 1")
        if "Hash" in self.options:
            self.send("setoption name Hash value %d" % hash_mb)
        # "Move Overhead" style safety margins (meant for laggy GUIs) would cut into the move time
        for name, (typ, lo, hi) in self.options.items():
            if name.lower().replace(" ", "").replace("_", "") == "moveoverhead" and typ == "spin":
                v = max(lo or 0, min(MOVE_OVERHEAD_MS, hi if hi is not None else MOVE_OVERHEAD_MS))
                self.send("setoption name %s value %d" % (name, v))
        self.ready(30)

    def _reader(self):
        for line in self.p.stdout:
            self.q.put(line.strip())
        self.q.put(None)                                    # end of output

    def send(self, line):
        try:
            self.p.stdin.write(line + "\n")
            self.p.stdin.flush()
        except (BrokenPipeError, OSError):
            raise EngineGone("the engine exited")

    def _until(self, done, timeout):
        """Lines up to and including the first one for which done(line) holds."""
        end = time.monotonic() + timeout
        while True:
            left = end - time.monotonic()
            if left <= 0:
                raise EngineGone("no answer within %.0f s" % timeout)
            try:
                line = self.q.get(timeout=left)
            except queue.Empty:
                continue
            if line is None:
                raise EngineGone("the engine exited")
            yield line
            if done(line):
                return

    def ready(self, timeout=10):
        self.send("isready")
        for _ in self._until(lambda l: l == "readyok", timeout):
            pass

    def think(self, fen, movetime):
        """ucinewgame + isready, position, go movetime; stop after movetime + GRACE_MS.
        Returns (bestmove, last info fields, wall ms)."""
        self.send("ucinewgame")
        self.ready()
        self.send("position fen " + fen)
        self.send("go movetime %d" % movetime)
        t0 = time.monotonic()
        info, best, stopped = {}, None, False
        while best is None:
            budget = (movetime + GRACE_MS) / 1000.0 - (time.monotonic() - t0)
            try:
                if budget > 0:
                    gen = self._until(lambda l: True, budget)
                else:
                    if not stopped:
                        self.send("stop"); stopped = True
                    gen = self._until(lambda l: True, STOP_WAIT_S)
                line = next(gen)
            except EngineGone:
                if budget > 0 and self.p.poll() is None:
                    continue                                # the move time ran out: send stop next round
                raise
            if line.startswith("info "):
                t = line.split()
                for key in ("depth", "seldepth", "nodes", "time"):
                    if key in t[:-1]:
                        info[key] = t[t.index(key) + 1]
                if "score" in t[:-2]:
                    k = t.index("score")
                    info["score"] = (t[k + 1], t[k + 2])
            elif line.startswith("bestmove"):
                parts = line.split()
                best = parts[1] if len(parts) > 1 else ""
        return best, info, int((time.monotonic() - t0) * 1000)

    def close(self):
        try:
            self.send("quit")
            self.p.wait(timeout=2)
        except Exception:
            pass
        if self.p.poll() is None:
            self.p.kill()
        for pipe in (self.p.stdin, self.p.stdout):
            try:
                pipe.close()
            except Exception:                               # a crashed engine leaves a broken pipe
                pass


def normalised_move(board, uci):
    """The move in standard UCI (king-takes-rook castling becomes e1g1), or None if illegal."""
    try:
        m = chess.Move.from_uci(uci)
    except ValueError:
        return None
    if m in board.legal_moves:
        return m.uci()
    for c in board.legal_moves:                             # castling sent as king takes rook
        if board.is_castling(c) and c.from_square == m.from_square and board.piece_at(m.to_square) \
                and board.piece_at(m.to_square).piece_type == chess.ROOK and board.piece_at(m.to_square).color == board.turn \
                and (chess.square_file(m.to_square) > chess.square_file(m.from_square)) == (chess.square_file(c.to_square) > chess.square_file(c.from_square)):
            return c.uci()
    return None


def run_batch(job):
    """job = (engine id, binary, [(position id, fen)], movetime ms, hash MB) -> rows"""
    eid, binary, positions, movetime, hash_mb = job
    rows, eng, failed_starts = [], None, 0
    for pid, fen in positions:
        row = {"engine": eid, "position": pid, "movetime_ms": movetime, "error": ""}
        for attempt in range(4):                      # restarts after a crash, a hang or a system sleep
            if failed_starts >= 2:
                row.update(move="", error="the engine does not start")
                break
            try:
                slept0 = sleep_clock()
                if eng is None:
                    try:
                        eng = Uci(binary, hash_mb)
                    except EngineGone:
                        failed_starts += 1
                        raise
                    failed_starts = 0
                best, info, wall = eng.think(fen, movetime)
                if sleep_clock() - slept0 > 0.5:
                    raise EngineGone("the computer slept during the search")   # the answer is not valid
                board = chess.Board(fen)
                nomove = not best or best in ("(none)", "0000")
                move = None if nomove else normalised_move(board, best)
                kind, val = info.get("score", ("", ""))
                row.update(move=move or "", depth=info.get("depth", ""), seldepth=info.get("seldepth", ""),
                           nodes=info.get("nodes", ""), time_ms=info.get("time", ""), wall_ms=wall,
                           score_cp=val if kind == "cp" else "", score_mate=val if kind == "mate" else "",
                           error="" if move else ("no move" if nomove else "illegal move %s" % best))
                break                                  # an illegal move is the engine's answer: unsolved
            except EngineGone as e:
                row.update(move="", error=str(e)[:200])
                if eng is not None:
                    eng.close()
                eng = None
        rows.append(row)
    if eng is not None:
        eng.close()
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
