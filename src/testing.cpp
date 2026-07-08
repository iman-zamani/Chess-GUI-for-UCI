/* Chess-GUI-for-UCI — engine developer tools. GPL-2.0 */
#include "testing.hpp"
#include <fstream>
#include <sstream>
#include <cmath>

std::vector<PerftCase> builtinPerftSuite(){
    // Standard, community-verified perft positions.
    return {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            {{1,20},{2,400},{3,8902},{4,197281},{5,4865609}}},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", // Kiwipete
            {{1,48},{2,2039},{3,97862},{4,4085603}}},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
            {{1,14},{2,191},{3,2812},{4,43238},{5,674624}}},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
            {{1,6},{2,264},{3,9467},{4,422333}}},
        {"r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1",   // pos4 mirrored
            {{1,6},{2,264},{3,9467},{4,422333}}},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
            {{1,44},{2,1486},{3,62379},{4,2103487}}},
        {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
            {{1,46},{2,2079},{3,89890},{4,3894594}}},
        // en-passant pin / discovered check corner cases
        {"8/8/8/8/k2Pp2Q/8/8/3K4 b - d3 0 1", {{1,6},{2,136},{3,863},{4,20471}}},
        {"8/8/3p4/1Pp4r/1K3p1k/8/4P1P1/1R6 w - c6 0 3", {{1,7},{2,92},{3,1709},{4,25240}}},
        {"n1n5/PPPk4/8/8/8/8/4Kppp/5N1N b - - 0 1", {{1,24},{2,496},{3,9483},{4,182838}}},
        {"8/PPPk4/8/8/8/8/4Kppp/8 b - - 0 1", {{1,18},{2,270},{3,4699},{4,79355}}},
        {"K1k5/8/P7/8/8/8/8/8 w - - 0 1", {{1,2},{2,6},{3,13},{4,63},{5,382},{6,2217}}},
        {"8/k1P5/8/1K6/8/8/8/8 w - - 0 1", {{1,10},{2,25},{3,268},{4,926},{5,10857},{6,43261}}},
        {"8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1", {{1,37},{2,183},{3,6559},{4,23527}}},
    };
}

std::vector<PerftCase> loadEpdSuite(const std::string& path){
    std::vector<PerftCase> out;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)){
        if (line.empty() || line[0]=='#') continue;
        size_t semi = line.find(';');
        if (semi == std::string::npos) continue;
        PerftCase c;
        c.fen = line.substr(0, semi);
        while (!c.fen.empty() && c.fen.back()==' ') c.fen.pop_back();
        std::string rest = line.substr(semi);
        std::istringstream ss(rest);
        std::string tok;
        while (std::getline(ss, tok, ';')){
            std::istringstream ts(tok);
            std::string dk; unsigned long long n;
            if (ts >> dk >> n && dk.size()>=2 && (dk[0]=='D'||dk[0]=='d'))
                c.depths.push_back({std::atoi(dk.c_str()+1), n});
        }
        bool ok=false; Position::fromFEN(c.fen, &ok);
        if (ok && !c.depths.empty()) out.push_back(c);
    }
    return out;
}

std::vector<PerftResult> runEnginePerftSuite(
    UciEngine& eng, const std::vector<PerftCase>& suite, int maxDepth,
    std::function<void(int,int,const PerftResult&)> progress,
    std::atomic<bool>& abortFlag)
{
    std::vector<PerftResult> out;
    int total = 0;
    for (auto& c : suite) for (auto& d : c.depths) if (d.first <= maxDepth) total++;
    int i = 0;
    for (auto& c : suite){
        for (auto& d : c.depths){
            if (d.first > maxDepth) continue;
            if (abortFlag) return out;
            eng.newGame();
            eng.setPosition(c.fen, {});
            PerftResult r;
            r.fen = c.fen; r.depth = d.first; r.expected = d.second;
            r.engineNodes = eng.goPerft(d.first);
            r.pass = (r.engineNodes >= 0 && (uint64_t)r.engineNodes == r.expected);
            out.push_back(r);
            i++;
            if (progress) progress(i, total, r);
            if (r.engineNodes < 0) return out;   // engine doesn't support perft
        }
    }
    return out;
}

bool runInternalPerftSelfTest(const std::vector<PerftCase>& suite, int maxDepth,
                              std::function<void(int,int,bool)> progress,
                              std::atomic<bool>& abortFlag){
    int total=0, i=0; bool all=true;
    for (auto& c : suite) for (auto& d : c.depths) if (d.first<=maxDepth) total++;
    for (auto& c : suite){
        bool ok=false;
        Position p = Position::fromFEN(c.fen, &ok);
        if (!ok) continue;
        for (auto& d : c.depths){
            if (d.first > maxDepth) continue;
            if (abortFlag) return all;
            bool pass = (p.perft(d.first) == d.second);
            all = all && pass;
            i++;
            if (progress) progress(i, total, pass);
        }
    }
    return all;
}

