/* Chess-GUI-for-UCI — UCI engine process wrapper (Win/POSIX). GPL-2.0 */
#pragma once
#include "cpu_placement.hpp"
#include "engine_config.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <thread>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// A UCI option advertised by the engine ("option name ... type ...").
struct UciOption {
    std::string name;
    std::string type;        // check | spin | combo | string | button
    std::string defVal;
    long long minV = 0, maxV = 0;
    std::vector<std::string> vars;   // for combo
};

// One line of a MultiPV search (multipv k). pvs()[0] is the engine's best line.
struct PvLine {
    int multipv = 1, depth = 0, seldepth = 0, scoreCp = 0, mateIn = 0;
    bool isMate = false, valid = false;
    bool lowerbound = false, upperbound = false;   // score is only a bound (fail high/low)
    long long nodes = 0, nps = 0, timeMs = 0;
    std::string pv;                  // space separated UCI moves
    std::string firstMove() const {
        size_t sp = pv.find(' ');
        return sp==std::string::npos ? pv : pv.substr(0,sp);
    }
};

// Latest search state, merged from all "info" lines of the current search.
struct EngineInfo {
    int depth = 0, seldepth = 0;
    int scoreCp = 0;        // from engine's point of view (side to move)
    bool isMate = false;
    int mateIn = 0;
    bool lowerbound = false, upperbound = false;
    long long nodes = 0, nps = 0, timeMs = 0, tbhits = 0;
    bool hasScore = false;  // a "score" was reported in this search
    int hashfull = -1;      // per mille, -1 = never reported
    std::string pv;         // space separated UCI moves
    std::string currmove;
    int currmoveNumber = 0;
    std::string lastString; // last "info string ..."
    bool valid = false;
};

// One line of engine I/O, for the UCI console and for error reports.
struct UciLogLine {
    long long tMs = 0;      // milliseconds since the engine was started
    char dir = '<';         // '>' sent to engine, '<' stdout, '!' stderr, '#' GUI note
    std::string text;
};

// Bounded, thread-safe log of one engine process's I/O. Shared with the UCI
// console, which can keep showing it after the process is gone.
class UciLogBuffer {
public:
    UciLogBuffer();
    void add(char dir, const std::string& text);
    // `cursor` is an absolute line counter: pass 0 first, then the value
    // returned in it to get only the lines added since.
    std::vector<UciLogLine> since(uint64_t& cursor) const;
    void setCapacity(size_t lines);
    std::string stderrTail(size_t maxLines) const;
private:
    mutable std::mutex mx_;
    std::deque<UciLogLine> lines_;
    uint64_t base_ = 0;                          // absolute index of lines_.front()
    size_t cap_ = 20000;
    long long startMs_ = 0;
};

class UciEngine;

// Every engine process the program has started, for the UCI console: live
// ones (which can be sent raw commands) and the most recent ended ones.
struct MonitoredEngine {
    uint64_t id = 0;
    std::string label;
    std::shared_ptr<UciLogBuffer> log;
    bool alive = false;
    long long startedAt = 0;                     // wall-clock ms since the epoch
};
class EngineMonitor {
public:
    static EngineMonitor& instance();
    std::vector<MonitoredEngine> list();         // newest first
    bool send(uint64_t id, const std::string& line);   // false if the engine is gone
    // used by UciEngine
    uint64_t add(UciEngine* e, const std::string& label, std::shared_ptr<UciLogBuffer> log);
    void setLabel(uint64_t id, const std::string& label);
    void ended(uint64_t id);
private:
    struct Entry { MonitoredEngine info; UciEngine* engine = nullptr; };
    std::mutex mx_;
    std::deque<Entry> entries_;
    uint64_t next_ = 1;
};

// Parses one "info ..." line into `info` (fields not present keep their
// values) and, when it carries a PV with a score, into `pvs` (by multipv).
// Exposed for tests.
void parseInfoLine(const std::string& line, EngineInfo& info, std::vector<PvLine>& pvs);

