#!/usr/bin/env python3
"""Build the candidate position pool for the position-based Elo estimator.

Sources (both CC0, https://database.lichess.org/):
  * Lichess puzzle database: tactics and defence positions, stratified by
    puzzle rating so the pool spans easy to very hard.
  * Lichess standard rated games (one monthly file): quiet positions from
    games where both players are rated 2200+, a middlegame and (when the game
    gets there) an endgame position per game.

Every position records its source and ID. Output: JSON lines, one position
per line, deterministic for a given seed and input files.

  python build_candidates.py --puzzles lichess_db_puzzle.csv.zst \
      --games lichess_db_standard_rated_2015-06.pgn.zst --out candidates.jsonl
"""
import argparse
import csv
import io
import json
import random
import re
import sys

import chess
import chess.pgn
import zstandard


def zst_text(path):
    fh = open(path, "rb")
    return io.TextIOWrapper(zstandard.ZstdDecompressor().stream_reader(fh), encoding="utf-8", errors="replace")


def position_key(board):
    """FEN without the move counters (like the GUI's repetition key)."""
    return " ".join(board.fen().split()[:4])


def legal_count(board):
    return board.legal_moves.count()


def non_pawn_pieces(board):
    return sum(len(board.pieces(pt, c)) for pt in (chess.KNIGHT, chess.BISHOP, chess.ROOK, chess.QUEEN)
               for c in (chess.WHITE, chess.BLACK))


def sample_puzzles(path, per_bin, rng, min_plays, max_rd, min_pop, lo, hi, width):
    """Reservoir-sample up to per_bin puzzles in every rating bin [lo, hi)."""
    bins = {}
    seen = {}
    reader = csv.DictReader(zst_text(path))
    for row in reader:
        try:
            rating = int(row["Rating"]); rd = int(row["RatingDeviation"])
            plays = int(row["NbPlays"]); pop = int(row["Popularity"])
        except (ValueError, KeyError):
            continue
        if plays < min_plays or rd > max_rd or pop < min_pop or not (lo <= rating < hi):
            continue
        b = (rating - lo) // width
        seen[b] = seen.get(b, 0) + 1
        res = bins.setdefault(b, [])
        if len(res) < per_bin:
            res.append(row)
        else:
            j = rng.randrange(seen[b])
            if j < per_bin:
                res[j] = row
    out = []
    for b in sorted(bins):
        for row in bins[b]:
            board = chess.Board(row["FEN"])
            moves = row["Moves"].split()
            board.push_uci(moves[0])            # the opponent's move; the puzzle starts after it
            themes = row["Themes"].split()
            out.append({
                "fen": board.fen(),
                "id": "lichess-puzzle:" + row["PuzzleId"],
                "category": "defence" if "defensiveMove" in themes else "tactics",
                "source": "Lichess puzzle database (CC0)",
                "puzzleRating": int(row["Rating"]),
                "solution": moves[1],
                "themes": themes,
                "url": row["GameUrl"],
            })
    return out, seen


HEADER = re.compile(r'^\[(\w+) "(.*)"\]\s*$')


def iter_games(path):
    """Yield (headers, movetext) without building python-chess objects for every game."""
    headers, moves, in_moves = {}, [], False
    for line in zst_text(path):
        line = line.rstrip("\n")
        m = HEADER.match(line)
        if m:
            if in_moves:
                yield headers, " ".join(moves)
                headers, moves, in_moves = {}, [], False
            headers[m.group(1)] = m.group(2)
        elif line.strip():
            in_moves = True
            moves.append(line)
    if headers:
        yield headers, " ".join(moves)


def game_ok(h, min_elo, min_base):
    try:
        if int(h.get("WhiteElo", 0)) < min_elo or int(h.get("BlackElo", 0)) < min_elo:
            return False
        base = int(h.get("TimeControl", "0+0").split("+")[0])
    except ValueError:
        return False
    ev = h.get("Event", "")
    # games that ended on time are fine: only the positions are used, not the result
    return base >= min_base and "Rated" in ev and h.get("Termination") in ("Normal", "Time forfeit")


def quiet(board, last_move_capture):
    return (not board.is_check() and not last_move_capture and legal_count(board) >= 3
            and not board.is_game_over())


