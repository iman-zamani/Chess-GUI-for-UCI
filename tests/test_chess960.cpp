/* Chess-GUI-for-UCI — Chess960: start positions, X-FEN / Shredder-FEN,
 * castling rules and notation, perft. GPL-2.0
 *
 * The perft numbers in tests/data/chess960_perft.epd were computed by two
 * independent implementations, Stockfish 19 ("go perft" with UCI_Chess960)
 * and python-chess 1.11.2; positions where they disagreed were dropped. */
#include "testlib.hpp"
#include "chess.hpp"
#include "testing.hpp"
#include <set>

TEST_CASE("chess960/start-positions"){
    // the numbering is the standard (Scharnagl) one
    CHECK_EQ(Position::chess960Start(518).toFEN(), Position::startpos().toFEN());
    CHECK(Position::chess960Start(518).chess960);
    CHECK_EQ(Position::chess960Start(0).toFEN(), std::string("bbqnnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w KQkq - 0 1"));
    CHECK_EQ(Position::chess960Start(1).toFEN(), std::string("bqnbnrkr/pppppppp/8/8/8/8/PPPPPPPP/BQNBNRKR w KQkq - 0 1"));
    CHECK_EQ(Position::chess960Start(959).toFEN(), std::string("rkrnnqbb/pppppppp/8/8/8/8/PPPPPPPP/RKRNNQBB w KQkq - 0 1"));
    std::set<std::string> seen;
    for (int n = 0; n < 960; n++){
        Position p = Position::chess960Start(n);
        std::string rank = p.toFEN().substr(p.toFEN().rfind('/') + 1, 8);
        seen.insert(rank);
        int k = (int)rank.find('K'), r1 = (int)rank.find('R'), r2 = (int)rank.rfind('R');
        int b1 = (int)rank.find('B'), b2 = (int)rank.rfind('B');
        CHECK(r1 < k && k < r2);                    // king between the rooks
        CHECK((b1 + b2) % 2 == 1);                  // bishops on opposite colours
        CHECK(p.castleWK && p.castleWQ && p.castleBK && p.castleBQ);
    }
    CHECK_EQ((int)seen.size(), 960);
}

TEST_CASE("chess960/fen-notations"){
    // Shredder-FEN rook files and X-FEN describe the same rights
    bool ok = false;
    Position s = Position::fromFEN("bqnbnrkr/pppppppp/8/8/8/8/PPPPPPPP/BQNBNRKR w HFhf - 0 1", &ok);
    REQUIRE(ok);
    CHECK(s.chess960);
    CHECK_EQ(s.toFEN(), std::string("bqnbnrkr/pppppppp/8/8/8/8/PPPPPPPP/BQNBNRKR w KQkq - 0 1"));
    // X-FEN needs the file letter for an inner rook (two rooks on the same side)
    Position x = Position::fromFEN("4k3/8/8/8/8/8/8/1R2K1RR w G - 0 1", &ok);
    REQUIRE(ok);
    CHECK(x.chess960);
    CHECK(x.castleWK && !x.castleWQ);
    CHECK_EQ((int)x.castleRookFile[0], 6);
    CHECK_EQ(x.toFEN(), std::string("4k3/8/8/8/8/8/8/1R2K1RR w G - 0 1"));
    // standard positions stay standard
    Position st = Position::fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", &ok);
    CHECK(!st.chess960);
    // a castling right without its rook is dropped
    Position none = Position::fromFEN("4k3/8/8/8/8/8/8/4K3 w KQ - 0 1", &ok);
    CHECK(ok && !none.castleWK && !none.castleWQ);
}

