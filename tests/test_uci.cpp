/* Chess-GUI-for-UCI — UciEngine wrapper tests against the mock engine. GPL-2.0 */
#include "testlib.hpp"
#include "chess.hpp"
#include "uci_engine.hpp"
#include "testing.hpp"

#include <chrono>
#include <cstdlib>
#include <thread>

static const std::string kMock = MOCK_ENGINE_PATH;

// Starts the mock engine with misbehaviour flags (passed through the
// environment, which the child process inherits).
static bool startMock(UciEngine& e, const std::string& flags = "", const std::string& path = kMock){
#ifdef _WIN32
    _putenv_s("MOCK_ENGINE_FLAGS", flags.c_str());
#else
    setenv("MOCK_ENGINE_FLAGS", flags.c_str(), 1);
#endif
    return e.start(path);
}

TEST_CASE("uci/handshake"){
    UciEngine e;
    REQUIRE(startMock(e));
    CHECK_EQ(e.name(), std::string("MockEngine 1.0"));
    CHECK(e.options().size() >= 7);
    bool sawHash = false;
    for (auto& o : e.options())
        if (o.name == "Hash"){ sawHash = true; CHECK_EQ(o.type, std::string("spin")); CHECK_EQ(o.maxV, 1024LL); }
    CHECK(sawHash);
    e.quit();
    CHECK(!e.alive());
}

TEST_CASE("uci/search-returns-legal-move"){
    UciEngine e;
    REQUIRE(startMock(e));
    e.newGame();
    e.setPosition(Position::startpos().toFEN(), {"e2e4", "e7e5"});
    std::string bm = e.goMovetime(100);
    Position p = Position::startpos();
    Move m; p.uciToMove("e2e4", m); p.makeMove(m); p.uciToMove("e7e5", m); p.makeMove(m);
    CHECK(p.uciToMove(bm, m));
    EngineInfo inf = e.lastInfo();
    CHECK(inf.valid);
    CHECK(inf.depth >= 1);
    // mate in one is found and reported as a mate score
    e.setPosition("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", {});
    CHECK_EQ(e.goDepth(1), std::string("a1a8"));
    inf = e.lastInfo();
    CHECK(inf.isMate);
    CHECK_EQ(inf.mateIn, 1);
}

TEST_CASE("uci/infinite-and-stop"){
    UciEngine e;
    REQUIRE(startMock(e));
    e.setPosition(Position::startpos().toFEN(), {});
    e.goInfinite();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    e.pump();
    CHECK(e.lastInfo().valid);
    CHECK(!e.stopAndWait(2000).empty());
}

TEST_CASE("uci/perft"){
    UciEngine e;
    REQUIRE(startMock(e));
    e.setPosition(Position::startpos().toFEN(), {});
    CHECK_EQ(e.goPerft(3), 8902LL);
    std::atomic<bool> abortFlag{false};
    auto res = runEnginePerftSuite(e, builtinPerftSuite(), 2, nullptr, abortFlag);
    REQUIRE(!res.empty());
    for (auto& r : res) CHECK_MSG(r.pass, r.fen << " d" << r.depth);
}

TEST_CASE("uci/perft-bug-detected"){
    UciEngine e;
    REQUIRE(startMock(e, "--perft-bug ep"));
    // the en passant pin position from the builtin suite needs ep captures
    e.setPosition("8/8/3p4/1Pp4r/1K3p1k/8/4P1P1/1R6 w - c6 0 3", {});
    long long n = e.goPerft(1);
    CHECK(n >= 0);
    CHECK(n != 7);
}

TEST_CASE("uci/no-uciok-fails-cleanly"){
    UciEngine e;
    CHECK(!startMock(e, "--no-uciok"));
    CHECK(!e.alive());
    CHECK(!e.lastError().empty());
}

TEST_CASE("uci/illegal-bestmove-reported"){
    UciEngine e;
    REQUIRE(startMock(e, "--illegal-bestmove"));
    e.setPosition(Position::startpos().toFEN(), {});
    std::string bm = e.goMovetime(50);
    Move m;
    CHECK(!Position::startpos().uciToMove(bm, m));
}

TEST_CASE("uci/crash-mid-search"){
    UciEngine e;
    REQUIRE(startMock(e, "--crash-on-go 1"));
    e.setPosition(Position::startpos().toFEN(), {});
    CHECK(e.goMovetime(200).empty());
    CHECK(!e.alive());
}

TEST_CASE("uci/missing-binary"){
    UciEngine e;
    CHECK(!startMock(e, "", kMock + "-does-not-exist"));
    CHECK(!e.alive());
}
