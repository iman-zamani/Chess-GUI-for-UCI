/* Chess-GUI-for-UCI — game screen: play, analyse, review. GPL-2.0 */
#pragma once
#include "analysis.hpp"
#include "chess.hpp"
#include "tournament.hpp"
#include "uci_engine.hpp"
#include "ui/board_view.hpp"
#include "ui/widgets.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

class App;

enum class GameMode { HUMAN_VS_HUMAN, HUMAN_VS_ENGINE, ANALYSIS };

class GameScreen {
public:
    explicit GameScreen(App& app) : app_(app) {}
    ~GameScreen(){ shutdown(); }

    // ---- entry points ----
    void newHumanGame(const Position& p);
    bool newEngineGame(const EngineConfig& opponent, const Position& p, bool humanWhite);
    void openAnalysis(const Position& p);          // analysis board; engine chosen in the panel
    void openGame(const Game& g);                  // e.g. a PGN game, opened at the first move
    void showNewGameDialog(GameMode preset, std::optional<Position> start = std::nullopt);
    void openPgnDialog();                          // file dialog / paste, then pick a game

    void draw();
    BoardView& board(){ return board_; }
    void update();                                 // clocks, engine moves; every frame
    void leave();                                  // stop engines when leaving the screen
    void shutdown();
    bool animating() const;                        // clocks or engines running
    void flip(){ flipped_ = !flipped_; }
    const Game& game() const { return game_; }
    std::string shownFen() const { return shownPosition().toFEN(); }   // what the board shows

private:
    // game flow
    void resetBoardState();
    Position shownPosition() const;
    void squarePressed(int sq);
    void commitMove(Move m);
    void humanCommitMove(Move m);
    void tryExecutePremove();
    bool humanToMove() const;
    // opponent engine (with pondering)
    void stopOpponent();
    void startEngineThinkIfNeeded();
    void startPonderIfPossible();
    void applyEngineMoveIfReady();
    // analysis
    void startAnalysis();
    void stopAnalysis();
    void startReview();
    // panels
    void drawPlayers(bool top);
    void drawMoveList(float height);
    void drawAnalysis();
    void drawControls();
    void drawNewGameDialog();
    void drawPgnDialogs();
    void drawPromotionPopup();
    void handleKeys();
    void savePgn();
    bool loadPgnText(const std::string& text, const std::string& source);

    App& app_;
    GameMode mode_ = GameMode::HUMAN_VS_HUMAN;
    Game game_;
    bool humanIsWhite_ = true;
    bool flipped_ = false;
    BoardView board_;
    int selectedSq_ = -1;
    std::vector<Move> legalForSelected_;
    std::optional<Move> pendingPromotion_;
    int viewPly_ = -1;                             // -1 = live position
    int premoveFrom_ = -1, premoveTo_ = -1;
    bool scrollMoves_ = false;

    // clocks
    TimeControl tc_;
    long long wClockMs_ = 0, bClockMs_ = 0;
    bool clocksRunning_ = false;
    long long lastTickMs_ = 0;

    // opponent engine. oppState: 0 idle, 1 thinking, 2 pondering, 3 ponderhit
    std::unique_ptr<UciEngine> opp_;
    std::thread oppThread_;
    std::atomic<int> oppState_{0};
    std::atomic<bool> oppDiscard_{false};
    std::string oppPonderMove_;
    bool oppPonderEnabled_ = false;
    std::mutex oppMx_;
    std::string oppMoveReady_;

    // analysis engine
    std::unique_ptr<AnalysisController> ana_;
    std::string anaEngine_;                        // engine name the analysis runs
    ui::Job review_;
    int reviewMs_ = 400;

    // dialogs
    bool openNewGame_ = false;
    GameMode dlgMode_ = GameMode::HUMAN_VS_ENGINE;
    int dlgColor_ = 0;                             // 0 white, 1 black, 2 random
    int dlgStart_ = 0;                             // 0 standard, 1 current position, 2 FEN
    std::string dlgFen_;
    std::optional<Position> dlgPreset_;
    bool openPgn_ = false, openPgnList_ = false;
    std::string pgnPath_;
    std::vector<PgnGame> pgnGames_;
    std::string pgnFilter_;
};
