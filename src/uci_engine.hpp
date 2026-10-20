/* Chess-GUI-for-UCI — UCI engine process wrapper (Win/POSIX). GPL-2.0 */
#pragma once
#include "engine_config.hpp"
#include <atomic>
#include <cstdint>
#include <deque>
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
    // Time allowed for "uciok" and for the first "readyok" (defaults 10 s / 30 s).
    void setStartTimeouts(int uciokMs, int readyokMs){ uciokTimeout = uciokMs; readyTimeout = readyokMs; }
    void quit();
    // True while the process runs and its stdout is open.
    bool alive();

    void setOption(const std::string& name, const std::string& value);
    bool isReady(int timeoutMs = 10000);            // isready -> readyok
    void newGame();                                 // ucinewgame + isready
    void setPosition(const std::string& fen, const std::vector<std::string>& uciMoves);

    // returns bestmove (uci) or "" on failure/timeout; fills last info
    std::string goMovetime(int ms);
    std::string goDepth(int depth);
    std::string goNodes(long long nodes, int timeoutMs = 600000);
    std::string goClock(int wtimeMs, int btimeMs, int wincMs, int bincMs);
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
private:
    bool spawn();
    void markDead();
    std::string waitBestmove(int timeoutMs);
    void resetSearchInfo();
    void parseInfo(const std::string& line);
    void parseOptionLine(const std::string& line);
    void addLog(char dir, const std::string& text);
    bool fillBuffers(int timeoutMs);             // read whatever is available

    EngineConfig cfg_;
    std::vector<UciOption> opts;
    std::string engName = "engine", engAuthor, errMsg;
    std::atomic<bool> running{false};
    EngineInfo info;
    std::vector<PvLine> pvs_;
    std::string ponder_;
    std::mutex infoMx;
    std::string rbuf, ebuf;                      // partial stdout / stderr data
    long long startMs = 0;
    int uciokTimeout = 10000, readyTimeout = 30000;

    std::mutex logMx;
    std::deque<UciLogLine> log_;
    uint64_t logBase = 0;                        // absolute index of log_.front()
    size_t logCap = 20000;
#ifdef _WIN32
    void* hChildIn = nullptr; void* hChildOut = nullptr; void* hChildErr = nullptr; void* hProc = nullptr;
#else
    int inFd = -1, outFd = -1, errFd = -1; long long pid = -1;
#endif
};
