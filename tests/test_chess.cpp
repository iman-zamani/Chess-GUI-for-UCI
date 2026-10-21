/* Chess-GUI-for-UCI — tests for the core rules: perft, FEN, SAN, PGN. GPL-2.0 */
#include "testlib.hpp"
#include "chess.hpp"
#include "testing.hpp"

// The six canonical positions from the Chess Programming Wiki "Perft Results" page
// (https://www.chessprogramming.org/Perft_Results). Depths are capped so the whole
// group runs in a few seconds in a Release build.
static const struct { const char* name; const char* fen; std::vector<uint64_t> nodes; } kCanonical[] = {
    {"initial", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        {20, 400, 8902, 197281, 4865609}},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        {48, 2039, 97862, 4085603}},
    {"position3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        {14, 191, 2812, 43238, 674624}},
    {"position4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        {6, 264, 9467, 422333}},
    {"position5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        {44, 1486, 62379, 2103487}},
    {"position6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        {46, 2079, 89890, 3894594}},
};

TEST_CASE("perft/canonical"){
    for (auto& c : kCanonical){
        bool ok = false;
        Position p = Position::fromFEN(c.fen, &ok);
        REQUIRE(ok);
        for (size_t d = 0; d < c.nodes.size(); d++){
            uint64_t n = p.perft((int)d + 1);
            CHECK_MSG(n == c.nodes[d], c.name << " depth " << d + 1 << ": got " << n
                                              << ", expected " << c.nodes[d]);
        }
    }
}

TEST_CASE("perft/suite-depth3"){
    auto suite = loadEpdSuite(CHESSGUI_SOURCE_DIR "/Resources/perftsuite.epd");
    REQUIRE(suite.size() >= 100);
    int checked = 0;
    for (auto& c : suite){
        bool ok = false;
        Position p = Position::fromFEN(c.fen, &ok);
        REQUIRE(ok);
        for (auto& d : c.depths){
            if (d.first > 3) continue;
            uint64_t n = p.perft(d.first);
            CHECK_MSG(n == d.second, c.fen << " depth " << d.first << ": got " << n
                                           << ", expected " << d.second);
            checked++;
        }
    }
    CHECK(checked >= 300);
}

TEST_CASE("perft/builtin-suite"){
    std::atomic<bool> abortFlag{false};
    CHECK(runInternalPerftSelfTest(builtinPerftSuite(), 4, nullptr, abortFlag));
}

TEST_CASE("fen/roundtrip"){
    auto suite = loadEpdSuite(CHESSGUI_SOURCE_DIR "/Resources/perftsuite.epd");
    REQUIRE(!suite.empty());
    for (auto& c : suite){
        bool ok = false;
        Position p = Position::fromFEN(c.fen, &ok);
        REQUIRE(ok);
        std::string again = p.toFEN();
        Position q = Position::fromFEN(again, &ok);
        REQUIRE(ok);
        CHECK_EQ(q.toFEN(), again);
        // the suite stores full FENs: printing must reproduce them exactly
        if (std::count(c.fen.begin(), c.fen.end(), ' ') == 5) CHECK_EQ(again, c.fen);
    }
}

TEST_CASE("fen/rejects-garbage"){
    bool ok = true;
    Position::fromFEN("not a fen", &ok); CHECK(!ok);
    Position::fromFEN("8/8/8/8/8/8/8/8 w - - 0 1", &ok); CHECK(!ok);              // no kings
    Position::fromFEN("rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &ok); CHECK(!ok);
    Position::fromFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1", &ok); CHECK(ok);
}

TEST_CASE("fen/counters-and-ep"){
    bool ok = false;
    Position p = Position::fromFEN("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3", &ok);
    REQUIRE(ok);
    CHECK_EQ(p.epSquare, nameToSquare("f6"));
    CHECK_EQ(p.fullmoveNumber, 3);
    Move m;
    REQUIRE(p.uciToMove("e5f6", m));
    CHECK(m.isEnPassant);
    CHECK_EQ(p.moveToSAN(m), std::string("exf6"));
}

TEST_CASE("san/roundtrip-all-legal-moves"){
    auto suite = loadEpdSuite(CHESSGUI_SOURCE_DIR "/Resources/perftsuite.epd");
    REQUIRE(!suite.empty());
    int moves = 0;
    for (auto& c : suite){
        Position p = Position::fromFEN(c.fen);
        std::vector<Move> legal; p.generateLegal(legal);
        for (const Move& m : legal){
            std::string san = p.moveToSAN(m);
            Move back;
            CHECK_MSG(p.sanToMove(san, back) && back == m, c.fen << ": SAN " << san);
            Move viaUci;
            CHECK(p.uciToMove(p.moveToUCI(m), viaUci) && viaUci == m);
            moves++;
        }
    }
    CHECK(moves > 2000);
}

TEST_CASE("san/notation-details"){
    // knights on b1 and f3... both can reach d2: file disambiguation
    Position p = Position::fromFEN("4k3/8/8/8/8/5N2/8/1N2K3 w - - 0 1");
    Move m; REQUIRE(p.uciToMove("b1d2", m));
    CHECK_EQ(p.moveToSAN(m), std::string("Nbd2"));
    // rooks on a1 and a5 -> rank disambiguation
    p = Position::fromFEN("4k3/8/8/R7/8/8/8/R3K3 w - - 0 1");
    REQUIRE(p.uciToMove("a1a3", m));
    CHECK_EQ(p.moveToSAN(m), std::string("R1a3"));
    // castling, promotion with check, mate
    p = Position::fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    REQUIRE(p.uciToMove("e1g1", m)); CHECK_EQ(p.moveToSAN(m), std::string("O-O"));
    REQUIRE(p.uciToMove("e1c1", m)); CHECK_EQ(p.moveToSAN(m), std::string("O-O-O"));
    p = Position::fromFEN("3k4/1P6/8/8/8/8/8/4K3 w - - 0 1");
    REQUIRE(p.uciToMove("b7b8q", m)); CHECK_EQ(p.moveToSAN(m), std::string("b8=Q+"));
    p = Position::fromFEN("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
    REQUIRE(p.uciToMove("a1a8", m)); CHECK_EQ(p.moveToSAN(m), std::string("Ra8#"));
}

TEST_CASE("pgn/roundtrip"){
    // Play a short game, export it, re-import it, and compare move by move.
    Game g; g.reset(Position::startpos());
    for (const char* u : {"e2e4","e7e5","g1f3","b8c6","f1b5","a7a6","b5c6","d7c6","e1g1","f7f6",
                          "d2d4","e5d4","f3d4","c6c5","d4e2","d8d1","f1d1"}){
        Move m; REQUIRE(g.pos.uciToMove(u, m)); REQUIRE(g.tryMove(m));
    }
    g.whiteName = "Alpha"; g.blackName = "Beta";
    std::string pgn = g.toPGN();
    auto games = parsePGN(pgn);
    REQUIRE(games.size() == 1);
    CHECK_EQ(games[0].white, std::string("Alpha"));
    Game back; std::string err;
    REQUIRE(gameFromPGN(games[0], back, &err));
    CHECK_EQ(back.sans.size(), g.sans.size());
    for (size_t i = 0; i < g.sans.size() && i < back.sans.size(); i++) CHECK_EQ(back.sans[i], g.sans[i]);
    CHECK_EQ(back.pos.toFEN(), g.pos.toFEN());
}

TEST_CASE("pgn/fen-start-and-annotations"){
    const char* text =
        "[Event \"Test\"]\n[White \"W\"]\n[Black \"B\"]\n[Result \"1-0\"]\n"
        "[SetUp \"1\"]\n[FEN \"6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 30\"]\n\n"
        "30. Ra7 {a comment} h6 (30... h5 31. Ra8#) $1 31. Ra8+ Kh7 32. Rf8!? 1-0\n\n"
        "[Event \"Second\"]\n[White \"X\"]\n[Black \"Y\"]\n[Result \"*\"]\n\n1. d4 d5 2. c4 *\n";
    auto games = parsePGN(text);
    REQUIRE(games.size() == 2);
    Game g; std::string err;
    REQUIRE(gameFromPGN(games[0], g, &err));
    CHECK_EQ(g.sans.size(), (size_t)5);
    CHECK(g.start.fullmoveNumber == 30);
    CHECK(g.result == GameResult::WHITE_WINS);
    std::string again = g.toPGN();
    CHECK(again.find("[FEN \"6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 30\"]") != std::string::npos);
    CHECK(again.find("30. Ra7 h6 31. Ra8+ Kh7 32. Rf8") != std::string::npos);
    Game g2;
    REQUIRE(gameFromPGN(games[1], g2, &err));
    CHECK_EQ(g2.sans.size(), (size_t)3);
}

TEST_CASE("pgn/illegal-move-reported"){
    auto games = parsePGN("[Event \"x\"]\n\n1. e4 e5 2. Ke3 *\n");
    REQUIRE(games.size() == 1);
    Game g; std::string err;
    CHECK(!gameFromPGN(games[0], g, &err));
    CHECK(err.find("Ke3") != std::string::npos);
}

TEST_CASE("game/draw-rules"){
    Game g;
    // threefold repetition by knight shuffles
    g.reset(Position::startpos());
    for (int i = 0; i < 2; i++)
        for (const char* u : {"g1f3","g8f6","f3g1","f6g8"}){ Move m; g.pos.uciToMove(u, m); g.tryMove(m); }
    CHECK(g.result == GameResult::DRAW);
    CHECK(g.reason == ResultReason::REPETITION);
    // stalemate
    g.reset(Position::fromFEN("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
    CHECK(g.result == GameResult::ONGOING);
    g.updateStatus();
    CHECK(g.reason == ResultReason::STALEMATE);
    // insufficient material after the queen is captured
    g.reset(Position::fromFEN("8/8/4k3/8/8/4n3/8/3QK3 b - - 0 1"));
    Move m; REQUIRE(g.pos.uciToMove("e3d1", m)); g.tryMove(m);
    CHECK(g.result == GameResult::DRAW);
    CHECK(g.reason == ResultReason::INSUFFICIENT);
}

TEST_CASE("game/undo"){
    Game g; g.reset(Position::startpos());
    Move m; g.pos.uciToMove("e2e4", m); g.tryMove(m);
    g.pos.uciToMove("e7e5", m); g.tryMove(m);
    g.undo();
    CHECK_EQ(g.sans.size(), (size_t)1);
    CHECK_EQ(g.pos.toFEN(), std::string("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"));
}

TEST_CASE("game/result-reasons"){
    CHECK_EQ(resultReasonText(ResultReason::ADJUDICATION), std::string("adjudication"));
    CHECK_EQ(resultReasonText(ResultReason::NONE), std::string(""));
    // a PGN result without a decisive final position keeps the result, reason unknown
    auto games = parsePGN("[Result \"0-1\"]\n\n1. e4 e5 0-1\n");
    REQUIRE(games.size() == 1);
    Game g; std::string err;
    REQUIRE(gameFromPGN(games[0], g, &err));
    CHECK(g.result == GameResult::BLACK_WINS);
    CHECK(g.reason == ResultReason::NONE);
}

#include "tournament.hpp"
TEST_CASE("game/time-control-label"){
    CHECK_EQ((TimeControl{300000, 0}.label()), std::string("5+0"));
    CHECK_EQ((TimeControl{180000, 2000}.label()), std::string("3+2"));
    CHECK_EQ((TimeControl{10000, 100}.label()), std::string("10s+0.1"));
}

TEST_CASE("game/review-marks"){
    CHECK_EQ(reviewMark(60, 25, false, false, 0), 4);     // -35 points: ??
    CHECK_EQ(reviewMark(60, 38, false, false, 0), 2);     // -22: ?
    CHECK_EQ(reviewMark(60, 48, false, false, 0), 6);     // -12: ?!
    CHECK_EQ(reviewMark(60, 55, false, false, 0), 0);
    CHECK_EQ(reviewMark(55, 55, true, true, 35), 1);      // only good move: next best 20 worse
    CHECK_EQ(reviewMark(70, 70, true, true, 25), 3);      // 45-point gap, not yet winning
    CHECK_EQ(reviewMark(95, 95, true, true, 40), 1);      // already winning: no !!
    CHECK_EQ(reviewMark(55, 55, true, true, 50), 0);      // several good moves
    CHECK_EQ(reviewMark(55, 55, true, false, 0), 0);      // only one legal move: nothing to mark
}
