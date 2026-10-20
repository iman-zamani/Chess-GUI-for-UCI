/* Chess-GUI-for-UCI — engine matches, tournaments and SPRT runs. GPL-2.0 */
#pragma once
#include "chess.hpp"
#include "openings.hpp"
#include "stats.hpp"
#include "uci_engine.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct TimeControl {
    int baseMs = 300000;     // 5+0 default
    int incMs = 0;
    std::string label() const;      // "5+0", "3+2", "10s+0.1"
    std::string pgnTag() const;     // PGN TimeControl tag: "300", "10+0.1"
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

// When to end a game early.
struct AdjudicationConfig {
    bool resign = false;
    int resignCp = 600;          // both engines agree one side is at least this far ahead...
    int resignMoves = 3;         // ...for this many consecutive moves each
    bool draw = false;
    int drawAfterMove = 40;      // from this full move on...
    int drawCp = 10;             // ...both engines report |score| <= drawCp...
    int drawMoves = 8;           // ...for this many consecutive moves each
    int maxMoves = 0;            // draw after this many full moves (0 = no limit)
};

enum class TournamentMode { Match, RoundRobin, Gauntlet };
const char* tournamentModeName(TournamentMode m);

struct MatchSettings {
    std::vector<EngineConfig> engines;
    TournamentMode mode = TournamentMode::Match;   // Match: the first two engines
    int gauntletSeeds = 1;       // Gauntlet: the first N engines each play all the others
    TimeControl tc{10000, 100};
    int timeMarginMs = 100;      // a move may overrun the clock by this much before it's a loss
    int gamesPerPairing = 2;     // rounded up to an even number: games are played in colour-swapped pairs
    int concurrency = 1;         // games in parallel
    std::string bookPath;        // "" = startFens, or the standard start position
    BookFormat bookFormat = BookFormat::Auto;
    int bookPlies = 8;           // PGN books: moves used from each game
    bool bookRandom = false;
    uint64_t seed = 1;
    std::vector<std::string> startFens;            // used when bookPath is empty
    AdjudicationConfig adj;
    bool sprtEnabled = false;    // Match mode only: stop when H0 or H1 is accepted
    SprtConfig sprt;
    std::string pgnPath;         // games appended as they finish ("" = none)
    std::string statePath;       // resumable state ("" = none)
    std::string event = "Chess-GUI-for-UCI";
    // Fingerprint of what defines the run (engines, time control, book, seed...):
    // a state file is only resumed when it matches.
    std::string fingerprint() const;
};

// One finished game.
struct GameRecord {
    int index = 0;               // game number (0-based); pair = index / 2
    int white = 0, black = 0;    // engine indices
    int opening = 0;             // book index (-1 = standard start)
    GameResult result = GameResult::ONGOING;
    ResultReason reason = ResultReason::NONE;
    int plies = 0;
};

// Standing of one engine over all its games.
struct Standing {
    std::string name;
    int wins = 0, losses = 0, draws = 0;
    double points() const { return wins + 0.5 * draws; }
    int games() const { return wins + losses + draws; }
};

// Head-to-head statistics of engine `a` against engine `b` (a's point of view).
struct PairingStats {
    int a = 0, b = 0;
    WdlCounts wdl;
    PentaCounts penta;
    int timeLosses = 0, crashes = 0;   // games lost on time / by a disconnect (either engine)
};

// The game a worker is playing right now.
struct LiveGame {
    bool active = false;
    int index = -1;
    std::string whiteName, blackName;
    Position pos = Position::startpos();
    std::vector<std::string> sans;
    int lastFrom = -1, lastTo = -1;
    long long wClockMs = 0, bClockMs = 0;
    long long postedAtMs = 0;    // steady-clock ms when the clocks were posted (GUI ticks the mover's)
    int scoreCpWhite = 0; bool scoreIsMate = false; int mateWhite = 0;
    std::vector<MoveStat> stats;
};

class MatchRunner {
public:
    struct Snapshot {
        std::vector<LiveGame> live;            // one entry per worker
        std::vector<Standing> standings;       // per engine (same order as settings.engines)
        std::vector<PairingStats> pairings;
        int gamesDone = 0, gamesTotal = 0, gamesResumed = 0;
        bool running = false, finished = false;
        bool sprt = false; double llr = 0, llrLower = 0, llrUpper = 0;
        SprtOutcome sprtOutcome = SprtOutcome::Continue;
        std::vector<GameRecord> recent;        // last finished games, newest last
        std::vector<std::string> log;
        std::string status;
        // legacy single-game fields (first worker), kept for simple views
        Position pos = Position::startpos();
        std::string whiteName, blackName;
        long long wClockMs = 0, bClockMs = 0, postedAtMs = 0;
        int lastFrom = -1, lastTo = -1;
        GameResult result = GameResult::ONGOING;
        ResultReason reason = ResultReason::NONE;
        int scoreCpWhitePOV = 0; bool scoreIsMate = false; int mateIn = 0;
        int gameIndex = 0, totalGames = 0;
        std::vector<MoveStat> stats;
    };

    ~MatchRunner(){ abort(); }
    // Validates the settings, loads the book, resumes from statePath if it
    // matches, and starts the workers. Returns false with *err on bad settings.
    bool start(const MatchSettings& s, std::string* err = nullptr);
    void abort();                              // stop now; running games are not counted
    bool running() const { return running_; }
    Snapshot snapshot();
    std::vector<std::string> collectedPGNs();
    std::string resultsJson();                 // settings, statistics and every game result

    // Kept for existing callers: a round-robin with explicit start positions.
    void startTournament(std::vector<EngineConfig> engines, TimeControl tc, int gamesPerPair,
                         const std::vector<std::string>& startFens = {});

    // Exposed for tests: the pairings (engine index pairs) a mode produces.
    static std::vector<std::pair<int, int>> pairingsFor(TournamentMode m, int engines, int seeds);
    // Adjudication decision after a move, from White-POV scores of the last plies.
    static bool adjudicate(const AdjudicationConfig& a, const std::vector<int>& whiteScores,
                           int fullMove, GameResult& result);

private:
    struct Job { int index; int white, black; int opening; int pairing; };
    void workerLoop(int w);
    void playGame(int w, const Job& job, std::vector<std::unique_ptr<UciEngine>>& engines);
    void recordGame(const Job& job, const Game& g, const std::string& pgn, bool timeLoss, bool crash);
    void log(const std::string& s);
    bool nextJob(Job& j);
    void loadState();
    void saveState();

    MatchSettings s_;
    std::vector<Opening> book_;
    std::unique_ptr<OpeningSchedule> schedule_;
    std::vector<std::pair<int, int>> pairings_;
    std::vector<Job> jobs_;
    std::vector<bool> done_;                  // per game index
    size_t nextJob_ = 0;
    std::vector<std::thread> threads_;
    std::atomic<bool> stop_{false}, running_{false};
    std::atomic<int> activeWorkers_{0};
    std::mutex mx_;                           // guards everything below and the snapshot
    Snapshot snap_;
    std::vector<std::string> pgns_;
    std::vector<GameRecord> allGames_;
    std::vector<std::vector<int>> pairResults_;  // per pair: results of its games (for pentanomial)
    std::vector<std::string> engineNames_;
};

// Writes a finished game as PGN with the match tags and per-move comments
// ([%eval], [%clk], depth). Exposed for tests.
std::string matchGamePgn(const Game& g, const MatchSettings& s, int round, int gameInRound,
                         const std::vector<std::string>& moveComments, const std::string& termination,
                         int bookPlies);
