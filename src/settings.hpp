/* Chess-GUI-for-UCI — persistent user settings (settings.json). GPL-2.0 */
#pragma once
#include <string>

// Everything the GUI remembers between runs. Stored as JSON in the user
// config directory; unknown keys written by newer versions are kept.
struct Settings {
    // window
    int windowW = 0, windowH = 0;             // logical size; 0 = fit the screen
    // appearance
    std::string theme = "dark";               // dark | light
    std::string boardTheme = "brown";         // see BoardTheme in ui/board_view
    bool showCoordinates = true;
    // folders last used in file dialogs
    std::string lastPgnDir, lastEngineDir, lastOpeningsDir, lastSaveDir;
    // time control for games and matches
    int tcBaseMs = 300000, tcIncMs = 0;
    // analysis
    int analysisMultiPV = 3;
    bool showArrows = true;
    // engines last chosen for each purpose (engine names from engines.json)
    std::string opponentEngine, analysisEngine, perftEngine, accTestEngine, accRefEngine, judgeEngine;
    std::string uciCheckEngine;
    // accuracy test
    int accCount = 100, accTestMs = 500, accRefMs = 1000, accMultiPV = 3;
    // matches
    int matchGamesPerPair = 2;

    bool load(const std::string& file, std::string* err = nullptr);   // missing file = defaults
    bool save(const std::string& file, std::string* err = nullptr) const;

private:
    std::string raw_;                         // last loaded JSON, to preserve unknown keys
};
