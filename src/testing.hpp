/* Chess-GUI-for-UCI — engine developer tools. GPL-2.0 */
#pragma once
#include "chess.hpp"
#include "uci_engine.hpp"
#include <string>
#include <vector>
#include <functional>
#include <atomic>

// ---------- Perft / move-generator verification ----------
struct PerftCase {
    std::string fen;
    std::vector<std::pair<int, uint64_t>> depths;   // (depth, expected nodes)
};

// Built-in verified suite (subset of the classic perftsuite).
// Full 126-position suite loads from Resources/perftsuite.epd if present.
std::vector<PerftCase> builtinPerftSuite();
std::vector<PerftCase> loadEpdSuite(const std::string& path);  // "fen ;D1 n ;D2 n ..."

struct PerftResult {
    std::string fen;
    int depth;
    uint64_t expected;
    long long engineNodes;   // -1 = engine has no perft support
    bool pass;
};

// Runs "go perft" on the engine for every case, compares to expected values
// (which are themselves cross-checked against the internal generator).
// progress(i, total) is called per case; abortFlag stops early.
std::vector<PerftResult> runEnginePerftSuite(
    UciEngine& engine, const std::vector<PerftCase>& suite, int maxDepth,
    std::function<void(int,int,const PerftResult&)> progress,
    std::atomic<bool>& abortFlag);

// Verifies the GUI's own generator against the suite (sanity self-test).
bool runInternalPerftSelfTest(const std::vector<PerftCase>& suite, int maxDepth,
                              std::function<void(int,int,bool)> progress,
                              std::atomic<bool>& abortFlag);

// Load a plain list of FENs, one per line ('#' comments allowed).
std::vector<std::string> loadFenList(const std::string& path);

// Lichess accuracy model: cp -> expected win %, and per-move accuracy from
// the win-percentage drop caused by the move.
double cpToWinPct(int cp);
double moveAccuracyFromWinDrop(double drop);
double acplToAccuracy(double acpl);   // legacy rough mapping (avoid; win-drop is correct)

// ---------- Accuracy testing vs a reference engine ----------
struct AccuracyMoveReport {
    std::string san;
    int cpLoss;            // centipawns lost vs reference best move
    std::string refBest;   // reference engine best move (uci)
    std::string played;    // move actually played (uci)
    int rank = -1;         // 0 = matched ref best, 1 = 2nd best line, ... -1 = not in top lines
    std::string alternatives;  // other acceptable moves per the reference, with cp deltas
    std::string fen;
};
struct AccuracyReport {
    std::vector<AccuracyMoveReport> moves;
    double avgCpLoss = 0;
    double accuracyPct = 0;    // lichess-style mapping from acpl
    int blunders = 0, mistakes = 0, inaccuracies = 0;
};

// Plays the test engine through `positions` (FENs). The reference engine
// searches each position once with MultiPV=multiPV, so every top move gets a
// score from the SAME search: if the test engine plays the 2nd/3rd line, it is
// charged only the real cp gap to the best line (credit for good alternatives).
// Moves outside the top lines are scored by evaluating the resulting position.
AccuracyReport runAccuracyTest(
    UciEngine& testEngine, UciEngine& refEngine,
    const std::vector<std::string>& positionFens,
    int testMovetimeMs, int refMovetimeMs, int multiPV,
    std::function<void(int,int,const AccuracyMoveReport&)> progress,
    std::atomic<bool>& abortFlag);

// Default set of varied middlegame/endgame test positions.
std::vector<std::string> defaultAccuracyPositions();
