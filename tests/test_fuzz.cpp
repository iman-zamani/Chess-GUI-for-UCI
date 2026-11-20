/* Chess-GUI-for-UCI — deterministic mutation tests for the text parsers. GPL-2.0
 *
 * Valid inputs are mutated (byte flips, insertions, deletions, truncations,
 * token swaps) with a fixed seed and fed to each parser. A case passes when
 * nothing crashes, hangs or trips a sanitizer, and accepted input satisfies a
 * few invariants. tests/fuzz/fuzz_parsers.cpp is the coverage-guided version.
 */
#include "testlib.hpp"
#include "chess.hpp"
#include "engine_config.hpp"
#include "game_tree.hpp"
#include "openings.hpp"
#include "perft_bisect.hpp"
#include "uci_engine.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace {

// splitmix64: the same sequence on every platform and standard library.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next(){
        uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    size_t below(size_t n){ return n ? (size_t)(next() % n) : 0; }
    bool chance(int percent){ return (int)below(100) < percent; }
};

// Bytes the parsers care about, so mutations often hit a meaningful branch.
const std::string kSpecial = "0123456789/-+=#!?()[]{};:%$\"'\\ \t\nKQRBNPkqrbnpxeO.wabcdfgh*";

std::vector<std::string> tokens(const std::string& s){
    std::vector<std::string> t;
    std::string cur;
    for (char c : s){
        if (c == ' '){ t.push_back(cur); cur.clear(); } else cur += c;
    }
    t.push_back(cur);
    return t;
}

std::string untokens(const std::vector<std::string>& t){
    std::string s;
    for (size_t i = 0; i < t.size(); i++){ if (i) s += ' '; s += t[i]; }
    return s;
}

char randomByte(Rng& r){
    return r.chance(70) ? kSpecial[r.below(kSpecial.size())] : (char)r.below(256);
}

std::string mutate(std::string s, Rng& r, const std::vector<std::string>& dictionary){
    int ops = 1 + (int)r.below(4);
    for (int k = 0; k < ops; k++){
        switch (r.below(8)){
        case 0:                                              // overwrite a byte
            if (!s.empty()) s[r.below(s.size())] = randomByte(r);
            break;
        case 1:                                              // insert bytes
            for (size_t n = 1 + r.below(3); n > 0; n--) s.insert(s.begin() + (long)r.below(s.size() + 1), randomByte(r));
            break;
        case 2:                                              // delete a run
            if (!s.empty()){ size_t p = r.below(s.size()); s.erase(p, 1 + r.below(8)); }
            break;
        case 3:                                              // truncate
            s.resize(r.below(s.size() + 1));
            break;
        case 4: {                                            // swap two tokens
            auto t = tokens(s);
            if (t.size() > 1){ size_t a = r.below(t.size()), b = r.below(t.size()); std::swap(t[a], t[b]); }
            s = untokens(t);
            break;
        }
        case 5: {                                            // duplicate or drop a token
            auto t = tokens(s);
            size_t a = r.below(t.size());
            if (r.chance(50)) t.insert(t.begin() + (long)a, t[a]); else t.erase(t.begin() + (long)a);
            s = untokens(t);
            break;
        }
        case 6: {                                            // insert a dictionary token
            auto t = tokens(s);
            if (!dictionary.empty()) t.insert(t.begin() + (long)r.below(t.size() + 1), dictionary[r.below(dictionary.size())]);
            s = untokens(t);
            break;
        }
        default:                                             // flip one bit
            if (!s.empty()) s[r.below(s.size())] ^= (char)(1 << r.below(8));
            break;
        }
    }
    return s;
}

// Rounds per parser: at least 20,000 mutated inputs each.
const int kRounds = 20000;

// Every invariant that a position accepted by fromFEN must satisfy.
void checkPosition(const Position& p, const std::string& input){
    std::string fen = p.toFEN();
    bool ok2 = false;
    Position q = Position::fromFEN(fen, &ok2);
    CHECK_MSG(ok2, "toFEN() of an accepted FEN does not parse: '" << input << "' -> '" << fen << "'");
    if (!ok2) return;
    CHECK_MSG(q.toFEN() == fen, "FEN round trip changed: '" << fen << "' -> '" << q.toFEN() << "'");
    std::vector<Move> legal;
    p.generateLegal(legal);
    for (const Move& m : legal){
        Position after = p;
        after.makeMove(m);
        CHECK_MSG(!after.inCheck(p.whiteToMove), "legal move " << p.moveToUCI(m) << " leaves the king in check in '" << fen << "'");
        std::string san = p.moveToSAN(m);
        Move back;
        bool read = p.sanToMove(san, back);
        CHECK_MSG(read && back == m, "SAN '" << san << "' of " << p.moveToUCI(m) << " is not read back in '" << fen << "'");
    }
}

} // namespace

