/* Chess-GUI-for-UCI — SFML GUI. GPL-2.0 */
#pragma once
#include <SFML/Graphics.hpp>
#include <cstdint>
#include <optional>
#include <map>
#include "chess.hpp"
#include "uci_engine.hpp"
#include "tournament.hpp"
#include "testing.hpp"
#include "analysis.hpp"
#include "ui_script.hpp"
#include "settings.hpp"
#include "ui/engine_manager.hpp"
#include "ui/board_view.hpp"
#include "ui/tools_screen.hpp"
#include "ui/editor_screen.hpp"

// Input event as the app sees it (from SFML or from a --script step).
struct UiEvent {
    enum Kind { None, Closed, Resized, KeyPressed, TextEntered,
                MousePressed, MouseReleased, MouseMoved } kind = None;
    unsigned width = 0, height = 0;                       // Resized
    sf::Keyboard::Key key = sf::Keyboard::Key::Unknown;   // KeyPressed
    bool ctrl = false, sys = false;
    char32_t unicode = 0;                                 // TextEntered
    sf::Mouse::Button button = sf::Mouse::Button::Left;   // Mouse*
};

enum class Screen { MENU, GAME, PICK_ENGINE, MATCH_SETUP, MATCH_VIEW,
                    TESTS, TEST_RUN, EDITOR, ENGINE_OPTIONS,
                    PGN_LOAD, PGN_LIST, ENGINE_MANAGER };

enum class EditorAction { PlayHuman, PlayEngine, Analyse, AddToMatch, Cancel };

enum class GameMode { HUMAN_VS_HUMAN, HUMAN_VS_ENGINE, ANALYSIS };

enum class TextTarget { NONE, ENGINE_PATH, MATCH_FEN, OPTION_VALUE, FILE_PATH };

struct Button {
    sf::FloatRect rect;
    std::string label;
    int id = 0;
    bool enabled = true;
    bool toggled = false;
};

class App {
public:
    bool init();
    void run();
    bool loadScript(const std::string& path);   // see ui_script.hpp
    bool scriptFailed() const { return scriptFailed_; }

    // ---- services for the screens in src/ui/ ----
    EngineRegistry& registry(){ return registry_; }
    void saveEngines();
    Settings& settings(){ return settings_; }
    void saveSettings();
    void toast(const std::string& s){ showToast(s); }
    void go(Screen s);
    const PieceSet& pieces() const { return pieceSet; }
    void editorDone(EditorAction a, const Position& p);   // the editor's "use it" buttons
private:
    Settings settings_;
    PieceSet pieceSet;
    // ---- scripted input (--script) ----
    std::vector<UiScriptStep> script;
    size_t scriptPos = 0;
    long long scriptWaitUntil = 0;
    long long scriptItemDeadline = 0;    // wait-item timeout
    std::optional<sf::Vector2f> scriptMouse;
    bool scriptButtonDown = false;       // scripted left button state for ImGui screens
    std::string pendingShot;
    bool scriptFailed_ = false;
    void stepScript();
    sf::Vector2f mouseLogical();

    // ---- Dear ImGui (screens being migrated to it) ----
    bool imguiOk = false;
    float imguiScale = 0;                // physical pixels per logical unit the fonts were built for
    sf::Clock imguiClock;
    bool imguiFrameActive = false;       // Update() called, Render()/EndFrame() pending
    bool imguiScreen() const { return screen == Screen::ENGINE_MANAGER || screen == Screen::TESTS
                                   || screen == Screen::EDITOR; }
    void setupImGuiFonts();
    void drawImGui();
    void drawMenuBar();
    void drawToast();
    void injectImGui(const UiEvent& e);  // --script input for ImGui screens
    EngineManagerWindow engineManager;
    ToolsScreen tools{*this};
    EditorScreen editor{*this};

    // ---- framework ----
    sf::RenderWindow window;
    sf::Font font;
    sf::Texture pieceTexture;
    bool piecesOk = false;
    int pieceCell = 150;                 // px per piece in the sprite sheet
    Screen screen = Screen::MENU;
    std::vector<Button> buttons;
    std::string toastText; sf::Clock toastClock;
    bool forceRedraw = true;

    void layout();
    void handleEvent(const UiEvent& e);
    void update();
    void render();
    void onButton(int id);
    int  activityLevel();
    void showToast(const std::string& s){ toastText = s; toastClock.restart(); forceRedraw=true; }

