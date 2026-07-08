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

// ---------- Accuracy testing vs a reference engine ----------
struct AccuracyMoveReport {
    std::string san;
    int cpLoss;            // centipawns lost vs reference best move
    std::string refBest;   // reference engine best move (uci)
    std::string played;    // move actually played (uci)
};
struct AccuracyReport {
    std::vector<AccuracyMoveReport> moves;
    double avgCpLoss = 0;
    double accuracyPct = 0;    // lichess-style mapping from acpl
    int blunders = 0, mistakes = 0, inaccuracies = 0;
};

// Plays the test engine through `positions` (FENs); for each, the test engine
// picks a move (movetimeMs) and the reference engine evaluates best vs played.
AccuracyReport runAccuracyTest(
    UciEngine& testEngine, UciEngine& refEngine,
    const std::vector<std::string>& positionFens,
    int testMovetimeMs, int refMovetimeMs,
    std::function<void(int,int,const AccuracyMoveReport&)> progress,
    std::atomic<bool>& abortFlag);

// Default set of varied middlegame/endgame test positions.
std::vector<std::string> defaultAccuracyPositions();
