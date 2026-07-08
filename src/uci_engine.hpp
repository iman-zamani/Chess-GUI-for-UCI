/* Chess-GUI-for-UCI — UCI engine process wrapper (Win/POSIX). GPL-2.0 */
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

struct EngineInfo {
    int depth = 0;
    int scoreCp = 0;        // from engine's point of view (side to move)
    bool isMate = false;
    int mateIn = 0;
    std::string pv;         // space separated UCI moves
    bool valid = false;
};

// Blocking wrapper around one UCI engine process.
// Use from a worker thread; never call go*() from the render thread.
class UciEngine {
public:
    ~UciEngine();
    bool start(const std::string& exePath);        // launch + "uci" handshake
    void quit();
    bool alive() const { return running; }

    void setOption(const std::string& name, const std::string& value);
    void newGame();                                 // ucinewgame + isready
    void setPosition(const std::string& fen, const std::vector<std::string>& uciMoves);

    // returns bestmove (uci) or "" on failure/timeout; fills last info
    std::string goMovetime(int ms);
    std::string goDepth(int depth);
    std::string goClock(int wtimeMs, int btimeMs, int wincMs, int bincMs);
    // infinite analysis: call goInfinite, poll lastInfo(), then stopAndWait()
    void goInfinite();
    std::string stopAndWait(int timeoutMs = 3000);

    // engine-side perft: sends "go perft d" (Stockfish-style). Returns node
    // count or -1 if the engine doesn't support it.
    long long goPerft(int depth, int timeoutMs = 120000);

    EngineInfo lastInfo();
    std::string name() const { return engName; }
    std::string lastError() const { return errMsg; }

    void sendRaw(const std::string& line);
    // Non-blocking-ish: parse any pending info lines (for infinite analysis).
    void pump();
private:
    std::string readLine(int timeoutMs);            // "" on timeout
    std::string waitBestmove(int timeoutMs);
    void parseInfo(const std::string& line);

    std::string engName = "engine", errMsg;
    std::atomic<bool> running{false};
    EngineInfo info;
    std::mutex infoMx;
    std::string rbuf;
#ifdef _WIN32
    void* hChildIn = nullptr; void* hChildOut = nullptr; void* hProc = nullptr;
#else
    int inFd = -1, outFd = -1; long long pid = -1;
#endif
};