TEST_CASE("fuzz/fen"){
    const std::vector<std::string> seeds = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
        "3k4/1P6/8/8/8/8/8/4K3 w - - 0 1",
        "bqnb1rkr/pp3ppp/3ppn2/2p5/5P2/P2P4/NPP1P1PP/BQ1BNRKR w HFhf - 2 9",   // Chess960, Shredder-FEN
        "nrbqkbrn/pppppppp/8/8/8/8/PPPPPPPP/NRBQKBRN w GBgb - 0 1",
        "4k3/8/8/8/8/8/8/R3K2R b KQ - 99 120",
    };
    const std::vector<std::string> dict = {"KQkq", "-", "e3", "h6", "w", "b", "8", "0", "HAha", "Kk", "/", "999999999999"};
    Rng r(0xF0E1);
    int accepted = 0;
    for (const auto& s : seeds){ bool ok = false; Position p = Position::fromFEN(s, &ok); REQUIRE(ok); checkPosition(p, s); }
    for (int i = 0; i < kRounds; i++){
        std::string in = mutate(seeds[r.below(seeds.size())], r, dict);
        bool ok = false;
        Position p = Position::fromFEN(in, &ok);
        if (!ok) continue;
        accepted++;
        checkPosition(p, in);
    }
    CHECK(accepted > 0);   // the invariants above were exercised
}

TEST_CASE("fuzz/san"){
    const std::vector<std::string> fens = {
        "r3k2r/pppq1ppp/2n2n2/3pp3/3PP3/2N2N2/PPPQ1PPP/R3K2R w KQkq - 0 8",
        "3k4/1P6/8/8/8/8/8/4K3 w - - 0 1",
        "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
        "bqnb1rkr/pp3ppp/3ppn2/2p5/5P2/P2P4/NPP1P1PP/BQ1BNRKR w HFhf - 2 9",
    };
    const std::vector<std::string> sans = {"O-O", "0-0-0", "Nfxe5", "Nf3-g5", "d4xe5", "dxe5!?", "b8=Q+", "b8q",
                                           "exf6e.p.", "Qxd7#", "a2-a4", "Kxe1", "R1a3", "Nce4", "e4"};
    std::vector<Position> pos;
    for (auto& f : fens){ bool ok = false; pos.push_back(Position::fromFEN(f, &ok)); REQUIRE(ok); }
    Rng r(0x5A11);
    for (int i = 0; i < kRounds; i++){
        const Position& p = pos[r.below(pos.size())];
        std::string in = mutate(sans[r.below(sans.size())], r, sans);
        Move m;
        if (!p.sanToMove(in, m)) continue;
        // an accepted move must be one of the legal moves
        std::vector<Move> legal;
        p.generateLegal(legal);
        bool found = false;
        for (auto& l : legal) found = found || l == m;
        CHECK_MSG(found, "sanToMove('" << in << "') gave an illegal move " << p.moveToUCI(m));
    }
}

