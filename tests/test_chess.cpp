/* Chess-GUI-for-UCI — tests for the core rules: perft, FEN, SAN, PGN. GPL-2.0 */
#include "testlib.hpp"
#include "chess.hpp"
#include "testing.hpp"
#include <algorithm>

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

TEST_CASE("fen/validation"){
    bool ok = true;
    // ranks must have eight squares each, not just 64 in total
    Position::fromFEN("rnbqkbnr/pppppppp/9/7/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &ok); CHECK(!ok);
    Position::fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPPRNBQKBNR w KQkq - 0 1", &ok); CHECK(!ok);      // seven ranks
    Position::fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR/8 w KQkq - 0 1", &ok); CHECK(!ok);   // nine
    Position::fromFEN("rnbqkbnr/pppppppp/08/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &ok); CHECK(!ok);
    // the side to move is w or b
    Position::fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1", &ok); CHECK(!ok);
    // en passant: only behind a pawn that can just have made a double push
    Position::fromFEN("4k3/8/8/8/3p4/8/8/4K2R b - e3 0 1", &ok); CHECK(!ok);            // no white pawn on e4
    Position::fromFEN("4k3/8/8/8/3pP3/8/8/4K2R b - e6 0 1", &ok); CHECK(!ok);           // wrong rank for Black to move
    Position::fromFEN("4k3/8/8/8/3pP3/4N3/8/4K3 b - e3 0 1", &ok); CHECK(!ok);          // the square is occupied
    Position::fromFEN("4k3/8/8/8/3pP3/8/4N3/4K3 b - e3 0 1", &ok); CHECK(!ok);          // the pawn's start square is occupied
    Position::fromFEN("4k3/8/8/8/3pP3/8/8/4K3 b - z9 0 1", &ok); CHECK(!ok);
    Position p = Position::fromFEN("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1", &ok); CHECK(ok);
    CHECK_EQ(p.epSquare, nameToSquare("e3"));
    // written after every double push, capturable or not (as most programs do)
    p = Position::fromFEN("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", &ok); CHECK(ok);
    CHECK_EQ(p.epSquare, nameToSquare("e3"));
    // a negative halfmove clock is read as 0
    p = Position::fromFEN("4k3/8/8/8/8/8/8/4K3 w - - -5 1", &ok); CHECK(ok);
    CHECK_EQ(p.halfmoveClock, 0);
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

TEST_CASE("pgn/comments-over-lines"){
    Game g; std::string err;
    // a { } comment continued on a line that starts with '[' is not a tag line
    auto games = parsePGN("[Event \"x\"]\n[Result \"*\"]\n\n1. e4 {\n[%clk 0:01:00] } e5 2. Nf3 Nc6 *\n");
    REQUIRE(games.size() == 1);
    REQUIRE(gameFromPGN(games[0], g, &err));
    CHECK_EQ(g.sans.size(), (size_t)4);
    // ';' comments run to the end of their line only
    games = parsePGN("[Event \"x\"]\n\n1. e4 ; a comment with a } brace\ne5 2. Nf3 Nc6 *\n");
    REQUIRE(games.size() == 1);
    REQUIRE(gameFromPGN(games[0], g, &err));
    CHECK_EQ(g.sans.size(), (size_t)4);
    // '%' escape lines are skipped; a ';' inside { } is text
    games = parsePGN("[Event \"x\"]\n\n1. e4 {so; what} e5\n% an escape line\n2. Nf3 *\n");
    REQUIRE(games.size() == 1);
    REQUIRE(gameFromPGN(games[0], g, &err));
    CHECK_EQ(g.sans.size(), (size_t)3);
    // two games stay two games
    games = parsePGN("[Event \"a\"]\n\n1. e4 {x\n[y]} *\n\n[Event \"b\"]\n\n1. d4 *\n");
    CHECK_EQ(games.size(), (size_t)2);
}

TEST_CASE("san/variants-are-read"){
    Move m;
    Position p = Position::fromFEN("r3k2r/pppq1ppp/2n2n2/3pp3/3PP3/2N2N2/PPPQ1PPP/R3K2R w KQkq - 0 8");
    for (const char* s : {"O-O", "0-0", "O-O+", "0-0-0", "O-O-O"}) CHECK(p.sanToMove(s, m));
    REQUIRE(p.sanToMove("0-0", m)); CHECK_EQ(p.moveToUCI(m), std::string("e1g1"));
    // more disambiguation than needed, long algebraic, capture marks
    REQUIRE(p.sanToMove("Nfxe5", m)); CHECK_EQ(p.moveToUCI(m), std::string("f3e5"));
    REQUIRE(p.sanToMove("Nf3xe5", m)); CHECK_EQ(p.moveToUCI(m), std::string("f3e5"));
    REQUIRE(p.sanToMove("Nf3-g5", m)); CHECK_EQ(p.moveToUCI(m), std::string("f3g5"));
    REQUIRE(p.sanToMove("d4xe5", m)); CHECK_EQ(p.moveToUCI(m), std::string("d4e5"));
    REQUIRE(p.sanToMove("dxe5!?", m)); CHECK_EQ(p.moveToUCI(m), std::string("d4e5"));
    REQUIRE(p.sanToMove("a2-a4", m)); CHECK_EQ(p.moveToUCI(m), std::string("a2a4"));
    // not enough disambiguation, wrong piece, nonsense
    p = Position::fromFEN("4k3/8/8/8/8/2N3N1/8/4K3 w - - 0 1");
    CHECK(!p.sanToMove("Ne4", m));                              // two knights can go there
    CHECK(p.sanToMove("Nce4", m));
    CHECK(!p.sanToMove("Be4", m));
    CHECK(!p.sanToMove("", m)); CHECK(!p.sanToMove("x", m)); CHECK(!p.sanToMove("Zz9", m)); CHECK(!p.sanToMove("e9", m));
    // promotions: with '=', without it, lower case; a missing piece is not a move
    p = Position::fromFEN("3k4/1P6/8/8/8/8/8/4K3 w - - 0 1");
    for (const char* s : {"b8=Q", "b8Q", "b8=Q+", "b8q"}){ REQUIRE(p.sanToMove(s, m)); CHECK_EQ(p.moveToUCI(m), std::string("b7b8q")); }
    REQUIRE(p.sanToMove("b8=N", m)); CHECK_EQ(p.moveToUCI(m), std::string("b7b8n"));
    CHECK(!p.sanToMove("b8", m));
    CHECK(!p.sanToMove("b8=K", m));
    // en passant with its suffix
    p = Position::fromFEN("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3");
    REQUIRE(p.sanToMove("exf6e.p.", m)); CHECK(m.isEnPassant);
    // the b-file pawn and the bishop are told apart by case
    p = Position::fromFEN("4k3/8/8/8/8/2p5/1P1B4/4K3 w - - 0 1");
    REQUIRE(p.sanToMove("bxc3", m)); CHECK_EQ(p.moveToUCI(m), std::string("b2c3"));
    REQUIRE(p.sanToMove("Bxc3", m)); CHECK_EQ(p.moveToUCI(m), std::string("d2c3"));
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

TEST_CASE("game/repetition-and-en-passant"){
    auto play = [](Game& g, std::initializer_list<const char*> moves){
        for (const char* u : moves){ Move m; REQUIRE(g.pos.uciToMove(u, m)); REQUIRE(g.tryMove(m)); }
    };
    // After 1.e4 nobody can capture en passant, so the en-passant square does not make that
    // position different from its later occurrences: the third one is a draw.
    Game g; g.reset(Position::startpos());
    play(g, {"e2e4", "g8f6", "g1f3", "f6g8", "f3g1"});
    CHECK(g.result == GameResult::ONGOING);                    // second occurrence
    play(g, {"g8f6", "g1f3", "f6g8", "f3g1"});
    CHECK(g.result == GameResult::DRAW);
    CHECK(g.reason == ResultReason::REPETITION);
    // When the capture is possible, the position with the right is a different one.
    g.reset(Position::fromFEN("4k1n1/3p4/8/4P3/8/8/8/4K1N1 b - - 0 1"));
    play(g, {"d7d5", "g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8"});
    CHECK(g.result == GameResult::ONGOING);                    // without the right: only twice
    play(g, {"g1f3"});                                         // (this one is there for the third time)
    CHECK(g.result == GameResult::DRAW);
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
