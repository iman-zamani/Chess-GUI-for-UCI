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
                    TESTS, TEST_RUN, EDITOR, ENGINE_OPTIONS,
                    ACC_SETUP, PGN_LOAD, PGN_LIST };

enum class GameMode { HUMAN_VS_HUMAN, HUMAN_VS_ENGINE, ANALYSIS };

enum class TextTarget { NONE, ENGINE_PATH, MATCH_FEN, OPTION_VALUE, FILE_PATH };

struct Button {
    sf::FloatRect rect;
    std::string label;
    int id = 0;
    bool enabled = true;
    bool toggled = false;
};

// Runs an engine in infinite-analysis mode on its own thread; the render
// thread never blocks on engine I/O (setFen/info/pvs/alive are all cheap).
class AnalysisController {
public:
    ~AnalysisController(){ shutdown(); }
    bool start(const std::string& path,
               const std::vector<std::pair<std::string,std::string>>& opts);
    void shutdown();
    void setFen(const std::string& fen);
    EngineInfo info();
    std::vector<PvLine> pvs();
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
    sf::Texture pieceTexture;
    bool piecesOk = false;
    Screen screen = Screen::MENU;
    std::vector<Button> buttons;
    std::string toast; sf::Clock toastClock;
    bool forceRedraw = true;

    void layout();
    void handleEvent(const sf::Event& e);
    void update();
    void render();
    void onButton(int id);
    int  activityLevel();
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
    void drawPvArrows(const std::vector<PvLine>& pvs, const Position& shown,
                      float bx, float by, float side, bool flipped, int count);
    void drawSquareTint(int sq, float bx, float by, float side, bool flipped, sf::Color c);
    void drawEvalBar(float x, float y, float h, int cpWhite, bool isMate, int mateIn);
    void drawMoveList(float x, float y, float w, float h,
                      const std::vector<std::string>& sans, int startMoveNo, bool startWhite);
    void drawPiece(int piece, float x, float y, float size, sf::Uint8 alpha=255);
    int  boardSquareAt(sf::Vector2f mouse, float bx, float by, float side, bool flipped) const;

    // ---- engines & per-engine UCI option overrides ----
    std::vector<EngineEntry> knownEngines;
    void scanEngines();
    EngineEntry makeEntry(int idx);
    std::map<std::string, std::vector<std::pair<std::string,std::string>>> engineOverrides;
    void loadOverrides(); void saveOverrides();
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

    // ---- board editor ----
    static const int BRUSH_HAND = 99;
    Position editPos;
    int editorBrush = WHITE_PAWN;        // 0 = eraser, BRUSH_HAND = hand tool
    bool editorPainting = false;
    int handFrom = -1, handPiece = 0;    // hand-tool carry state
    bool editorPlayWhite = true;
    Screen editorReturn = Screen::MENU;  // MATCH_SETUP => show "Done" button
    std::vector<sf::FloatRect> paletteRects;
    std::string editorFEN() const;
    std::string editorProblem() const;

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
    // accuracy settings
    int accCount = 100, accTestMs = 500, accRefMs = 1000, accMultiPV = 3;
    void pushTestLine(const std::string& s);
    void startPerftTest(const EngineEntry& e);
    void startInternalSelfTest();
    void startAccuracyTest(const EngineEntry& test, const EngineEntry& ref);
    void startGameReview(const std::string& enginePath);
    std::string copyAllTestLines();
    EngineEntry accTestEngine;
};