    // drawing helpers
    void text(const std::string& s, float x, float y, unsigned size,
              sf::Color c = sf::Color::White, bool center=false);
    void drawButtons();
    void drawBoard(float bx, float by, float side, const Position& p,
                   int selSq, const std::vector<Move>* legal,
                   int lastFrom, int lastTo, bool flipped, int dragSq,
                   sf::Vector2f dragPos);
    void drawArrow(float bx, float by, float side, int from, int to,
                   bool flipped, sf::Color c);
    void drawPvArrows(const std::vector<PvLine>& pvs, const Position& shown,
                      float bx, float by, float side, bool flipped, int count);
    void drawSquareTint(int sq, float bx, float by, float side, bool flipped, sf::Color c);
    void drawEvalBar(float x, float y, float h, int cpWhite, bool isMate, int mateIn);
    void drawMoveList(float x, float y, float w, float h,
                      const std::vector<std::string>& sans, int startMoveNo, bool startWhite);
    void drawPiece(int piece, float x, float y, float size, std::uint8_t alpha=255);
    int  boardSquareAt(sf::Vector2f mouse, float bx, float by, float side, bool flipped) const;

    // ---- engines & per-engine UCI option overrides ----
    EngineRegistry registry_;            // engines.json + discovered engines
    void scanEngines();
    void importLegacyOverrides();
    EngineConfig& configFor(const std::string& path);   // adds a default entry if unknown
    EngineConfig makeEntry(int idx);
    void setOverride(const std::string& path, const std::string& name, const std::string& val);
    std::string overrideOrDefault(const std::string& path, const UciOption& o);
    std::string optsPath;
    std::vector<UciOption> optsList;
    int optScroll = 0;
    int editingOption = -1;
    std::string optionEditBuffer;

    // ---- text input ----
    TextTarget textFocus = TextTarget::NONE;
    std::string enginePathInput;
    std::string matchFenInput;
    std::string filePathInput;
    int filePurpose = 0;                 // 1 = openings list, 2 = PGN file
    void commitTextInput();

    int pickPurpose = 0;   // 1 opponent 2 analysis 4 perft 5 acc-test 6 acc-ref 7 judge 8 review
    std::vector<int> matchSelection;

    // ---- game state ----
    GameMode mode = GameMode::HUMAN_VS_HUMAN;
    Game game;
    bool humanIsWhite = true;
    bool menuPlayWhite = true;           // color toggle next to "Play vs Engine"
    bool boardFlipped = false;
    int selectedSq = -1, dragFromSq = -1;
    bool dragging = false;
    sf::Vector2f dragPos;
    std::vector<Move> legalForSelected;
    std::optional<Move> pendingPromotion;
    int viewPly = -1;
    std::optional<Position> oneShotStart;   // consumed once by the next engine pick
    Position shownPosition() const;
    float gameButtonsTop = 0;               // panel band boundary shared layout<->render

    // premove (human vs engine)
    int premoveFrom = -1, premoveTo = -1;
    void tryExecutePremove();

    // clocks
    TimeControl tc;
    long long wClockMs=0, bClockMs=0;
    sf::Clock tickClock;
    bool clocksRunning = false;

    // opponent engine, with pondering
    // oppState: 0 idle, 1 thinking, 2 pondering, 3 ponderhit (thinking on)
    std::unique_ptr<UciEngine> oppEngine;
    std::thread oppThread;
    std::atomic<int> oppState{0};
    std::atomic<bool> oppDiscard{false};
    std::string oppPonderMove;
    bool oppPonderEnabled = false;
    std::mutex oppMx; std::string oppMoveReady;
    void startEngineThinkIfNeeded();
    void startPonderIfPossible();
    void applyEngineMoveIfReady();
    void humanCommitMove(Move m);        // routes ponderhit/miss, then commits
    void stopOpponent();

    // analysis engine
    std::unique_ptr<AnalysisController> anaCtl;
    bool analysing = false;
    std::string anaEnginePath;
    int arrowsN = 1;                     // suggested-move arrows (MultiPV)
    void startAnalysis(const std::string& path);
    void stopAnalysis();

    void newGame(const Position& p);
    void trySquareAction(int sq);
    void commitMove(Move m);
    void savePGN();
    void copyFEN(); void pasteFEN();

    // ---- match / tournament ----
    MatchRunner runner;
    int matchGamesPerPair = 2;
    std::vector<std::string> matchFens;  // opening positions (empty = standard)
    std::string judgePath;               // independent eval/arrow engine
    std::unique_ptr<AnalysisController> judgeCtl;
    bool matchFlipped = false;
    void saveTournamentPGNs();

    // ---- PGN analysis ----
    std::vector<PgnGame> pgnGames;
    int pgnScroll = 0;
    bool loadPGNText(const std::string& text, const std::string& sourceLabel);
    void openPgnGame(int idx);

    // ---- tests ----
    std::thread testThread;
    std::atomic<bool> testAbort{false};
    std::atomic<bool> testRunning{false};
    std::mutex testMx;
    std::vector<std::string> testLines;
    std::string testTitle;
    Screen testReturn = Screen::TESTS;   // where TEST_RUN "Back" goes
    void pushTestLine(const std::string& s);
    void startGameReview(const std::string& enginePath);
    std::string copyAllTestLines();
};
