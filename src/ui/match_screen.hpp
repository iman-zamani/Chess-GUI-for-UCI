/* Chess-GUI-for-UCI — engine match / tournament screen. GPL-2.0 */
#pragma once
#include "analysis.hpp"
#include "tournament.hpp"
#include "ui/board_view.hpp"
#include <memory>
#include <string>
#include <vector>

class App;

class MatchScreen {
public:
    explicit MatchScreen(App& app) : app_(app) {}
    ~MatchScreen(){ shutdown(); }
    void draw();
    void update();
    void shutdown();
    bool animating() const { return runner_.running() || judge_ != nullptr; }
    void addOpening(const std::string& fen){ openings_.push_back(fen); }
    size_t openingCount() const { return openings_.size(); }
private:
    void drawSetup();
    void drawLive();
    void start();
    void stopJudge();
    void savePgns();
    void loadOpenings(const std::string& path);

    App& app_;
    MatchRunner runner_;
    bool live_ = false;                    // showing the running/finished match
    std::vector<std::string> selected_;    // engine names
    std::vector<std::string> openings_;    // FENs; empty = standard start
    std::string fenInput_, openingsPath_;
    std::string judgeEngine_;              // "" = no judge
    std::unique_ptr<AnalysisController> judge_;
    int judgeLines_ = 1;
    bool flipped_ = false;
    BoardView board_;
};
