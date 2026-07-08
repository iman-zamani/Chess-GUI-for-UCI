/* Chess-GUI-for-UCI — SFML GUI. GPL-2.0 */
#pragma once
#include <SFML/Graphics.hpp>
#include <optional>
#include <map>
#include "chess.hpp"
#include "uci_engine.hpp"
#include "tournament.hpp"
#include "testing.hpp"

enum class Screen { MENU, GAME, PICK_ENGINE, MATCH_SETUP, MATCH_VIEW,
                    TESTS, TEST_RUN, EDITOR, ENGINE_OPTIONS };

enum class GameMode { HUMAN_VS_HUMAN, HUMAN_VS_ENGINE, ANALYSIS };

enum class TextTarget { NONE, ENGINE_PATH, MATCH_FEN, OPTION_VALUE };

struct Button {
    sf::FloatRect rect;
    std::string label;
    int id = 0;
    bool enabled = true;
    bool toggled = false;
};

// Runs the analysis engine on its own thread so the render thread NEVER
// blocks on engine I/O. The GUI only calls setFen() (cheap, mutex) and
// info()/alive() (cheap).
class AnalysisController {
public:
    ~AnalysisController(){ shutdown(); }
    bool start(const std::string& path,
               const std::vector<std::pair<std::string,std::string>>& opts);
    void shutdown();
    void setFen(const std::string& fen);
    EngineInfo info();
    bool alive();
    std::string engineName();
    std::string lastError() const { return err; }
private:
    void loop();
    std::unique_ptr<UciEngine> eng;
    std::thread th;
    std::atomic<bool> stop_{false};
    std::mutex mx;
    std::string target;
    std::string err;
};

class App {
public:
    bool init();
    void run();
private:
    // ---- framework ----
    sf::RenderWindow window;
    sf::Font font;
    sf::Texture pieceTexture;      // 6x2 grid of 150px sprites
    bool piecesOk = false;
    Screen screen = Screen::MENU;
    std::vector<Button> buttons;
    std::string toast; sf::Clock toastClock;
    bool forceRedraw = true;       // event-driven rendering when idle

    void layout();
    void handleEvent(const sf::Event& e);
    void update();
    void render();
    void onButton(int id);
    int  activityLevel();          // 0 idle, 1 background activity, 2 interactive
    void showToast(const std::string& s){ toast = s; toastClock.restart(); forceRedraw=true; }

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
    void drawEvalBar(float x, float y, float h, int cpWhite, bool isMate, int mateIn);
    void drawMoveList(float x, float y, float w, float h,
                      const std::vector<std::string>& sans, int startMoveNo, bool startWhite);
    void drawPiece(int piece, float x, float y, float size, sf::Uint8 alpha=255);
    int  boardSquareAt(sf::Vector2f mouse, float bx, float by, float side, bool flipped) const;

    // ---- engines & per-engine UCI option overrides ----
    std::vector<EngineEntry> knownEngines;
    void scanEngines();
    EngineEntry makeEntry(int idx);     // path + name + stored option overrides
    std::map<std::string, std::vector<std::pair<std::string,std::string>>> engineOverrides;
    void loadOverrides(); void saveOverrides();
    void setOverride(const std::string& path, const std::string& name, const std::string& val);
    std::string overrideOrDefault(const std::string& path, const UciOption& o);
    // options editor state
    std::string optsPath;               // engine being configured
    std::vector<UciOption> optsList;
    int optScroll = 0;
    int editingOption = -1;             // index into optsList while typing
    std::string optionEditBuffer;

    // ---- text input ----
    TextTarget textFocus = TextTarget::NONE;
    std::string enginePathInput;
    std::string matchStartFen;          // empty = standard
    void commitTextInput();

    int pickPurpose = 0;                // 1 opponent 2 analysis 4 perft 5 acc-test 6 acc-ref
    std::vector<int> matchSelection;

    // ---- game state ----
    GameMode mode = GameMode::HUMAN_VS_HUMAN;
    Game game;
    bool humanIsWhite = true;
    bool boardFlipped = false;
    int selectedSq = -1, dragFromSq = -1;
    bool dragging = false;
    sf::Vector2f dragPos;
    std::vector<Move> legalForSelected;
    std::optional<Move> pendingPromotion;
    int viewPly = -1;                   // -1 = live; else browsing history
    std::optional<Position> pendingStart;   // custom start position (editor / menu)
    Position shownPosition() const;     // live or browsed position

    // clocks
    TimeControl tc;
    long long wClockMs=0, bClockMs=0;
    sf::Clock tickClock;
    bool clocksRunning = false;

    // opponent engine (its own thread)
    std::unique_ptr<UciEngine> oppEngine;
    std::thread oppThread;
    std::atomic<bool> oppThinking{false};
    std::mutex oppMx; std::string oppMoveReady;
    void startEngineThinkIfNeeded();
    void applyEngineMoveIfReady();
    void stopOpponent();

    // analysis engine (threaded controller)
    std::unique_ptr<AnalysisController> anaCtl;
    bool analysing = false;
    std::string anaEnginePath;          // remembered so toggle can restart it
    void startAnalysis(const std::string& path);
    void stopAnalysis();

    void newGame(const Position& p);
    void trySquareAction(int sq);
    void commitMove(Move m);
    void savePGN();
    void copyFEN(); void pasteFEN();

    // ---- board editor ----
    Position editPos;
    int editorBrush = WHITE_PAWN;       // 0 = eraser
    bool editorPainting = false;
    bool editorPlayWhite = true;        // color you take when starting vs engine
    std::vector<sf::FloatRect> paletteRects;   // for sprite overlay
    std::string editorFEN() const;      // sanitized FEN of editPos
    std::string editorProblem() const;  // "" if position is usable

    // ---- match / tournament ----
    MatchRunner runner;
    int matchGamesPerPair = 2;
    void saveTournamentPGNs();

    // ---- tests ----
    std::thread testThread;
    std::atomic<bool> testAbort{false};
    std::atomic<bool> testRunning{false};
    std::mutex testMx;
    std::vector<std::string> testLines;
    std::string testTitle;
    void pushTestLine(const std::string& s);
    void startPerftTest(const EngineEntry& e);
    void startInternalSelfTest();
    void startAccuracyTest(const EngineEntry& test, const EngineEntry& ref);
    std::string copyAllTestLines();
    EngineEntry accTestEngine;
};
