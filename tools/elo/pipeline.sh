#!/bin/sh
# Rebuilds the Elo estimator model from scratch. Every step is resumable: rerun
# the script after an interruption and finished work is kept.
#
#   WORK=/path/with/space tools/elo/pipeline.sh
#
# Needs: python3 with tools/elo/requirements.txt, git, make, a C/C++ compiler,
# Go (for Blunder), curl, and a built chess-cli (cmake --build build).
# Compute (Apple M4, 4 workers): reference analysis ~8 h, engine runs ~6 h.
# Keep the computer awake: a sleep invalidates move-time based answers
# (run_engines.py retries an answer that spans a sleep).
set -eu
REPO=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:?set WORK to a scratch directory}
PY=${PY:-python3}
WORKERS=${WORKERS:-4}
# kept out of every fit and of the cross-validation, for the final check of the shipped model
HOLDOUT=${HOLDOUT:-berserk-3.2.0}
T=$REPO/tools/elo
mkdir -p "$WORK/downloads" "$WORK/data"

# 1. sources (Lichess, CC0)
cd "$WORK/downloads"
[ -f lichess_db_puzzle.csv.zst ] || curl -fLO https://database.lichess.org/lichess_db_puzzle.csv.zst
[ -f lichess_db_standard_rated_2015-06.pgn.zst ] || \
    curl -fLO https://database.lichess.org/standard/lichess_db_standard_rated_2015-06.pgn.zst

# 2. candidate positions
[ -f "$WORK/data/candidates.jsonl" ] || "$PY" "$T/build_candidates.py" \
    --puzzles lichess_db_puzzle.csv.zst --games lichess_db_standard_rated_2015-06.pgn.zst \
    --out "$WORK/data/candidates.jsonl"

# 3. reference analysis: Stockfish 19 (tag sf_19), every legal move, 10M nodes, single thread
SF="$WORK/stockfish-sf_19/src/stockfish"
if [ ! -x "$SF" ]; then
    git clone -q --depth 1 --branch sf_19 https://github.com/official-stockfish/Stockfish "$WORK/stockfish-sf_19"
    make -C "$WORK/stockfish-sf_19/src" -j profile-build
fi
"$PY" "$T/reference_eval.py" --engine "$SF" --candidates "$WORK/data/candidates.jsonl" \
    --out "$WORK/data/reference.jsonl" --nodes 10000000 --workers "$WORKERS"

# 4. calibration engines (pinned tags in data/elo/engines.json)
"$PY" "$T/build_engines.py" --workdir "$WORK/engines" --jobs 2

# 5. this machine's speed (stored in the model as calibNps)
"$REPO/build/chess-cli" bench --json > "$WORK/data/bench.json"
NPS=$("$PY" -c "import json,sys; print(json.load(open(sys.argv[1]))['nps'])" "$WORK/data/bench.json")

# 6. engine answers: 500 ms per position
"$PY" "$T/run_engines.py" --status "$WORK/engines/build_status.json" --positions "$WORK/data/candidates.jsonl" \
    --out "$WORK/data/responses.csv" --movetime 500 --workers "$WORKERS"

# 7. cross-validation over thresholds and models; pick the best leave-one-engine-out MAE
#    (choosing on it makes that number slightly optimistic: the held-out engine and the
#    leave-one-family-out figures are the honest checks)
"$PY" "$T/validate.py" --data "$WORK/data" --thresholds 2 3 5 7 10 --models anchored joint \
    --holdout "$HOLDOUT" --out "$WORK/data/cv.json"
set -- $("$PY" -c "
import json,sys; r=min(json.load(open(sys.argv[1]))['results'], key=lambda r: r['loeo']['mae'])
print(r['threshold'], r['model'])" "$WORK/data/cv.json")

# 8. final fit and selection -> Resources/elo
"$PY" "$T/fit_irt.py" --data "$WORK/data" --threshold "$1" --model "$2" --holdout "$HOLDOUT" \
    --out "$WORK/data/items.json"
"$PY" "$T/select_positions.py" --data "$WORK/data" --items "$WORK/data/items.json" --cv "$WORK/data/cv.json" \
    --calib-nps "$NPS" --out-dir "$REPO/Resources/elo"
echo "model written to $REPO/Resources/elo (threshold $1, $2 model)"