std::vector<std::string> defaultAccuracyPositions(){
    return {
        "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3",
        "rnbqkb1r/ppp1pppp/5n2/3p4/3P1B2/8/PPP1PPPP/RN1QKBNR w KQkq - 2 3",
        "r1bq1rk1/ppp2ppp/2np1n2/2b1p3/2B1P3/2PP1N2/PP3PPP/RNBQ1RK1 w - - 1 7",
        "r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 10",
        "r1bqk2r/2ppbppp/p1n2n2/1p2p3/4P3/1B3N2/PPPP1PPP/RNBQR1K1 w kq - 0 7",
        "2rq1rk1/pb2bppp/1pn1pn2/2pp4/3P4/1P2PNP1/PBPN1PBP/R2Q1RK1 w - - 0 11",
        "r4rk1/pp2ppbp/2n3p1/q7/3P4/2N1BN2/PP2QPPP/2R2RK1 w - - 4 14",
        "8/5pk1/6p1/8/3Q4/6PK/5P2/2q5 w - - 0 40",
        "8/8/4kpp1/3p1b2/p6P/2B5/6P1/6K1 b - - 2 47",
        "4rrk1/1pp1qppp/p1np1n2/4p3/2B1P3/2NP1N2/PPP1QPPP/2KR3R w - - 0 12",
        "r3kb1r/pp3ppp/2n1bn2/2p1p3/4P3/2NP1N2/PPP2PPP/R1B1KB1R w KQkq - 0 8",
        "1k1r3r/ppq2ppp/2pb1n2/8/3P4/2N2B2/PPP2PPP/R1BQR1K1 w - - 4 13",
        "8/pp3ppk/2p4p/8/2P1r3/1P5P/P4PP1/3R2K1 b - - 1 28",
        "6k1/5pp1/7p/8/1P6/P3q3/5RPP/5QK1 b - - 2 33",
        "r1b2rk1/2q1bppp/p2ppn2/1p6/3NPP2/2N1B3/PPP1Q1PP/2KR3R w - - 0 12",
    };
}

static double acplToAccuracy(double acpl){
    // Lichess-style logistic mapping
    double a = 103.1668 * std::exp(-0.04354 * acpl) - 3.1669;
    if (a > 100) a = 100; if (a < 0) a = 0;
    return a;
}

AccuracyReport runAccuracyTest(
    UciEngine& test, UciEngine& ref,
    const std::vector<std::string>& fens,
    int testMs, int refMs,
    std::function<void(int,int,const AccuracyMoveReport&)> progress,
    std::atomic<bool>& abortFlag)
{
    AccuracyReport rep;
    long long totalLoss = 0; int counted = 0;
    int total = (int)fens.size();
    auto evalCpForMover = [&](const Position& p)->int{
        ref.setPosition(p.toFEN(), {});
        ref.goMovetime(refMs);
        EngineInfo i = ref.lastInfo();
        if (i.isMate) return i.mateIn > 0 ? 10000 : -10000;
        return i.scoreCp;
    };
    for (int idx=0; idx<total; idx++){
        if (abortFlag) break;
        bool ok=false;
        Position p = Position::fromFEN(fens[idx], &ok);
        if (!ok) continue;
        // reference: best score for side to move + best move
        ref.newGame();
        ref.setPosition(p.toFEN(), {});
        std::string refBest = ref.goMovetime(refMs);
        EngineInfo refInfo = ref.lastInfo();
        int bestScore = refInfo.isMate ? (refInfo.mateIn>0?10000:-10000) : refInfo.scoreCp;
        // test engine plays
        test.newGame();
        test.setPosition(p.toFEN(), {});
        std::string played = test.goMovetime(testMs);
        AccuracyMoveReport m;
        m.refBest = refBest; m.played = played;
        Move mv;
        if (played.empty() || !p.uciToMove(played, mv)){
            m.san = "(illegal: " + played + ")";
            m.cpLoss = 1000;
        } else {
            m.san = p.moveToSAN(mv);
            if (played == refBest) m.cpLoss = 0;
            else {
                Position after = p; after.makeMove(mv);
                // eval after our move is from opponent's POV -> negate
                int ourScoreAfter = -evalCpForMover(after);
                m.cpLoss = bestScore - ourScoreAfter;
                if (m.cpLoss < 0) m.cpLoss = 0;
                if (m.cpLoss > 1000) m.cpLoss = 1000;
            }
        }
        if (m.cpLoss >= 300) rep.blunders++;
        else if (m.cpLoss >= 100) rep.mistakes++;
        else if (m.cpLoss >= 50) rep.inaccuracies++;
        totalLoss += m.cpLoss; counted++;
        rep.moves.push_back(m);
        if (progress) progress(idx+1, total, m);
    }
    if (counted){
        rep.avgCpLoss = (double)totalLoss / counted;
        rep.accuracyPct = acplToAccuracy(rep.avgCpLoss);
    }
    return rep;
}
