/* Chess-GUI-for-UCI — game screen: play, analyse, review. GPL-2.0 */
#pragma once
#include "analysis.hpp"
#include "chess.hpp"
#include "game_tree.hpp"
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
    void openGame(const GameTree& t);              // e.g. a PGN game, opened at the start
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
    bool atLiveEnd() const { return cur_ == tree_.mainLineEnd(); }
    bool canBranch() const;                        // moving from a past position makes a variation
    void branchMove(Move m);                       // a move from the shown (past) position
    void goTo(int node);
    void addLine(const std::vector<Move>& line);   // from the shown position (a PV click)
    void syncMainLine();                           // game_ = the tree's main line (analysis)
    void applyReview();                            // review results -> tree (UI thread)
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
    void drawVariation(int first, int nesting);     // one variation, starting with node `first`
    void moveToken(int node, bool numbered);       // a clickable move in the move list
    void drawEvalGraph();
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
    GameTree tree_;                                // the game with its variations; main line == game_
    int cur_ = 0;                                  // node shown on the board
    bool openComment_ = false;
    int commentNode_ = -1;
    std::string commentBuf_;
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
    MoveStat oppMoveStat_;                         // stats for oppMoveReady_
    std::vector<MoveStat> oppStats_;               // one per engine move of this game
    bool statsOpen_ = false;                       // stats panel expanded (last frame)

    // analysis engine
    std::unique_ptr<AnalysisController> ana_;
    std::string anaEngine_;                        // engine name the analysis runs
    ui::Job review_;
    int reviewMs_ = 400;
    int reviewThreads_ = 0;                        // 0 = automatic
    struct ReviewOut {
        bool ready = false;
        std::vector<int> nodes;                    // main line node of each move
        std::vector<int> cpW, mateW;               // White's view after each move (+ the start at index 0)
        std::vector<bool> isMate;
        std::vector<int> nag;                      // 0 or 1..6 per move
        std::string summary, engine;
    };
    std::mutex reviewMx_;
    ReviewOut reviewOut_;
    std::string reviewSummary_;

    // dialogs
    bool openNewGame_ = false;
    GameMode dlgMode_ = GameMode::HUMAN_VS_ENGINE;
    int dlgColor_ = 0;                             // 0 white, 1 black, 2 random
    int dlgStart_ = 0;                             // 0 standard, 1 current position, 2 FEN, 3 Chess960
    int dlg960_ = 518;                             // Chess960 start position number
    std::string dlgFen_;
    std::optional<Position> dlgPreset_;
    bool openPgn_ = false, openPgnList_ = false;
    std::string pgnPath_;
    std::vector<PgnGame> pgnGames_;
    std::string pgnFilter_;
};
