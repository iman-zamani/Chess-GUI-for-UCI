/* Chess-GUI-for-UCI — SFML GUI. GPL-2.0 */
#pragma once
#include <SFML/Graphics.hpp>
#include <optional>
#include <deque>
#include "chess.hpp"
#include "uci_engine.hpp"
#include "tournament.hpp"
#include "testing.hpp"

enum class Screen { MENU, GAME, PICK_ENGINE, MATCH_SETUP, MATCH_VIEW,
                    TESTS, TEST_RUN, ANALYSIS_SETUP };

enum class GameMode { HUMAN_VS_HUMAN, HUMAN_VS_ENGINE, ANALYSIS };

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
private:
    // ---- framework ----
    sf::RenderWindow window;
    sf::Font font;
    sf::Texture pieceTexture;      // 6x2 grid of 150px sprites
    bool piecesOk = false;
    Screen screen = Screen::MENU;
    std::vector<Button> buttons;
    std::string toast; sf::Clock toastClock;

    void layout();                  // rebuild buttons for current screen
    void handleEvent(const sf::Event& e);
    void update();
    void render();
    void onButton(int id);
    void showToast(const std::string& s){ toast = s; toastClock.restart(); }

    // drawing helpers
    void text(const std::string& s, float x, float y, unsigned size,
              sf::Color c = sf::Color::White, bool center=false);
    void drawButtons();
    void drawBoard(float bx, float by, float side, const Position& p,
                   int selSq, const std::vector<Move>* legal,
                   int lastFrom, int lastTo, bool flipped, int dragSq,
                   sf::Vector2f dragPos);
    void drawEvalBar(float x, float y, float h, int cpWhite, bool isMate, int mateIn);
    void drawMoveList(float x, float y, float w, float h,
                      const std::vector<std::string>& sans, int startMoveNo, bool startWhite);
    void drawPiece(int piece, float x, float y, float size, sf::Uint8 alpha=255);
    int  boardSquareAt(sf::Vector2f mouse, float bx, float by, float side, bool flipped) const;

    // ---- engines list ----
    std::vector<EngineEntry> knownEngines;      // discovered + user added
    void scanEngines();
    std::string enginePathInput;                // text field
    bool typingPath = false;
    int pickPurpose = 0;                        // 1=opponent 2=analysis 3=matchA.. see cpp
    std::vector<int> matchSelection;            // toggled engine idxs for tournaments

    // ---- game state ----
    GameMode mode = GameMode::HUMAN_VS_HUMAN;
    Game game;
    bool humanIsWhite = true;
    bool boardFlipped = false;
    int selectedSq = -1, dragFromSq = -1;
    bool dragging = false;
    sf::Vector2f dragPos;
    std::vector<Move> legalForSelected;
    std::optional<Move> pendingPromotion;       // waiting for user choice
    int viewPly = -1;                           // -1 = live; else browse history
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
    // analysis engine
    std::unique_ptr<UciEngine> anaEngine;
    bool analysing = false;
    std::string anaFenSent;
    void ensureAnalysisFollows();
    void stopAnalysis();

    void newGame(const Position& p);
    void trySquareAction(int sq);               // click/drop logic
    void commitMove(Move m);
    void savePGN();
    void copyFEN(); void pasteFEN();

    // ---- match / tournament ----
    MatchRunner runner;
    int matchGamesPerPair = 2;
    void saveTournamentPGNs();

    // ---- tests ----
    std::thread testThread;
    std::atomic<bool> testAbort{false};
    std::atomic<bool> testRunning{false};
    std::mutex testMx;
    std::vector<std::string> testLines;         // live report
    std::string testTitle;
    void pushTestLine(const std::string& s);
    void startPerftTest(const std::string& enginePath);
    void startInternalSelfTest();
    void startAccuracyTest(const std::string& testPath, const std::string& refPath);
    std::string refEnginePath;                  // e.g. stockfish, for accuracy test
    int pendingTestKind = 0;                    // 1 perft, 2 accuracy(test engine), 3 accuracy(ref engine)
    std::string accTestEnginePath;
};
