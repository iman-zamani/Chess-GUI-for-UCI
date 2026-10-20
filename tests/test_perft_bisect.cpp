/* Chess-GUI-for-UCI — perft divide parsing and bisection tests. GPL-2.0 */
#include "testlib.hpp"
#include "perft_bisect.hpp"
#include <algorithm>

static const std::string kMock = MOCK_ENGINE_PATH;

static bool startMock(UciEngine& e, std::vector<std::string> args){
    EngineConfig c; c.path = kMock; c.args = args;
    return e.start(c);
}

TEST_CASE("bisect/divide-line-formats"){
    std::string m; uint64_t n = 0;
    CHECK(parseDivideLine("e2e4: 20", m, n)); CHECK_EQ(m, std::string("e2e4")); CHECK_EQ(n, (uint64_t)20);
    CHECK(parseDivideLine("a7a8q: 1", m, n)); CHECK_EQ(m, std::string("a7a8q"));
    CHECK(parseDivideLine("g1f3 440", m, n)); CHECK_EQ(n, (uint64_t)440);
    CHECK(parseDivideLine("b1c3 - 1,234", m, n)); CHECK_EQ(n, (uint64_t)1234);
    CHECK(parseDivideLine("e2e4: 600 nodes", m, n)); CHECK_EQ(n, (uint64_t)600);
    CHECK(parseDivideLine("Move: d2d4 Count: 560", m, n)); CHECK_EQ(m, std::string("d2d4"));
    CHECK(!parseDivideLine("Nodes searched: 8902", m, n));
    CHECK(!parseDivideLine("info depth 1 score cp 10", m, n));
    CHECK(!parseDivideLine("bestmove e2e4", m, n));
    long long t = 0;
    CHECK(parseTotalLine("Nodes searched: 8902", t)); CHECK_EQ(t, 8902LL);
    CHECK(parseTotalLine("Total: 197281", t)); CHECK_EQ(t, 197281LL);
    CHECK(parseTotalLine("Nodes: 400", t)); CHECK_EQ(t, 400LL);
    CHECK(!parseTotalLine("e2e4: 20", t));
}

TEST_CASE("bisect/divide-from-engine"){
    UciEngine e;
    REQUIRE(startMock(e, {}));
    PerftDivide d = enginePerftDivide(e, Position::startpos().toFEN(), 2);
    REQUIRE(d.ok);
    CHECK_EQ(d.moves.size(), (size_t)20);
    CHECK_EQ(d.total, 400LL);
    PerftDivide t = internalPerftDivide(Position::startpos(), 2);
    DivideDiff diff = compareDivide(d, t);
    CHECK(diff.missing.empty()); CHECK(diff.extra.empty()); CHECK(diff.wrongCount.empty());
}

TEST_CASE("bisect/correct-engine-agrees"){
    UciEngine e;
    REQUIRE(startMock(e, {}));
    std::atomic<bool> ab{false};
    auto rep = perftBisect(e, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 2, nullptr, ab);
    CHECK(rep.engineAgrees);
    CHECK(!rep.found);
}

TEST_CASE("bisect/finds-missing-en-passant"){
    // Startpos depth 5 is the first depth where en passant captures occur. The
    // bisection must walk down 4 plies to a position with an ep capture.
    UciEngine e;
    REQUIRE(startMock(e, {"--perft-bug", "ep"}));
    std::atomic<bool> ab{false};
    int steps = 0;
    auto rep = perftBisect(e, Position::startpos().toFEN(), 5, [&](const BisectStep&){ steps++; }, ab);
    REQUIRE(rep.found);
    CHECK(rep.extra.empty());
    REQUIRE(!rep.missing.empty());
    Position bug = Position::fromFEN(rep.bugFen);
    CHECK(bug.epSquare >= 0);
    Move m;
    REQUIRE(bug.uciToMove(rep.missing[0], m));
    CHECK(m.isEnPassant);
    CHECK_EQ(rep.steps.size(), (size_t)5);                // depths 5,4,3,2,1
    CHECK(rep.summary.find("missing legal move") != std::string::npos);
}

TEST_CASE("bisect/finds-illegal-king-moves"){
    UciEngine e;
    REQUIRE(startMock(e, {"--perft-bug", "king-into-check"}));
    std::atomic<bool> ab{false};
    // the black king on e8 may not step to d8/f8 next to... any position with attacked squares
    auto rep = perftBisect(e, "4k3/8/8/8/8/8/3R4/4K3 b - - 0 1", 3, nullptr, ab);
    REQUIRE(rep.found);
    CHECK(!rep.extra.empty());
    CHECK(rep.summary.find("illegal move") != std::string::npos);
}

TEST_CASE("bisect/finds-missing-castling-and-underpromotion"){
    std::atomic<bool> ab{false};
    {
        UciEngine e;
        REQUIRE(startMock(e, {"--perft-bug", "castle"}));
        auto rep = perftBisect(e, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", 2, nullptr, ab);
        REQUIRE(rep.found);
        CHECK(std::find(rep.missing.begin(), rep.missing.end(), "e1c1") != rep.missing.end());
    }
    {
        UciEngine e;
        REQUIRE(startMock(e, {"--perft-bug", "underpromo"}));
        auto rep = perftBisect(e, "8/P6k/8/8/8/8/8/K7 w - - 0 1", 3, nullptr, ab);
        REQUIRE(rep.found);
        CHECK(rep.missing.size() == 3);                   // a8=R, a8=B, a8=N
    }
}

TEST_CASE("bisect/engine-without-perft"){
    UciEngine e;
    REQUIRE(startMock(e, {"--no-perft"}));
    std::atomic<bool> ab{false};
    auto rep = perftBisect(e, Position::startpos().toFEN(), 2, nullptr, ab);
    CHECK(rep.error);
    CHECK(rep.errorText.find("perft") != std::string::npos);
}