TEST_CASE("fuzz/pgn"){
    const std::vector<std::string> seeds = {
        "[Event \"Test\"]\n[White \"W\"]\n[Black \"B\"]\n[Result \"1-0\"]\n"
        "[SetUp \"1\"]\n[FEN \"6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 30\"]\n\n"
        "30. Ra7 {a comment} h6 (30... h5 31. Ra8#) $1 31. Ra8+ Kh7 32. Rf8!? 1-0\n\n"
        "[Event \"Second\"]\n[White \"X\"]\n[Black \"Y\"]\n[Result \"*\"]\n\n1. d4 d5 2. c4 *\n",
        "[Event \"x\"]\n[Result \"*\"]\n\n1. e4 {\n[%clk 0:01:00] } e5 2. Nf3 Nc6 *\n",
        "[Event \"x\"]\n\n1. e4 ; a comment with a } brace\ne5 2. Nf3 Nc6 *\n",
        "[Event \"x\"]\n\n1. e4 {so; what} e5\n% an escape line\n2. Nf3 *\n",
        "[Event \"v\"]\n[White \"A\"]\n[Black \"B\"]\n[Result \"1/2-1/2\"]\n\n"
        "1. e4 {[%eval 0.25] [%clk 0:05:00]} e5 (1... c5 2. Nf3 (2. c3 d5) d6 $2) 2. Nf3 Nc6 "
        "3. Bb5 a6 4. Ba4?! Nf6 5. O-O Be7 6. Re1 b5 7. Bb3 d6 8. c3 O-O 1/2-1/2\n",
        "[Variant \"Chess960\"]\n[FEN \"bqnb1rkr/pp3ppp/3ppn2/2p5/5P2/P2P4/NPP1P1PP/BQ1BNRKR w HFhf - 2 9\"]\n\n"
        "9. g3 cxb2 *\n",
    };
    const std::vector<std::string> dict = {"(", ")", "{", "}", "$3", "1-0", "*", "[%eval #-3]", "O-O-O", "e8=Q", "..", ";",
                                           "\n[FEN \"8/8/8/8/8/8/8/8 w - - 0 1\"]\n", "\n\n[Event \"y\"]\n\n"};
    Rng r(0x9C9);
    int trees = 0;
    for (int i = 0; i < kRounds; i++){
        std::string in = mutate(seeds[r.below(seeds.size())], r, dict);
        for (const PgnGame& pg : parsePGN(in)){
            Game g; std::string err;
            gameFromPGN(pg, g, &err);
            GameTree t; std::string terr;
            if (!gameTreeFromPGN(pg, t, &terr)) continue;
            trees++;
            // a tree that was read prints and reads back with the same main line
            std::string out = t.toPGN();
            auto again = parsePGN(out);
            CHECK_MSG(again.size() == 1, "toPGN() output is not one game: '" << in << "' -> '" << out << "'");
            if (again.size() != 1) continue;
            GameTree t2; std::string err2;
            bool ok2 = gameTreeFromPGN(again[0], t2, &err2);
            CHECK_MSG(ok2, "toPGN() output does not read back (" << err2 << "): '" << out << "'");
            if (!ok2) continue;
            auto a = t.mainLine(), b = t2.mainLine();
            bool same = a.size() == b.size() && t.start().toFEN() == t2.start().toFEN();
            for (size_t k = 0; same && k < a.size(); k++) same = t.at(a[k]).move == t2.at(b[k]).move;
            CHECK_MSG(same, "main line changed through toPGN(): '" << in << "' -> '" << out << "'");
        }
    }
    CHECK(trees > 0);
}

TEST_CASE("fuzz/books"){
    const std::string epd =
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - hmvc 0; fmvn 1; id \"e4\";\n"
        "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3\n"
        "bqnb1rkr/pp3ppp/3ppn2/2p5/5P2/P2P4/NPP1P1PP/BQ1BNRKR w HFhf - id \"960\";\n";
    const std::string pgn =
        "[Event \"a\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 *\n\n"
        "[Event \"b\"]\n[FEN \"3k4/1P6/8/8/8/8/8/4K3 w - - 0 1\"]\n\n1. b8=Q+ Kd7 *\n\n"
        "[Event \"c\"]\n\n1. d4 Nf6 2. c4 e6 3. Nc3 Bb4 4. Qc2 *\n";
    const std::vector<std::string> dict = {";", "id", "hmvc", "fmvn", "\"", "-", "\n", "KQkq", "c3", "{", "("};
    Rng r(0xB00C);
    for (int i = 0; i < kRounds; i++){
        BookLoadResult e = parseEpdBook(mutate(epd, r, dict));
        BookLoadResult p = parsePgnBook(mutate(pgn, r, dict), (int)r.below(12));
        // every opening a book hands out is playable: its FEN parses and its moves are legal
        for (const BookLoadResult* b : {&e, &p})
            for (const Opening& o : b->openings){
                bool ok = false;
                Position pos = Position::fromFEN(o.fen, &ok);
                CHECK_MSG(ok, "book opening with a bad FEN '" << o.fen << "'");
                for (size_t k = 0; ok && k < o.moves.size(); k++){
                    Move m;
                    ok = pos.uciToMove(o.moves[k], m);
                    CHECK_MSG(ok, "book move " << o.moves[k] << " is not legal after '" << o.fen << "'");
                    if (ok) pos.makeMove(m);
                }
            }
    }
}

