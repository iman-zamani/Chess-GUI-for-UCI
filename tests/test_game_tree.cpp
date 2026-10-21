/* Chess-GUI-for-UCI — game tree: variations, promotion, deletion and PGN with
 * variations, comments, NAGs and [%eval]/[%clk]. GPL-2.0 */
#include "testlib.hpp"
#include "game_tree.hpp"

static std::string oneLine(std::string s){ for (auto& c : s) if (c == '\n') c = ' '; return s; }

static int play(GameTree& t, int at, const std::string& uci){
    Move m;
    REQUIRE(t.at(at).pos.uciToMove(uci, m));
    return t.add(at, m);
}

TEST_CASE("tree/variations-promote-remove"){
    GameTree t;
    int e4 = play(t, 0, "e2e4"), e5 = play(t, e4, "e7e5"), nf3 = play(t, e5, "g1f3");
    int c5 = play(t, e4, "c7c5");                       // a variation on move 1 for Black
    CHECK_EQ(play(t, e4, "e7e5"), e5);                  // the same move again: no duplicate
    CHECK_EQ((int)t.mainLine().size(), 3);
    CHECK(t.onMainLine(nf3) && !t.onMainLine(c5));
    CHECK_EQ(t.at(c5).san, std::string("c5"));
    int nc3 = play(t, c5, "b1c3");
    CHECK_EQ(t.depth(nc3), 3);
    t.promote(nc3);                                     // 1...c5 2.Nc3 becomes the main line
    CHECK(t.onMainLine(nc3) && !t.onMainLine(nf3));
    CHECK_EQ(t.mainLineEnd(), nc3);
    t.remove(c5);                                       // and goes away with its continuation
    CHECK(!t.at(nc3).alive);
    CHECK_EQ(t.mainLineEnd(), nf3);
    Game g = t.lineGame(nf3);
    CHECK_EQ((int)g.moves.size(), 3);
    CHECK_EQ(g.pos.toFEN(), t.at(nf3).pos.toFEN());
}

TEST_CASE("tree/pgn-round-trip"){
    const std::string text =
        "[Event \"Test\"]\n[White \"A\"]\n[Black \"B\"]\n[Result \"*\"]\n[ECO \"C60\"]\n\n"
        "{Start comment} 1. e4 e5 (1... c5 2. Nf3 (2. c3 d5) d6) 2. Nf3 {good} Nc6 $1 3. Bb5!? a6 *\n";
    auto games = parsePGN(text);
    REQUIRE(games.size() == 1);
    GameTree t;
    std::string err;
    REQUIRE(gameTreeFromPGN(games[0], t, &err));
    CHECK_EQ(t.white, std::string("A"));
    CHECK_EQ(t.tags["ECO"], std::string("C60"));
    CHECK_EQ(t.at(0).comment, std::string("Start comment"));
    auto main = t.mainLine();
    REQUIRE(main.size() == 6);
    CHECK_EQ(t.at(main[2]).comment, std::string("good"));
    CHECK(t.at(main[3]).nags == std::vector<int>{1});
    CHECK(t.at(main[4]).nags == std::vector<int>{5});   // "Bb5!?" -> $5
    CHECK_EQ((int)t.at(main[0]).children.size(), 2);     // 1...e5 and 1...c5
    std::string pgn = t.toPGN();
    CHECK(oneLine(pgn).find("{Start comment} 1. e4 e5 (1... c5 2. Nf3 (2. c3 d5) 2... d6) 2. Nf3 {good} 2... Nc6 $1 3. Bb5 $5 a6 *")
          != std::string::npos);
    // reading what we wrote gives the same text again
    GameTree back;
    REQUIRE(gameTreeFromPGN(parsePGN(pgn)[0], back, &err));
    CHECK_EQ(back.toPGN(), pgn);
}

TEST_CASE("tree/pgn-eval-and-clock"){
    auto games = parsePGN("[White \"x\"]\n\n1. d4 {[%eval 0.25] [%clk 0:04:59.5] book} d5 {[%eval #-3]} *\n");
    GameTree t;
    REQUIRE(gameTreeFromPGN(games[0], t));
    auto main = t.mainLine();
    REQUIRE(main.size() == 2);
    CHECK(t.at(main[0]).hasEval && !t.at(main[0]).evalMate && t.at(main[0]).evalCp == 25);
    CHECK_EQ(t.at(main[0]).clockMs, 299500LL);
    CHECK_EQ(t.at(main[0]).comment, std::string("book"));
    CHECK(t.at(main[1]).evalMate && t.at(main[1]).mateIn == -3);
    std::string pgn = t.toPGN();
    CHECK(oneLine(pgn).find("1. d4 {[%eval 0.25] [%clk 0:04:59.5] book} 1... d5 {[%eval #-3]} *") != std::string::npos);
}

TEST_CASE("tree/pgn-illegal-move-keeps-the-prefix"){
    auto games = parsePGN("1. e4 e5 2. Ke3 Nc6 *\n");
    GameTree t;
    std::string err;
    CHECK(!gameTreeFromPGN(games[0], t, &err));
    CHECK(err.find("Ke3") != std::string::npos);
    CHECK_EQ((int)t.mainLine().size(), 2);
}

TEST_CASE("tree/chess960-and-fen-start"){
    GameTree t;
    t.reset(Position::chess960Start(0));
    play(t, 0, "g2g4");
    std::string pgn = t.toPGN();
    CHECK(pgn.find("[Variant \"Chess960\"]") != std::string::npos);
    GameTree back;
    REQUIRE(gameTreeFromPGN(parsePGN(pgn)[0], back));
    CHECK(back.start().chess960);
    CHECK_EQ(back.at(back.mainLineEnd()).pos.toFEN(), t.at(t.mainLineEnd()).pos.toFEN());
}
