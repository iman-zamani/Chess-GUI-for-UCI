/* Chess-GUI-for-UCI — engine testing tools screen. GPL-2.0 */
#pragma once
#include "perft_bisect.hpp"
#include "uci_check.hpp"
#include "ui/widgets.hpp"
#include <mutex>
#include <utility>

class App;

// Engine developer tools: move-generator tests (GUI self-test, "go perft"
// suite, perft bisection), the UCI compliance checker and the accuracy test,
// with a shared output log and structured result panels.
class ToolsScreen {
public:
    explicit ToolsScreen(App& app) : app_(app) {}
    void draw();
    bool busy() const { return job_.running(); }
    ui::Job& job(){ return job_; }
private:
    enum class View { Log, Bisect, UciCheck };
    void drawPerftTab();
    void drawBisectTab();
    void drawUciCheckTab();
    void drawAccuracyTab();
    void drawBisectResult();
    void drawUciCheckResult();
    void runSelfTest();
    void runEnginePerft(const EngineConfig& cfg);
    void runBisect(const EngineConfig& cfg, const std::string& fen, int depth);
    void runUciCheck(const EngineConfig& cfg, bool quick);
    void runAccuracy(const EngineConfig& test, const EngineConfig& ref);
    std::string saveDialog(const std::string& stem);

    App& app_;
    ui::Job job_;
    View view_ = View::Log;
    std::string suitePath_ = "Resources/perftsuite.epd";
    int perftDepth_ = 4;
    // perft bisection
    std::string bisectFen_ = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    int bisectDepth_ = 4;
    bool selectBisectTab_ = false;
    std::mutex resMx_;                                   // guards the results below
    BisectReport bisect_;
    bool bisectDone_ = false;
    std::vector<std::pair<std::string, int>> suiteFailures_;   // (fen, depth) from the last suite run
    // UCI compliance
    bool quickCheck_ = false;
    std::string checkEngine_;
    std::vector<CheckResult> checks_;
};
