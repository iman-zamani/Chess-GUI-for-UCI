/* Chess-GUI-for-UCI — Syzygy probing and tablebase adjudication. GPL-2.0
 *
 * The probing cases need real tables and are skipped unless CHESSGUI_SYZYGY_PATH
 * points at a directory with KQvK, KRvKR and KPvK (the tests never download). */
#include "testlib.hpp"
#include "syzygy.hpp"
#include "tournament.hpp"
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

static const char* tablesDir(){
    const char* p = std::getenv("CHESSGUI_SYZYGY_PATH");
    if (!p || !*p){ std::fprintf(stderr, "  (skipped: CHESSGUI_SYZYGY_PATH is not set)\n"); return nullptr; }
    return p;
}

TEST_CASE("syzygy/no-tables"){
    fs::path dir = fs::temp_directory_path() / "chessgui_no_tables";
    fs::create_directories(dir);
    std::string err;
    CHECK(!syzygy::init(dir.string(), &err));
    CHECK(!err.empty());
    CHECK_EQ(syzygy::largest(), 0);
    Position p = Position::fromFEN("8/8/8/4k3/8/8/8/KQ6 w - - 0 1");
    CHECK(syzygy::probeWdl(p) == syzygy::Wdl::Unknown);
    CHECK_EQ(syzygy::pieceCount(p), 3);
    AdjudicationConfig a;                      // no path: never adjudicates
    GameResult r;
    CHECK(!MatchRunner::adjudicateSyzygy(a, p, r));
    MatchSettings s;
    s.engines.resize(2);
    s.engines[0].path = s.engines[1].path = "x";
    s.adj.syzygyPath = dir.string();
    MatchRunner m;
    CHECK(!m.start(s, &err));                  // a path without tables is an error, not silently ignored
    CHECK(err.find("Syzygy") != std::string::npos);
}

TEST_CASE("syzygy/probe-wdl"){
    const char* dir = tablesDir();
    if (!dir) return;
    std::string err;
    REQUIRE(syzygy::init(dir, &err));
    CHECK(syzygy::largest() >= 3);
    using syzygy::Wdl;
    CHECK(syzygy::probeWdl(Position::fromFEN("8/8/8/4k3/8/8/8/KQ6 w - - 0 1")) == Wdl::Win);
    CHECK(syzygy::probeWdl(Position::fromFEN("8/8/8/4k3/8/8/8/KQ6 b - - 0 1")) == Wdl::Loss);
    CHECK(syzygy::probeWdl(Position::fromFEN("8/8/4k3/4r3/8/8/1R6/K7 w - - 0 1")) == Wdl::Draw);
    // results are for the side to move. Black king outside the pawn's square: White wins.
    CHECK(syzygy::probeWdl(Position::fromFEN("8/8/8/8/8/8/4P3/4K2k w - - 0 1")) == Wdl::Win);
    // White king on the sixth in front of its pawn wins whoever moves: Black to move loses
    CHECK(syzygy::probeWdl(Position::fromFEN("4k3/8/4K3/4P3/8/8/8/8 b - - 0 1")) == Wdl::Loss);
    // pawn already on the sixth with Black's king in front: every try ends in stalemate
    CHECK(syzygy::probeWdl(Position::fromFEN("4k3/8/4P3/4K3/8/8/8/8 w - - 0 1")) == Wdl::Draw);
    // WDL tables only answer with the halfmove clock at 0
    CHECK(syzygy::probeWdl(Position::fromFEN("8/8/8/4k3/8/8/8/KQ6 w - - 5 10")) == Wdl::Unknown);
    AdjudicationConfig a;
    a.syzygyPath = dir;
    GameResult r;
    CHECK(MatchRunner::adjudicateSyzygy(a, Position::fromFEN("8/8/8/4k3/8/8/8/KQ6 b - - 0 1"), r));
    CHECK(r == GameResult::WHITE_WINS);
    CHECK(MatchRunner::adjudicateSyzygy(a, Position::fromFEN("8/8/4k3/4r3/8/8/1R6/K7 w - - 0 1"), r));
    CHECK(r == GameResult::DRAW);
    a.syzygyPieces = 2;                        // limit below the position's 3 pieces
    CHECK(!MatchRunner::adjudicateSyzygy(a, Position::fromFEN("8/8/8/4k3/8/8/8/KQ6 b - - 0 1"), r));
    syzygy::init("");
}

TEST_CASE("syzygy/match-adjudicates-from-the-tables"){
    const char* dir = tablesDir();
    if (!dir) return;
    MatchSettings s;
    EngineConfig a; a.path = MOCK_ENGINE_PATH; a.name = "A"; a.args = {"--name", "A", "--think-ms", "5"};
    EngineConfig b = a; b.name = "B"; b.args[1] = "B";
    s.engines = {a, b};
    s.tc = {2000, 20};
    s.gamesPerPairing = 4;
    s.concurrency = 2;
    // a table win for White from the start: adjudicated before the first move
    s.startFens = {"8/8/8/4k3/8/8/8/KQ6 w - - 0 1"};
    s.adj.syzygyPath = dir;
    MatchRunner m;
    std::string err;
    REQUIRE(m.start(s, &err));
    for (int i = 0; i < 300 && m.running(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(!m.running());
    auto snap = m.snapshot();
    CHECK_EQ(snap.gamesDone, 4);
    CHECK_EQ(snap.pairings[0].wdl.wins, 2);    // each engine wins its two games with White
    CHECK_EQ(snap.pairings[0].wdl.losses, 2);
    for (auto& g : snap.recent){ CHECK(g.reason == ResultReason::ADJUDICATION); CHECK_EQ(g.plies, 0); }
    syzygy::init("");
}
