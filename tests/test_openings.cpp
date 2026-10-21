/* Chess-GUI-for-UCI — opening book tests. GPL-2.0 */
#include "testlib.hpp"
#include "openings.hpp"
#include <set>

TEST_CASE("openings/epd-with-opcodes"){
    auto r = parseEpdBook(
        "# comment\n"
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - id \"e4\"; hmvc 0; fmvn 1;\n"
        "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3\n"           // plain FEN line
        "rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - c0 \"Indian\"; hmvc 1; fmvn 2;\n"
        "8/8/8/8/8/8/8/8 w - - id \"no kings\";\n"                                     // skipped
        "not a position\n");
    CHECK_EQ(r.openings.size(), (size_t)3);
    CHECK_EQ(r.skipped, 2);
    REQUIRE(r.openings.size() == 3);
    CHECK_EQ(r.openings[0].label, std::string("e4"));
    CHECK_EQ(r.openings[1].fen, std::string("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"));
    CHECK_EQ(r.openings[2].fen, std::string("rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 1 2"));
}

TEST_CASE("openings/pgn-first-plies"){
    const char* pgn =
        "[Event \"a\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 *\n\n"
        "[Event \"b\"]\n\n1. d4 *\n\n"
        "[Event \"c\"]\n[SetUp \"1\"]\n[FEN \"4k3/8/8/8/8/8/4P3/4K3 w - - 0 1\"]\n\n1. e4 Kd7 2. e5 *\n";
    auto r = parsePgnBook(pgn, 4);
    REQUIRE(r.openings.size() == 3);
    CHECK_EQ(r.openings[0].moves.size(), (size_t)4);                 // capped at 4 plies
    CHECK_EQ(r.openings[0].finalPosition().toFEN(),
             std::string("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"));
    CHECK_EQ(r.openings[1].moves.size(), (size_t)1);                 // shorter game: all its moves
    CHECK_EQ(r.openings[2].fen, std::string("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"));
    CHECK_EQ(r.openings[2].moves.size(), (size_t)3);
    auto all = parsePgnBook(pgn, 0);
    CHECK_EQ(all.openings[0].moves.size(), (size_t)8);
}

TEST_CASE("openings/schedule"){
    OpeningSchedule seq(5, false, 0);
    for (size_t k = 0; k < 12; k++) CHECK_EQ(seq.forPair(k), k % 5);
    OpeningSchedule a(50, true, 42), b(50, true, 42), c(50, true, 43);
    std::set<size_t> firstPass;
    bool differs = false;
    for (size_t k = 0; k < 50; k++){
        CHECK_EQ(a.forPair(k), b.forPair(k));                         // reproducible
        firstPass.insert(a.forPair(k));
        if (a.forPair(k) != c.forPair(k)) differs = true;
    }
    CHECK_EQ(firstPass.size(), (size_t)50);                            // every opening once per pass
    CHECK(differs);
    bool secondPassDiffers = false;
    for (size_t k = 0; k < 50; k++) if (a.forPair(k) != a.forPair(k + 50)) secondPassDiffers = true;
    CHECK(secondPassDiffers);
}

TEST_CASE("openings/shipped-files-load"){
    auto r = loadOpeningBook(CHESSGUI_SOURCE_DIR "/Resources/accuracy.epd");
    CHECK(r.openings.size() >= 1000);
    CHECK(r.error.empty());
}
