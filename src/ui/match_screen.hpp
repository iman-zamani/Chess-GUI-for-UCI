/* Chess-GUI-for-UCI — engine match / tournament / SPRT screen. GPL-2.0 */
#pragma once
#include "analysis.hpp"
#include "tournament.hpp"
#include "ui/board_view.hpp"
#include <memory>
#include <string>
#include <vector>

class App;

// Everything the setup form edits, by engine name (resolved at start).
// Saved as the last setup and as named presets in match_presets.json.
struct MatchSetup {
    std::string name;                        // preset name ("" for the last-used setup)
    TournamentMode mode = TournamentMode::Match;
    std::vector<std::string> engines;        // Match: [dev, base]
    int gauntletSeeds = 1;
    double baseSec = 10, incSec = 0.1;
    int timeMarginMs = 100;
    int gamesPerPairing = 100;
    int concurrency = 0;                     // 0 = half the CPU cores
    std::string bookPath;
    int bookPlies = 8;
    bool bookRandom = true;
    int seed = 1;
    AdjudicationConfig adj;
    bool sprt = false;
    SprtConfig sprtCfg;
    bool savePgn = true, resumable = true;
};

class MatchScreen {
public:
    explicit MatchScreen(App& app);
    ~MatchScreen(){ shutdown(); }
    void draw();
    void update();
    void shutdown();
    bool animating() const { return runner_.running() || judge_ != nullptr; }
    void addOpening(const std::string& fen){ openings_.push_back(fen); }
    size_t openingCount() const { return openings_.size(); }
private:
    void drawSetup();
    void drawEnginesBlock();
    void drawOpeningsBlock();
    void drawRulesBlock();
    void drawPresetsBlock();
    void drawLive();
    void drawStats(const MatchRunner::Snapshot& s);
    void drawBoards(const MatchRunner::Snapshot& s);
    void start(bool resume);
    void stopJudge();
    void savePgns();
    void exportJson();
    void loadPresets();
    void savePresets();
    std::string outputBase() const;          // games/<stem> for the PGN and state files

    App& app_;
    MatchRunner runner_;
    bool live_ = false;
    MatchSetup setup_;
    std::vector<MatchSetup> presets_;
    std::string presetName_;
    std::vector<std::string> openings_;      // FENs added from the editor / typed
    std::string fenInput_;
    std::string judgeEngine_;
    std::unique_ptr<AnalysisController> judge_;
    int judgeLines_ = 1;
    bool flipped_ = false;
    int selectedGame_ = 0;                   // worker shown on the main board
    BoardView board_;
    std::vector<BoardView> minis_;
    std::string bookPreviewKey_, bookPreview_;   // "path|plies" -> "123 openings" (loading is not free)
    bool bookPreviewOk_ = false;
    std::string lastPgnPath_, lastStatePath_, lastError_;
    MatchSettings lastSettings_;
};
