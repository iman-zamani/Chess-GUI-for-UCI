/* Chess-GUI-for-UCI — opening book tests. GPL-2.0 */
#include "testlib.hpp"
#include "openings.hpp"
#include <filesystem>
#include <fstream>
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

TEST_CASE("openings/pgn-variants-and-broken-games"){
    // castling written with zeros and a ';' comment: nothing is lost
    auto r = parsePgnBook("[Event \"x\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bc4 Bc5 ; Italian\n4. 0-0 Nf6 5. d3 d6 *\n", 10);
    REQUIRE(r.openings.size() == 1);
    CHECK_EQ(r.openings[0].moves.size(), (size_t)10);
    CHECK_EQ(r.truncated, 0);
    CHECK(r.warning().empty());
    // a game with an unreadable move inside the plies that are used is reported, and its prefix kept
    r = parsePgnBook("[Event \"a\"]\n\n1. e4 e5 2. Ke3 Nc6 *\n\n[Event \"b\"]\n\n1. d4 d5 2. c4 e6 *\n", 4);
    REQUIRE(r.openings.size() == 2);
    CHECK_EQ(r.openings[0].moves.size(), (size_t)2);
    CHECK_EQ(r.openings[1].moves.size(), (size_t)4);
    CHECK_EQ(r.truncated, 1);
    CHECK(r.warning().find("game 1") != std::string::npos);
    // ... but not when the bad move comes after them: the rest of the game is never read
    r = parsePgnBook("[Event \"a\"]\n\n1. e4 e5 2. Ke3 Nc6 *\n", 2);
    CHECK_EQ(r.truncated, 0);
    CHECK_EQ(r.openings[0].moves.size(), (size_t)2);
}

TEST_CASE("openings/pgn-unclosed-comment"){
    // the open comment ends at the next game's tags; the broken game is counted and named
    auto r = parsePgnBook("[Event \"a\"]\n\n1. e4 {oops e5 2. Nf3 *\n\n"
                          "[Event \"b\"]\n\n1. d4 d5 *\n\n"
                          "[Event \"c\"]\n\n1. c4 e5 *\n", 2);
    REQUIRE(r.openings.size() == 3);
    CHECK_EQ(r.openings[0].moves.size(), (size_t)1);
    CHECK_EQ(r.openings[1].moves.size(), (size_t)2);
    CHECK_EQ(r.openings[2].moves.size(), (size_t)2);
    CHECK_EQ(r.truncated, 1);
    CHECK(r.firstProblem.find("game 1") != std::string::npos);
    CHECK(r.firstProblem.find("comment") != std::string::npos);
    CHECK(!r.warning().empty());
}

TEST_CASE("openings/book-in-a-folder-outside-ascii"){
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / fs::u8path(u8"chessgui-\u00e9checs-\u68cb");
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path book = dir / fs::u8path(u8"d\u00e9buts.epd");
    { std::ofstream f(book); f << "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1\n"; }
    auto r = loadOpeningBook(book.u8string());
    CHECK(r.error.empty());
    CHECK_EQ(r.openings.size(), (size_t)1);
    fs::remove_all(dir, ec);
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
