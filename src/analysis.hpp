/* Chess-GUI-for-UCI — background infinite analysis. GPL-2.0 */
#pragma once
#include "uci_engine.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Runs an engine in infinite-analysis mode on its own thread; the caller
// (e.g. the render thread) never blocks on engine I/O: setFen/info/pvs/alive
// are all cheap.
class AnalysisController {
public:
    ~AnalysisController(){ shutdown(); }
    bool start(const EngineConfig& cfg);
    void shutdown();
    void setFen(const std::string& fen);
    std::string fen();                   // position currently being analysed
    EngineInfo info();
    std::vector<PvLine> pvs();
    bool alive();
    std::string engineName();
    std::string lastError() const { return err; }
    UciEngine* engine(){ return eng.get(); }
private:
    void loop();
    std::unique_ptr<UciEngine> eng;
    std::thread th;
    std::atomic<bool> stop_{false};
    std::mutex mx;
    std::string target, current;
    std::string err;
};
