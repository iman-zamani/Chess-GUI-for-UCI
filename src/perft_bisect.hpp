/* Chess-GUI-for-UCI — perft divide comparison and bisection. GPL-2.0 */
#pragma once
#include "chess.hpp"
#include "uci_engine.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// "go perft N" output split per root move ("divide").
struct PerftDivide {
    std::map<std::string, uint64_t> moves;   // uci move -> node count
    long long total = -1;                    // "Nodes searched: N" (-1 if missing)
    bool ok = false;                         // a divide was recognised
    std::string error;
};

// Parses divide output in the common formats:
//   "e2e4: 20"  (Stockfish, most engines)     "e2e4 20"   "e2e4 - 20"
//   "e2e4: 20 nodes"                          "Move: e2e4 Count: 20"
// and totals "Nodes searched: N", "Nodes: N", "Total: N", "Total nodes: N".
// Engines that print castling as king-takes-rook (e1h1) are accepted for
// standard chess and mapped to e1g1.
bool parseDivideLine(const std::string& line, std::string& move, uint64_t& count);
bool parseTotalLine(const std::string& line, long long& total);

// Runs "position fen F" + "go perft d" and collects the divide.
PerftDivide enginePerftDivide(UciEngine& e, const std::string& fen, int depth, int timeoutMs = 300000);
// The same from the GUI's own (verified) move generator.
PerftDivide internalPerftDivide(const Position& p, int depth);

struct DivideDiff {
    std::vector<std::string> missing;        // legal moves the engine did not list
    std::vector<std::string> extra;          // moves the engine listed that are illegal here
    std::vector<std::string> wrongCount;     // both list them, counts differ
};
DivideDiff compareDivide(const PerftDivide& engine, const PerftDivide& truth);

struct BisectStep {
    std::string fen;
    int depth = 0;
    uint64_t expected = 0;
    long long engineNodes = -1;
    std::string chosenMove;                  // mismatching child we descended into ("" at the end)
    DivideDiff diff;
};

struct BisectReport {
    bool engineAgrees = false;               // no mismatch at the start position/depth
    bool found = false;                      // located missing/extra moves at some node
    bool error = false;
    std::string errorText;
    std::vector<BisectStep> steps;           // root first
    std::string bugFen;                      // position where the generator is wrong
    std::vector<std::string> missing, extra; // the offending moves there (UCI)
    std::string summary;                     // human readable conclusion
};

// "Find my movegen bug": compares the engine's divide with the truth and
// descends into the first child whose count differs, one ply at a time,
// until a position where the engine lists the wrong set of moves.
BisectReport perftBisect(UciEngine& e, const std::string& fen, int depth,
                         std::function<void(const BisectStep&)> progress, std::atomic<bool>& abortFlag);
