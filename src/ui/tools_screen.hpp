/* Chess-GUI-for-UCI — engine testing tools screen. GPL-2.0 */
#pragma once
#include "elo_estimator.hpp"
#include "elo_ladder.hpp"
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
    void openEloTab(){ selectEloTab_ = true; }
    ui::Job& job(){ return job_; }
private:
    enum class View { Log, Bisect, UciCheck, Elo, Ladder };
    void drawPerftTab();
    void drawBisectTab();
    void drawUciCheckTab();
    void drawAccuracyTab();
    void drawEloTab();
    void drawEloResult();
    void runElo(const EngineConfig& cfg, bool full);
    bool loadEloModel();
    void drawLadderTab();
    void drawLadderResult();
    void drawLadderChart(const std::vector<LadderLevel>& levels, const LadderFit& fit);
    void runLadder(const LadderSettings& s);
    void startLadder(bool resume);
    const std::string& bundledReference();
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
    // Elo estimate
    bool selectEloTab_ = false;
    bool eloFull_ = false;
    bool modelTried_ = false;
    std::string modelPath_, modelError_;
    EloModel model_;
    EloResult elo_;
    EloProgress eloProgress_;
    bool eloRunning_ = false;
    // Elo from games
    LadderResult ladder_;
    LadderProgress ladderProgress_;
    bool ladderRunning_ = false;
    bool ladderEnded_ = false;                           // a run ended (handled on the UI thread)
    bool referenceLooked_ = false;
    std::string referencePath_;                          // the bundled Stockfish 11 ("" = not installed)
};
