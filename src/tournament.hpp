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

struct EngineEntry {
    std::string path;
    std::string name;       // filled after first launch
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
        std::string status;                 // human readable
        int gameIndex=0, totalGames=0;
        std::vector<Standing> standings;    // tournaments only
        bool finished=false;
        std::vector<std::string> log;       // recent messages
    };

    ~MatchRunner(){ abort(); }
    // Single match: engineA vs engineB, `games` games, colors alternate.
    void startMatch(EngineEntry a, EngineEntry b, TimeControl tc, int games);
    // Round-robin tournament.
    void startTournament(std::vector<EngineEntry> engines, TimeControl tc, int gamesPerPair);
    void abort();
    bool running() const { return thread_.joinable() && !done_; }
    Snapshot snapshot();
    std::vector<std::string> collectedPGNs();

private:
    void playOneGame(UciEngine& w, UciEngine& b, TimeControl tc, Game& g);
    void post(std::function<void(Snapshot&)> f);
    void logMsg(const std::string& s);

    Snapshot snap_;
    std::mutex mx_;
    std::thread thread_;
    std::atomic<bool> abort_{false};
    std::atomic<bool> done_{true};
    std::vector<std::string> pgns_;
};