TEST_CASE("chess960/castling-moves-and-notation"){
    // king b1, rooks a1 and h1: O-O-O puts the king on c1 and the rook on d1
    bool ok = false;
    Position p = Position::fromFEN("6k1/8/8/8/8/8/8/RK5R w KQ - 0 1", &ok);
    REQUIRE(ok);
    REQUIRE(p.chess960);
    Move oo, ooo;
    REQUIRE(p.uciToMove("b1h1", oo));             // king takes rook = UCI_Chess960 notation
    REQUIRE(p.uciToMove("b1a1", ooo));
    CHECK(oo.isCastle && ooo.isCastle);
    CHECK_EQ(p.moveToSAN(oo), std::string("O-O"));
    CHECK_EQ(p.moveToSAN(ooo), std::string("O-O-O"));
    CHECK_EQ(p.moveToUCI(oo), std::string("b1h1"));
    Position q = p; q.makeMove(ooo);
    CHECK_EQ(q.toFEN(), std::string("6k1/8/8/8/8/8/8/2KR3R b - - 1 1"));
    Position r = p; r.makeMove(oo);
    CHECK_EQ(r.toFEN(), std::string("6k1/8/8/8/8/8/8/R4RK1 b - - 1 1"));
    // king-to-target notation is understood when it can't be a plain king move
    Move alt;
    CHECK(p.uciToMove("b1g1", alt) && alt == oo);
    // king and rook swap squares (king f1, rook g1)
    Position sw = Position::fromFEN("6k1/8/8/8/8/8/8/5KR1 w K - 0 1", &ok);
    REQUIRE(ok && sw.chess960);
    Move swap;
    REQUIRE(sw.uciToMove("f1g1", swap));
    CHECK(swap.isCastle);
    sw.makeMove(swap);
    CHECK_EQ(sw.toFEN(), std::string("6k1/8/8/8/8/8/8/5RK1 b - - 1 1"));
    // the king may not pass through an attacked square
    Position att = Position::fromFEN("2r3k1/8/8/8/8/8/8/R3K3 w Q - 0 1", &ok);   // c1 attacked
    REQUIRE(ok);
    Move none;
    CHECK(!att.uciToMove("e1c1", none) && !att.uciToMove("e1a1", none));
}

TEST_CASE("chess960/standard-castling-notations"){
    Position p = Position::fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    Move a, b;
    REQUIRE(p.uciToMove("e1g1", a));
    REQUIRE(p.uciToMove("e1h1", b));             // king takes rook, as some engines send it
    CHECK(a == b && a.isCastle);
    CHECK_EQ(p.moveToUCI(b), std::string("e1g1"));
}

TEST_CASE("chess960/pgn-variant-tag"){
    Game g;
    g.reset(Position::chess960Start(0));
    Move m;
    REQUIRE(g.pos.uciToMove("g2g4", m)); g.tryMove(m);
    std::string pgn = g.toPGN();
    CHECK(pgn.find("[Variant \"Chess960\"]") != std::string::npos);
    CHECK(pgn.find("[FEN \"bbqnnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w KQkq - 0 1\"]") != std::string::npos);
    auto games = parsePGN(pgn);
    REQUIRE(games.size() == 1);
    Game back;
    REQUIRE(gameFromPGN(games[0], back, nullptr));
    CHECK(back.start.chess960);
    CHECK_EQ(back.pos.toFEN(), g.pos.toFEN());
}

TEST_CASE("chess960/perft"){
    auto suite = loadEpdSuite(std::string(CHESSGUI_SOURCE_DIR) + "/tests/data/chess960_perft.epd");
    REQUIRE(suite.size() >= 20);
    int checked = 0;
    for (auto& c : suite){
        Position p = Position::fromFEN(c.fen);
        p.chess960 = true;                         // #518 and similar look standard: same rules
        for (auto& d : c.depths){
            if (d.first > 4) continue;
            uint64_t n = p.perft(d.first);
            CHECK_MSG(n == d.second, c.fen + " depth " + std::to_string(d.first) + ": " + std::to_string(n)
                                     + " != " + std::to_string(d.second));
            checked++;
        }
    }
    CHECK(checked >= 80);
}
