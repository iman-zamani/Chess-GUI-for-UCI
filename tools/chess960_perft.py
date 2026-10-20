#!/usr/bin/env python3
"""Regenerates tests/data/chess960_perft.epd.

Every perft count is computed twice, by Stockfish ("go perft" with
UCI_Chess960) and by python-chess; a position is kept only when the two agree
at every depth. Positions: 12 start positions and up to 18 positions reached by
seeded random moves that still have castling rights.

  python chess960_perft.py --stockfish /path/to/stockfish --out chess960_perft.epd
"""
import argparse, random, subprocess, sys

import chess

STARTS = [0, 1, 2, 3, 100, 200, 314, 518, 600, 707, 850, 959]


def pc_perft(b, d):
    if d == 1:
        return b.legal_moves.count()
    n = 0
    for m in b.legal_moves:
        b.push(m)
        n += pc_perft(b, d - 1)
        b.pop()
    return n


def sf_perft(sf, fen, d):
    p = subprocess.run([sf], input="setoption name UCI_Chess960 value true\nposition fen %s\ngo perft %d\nquit\n" % (fen, d),
                       capture_output=True, text=True, timeout=600)
    for line in p.stdout.splitlines():
        if line.startswith("Nodes searched:"):
            return int(line.split(":")[1])
    raise RuntimeError("no perft result from Stockfish")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stockfish", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--depth", type=int, default=4)
    ap.add_argument("--seed", type=int, default=960)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    fens = [chess.Board.from_chess960_pos(n).fen() for n in STARTS]
    tries = 0
    while len(fens) < 30 and tries < 5000:
        tries += 1
        b = chess.Board.from_chess960_pos(rng.randrange(960))
        for _ in range(rng.randrange(6, 22)):
            moves = list(b.legal_moves)
            if not moves:
                break
            b.push(rng.choice(moves))
        if b.is_game_over() or not b.castling_rights:
            continue
        if not any(b.is_castling(m) for m in b.legal_moves) and rng.random() < 0.7:
            continue                      # prefer positions with a legal castling move
        fens.append(b.fen())
    lines = []
    for fen in fens:
        b = chess.Board(fen, chess960=True)
        vals = []
        for d in range(1, a.depth + 1):
            x, y = pc_perft(b, d), sf_perft(a.stockfish, fen, d)
            if x != y:
                print("disagreement, dropped:", fen, d, x, y, file=sys.stderr)
                vals = None
                break
            vals.append((d, x))
        if vals:
            lines.append(fen + " " + " ".join(";D%d %d" % v for v in vals))
            print(lines[-1], file=sys.stderr, flush=True)
    open(a.out, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