// Blocking wrapper around one UCI engine process.
// Use from a worker thread; never call go*() from the render thread.
class UciEngine {
public:
    ~UciEngine();
    // Launch with arguments and working directory, run the "uci" handshake,
    // apply cfg.options, then wait for "readyok".
    bool start(const EngineConfig& cfg);
    bool start(const std::string& exePath);         // default config for an executable
    // Where the process runs (core pinning / efficiency cores / no power
    // throttling); applies from the next start(). See cpu_placement.hpp.
    void setPlacement(const CpuPlacement& p){ placement_ = p; }
    const CpuPlacement& placement() const { return placement_; }
    // False when the placement asked for could not be applied at the last start.
    bool placementApplied() const { return placementApplied_; }
    // Windows: whether the opt-out of power throttling was accepted (Windows 10 1709 and later)
    bool powerThrottlingOff() const { return powerThrottlingOff_; }
    // CPU time (user + system) the engine process has used, in seconds; -1 if unknown.
    double cpuSeconds();
    // Part of cpuSeconds() spent on performance cores (Apple silicon); -1 if unknown.
    double pCoreSeconds();
    // Time allowed for "uciok" and for the first "readyok" (defaults 10 s / 30 s).
    void setStartTimeouts(int uciokMs, int readyokMs){ uciokTimeout = uciokMs; readyTimeout = readyokMs; }
    void quit();
    // True while the process runs and its stdout is open.
    bool alive();
    // A flag that cancels whatever this engine is waiting for (a search, perft, isready, the
    // start handshake); every wait looks at it. A running search is sent "stop", and if its
    // bestmove does not follow within a second the engine is ended, as is one running perft,
    // so it can never be used out of step. The call returns its failure value ("", -1, false)
    // and no new search is started while the flag is set. Null (the default): none.
    void setCancel(const std::atomic<bool>* flag){ cancel_ = flag; }
    bool cancelled() const { return cancel_ && *cancel_; }

    void setOption(const std::string& name, const std::string& value);
    bool isReady(int timeoutMs = 10000);            // isready -> readyok
    void newGame();                                 // ucinewgame + isready
    void setPosition(const std::string& fen, const std::vector<std::string>& uciMoves);

    // returns bestmove (uci) or "" on failure/timeout; fills last info
    std::string goMovetime(int ms);
    // go movetime ms; "stop" if there is no bestmove after ms + graceMs (for
    // engines that overrun or ignore movetime), then wait up to stopWaitMs.
    std::string goMovetimeStrict(int ms, int graceMs, int stopWaitMs = 5000);
    // go depth / go nodes; "stop" if there is no bestmove after timeoutMs.
    std::string goDepth(int depth, int timeoutMs = 600000);
    std::string goNodes(long long nodes, int timeoutMs = 600000);
    // Wall time of the last search, from the moment "go" was written to the moment
    // the reader thread received "bestmove" (or until the wait gave up), and whether
    // it needed a "stop".
    long long lastSearchWallMs() const { return lastWallMs_; }
    bool lastSearchStopped() const { return lastStopped_; }
    // timeoutMs < 0: wtime + btime + 20 s. With a timeout or `cancel` the call can return
    // "" while the engine is still searching (searching() is then true): stopSearch() it.
    std::string goClock(int wtimeMs, int btimeMs, int wincMs, int bincMs, int timeoutMs = -1,
                        const std::atomic<bool>* cancel = nullptr);
    // True from a "go" until its "bestmove" has been read.
    bool searching() const { return searching_; }
    // Ends a search that is still running: "stop", then up to waitMs for the bestmove.
    // False if none came: the engine is out of step with us and must not be used again
    // (the next command would kill it, see ensureIdle). The bestmove read, if any, goes to
    // *bestmove ("(none)" as is), and lastSearchWallMs() is the time it arrived, which can
    // be before the "stop" if it was already waiting to be read.
    bool stopSearch(int waitMs = 1000, std::string* bestmove = nullptr);
    // infinite analysis: call goInfinite, poll lastInfo(), then stopAndWait()
    void goInfinite();
    std::string stopAndWait(int timeoutMs = 3000);
    // Pondering: send "go ponder <clocks>" and block until bestmove arrives
    // (which only happens after "stop" or after "ponderhit" completes).
    std::string goPonderWait(int wtimeMs, int btimeMs, int wincMs, int bincMs,
                             int timeoutMs = 7200000);

    // engine-side perft: sends "go perft d" (Stockfish-style). Returns node
    // count or -1 if the engine doesn't support it.
    long long goPerft(int depth, int timeoutMs = 120000);

