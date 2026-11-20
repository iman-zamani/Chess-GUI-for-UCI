/* Chess-GUI-for-UCI — libFuzzer harness for the text parsers. GPL-2.0
 *
 * Built only with -DCHESSGUI_FUZZ=ON (Clang). The first input byte picks the
 * parser, the rest is its input. Run: ./fuzz_parsers -seed=1 -runs=200000
 * The invariants match tests/test_fuzz.cpp; a broken one aborts, which
 * libFuzzer reports as a crash with the input saved.
 */
#include "chess.hpp"
#include "engine_config.hpp"
#include "game_tree.hpp"
#include "openings.hpp"
#include "perft_bisect.hpp"
#include "uci_engine.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

static void require(bool ok){ if (!ok) std::abort(); }

static void fuzzFen(const std::string& in){
    bool ok = false;
    Position p = Position::fromFEN(in, &ok);
    if (!ok) return;
    std::string fen = p.toFEN();
    bool ok2 = false;
    Position q = Position::fromFEN(fen, &ok2);
    require(ok2 && q.toFEN() == fen);
    std::vector<Move> legal;
    p.generateLegal(legal);
    for (const Move& m : legal){
        Position after = p;
        after.makeMove(m);
        require(!after.inCheck(p.whiteToMove));
        Move back;
        require(p.sanToMove(p.moveToSAN(m), back) && back == m);
    }
}

static void fuzzSan(const std::string& in){
    static const Position p = Position::fromFEN("r3k2r/pPpq2pp/2n4n/3pPp2/3P4/2N2N2/PPPQ2PP/R3K2R w KQkq f6 0 8");
    Move m;
    if (!p.sanToMove(in, m)) return;
    std::vector<Move> legal;
    p.generateLegal(legal);
    bool found = false;
    for (auto& l : legal) found = found || l == m;
    require(found);
}

static void fuzzPgn(const std::string& in){
    for (const PgnGame& pg : parsePGN(in)){
        Game g; std::string err;
        gameFromPGN(pg, g, &err);
        GameTree t;
        if (!gameTreeFromPGN(pg, t, &err)) continue;
        auto again = parsePGN(t.toPGN());
        require(again.size() == 1);
        GameTree t2;
        require(gameTreeFromPGN(again[0], t2, &err));
        require(t.mainLine().size() == t2.mainLine().size());
    }
}

static void fuzzInfo(const std::string& in){
    EngineInfo info;
    std::vector<PvLine> pvs;
    parseInfoLine("info " + in, info, pvs);
    parseInfoLine(in, info, pvs);
    require(pvs.size() <= 256);
}

static void fuzzMisc(const std::string& in){
    std::string move; uint64_t count = 0; long long total = 0;
    parseDivideLine(in, move, count);
    if (parseTotalLine(in, total)) require(total >= 0);
    std::string err;
    std::vector<std::string> v = splitArgs(in, &err);
    require(splitArgs(joinArgs(v)) == v);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size){
    if (size == 0) return 0;
    std::string in(reinterpret_cast<const char*>(data) + 1, size - 1);
    switch (data[0] % 7){
    case 0: fuzzFen(in); break;
    case 1: fuzzSan(in); break;
    case 2: fuzzPgn(in); break;
    case 3: parseEpdBook(in); break;
    case 4: parsePgnBook(in, data[0] / 7 % 16); break;
    case 5: fuzzInfo(in); break;
    default: fuzzMisc(in); break;
    }
    return 0;
}
