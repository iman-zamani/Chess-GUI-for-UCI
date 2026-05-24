#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

class UciEngine {
public:
    UciEngine();
    ~UciEngine();

    bool startEngine(const std::string& path);
    void stopEngine();
    void sendCommand(const std::string& cmd);
    
    // Non-blocking: returns the "bestmove e2e4" string if ready, otherwise empty
    std::string getBestMove(); 
    bool isEngineRunning() const;

private:
    void readOutput();

    std::thread readerThread;
    std::atomic<bool> isRunning;
    std::mutex moveMutex;
    std::string latestBestMove;

#ifdef _WIN32
    void* hChildStd_IN_Rd;
    void* hChildStd_IN_Wr;
    void* hChildStd_OUT_Rd;
    void* hChildStd_OUT_Wr;
    void* hProcess;
#else
    int pipeIn[2];
    int pipeOut[2];
    int childPid;
#endif
};