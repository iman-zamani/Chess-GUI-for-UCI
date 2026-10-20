/* Chess-GUI-for-UCI — engine match & tournament orchestration. GPL-2.0 */
#pragma once
#include "chess.hpp"
#include "uci_engine.hpp"
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>

struct TimeControl {
    int baseMs = 300000;    // 5+0 default
    int incMs = 0;
    std::string label() const;
};

// What the engine reported for one of its moves (for search statistics).
struct MoveStat {
    int ply = 0;                 // 0-based ply in the game
    bool white = true;
    int depth = 0, seldepth = 0, hashfull = -1;
    long long nodes = 0, nps = 0, timeMs = 0;   // timeMs: wall-clock time the move took
    int scoreCp = 0, mate = 0;   // mover's point of view
    bool isMate = false;
    static MoveStat from(const EngineInfo& i, int ply, bool white, long long timeMs){
        MoveStat s; s.ply = ply; s.white = white; s.depth = i.depth; s.seldepth = i.seldepth;
        s.hashfull = i.hashfull; s.nodes = i.nodes; s.nps = i.nps; s.timeMs = timeMs;
        s.scoreCp = i.scoreCp; s.mate = i.mateIn; s.isMate = i.isMate;
        return s;
    }
};

struct Standing {
    std::string name;
    int wins=0, losses=0, draws=0;
    double points() const { return wins + 0.5*draws; }
};

// Runs on its own thread. GUI polls snapshot() each frame.
class MatchRunner {
public:
    struct Snapshot {
        Position pos = Position::startpos();
        std::vector<std::string> sans;
        std::string whiteName, blackName;
        long long wClockMs=0, bClockMs=0;
        GameResult result = GameResult::ONGOING;
        ResultReason reason = ResultReason::NONE;
        int scoreCpWhitePOV = 0; bool scoreIsMate=false; int mateIn=0;
        int lastFrom=-1, lastTo=-1;         // last move squares for highlighting
        long long postedAtMs=0;             // steady-clock ms when clocks were posted
                                            // (GUI subtracts elapsed time for live display)
        std::string status;                 // human readable
        int gameIndex=0, totalGames=0;
        std::vector<Standing> standings;    // tournaments only
        std::vector<MoveStat> stats;        // per move of the current game
        bool finished=false;
        std::vector<std::string> log;       // recent messages
    };

    ~MatchRunner(){ abort(); }
    // Single match: engineA vs engineB, `games` games, colors alternate.
    void startMatch(EngineConfig a, EngineConfig b, TimeControl tc, int games,
                    const std::vector<std::string>& startFens = {});
    // Round-robin tournament. Empty startFens => standard start position.
    // With multiple openings, each opening is played twice per pairing
    // (colors alternate), cycling through the list.
    void startTournament(std::vector<EngineConfig> engines, TimeControl tc, int gamesPerPair,
                         const std::vector<std::string>& startFens = {});
    void abort();
    bool running() const { return thread_.joinable() && !done_; }
    Snapshot snapshot();
    std::vector<std::string> collectedPGNs();

private:
    void playOneGame(UciEngine& w, UciEngine& b, TimeControl tc, Game& g,
                     const Position& startPos);
    void post(std::function<void(Snapshot&)> f);
    void logMsg(const std::string& s);

    Snapshot snap_;
    std::mutex mx_;
    std::thread thread_;
    std::atomic<bool> abort_{false};
    std::atomic<bool> done_{true};
    std::vector<std::string> pgns_;
};