TEST_CASE("fuzz/info-line"){
    const std::vector<std::string> seeds = {
        "info depth 12 seldepth 18 multipv 1 score cp 34 nodes 123456 nps 987654 hashfull 12 tbhits 0 time 125 pv e2e4 e7e5 g1f3",
        "info depth 20 multipv 3 score mate -4 lowerbound nodes 1 time 0 pv a7a8q b8a8",
        "info currmove e2e4 currmovenumber 1",
        "info string NNUE evaluation using nn-abc.nnue enabled",
        "info depth 5 score cp -12 upperbound pv",
    };
    const std::vector<std::string> dict = {"depth", "multipv", "score", "cp", "mate", "pv", "string", "nodes", "-1",
                                           "999999999999999999999", "256", "257", "0", "lowerbound", "info"};
    Rng r(0x1F0);
    EngineInfo info;
    std::vector<PvLine> pvs;
    for (int i = 0; i < kRounds; i++){
        if (i % 64 == 0){ info = EngineInfo(); pvs.clear(); }   // a new search now and then
        parseInfoLine(mutate(seeds[r.below(seeds.size())], r, dict), info, pvs);
        CHECK(pvs.size() <= 256);
        for (size_t k = 0; k < pvs.size(); k++)
            if (pvs[k].valid) CHECK_EQ(pvs[k].multipv, (int)k + 1);
    }
}

TEST_CASE("fuzz/divide-and-args"){
    const std::vector<std::string> lines = {"e2e4: 20", "a7a8q: 1", "g1f3 440", "b1c3 - 1,234", "e2e4: 600 nodes",
                                            "Move: d2d4 Count: 560", "Nodes searched: 8902", "Total: 197281",
                                            "Nodes: 400", "Total nodes: 1'000"};
    const std::vector<std::string> dict = {":", "-", "Count", "nodes", "Nodes searched:", "18446744073709551616",
                                           "99999999999999999999", ",", "'", "e1h1", "E2E4"};
    Rng r(0xD1D);
    for (int i = 0; i < kRounds; i++){
        std::string in = mutate(lines[r.below(lines.size())], r, dict);
        std::string move; uint64_t count = 0; long long total = 0;
        if (parseDivideLine(in, move, count)){
            CHECK_MSG(move.size() == 4 || move.size() == 5, "divide move '" << move << "' from '" << in << "'");
            CHECK_MSG(nameToSquare(move.substr(0, 2)) >= 0 && nameToSquare(move.substr(2, 2)) >= 0,
                      "divide move '" << move << "' from '" << in << "'");
        }
        if (parseTotalLine(in, total)) CHECK_MSG(total >= 0, "negative total " << total << " from '" << in << "'");
    }

    const std::vector<std::string> args = {"--threads 4", "-o 'a b' \"c \\\" d\"", "C:\\nets\\big.nnue", "'' \"\"",
                                           "a\\ b\\'c", "--uci  \t x"};
    const std::vector<std::string> adict = {"'", "\"", "\\", " ", "\t", "''", "\\'"};
    Rng ra(0xA465);
    for (int i = 0; i < kRounds; i++){
        std::string err;
        std::vector<std::string> v = splitArgs(mutate(args[ra.below(args.size())], ra, adict), &err);
        // joinArgs is the inverse of splitArgs for any list, including empty and quoted arguments
        std::vector<std::string> back = splitArgs(joinArgs(v), &err);
        CHECK_MSG(back == v, "splitArgs(joinArgs(x)) != x for '" << joinArgs(v) << "'");
        // and for arbitrary bytes in an argument
        std::vector<std::string> raw;
        for (size_t n = ra.below(4); n > 0; n--){
            std::string a;
            for (size_t k = ra.below(6); k > 0; k--) a += randomByte(ra);
            raw.push_back(a);
        }
        std::string err2;
        CHECK_MSG(splitArgs(joinArgs(raw), &err2) == raw && err2.empty(), "args round trip failed for '" << joinArgs(raw) << "'");
    }
}
