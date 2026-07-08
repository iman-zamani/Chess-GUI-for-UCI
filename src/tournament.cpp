/* Chess-GUI-for-UCI — engine match & tournament orchestration. GPL-2.0 */
#include "tournament.hpp"
#include <chrono>
#include <sstream>

using clk = std::chrono::steady_clock;

std::string TimeControl::label() const{
    std::ostringstream o;
    o << baseMs/60000 << "+" << incMs/1000;
    return o.str();
}

void MatchRunner::post(std::function<void(Snapshot&)> f){
    std::lock_guard<std::mutex> lk(mx_);
    f(snap_);
}
void MatchRunner::logMsg(const std::string& s){
    post([&](Snapshot& sn){
        sn.log.push_back(s);
        if (sn.log.size() > 12) sn.log.erase(sn.log.begin());
        sn.status = s;
    });
}
MatchRunner::Snapshot MatchRunner::snapshot(){
    std::lock_guard<std::mutex> lk(mx_);
    return snap_;
}
std::vector<std::string> MatchRunner::collectedPGNs(){
    std::lock_guard<std::mutex> lk(mx_);
    return pgns_;
}
void MatchRunner::abort(){
    abort_ = true;
    if (thread_.joinable()) thread_.join();
    abort_ = false;
}

void MatchRunner::playOneGame(UciEngine& we, UciEngine& be, TimeControl tc, Game& g,
                              const Position& startPos){
    g.reset(startPos);
    g.whiteName = we.name(); g.blackName = be.name();
    we.newGame(); be.newGame();
    long long wClock = tc.baseMs, bClock = tc.baseMs;
    std::vector<std::string> uciMoves;
    std::string startFen = g.start.toFEN();
    post([&](Snapshot& s){
        s.pos = g.pos; s.sans.clear();
        s.whiteName=g.whiteName; s.blackName=g.blackName;
        s.wClockMs=wClock; s.bClockMs=bClock;
        s.result=GameResult::ONGOING; s.reason=ResultReason::NONE;
        s.scoreCpWhitePOV=0; s.scoreIsMate=false;
    });
    while (g.result == GameResult::ONGOING && !abort_){
        bool whiteMoves = g.pos.whiteToMove;
        UciEngine& e = whiteMoves ? we : be;
        e.setPosition(startFen, uciMoves);
        auto t0 = clk::now();
        std::string bm = e.goClock((int)wClock,(int)bClock,tc.incMs,tc.incMs);
        long long spent = std::chrono::duration_cast<std::chrono::milliseconds>(clk::now()-t0).count();
        long long& myClock = whiteMoves ? wClock : bClock;
        myClock -= spent;
        if (myClock <= 0){
            g.result = whiteMoves? GameResult::BLACK_WINS : GameResult::WHITE_WINS;
            g.reason = ResultReason::TIMEOUT;
            break;
        }
        myClock += tc.incMs;
        Move m;
        if (bm.empty() || !g.pos.uciToMove(bm, m)){
            g.result = whiteMoves? GameResult::BLACK_WINS : GameResult::WHITE_WINS;
            g.reason = ResultReason::ILLEGAL_MOVE;
            logMsg((whiteMoves?g.whiteName:g.blackName)+" played illegal/no move: '"+bm+"'");
            break;
        }
        EngineInfo inf = e.lastInfo();
        g.tryMove(m);
        uciMoves.push_back(bm);
        post([&](Snapshot& s){
            s.pos = g.pos; s.sans = g.sans;
            s.wClockMs=wClock; s.bClockMs=bClock;
            int cp = inf.isMate ? (inf.mateIn>0?10000:-10000) : inf.scoreCp;
            s.scoreCpWhitePOV = whiteMoves ? cp : -cp;   // engine reports for mover
            s.scoreIsMate = inf.isMate; s.mateIn = inf.mateIn;
            s.result = g.result; s.reason = g.reason;
        });
        if (g.moves.size() > 500){ g.result=GameResult::DRAW; g.reason=ResultReason::AGREEMENT; }
    }
    post([&](Snapshot& s){ s.result=g.result; s.reason=g.reason; });
}

void MatchRunner::startMatch(EngineEntry a, EngineEntry b, TimeControl tc, int games,
                             const std::string& startFen){
    startTournament({a,b}, tc, games, startFen);
}

void MatchRunner::startTournament(std::vector<EngineEntry> engines, TimeControl tc, int gamesPerPair,
                                  const std::string& startFen){
    abort();
    done_ = false;
    {
        std::lock_guard<std::mutex> lk(mx_);
        snap_ = Snapshot{};
        pgns_.clear();
    }
    thread_ = std::thread([this, engines, tc, gamesPerPair, startFen](){
        Position startPos = Position::startpos();
        if (!startFen.empty()){
            bool ok=false;
            Position p = Position::fromFEN(startFen, &ok);
            if (ok) startPos = p;
            else logMsg("Invalid start FEN, using standard position.");
        }
        // launch all engines
        std::vector<std::unique_ptr<UciEngine>> eng;
        std::vector<Standing> table;
        for (auto& e : engines){
            auto u = std::make_unique<UciEngine>();
            if (!u->start(e.path)){
                logMsg("Failed to start engine: " + e.path + " (" + u->lastError() + ")");
                done_ = true; 
                post([](Snapshot& s){ s.finished = true; });
                return;
            }
            for (auto& opt : e.options) u->setOption(opt.first, opt.second);
            table.push_back({u->name(),0,0,0});
            eng.push_back(std::move(u));
        }
        post([&](Snapshot& s){ s.standings = table; });
        // build pairings
        struct Pair{int a,b;};
        std::vector<Pair> pairs;
        for (size_t i=0;i<eng.size();i++)
            for (size_t j=i+1;j<eng.size();j++)
                for (int g=0; g<gamesPerPair; g++)
                    pairs.push_back(g%2==0 ? Pair{(int)i,(int)j} : Pair{(int)j,(int)i});
        int total = (int)pairs.size();
        post([&](Snapshot& s){ s.totalGames = total; });
        for (int gi=0; gi<total && !abort_; gi++){
            post([&](Snapshot& s){ s.gameIndex = gi+1; });
            logMsg("Game " + std::to_string(gi+1) + "/" + std::to_string(total) + ": "
                   + eng[pairs[gi].a]->name() + " vs " + eng[pairs[gi].b]->name()
                   + " (" + tc.label() + ")");
            Game g;
            playOneGame(*eng[pairs[gi].a], *eng[pairs[gi].b], tc, g, startPos);
            if (abort_) break;
            // record
            {
                std::lock_guard<std::mutex> lk(mx_);
                pgns_.push_back(g.toPGN());
            }
            int wi = pairs[gi].a, bi = pairs[gi].b;
            if (g.result==GameResult::WHITE_WINS){ table[wi].wins++; table[bi].losses++; }
            else if (g.result==GameResult::BLACK_WINS){ table[bi].wins++; table[wi].losses++; }
            else { table[wi].draws++; table[bi].draws++; }
            post([&](Snapshot& s){ s.standings = table; });
            logMsg("Result: " + g.resultString());
        }
        for (auto& e : eng) e->quit();
        post([](Snapshot& s){ s.finished = true; s.status = "Finished."; });
        done_ = true;
    });
}
