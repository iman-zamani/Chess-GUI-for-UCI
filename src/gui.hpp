/* Chess-GUI-for-UCI — application shell (window, ImGui, screens). GPL-2.0 */
#pragma once
#include <SFML/Graphics.hpp>
#include <optional>
#include <string>
#include <vector>
#include "chess.hpp"
#include "engine_config.hpp"
#include "settings.hpp"
#include "ui_script.hpp"
#include "ui/board_view.hpp"
#include "ui/editor_screen.hpp"
#include "ui/engine_manager.hpp"
#include "ui/game_screen.hpp"
#include "ui/match_screen.hpp"
#include "ui/tools_screen.hpp"
#include "ui/uci_console.hpp"

enum class Screen { HOME, GAME, MATCH, TOOLS, EDITOR, ENGINES };
enum class EditorAction { PlayHuman, PlayEngine, Analyse, AddToMatch, Cancel };

// Input event from a --script step (see ui_script.hpp).
struct ScriptEvent {
    enum Kind { None, KeyPressed, TextEntered, MousePressed, MouseReleased, MouseMoved } kind = None;
    sf::Keyboard::Key key = sf::Keyboard::Key::Unknown;
    bool ctrl = false;
    char32_t unicode = 0;
};

class App {
public:
    bool init();
    void run();
    bool loadScript(const std::string& path);
    bool scriptFailed() const { return scriptFailed_; }

    // ---- services for the screens in src/ui/ ----
    EngineRegistry& registry(){ return registry_; }
    void scanEngines();
    void saveEngines();
    Settings& settings(){ return settings_; }
    void saveSettings();
    void toast(const std::string& s);
    void go(Screen s);
    const PieceSet& pieces() const { return pieces_; }
    void openEditor(const Position& p);                 // edit a copy of `p`
    void openEditorForMatch();                          // "Done" adds the position to the match
    void editorDone(EditorAction a, const Position& p); // the editor's "use it" buttons
    GameScreen& gameScreen(){ return game_; }

private:
    void processEvent(const sf::Event& ev);
    void beginImGuiFrame();
    void drawFrame();
    void drawMenuBar();
    void drawHome();
    void drawToast();
    void drawAbout();
    void setupFonts();
    bool animating() const;
    void handleGlobalKeys();

    // scripted input (--script)
    void stepScript();
    void scriptEvent(const ScriptEvent& e);
    ImVec2 logicalToPixels(float x, float y) const;
    BoardView* activeBoard();

    sf::RenderWindow window_;
    float dpiScale_ = 1;                  // physical pixels per logical unit (HiDPI)
    bool imguiOk_ = false;
    sf::Clock frameClock_;
    Screen screen_ = Screen::HOME;
    Settings settings_;
    EngineRegistry registry_;
    PieceSet pieces_;
    std::string toastText_;
    sf::Clock toastClock_;
    bool showAbout_ = false;

    GameScreen game_{*this};
    MatchScreen match_{*this};
    ToolsScreen tools_{*this};
    EditorScreen editor_{*this};
    EngineManagerWindow engineManager_;
    UciConsole console_;
    bool showConsole_ = false;

    std::vector<UiScriptStep> script_;
    size_t scriptPos_ = 0;
    long long scriptWaitUntil_ = 0, scriptItemDeadline_ = 0;
    std::optional<ImVec2> scriptMouse_;   // pixels
    bool scriptButtonDown_ = false;
    std::string pendingShot_;
    bool scriptFailed_ = false;
};
