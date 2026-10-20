/* Chess-GUI-for-UCI — background infinite analysis. GPL-2.0 */
#include "analysis.hpp"
#include <chrono>

bool AnalysisController::start(const EngineConfig& cfg){
    eng = std::make_unique<UciEngine>();
    if (!eng->start(cfg)){ err = eng->lastError(); eng.reset(); return false; }
    eng->newGame();
    stop_ = false;
    th = std::thread([this](){ loop(); });
    return true;
}
void AnalysisController::loop(){
    std::string searchingFen;
    bool searching = false;
    while (!stop_){
        if (!eng->alive()){ std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
        std::string want;
        { std::lock_guard<std::mutex> lk(mx); want = target; }
        if (!want.empty() && want != searchingFen){
            if (searching) eng->stopAndWait(2000);
            eng->setPosition(want, {});
            eng->goInfinite();                       // clears the previous search's info
            searching = true;
            searchingFen = want;
            { std::lock_guard<std::mutex> lk(mx); current = want; }
            continue;
        }
        eng->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (eng && eng->alive() && searching) eng->stopAndWait(800);
}
void AnalysisController::shutdown(){
    stop_ = true;
    if (th.joinable()) th.join();
    if (eng) eng->quit();
    eng.reset();
}
std::string AnalysisController::fen(){
    std::lock_guard<std::mutex> lk(mx);
    return current;
}
void AnalysisController::setFen(const std::string& fen){
    std::lock_guard<std::mutex> lk(mx);
    target = fen;
}
EngineInfo AnalysisController::info(){ return eng ? eng->lastInfo() : EngineInfo{}; }
std::vector<PvLine> AnalysisController::pvs(){ return eng ? eng->lastPvs() : std::vector<PvLine>{}; }
bool AnalysisController::alive(){ return eng && eng->alive(); }
std::string AnalysisController::engineName(){ return eng? eng->name() : "engine"; }