def sample_games(path, n_mid, n_end, rng, min_elo, min_base, max_games):
    mids, ends = [], []
    seen_mid = seen_end = 0
    games = 0
    for h, movetext in iter_games(path):
        if not game_ok(h, min_elo, min_base):
            continue
        game = chess.pgn.read_game(io.StringIO(movetext))
        if game is None:
            continue
        games += 1
        board = game.board()
        cands_mid, cands_end = [], []
        for ply, mv in enumerate(game.mainline_moves(), start=1):
            capture = board.is_capture(mv)
            board.push(mv)
            move_no = (ply + 1) // 2
            if move_no < 12 or move_no > 80:
                continue
            if not quiet(board, capture):
                continue
            item = (ply, board.fen())
            if non_pawn_pieces(board) <= 4:
                cands_end.append(item)
            elif move_no <= 40:
                cands_mid.append(item)
        gid = h.get("Site", "").rsplit("/", 1)[-1]
        meta = {"white": h.get("WhiteElo"), "black": h.get("BlackElo"), "timeControl": h.get("TimeControl"),
                "url": h.get("Site")}
        picks = []
        if cands_mid:
            picks.append((rng.choice(cands_mid), mids, n_mid, "positional"))
        if cands_end:                       # up to two endgame positions, at least 10 plies apart
            first = rng.choice(cands_end)
            picks.append((first, ends, n_end, "endgame"))
            far = [c for c in cands_end if abs(c[0] - first[0]) >= 10]
            if far:
                picks.append((rng.choice(far), ends, n_end, "endgame"))
        for (ply, fen), res, n, cat in picks:
            rec = dict({"fen": fen, "id": "lichess-game:%s#%d" % (gid, ply), "category": cat,
                        "source": "Lichess standard rated games 2015-06 (CC0)"}, **meta)
            if cat == "positional":
                seen_mid += 1; k = seen_mid
            else:
                seen_end += 1; k = seen_end
            if len(res) < n:
                res.append(rec)
            else:
                j = rng.randrange(k)
                if j < n:
                    res[j] = rec
        if max_games and games >= max_games:
            break
    return mids, ends, games


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--puzzles", required=True)
    ap.add_argument("--games", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--seed", type=int, default=20261020)
    ap.add_argument("--puzzle-bin-size", type=int, default=100, help="rating bin width")
    ap.add_argument("--puzzles-per-bin", type=int, default=75)
    ap.add_argument("--puzzle-min", type=int, default=600)
    ap.add_argument("--puzzle-max", type=int, default=3300)
    ap.add_argument("--middlegames", type=int, default=1700)
    ap.add_argument("--endgames", type=int, default=1300)
    ap.add_argument("--min-elo", type=int, default=2200)
    ap.add_argument("--min-base-seconds", type=int, default=180)
    ap.add_argument("--max-games", type=int, default=0, help="stop after this many qualifying games (0 = all)")
    a = ap.parse_args()

    rng = random.Random(a.seed)
    puzzles, seen = sample_puzzles(a.puzzles, a.puzzles_per_bin, rng, min_plays=300, max_rd=80, min_pop=80,
                                   lo=a.puzzle_min, hi=a.puzzle_max, width=a.puzzle_bin_size)
    print("puzzles: %d sampled; eligible per bin: %s" % (len(puzzles), {a.puzzle_min + b * a.puzzle_bin_size: n
                                                                        for b, n in sorted(seen.items())}),
          file=sys.stderr)
    mids, ends, games = sample_games(a.games, a.middlegames, a.endgames, rng, a.min_elo, a.min_base_seconds, a.max_games)
    print("games: %d qualifying; %d middlegame, %d endgame positions" % (games, len(mids), len(ends)), file=sys.stderr)

    out, keys = [], set()
    for rec in puzzles + mids + ends:
        b = chess.Board(rec["fen"])
        k = position_key(b)
        if k in keys or not b.is_valid():
            continue
        keys.add(k)
        out.append(rec)
    with open(a.out, "w") as f:
        for rec in out:
            f.write(json.dumps(rec, sort_keys=True) + "\n")
    cats = {}
    for r in out:
        cats[r["category"]] = cats.get(r["category"], 0) + 1
    print("wrote %d positions to %s: %s" % (len(out), a.out, cats), file=sys.stderr)


if __name__ == "__main__":
    main()