    EngineInfo lastInfo();
    std::vector<PvLine> lastPvs();               // MultiPV lines (index 0 = best)
    std::string lastPonder();                    // ponder hint from last bestmove
    const std::vector<UciOption>& options() const { return opts; }
    bool hasOption(const std::string& name) const;
    std::string name() const { return engName; }
    std::string author() const { return engAuthor; }
    std::string lastError() const { return errMsg; }
    const EngineConfig& config() const { return cfg_; }

    void sendRaw(const std::string& line);
    // Returns the next stdout line, or "" after timeoutMs (or if the engine died).
    std::string readLine(int timeoutMs);
    // Non-blocking-ish: parse any pending info lines (for infinite analysis).
    void pump();

    // I/O log (bounded). `cursor` is an absolute line counter: pass 0 first,
    // then the value returned in it to get only lines added since.
    std::vector<UciLogLine> logSince(uint64_t& cursor);
    void setLogCapacity(size_t lines);
    void logNote(const std::string& text){ addLog('#', text); }
    std::string stderrTail(size_t maxLines = 10);
    std::shared_ptr<UciLogBuffer> logBuffer() const { return log_; }
    // Shown in the UCI console before the engine's name, e.g. "Opponent".
    void setRole(const std::string& role);
private:
    bool spawn();
    void markDead();
    std::string waitBestmove(int timeoutMs, const std::atomic<bool>* cancel = nullptr);
    void abandonSearch();                        // after a cancel: idle (stopped) or ended
    int pollMs(long long leftMs, int usualMs) const;
    std::string searchTimed(const std::string& goCmd, int timeoutMs, int stopWaitMs = 5000);
    void resetSearchInfo();
    // Before any command that needs an idle engine: if the last search never delivered
    // its bestmove, stop it; an engine that doesn't answer that either is killed, so its
    // late bestmove can never be taken for the answer to a later search.
    bool ensureIdle();
    void parseInfo(const std::string& line);
    void parseOptionLine(const std::string& line);
    void addLog(char dir, const std::string& text);
    void deliver(std::string& buf, bool isOut, bool all);   // complete lines -> log (+ queue for stdout)
#ifdef _WIN32
    void pipeLoop(bool isOut);                   // one blocking reader thread per pipe
#else
    void readerLoop();                           // reader thread: engine output -> log + queue
#endif

    EngineConfig cfg_;
    CpuPlacement placement_;
    bool placementApplied_ = true;
    bool powerThrottlingOff_ = true;
    long long lastWallMs_ = 0;
    bool lastStopped_ = false;
    std::vector<UciOption> opts;
    int chess960Mode_ = -1;                      // UCI_Chess960 as last sent (-1 = never)
    std::string engName = "engine", engAuthor, errMsg;
    std::atomic<bool> running{false};
    EngineInfo info;
    std::vector<PvLine> pvs_;
    std::string ponder_;
    std::mutex infoMx;
    // A reader thread reads stdout/stderr continuously, so output is logged as
    // it arrives (the console sees replies even when no search is waiting)
    // and the engine can never block on a full pipe.
    std::thread reader_;
#ifdef _WIN32
    std::thread errReader_;
#endif
    std::atomic<bool> stopReader_{false};
    std::mutex qMx_;
    std::condition_variable qCv_;
    struct QueuedLine { std::string text; long long atNs; };   // steady-clock time the reader thread got it
    std::deque<QueuedLine> queue_;               // stdout lines not yet read by readLine(), with their arrival time
    bool stdoutEof_ = false;
    std::atomic<bool> searching_{false};
    std::atomic<long long> goSentNs_{0};         // steady-clock time the last "go" was written
    int uciokTimeout = 10000, readyTimeout = 30000;
    const std::atomic<bool>* cancel_ = nullptr;

    std::shared_ptr<UciLogBuffer> log_ = std::make_shared<UciLogBuffer>();
    size_t logCap_ = 20000;
    uint64_t monitorId_ = 0;
    std::string role_;
    std::mutex writeMx_;                         // sendRaw may be called from the console too
    void updateMonitorLabel();
#ifdef _WIN32
    void* hChildIn = nullptr; void* hChildOut = nullptr; void* hChildErr = nullptr; void* hProc = nullptr;
#else
    int inFd = -1, outFd = -1, errFd = -1; long long pid = -1;
#endif
};
