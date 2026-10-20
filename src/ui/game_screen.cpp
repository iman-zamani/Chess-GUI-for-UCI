/* Chess-GUI-for-UCI — game screen: play, analyse, review. GPL-2.0 */
#include "ui/game_screen.hpp"
#include "gui.hpp"
#include "testing.hpp"
#include "ui/file_dialog.hpp"
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace fs = std::filesystem;

static long long steadyMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static const struct { const char* label; int baseMs, incMs; } kTimeControls[] = {
    {"1+0", 60000, 0}, {"3+0", 180000, 0}, {"3+2", 180000, 2000}, {"5+0", 300000, 0},
    {"10+0", 600000, 0}, {"15+10", 900000, 10000}, {"30+0", 1800000, 0},
};

// UCI moves of the game so far, for "position fen <start> moves ..."
static std::vector<std::string> uciMoves(const Game& g){
    std::vector<std::string> out;
    Position p = g.start;
    for (auto& m : g.moves){ out.push_back(p.moveToUCI(m)); p.makeMove(m); }
    return out;
}

// "1. e4 e5 2. Nf3" from a UCI PV; stops at the first illegal move.
static std::string pvToSan(const Position& start, const std::string& pv, int maxMoves = 12){
    std::istringstream ss(pv);
    std::string u, out;
    Position p = start;
    int n = 0;
    while (ss >> u && n < maxMoves){
        Move m;
        if (!p.uciToMove(u, m)) break;
        if (p.whiteToMove) out += std::to_string(p.fullmoveNumber) + ". ";
        else if (n == 0) out += std::to_string(p.fullmoveNumber) + "... ";
        out += p.moveToSAN(m) + " ";
        p.makeMove(m);
        n++;
    }
    return out;
}

// =============================================================== entry points
void GameScreen::resetBoardState(){
    selectedSq_ = -1; legalForSelected_.clear();
    pendingPromotion_.reset(); viewPly_ = -1;
    premoveFrom_ = premoveTo_ = -1;
    scrollMoves_ = true;
}

void GameScreen::newHumanGame(const Position& p){
    stopOpponent(); stopAnalysis();
    mode_ = GameMode::HUMAN_VS_HUMAN;
    game_.reset(p);
    game_.whiteName = "White"; game_.blackName = "Black";
    resetBoardState();
    tc_ = TimeControl{app_.settings().tcBaseMs, app_.settings().tcIncMs};
    wClockMs_ = bClockMs_ = tc_.baseMs;
    clocksRunning_ = true;
    lastTickMs_ = steadyMs();
}

bool GameScreen::newEngineGame(const EngineConfig& cfg, const Position& p, bool humanWhite){
    stopOpponent(); stopAnalysis();
    opp_ = std::make_unique<UciEngine>();
    if (!opp_->start(cfg)){
        app_.toast("Could not start " + cfg.name + ": " + opp_->lastError());
        opp_.reset();
        return false;
    }
    const std::string* ponder = cfg.option("Ponder");
    oppPonderEnabled_ = ponder && *ponder == "true";
    opp_->newGame();
    mode_ = GameMode::HUMAN_VS_ENGINE;
    humanIsWhite_ = humanWhite;
    flipped_ = !humanWhite;
    game_.reset(p);
    game_.whiteName = humanWhite ? "Human" : opp_->name();
    game_.blackName = humanWhite ? opp_->name() : "Human";
    resetBoardState();
    tc_ = TimeControl{app_.settings().tcBaseMs, app_.settings().tcIncMs};
    wClockMs_ = bClockMs_ = tc_.baseMs;
    clocksRunning_ = true;
    lastTickMs_ = steadyMs();
    return true;
}

void GameScreen::openAnalysis(const Position& p){
    stopOpponent();
    mode_ = GameMode::ANALYSIS;
    game_.reset(p);
    game_.whiteName = "White"; game_.blackName = "Black";
    resetBoardState();
    clocksRunning_ = false;
}

void GameScreen::openGame(const Game& g){
    stopOpponent();
    mode_ = GameMode::ANALYSIS;
    game_ = g;
    resetBoardState();
    viewPly_ = game_.moves.empty() ? -1 : 0;       // open at the start; step through with ->
    clocksRunning_ = false;
    flipped_ = false;
}

void GameScreen::showNewGameDialog(GameMode preset, std::optional<Position> start){
    dlgMode_ = preset;
    dlgPreset_ = start;
    dlgStart_ = start ? 1 : 0;
    openNewGame_ = true;
}

void GameScreen::leave(){
    stopOpponent();
    stopAnalysis();
    clocksRunning_ = false;
}

void GameScreen::shutdown(){
    review_.abort();
    stopOpponent();
    stopAnalysis();
    if (opp_){ opp_->quit(); opp_.reset(); }
}

bool GameScreen::animating() const{
    return clocksRunning_ || oppState_ != 0 || ana_ != nullptr || review_.running();
}

// =============================================================== game flow
Position GameScreen::shownPosition() const{
    if (viewPly_ < 0) return game_.pos;
    Position p = game_.start;
    for (int i = 0; i < viewPly_ && i < (int)game_.moves.size(); i++) p.makeMove(game_.moves[i]);
    return p;
}

bool GameScreen::humanToMove() const{
    return mode_ != GameMode::HUMAN_VS_ENGINE || game_.pos.whiteToMove == humanIsWhite_;
}

void GameScreen::commitMove(Move m){
    if (!game_.tryMove(m)) return;
    if (clocksRunning_){
        if (!game_.pos.whiteToMove) wClockMs_ += tc_.incMs; else bClockMs_ += tc_.incMs;
    }
    selectedSq_ = -1; legalForSelected_.clear(); viewPly_ = -1;
    if (game_.result != GameResult::ONGOING) clocksRunning_ = false;
    scrollMoves_ = true;
}

void GameScreen::humanCommitMove(Move m){
    // pondering: tell the engine whether the human played its predicted move
    if (mode_ == GameMode::HUMAN_VS_ENGINE && oppState_ == 2 && opp_ && opp_->alive()){
        if (game_.pos.moveToUCI(m) == oppPonderMove_){
            opp_->sendRaw("ponderhit");                 // the ponder search becomes the real one
            oppState_ = 3;
        } else {
            oppDiscard_ = true;                         // wrong guess: discard that search
            opp_->sendRaw("stop");
        }
    }
    commitMove(m);
}

// A press on a square: select a piece, or move the selected piece there.
void GameScreen::squarePressed(int sq){
    if (sq < 0 || game_.result != GameResult::ONGOING || viewPly_ >= 0) return;
    if (!humanToMove() || pendingPromotion_) return;
    for (const Move& m : legalForSelected_){
        if (m.to == sq){
            if (m.promo){ Move base = m; base.promo = 0; pendingPromotion_ = base; return; }
            humanCommitMove(m);
            return;
        }
    }
    int v = game_.pos.board[sq];
    if (v && (v > 0) == game_.pos.whiteToMove){
        selectedSq_ = sq;
        std::vector<Move> all; game_.pos.generateLegal(all);
        legalForSelected_.clear();
        for (auto& m : all) if (m.from == sq) legalForSelected_.push_back(m);
    } else { selectedSq_ = -1; legalForSelected_.clear(); }
}

void GameScreen::tryExecutePremove(){
    if (premoveFrom_ < 0 || premoveTo_ < 0){ premoveFrom_ = premoveTo_ = -1; return; }
    int from = premoveFrom_, to = premoveTo_;
    premoveFrom_ = premoveTo_ = -1;
    if (game_.result != GameResult::ONGOING || viewPly_ >= 0 || !humanToMove()) return;
    std::string u = squareName(from) + squareName(to);
    Move m;
    bool ok = game_.pos.uciToMove(u, m);
    if (!ok && std::abs(game_.pos.board[from]) == 1 && (to / 8 == 0 || to / 8 == 7))
        ok = game_.pos.uciToMove(u + "q", m);           // premoved promotion: auto-queen
    if (ok) humanCommitMove(m);
}

// =============================================================== opponent engine
void GameScreen::stopOpponent(){
    oppDiscard_ = true;
    if (oppThread_.joinable()){
        if (opp_) opp_->sendRaw("stop");
        for (int i = 0; i < 60 && oppState_ != 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(25));
        if (oppState_ != 0 && opp_) opp_->quit();      // hung engine: kill it
        oppThread_.join();
    }
    oppState_ = 0;
    oppDiscard_ = false;
    oppPonderMove_.clear();
    std::lock_guard<std::mutex> lk(oppMx_);
    oppMoveReady_.clear();
}

void GameScreen::startEngineThinkIfNeeded(){
    if (mode_ != GameMode::HUMAN_VS_ENGINE || !opp_ || !opp_->alive()) return;
    if (game_.result != GameResult::ONGOING || pendingPromotion_) return;
    if (humanToMove() || oppState_ != 0) return;
    oppState_ = 1;
    if (oppThread_.joinable()) oppThread_.join();
    std::string startFen = game_.start.toFEN();
    auto moves = uciMoves(game_);
    long long wc = wClockMs_, bc = bClockMs_; int inc = tc_.incMs;
    oppThread_ = std::thread([this, startFen, moves, wc, bc, inc](){
        opp_->setPosition(startFen, moves);
        std::string bm = opp_->goClock((int)wc, (int)bc, inc, inc);
        { std::lock_guard<std::mutex> lk(oppMx_); oppMoveReady_ = bm.empty() ? "(none)" : bm; }
        oppState_ = 0;
    });
}

void GameScreen::startPonderIfPossible(){
    if (!oppPonderEnabled_ || mode_ != GameMode::HUMAN_VS_ENGINE) return;
    if (!opp_ || !opp_->alive() || oppState_ != 0 || game_.result != GameResult::ONGOING) return;
    if (!humanToMove()) return;                           // ponder happens on the human's turn
    std::string hint = opp_->lastPonder();
    Move pm;
    if (hint.empty() || !game_.pos.uciToMove(hint, pm)) return;
    oppPonderMove_ = hint;
    oppDiscard_ = false;
    oppState_ = 2;
    if (oppThread_.joinable()) oppThread_.join();
    std::string startFen = game_.start.toFEN();
    auto moves = uciMoves(game_);
    moves.push_back(hint);                                // think about the reply to its guess
    long long wc = wClockMs_, bc = bClockMs_; int inc = tc_.incMs;
    oppThread_ = std::thread([this, startFen, moves, wc, bc, inc](){
        opp_->setPosition(startFen, moves);
        std::string bm = opp_->goPonderWait((int)wc, (int)bc, inc, inc);
        if (oppDiscard_){ oppState_ = 0; return; }        // ponder miss: the result is garbage
        { std::lock_guard<std::mutex> lk(oppMx_); oppMoveReady_ = bm.empty() ? "(none)" : bm; }
        oppState_ = 0;
    });
}

void GameScreen::applyEngineMoveIfReady(){
    std::string bm;
    { std::lock_guard<std::mutex> lk(oppMx_); bm = oppMoveReady_; oppMoveReady_.clear(); }
    if (bm.empty()) return;
    Move m;
    if (bm != "(none)" && game_.pos.uciToMove(bm, m)){
        commitMove(m);
        startPonderIfPossible();                          // think on the human's time
        tryExecutePremove();                              // may answer at once (ponderhit-aware)
    } else if (game_.result == GameResult::ONGOING){
        game_.result = humanIsWhite_ ? GameResult::WHITE_WINS : GameResult::BLACK_WINS;
        game_.reason = ResultReason::ILLEGAL_MOVE;
        app_.toast(opp_ && !opp_->alive() ? "The engine stopped responding" : "Engine returned invalid move '" + bm + "'");
        clocksRunning_ = false;
    }
}

void GameScreen::update(){
    long long now = steadyMs();
    long long dt = lastTickMs_ ? now - lastTickMs_ : 0;
    lastTickMs_ = now;
    if (clocksRunning_ && game_.result == GameResult::ONGOING && !game_.moves.empty()){
        if (game_.pos.whiteToMove) wClockMs_ -= dt; else bClockMs_ -= dt;
        if (wClockMs_ <= 0 || bClockMs_ <= 0){
            bool whiteFlagged = wClockMs_ <= 0;
            if (whiteFlagged) wClockMs_ = 0; else bClockMs_ = 0;
            game_.result = whiteFlagged ? GameResult::BLACK_WINS : GameResult::WHITE_WINS;
            game_.reason = ResultReason::TIMEOUT;
            clocksRunning_ = false;
            stopOpponent();
        }
    }
    applyEngineMoveIfReady();
    startEngineThinkIfNeeded();
    if (ana_) ana_->setFen(shownPosition().toFEN());
}

// =============================================================== analysis
void GameScreen::startAnalysis(){
    stopAnalysis();
    Settings& st = app_.settings();
    const EngineConfig* c = ui::findEngine(app_.registry(), st.analysisEngine);
    if (!c){ app_.toast("Pick an analysis engine first (Engines > Manage engines)"); return; }
    EngineConfig cfg = *c;
    cfg.setOption("MultiPV", std::to_string(std::max(1, st.analysisMultiPV)));
    ana_ = std::make_unique<AnalysisController>();
    if (!ana_->start(cfg)){
        app_.toast("Analysis engine failed: " + ana_->lastError());
        ana_.reset();
        return;
    }
    anaEngine_ = cfg.name;
    ana_->setFen(shownPosition().toFEN());
}

void GameScreen::stopAnalysis(){
    if (ana_){ ana_->shutdown(); ana_.reset(); }
}

void GameScreen::startReview(){
    if (game_.moves.empty()){ app_.toast("No moves to review"); return; }
    const EngineConfig* c = ui::findEngine(app_.registry(), app_.settings().analysisEngine);
    if (!c){ app_.toast("Pick an analysis engine first"); return; }
    EngineConfig cfg = *c;
    cfg.setOption("MultiPV", "1");
    std::string startFen = game_.start.toFEN();
    auto moves = uciMoves(game_);
    std::vector<std::string> sans = game_.sans;
    int startNo = game_.start.fullmoveNumber;
    bool startWhite = game_.start.whiteToMove;
    int ms = reviewMs_;
    review_.start("Game review: " + game_.whiteName + " vs " + game_.blackName,
                  [cfg, startFen, moves, sans, startNo, startWhite, ms](ui::Job& j){
        UciEngine e;
        if (!e.start(cfg)){ j.log("FAIL: cannot start engine: " + e.lastError()); return; }
        e.newGame();
        int N = (int)moves.size();
        j.log("Engine: " + e.name() + "  (" + std::to_string(ms) + " ms per position, "
              + std::to_string(N + 1) + " positions)");
        std::vector<int> score(N + 1, 0);                 // mover's point of view at each ply
        auto sc = [](const EngineInfo& i){
            if (i.isMate) return i.mateIn > 0 ? 32000 - i.mateIn : -32000 - i.mateIn;
            return i.scoreCp;
        };
        for (int i = 0; i <= N && !j.stopRequested() && e.alive(); i++){
            e.setPosition(startFen, std::vector<std::string>(moves.begin(), moves.begin() + i));
            e.goMovetime(ms);
            score[i] = sc(e.lastInfo());
        }
        long long lossW = 0, lossB = 0; int cw = 0, cb = 0;
        double accW = 0, accB = 0;
        int num = startNo; bool wtm = startWhite;
        for (int i = 0; i < N && !j.stopRequested(); i++){
            int loss = std::max(0, std::min(1000, score[i] + score[i + 1]));   // POV flips each ply
            const char* mark = loss >= 300 ? " ??" : loss >= 100 ? " ?" : loss >= 50 ? " ?!" : "";
            int whiteCp = wtm ? score[i] : -score[i];
            char b[200];
            std::snprintf(b, sizeof b, "%d.%s %-8s eval %+.2f  loss %d%s",
                          num, wtm ? "" : "..", sans[i].c_str(), whiteCp / 100.0, loss, mark);
            j.log(b);
            double acc = moveAccuracyFromWinDrop(cpToWinPct(score[i]) - cpToWinPct(-score[i + 1]));
            if (wtm){ lossW += loss; accW += acc; cw++; } else { lossB += loss; accB += acc; cb++; }
            if (!wtm) num++;
            wtm = !wtm;
        }
        char b[160];
        if (cw){ std::snprintf(b, sizeof b, ">>> White: avg cp loss %.1f, accuracy %.1f%%", (double)lossW / cw, accW / cw); j.log(b); }
        if (cb){ std::snprintf(b, sizeof b, ">>> Black: avg cp loss %.1f, accuracy %.1f%%", (double)lossB / cb, accB / cb); j.log(b); }
        if (!e.alive()) j.log("FAIL: the engine terminated during the review.");
        e.quit();
    });
}

// =============================================================== drawing
void GameScreen::handleKeys(){
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    int n = (int)game_.moves.size();
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) flipped_ = !flipped_;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)){ int cur = viewPly_ < 0 ? n : viewPly_; viewPly_ = std::max(0, cur - 1); }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && viewPly_ >= 0){ viewPly_++; if (viewPly_ >= n) viewPly_ = -1; }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) viewPly_ = n ? 0 : -1;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) viewPly_ = -1;
}

void GameScreen::draw(){
    handleKeys();
    Settings& st = app_.settings();
    float em = ImGui::GetFontSize();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float barW = ana_ ? em * 1.2f : 0;
    float side = std::min(avail.y, avail.x * 0.60f - barW);
    Position shown = shownPosition();

    // ---- eval bar + board ----
    ImGui::BeginGroup();
    if (ana_){
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(barW - ImGui::GetStyle().ItemSpacing.x, side));
        EngineInfo inf = ana_->info();
        bool fresh = inf.valid && ana_->fen() == shown.toFEN();
        int cp = fresh ? (shown.whiteToMove ? inf.scoreCp : -inf.scoreCp) : 0;
        int mate = fresh ? (shown.whiteToMove ? inf.mateIn : -inf.mateIn) : 0;
        drawEvalBar(ImGui::GetWindowDrawList(), p0, ImVec2(p0.x + barW - ImGui::GetStyle().ItemSpacing.x, p0.y + side),
                    cp, fresh && inf.isMate, mate, flipped_);
        ImGui::SameLine();
    }
    BoardSpec spec;
    spec.pos = &shown;
    spec.flipped = flipped_;
    spec.theme = st.boardTheme;
    spec.showCoords = st.showCoordinates;
    if (viewPly_ > 0){ spec.lastFrom = game_.moves[viewPly_ - 1].from; spec.lastTo = game_.moves[viewPly_ - 1].to; }
    else if (viewPly_ < 0 && !game_.moves.empty()){ spec.lastFrom = game_.moves.back().from; spec.lastTo = game_.moves.back().to; }
    bool live = viewPly_ < 0 && game_.result == GameResult::ONGOING;
    if (viewPly_ < 0){ spec.selected = selectedSq_; spec.targets = legalForSelected_; }
    if (premoveFrom_ >= 0) spec.tints.push_back({premoveFrom_, IM_COL32(220,60,60,70)});
    if (premoveTo_ >= 0) spec.tints.push_back({premoveTo_, IM_COL32(220,60,60,150)});
    spec.draggable.assign(64, false);
    if (live && !pendingPromotion_){
        bool mine = mode_ == GameMode::HUMAN_VS_ENGINE ? humanIsWhite_ : game_.pos.whiteToMove;
        for (int sq = 0; sq < 64; sq++){
            int v = game_.pos.board[sq];
            spec.draggable[sq] = v && (v > 0) == mine;
        }
    }
    if (ana_ && st.showArrows){
        auto pvs = ana_->pvs();
        if (ana_->fen() == shown.toFEN()){
            static const int alpha[5] = {210, 120, 85, 62, 45};
            for (int r = std::min((int)pvs.size(), 5) - 1; r >= 0; r--){   // best line on top
                Move m;
                if (pvs[r].valid && shown.uciToMove(pvs[r].firstMove(), m))
                    spec.arrows.push_back({m.from, m.to, IM_COL32(70,140,220,alpha[r])});
            }
        }
    }
    spec.dimmed = pendingPromotion_.has_value();
    BoardInput in = board_.draw("game-board", side, spec, app_.pieces());
    bool premoveMode = mode_ == GameMode::HUMAN_VS_ENGINE && live && !humanToMove();
    if (in.pressed >= 0){
        if (premoveMode){
            int v = game_.pos.board[in.pressed];
            bool own = v && (v > 0) == humanIsWhite_;
            if (premoveFrom_ >= 0 && premoveTo_ < 0){
                if (in.pressed == premoveFrom_) premoveFrom_ = -1;               // click again = cancel
                else if (premovePlausible(game_.pos, premoveFrom_, in.pressed)) premoveTo_ = in.pressed;
                else if (own) premoveFrom_ = in.pressed;                          // implausible: reselect
                else premoveFrom_ = -1;
            } else {
                premoveFrom_ = premoveTo_ = -1;
                if (own) premoveFrom_ = in.pressed;
            }
        } else squarePressed(in.pressed);
    }
    if (in.dropFrom >= 0){
        if (premoveMode){
            if (in.dropFrom == premoveFrom_ && premovePlausible(game_.pos, premoveFrom_, in.dropTo)) premoveTo_ = in.dropTo;
        } else if (selectedSq_ == in.dropFrom) squarePressed(in.dropTo);
    }
    if (in.rightPressed >= 0){ premoveFrom_ = premoveTo_ = -1; selectedSq_ = -1; legalForSelected_.clear(); }
    ImGui::EndGroup();

    // ---- side panel ----
    ImGui::SameLine();
    ImGui::BeginChild("game-side", ImVec2(0, 0));
    drawPlayers(true);
    // room below the move list: nav, status, player, 3 control rows, analysis header/rows, lines
    float rowH = ImGui::GetFrameHeightWithSpacing();
    float lineH = ImGui::GetTextLineHeightWithSpacing();
    float reserve = rowH * 8.5f + ui::fonts().large->FontSize * 1.3f
                  + (ana_ ? lineH * (std::max(1, st.analysisMultiPV) + 2) : 0);
    drawMoveList(std::max(em * 6, ImGui::GetContentRegionAvail().y - reserve));
    // status line
    std::string status;
    if (game_.result != GameResult::ONGOING){
        status = game_.resultString();
        if (game_.reason != ResultReason::NONE) status += "  (" + resultReasonText(game_.reason) + ")";
    } else if (viewPly_ >= 0)
        status = "Viewing move " + std::to_string(viewPly_) + "/" + std::to_string(game_.moves.size()) + "  (Down arrow: back to live)";
    else if (mode_ == GameMode::HUMAN_VS_ENGINE && !humanToMove())
        status = premoveTo_ >= 0 ? "Engine is thinking... (premove set)" : "Engine is thinking...";
    else status = game_.pos.whiteToMove ? "White to move" : "Black to move";
    ImGui::TextColored(ui::kAccent, "%s", status.c_str());
    drawPlayers(false);
    drawControls();
    drawAnalysis();
    if (!review_.empty()){
        ImGui::SeparatorText("Game review");
        ImGui::BeginChild("review", ImVec2(0, std::max(em * 8, ImGui::GetContentRegionAvail().y)));
        ui::jobPanel(review_, [&](){ return filedialog::saveFile("Save review", st.lastSaveDir + "/review_" + ui::timeStamp() + ".txt",
                                                                 {"Text files", "*.txt"}); },
                     [&](const std::string& m){ app_.toast(m); });
        ImGui::EndChild();
    }
    ImGui::EndChild();

    drawPromotionPopup();
    drawNewGameDialog();
    drawPgnDialogs();
}

void GameScreen::drawPlayers(bool top){
    bool whiteRow = top == flipped_;                      // the side shown on this edge of the board
    const std::string& name = whiteRow ? game_.whiteName : game_.blackName;
    long long ms = whiteRow ? wClockMs_ : bClockMs_;
    bool toMove = game_.result == GameResult::ONGOING && game_.pos.whiteToMove == whiteRow;
    ImGui::PushFont(ui::fonts().large);
    ImGui::TextColored(toMove ? ImVec4(1, 1, 1, 1) : ui::kDim, "%s", name.c_str());
    if (mode_ != GameMode::ANALYSIS){
        std::string c = ui::clockText(ms);
        float w = ImGui::CalcTextSize(c.c_str()).x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - w);
        ImGui::TextColored(ms < 30000 ? ui::kBad : (toMove ? ImVec4(1, 1, 1, 1) : ui::kDim), "%s", c.c_str());
    }
    ImGui::PopFont();
}

void GameScreen::drawMoveList(float height){
    ImGui::BeginChild("moves", ImVec2(0, height), ImGuiChildFlags_Borders);
    int shownPly = viewPly_ < 0 ? (int)game_.moves.size() : viewPly_;
    if (ImGui::BeginTable("movetable", 3, ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableSetupColumn("no", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.5f);
        ImGui::TableSetupColumn("w", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch);
        int num = game_.start.fullmoveNumber;
        size_t i = 0;
        auto cell = [&](size_t ply){
            ImGui::TableNextColumn();
            if (ply >= game_.sans.size()) return;
            bool cur = (int)ply == shownPly - 1;
            ImGui::PushID((int)ply);
            if (ImGui::Selectable(game_.sans[ply].c_str(), cur)){
                viewPly_ = (int)ply + 1 >= (int)game_.moves.size() ? -1 : (int)ply + 1;
            }
            if (cur && scrollMoves_){ ImGui::SetScrollHereY(0.8f); scrollMoves_ = false; }
            ImGui::PopID();
        };
        if (!game_.start.whiteToMove && !game_.sans.empty()){
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextDisabled("%d.", num);
            ImGui::TableNextColumn(); ImGui::TextDisabled("...");
            cell(0); i = 1; num++;
        }
        for (; i < game_.sans.size(); i += 2){
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextDisabled("%d.", num++);
            cell(i); cell(i + 1);
        }
        ImGui::EndTable();
    }
    if (game_.sans.empty()) ImGui::TextDisabled("No moves yet.");
    ImGui::EndChild();
    // navigation
    int n = (int)game_.moves.size();
    if (ImGui::Button("|<")) viewPly_ = n ? 0 : -1;
    ImGui::SameLine();
    if (ImGui::Button(" < ")){ int cur = viewPly_ < 0 ? n : viewPly_; viewPly_ = std::max(0, cur - 1); }
    ImGui::SameLine();
    if (ImGui::Button(" > ") && viewPly_ >= 0){ viewPly_++; if (viewPly_ >= n) viewPly_ = -1; }
    ImGui::SameLine();
    if (ImGui::Button(">|")) viewPly_ = -1;
    ImGui::SameLine();
    if (ImGui::Button("Flip")) flipped_ = !flipped_;
}

void GameScreen::drawControls(){
    if (ImGui::Button("New game...")) showNewGameDialog(mode_);
    ImGui::SameLine();
    ImGui::BeginDisabled(game_.moves.empty());
    if (ImGui::Button("Takeback")){
        stopOpponent();
        game_.undo();
        if (mode_ == GameMode::HUMAN_VS_ENGINE && !humanToMove()) game_.undo();
        resetBoardState();
        clocksRunning_ = game_.result == GameResult::ONGOING && mode_ != GameMode::ANALYSIS;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(game_.result != GameResult::ONGOING || mode_ == GameMode::ANALYSIS);
    if (ImGui::Button("Resign")){
        bool resignerWhite = mode_ == GameMode::HUMAN_VS_ENGINE ? humanIsWhite_ : game_.pos.whiteToMove;
        game_.result = resignerWhite ? GameResult::BLACK_WINS : GameResult::WHITE_WINS;
        game_.reason = ResultReason::RESIGNATION;
        clocksRunning_ = false;
        stopOpponent();
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Copy FEN")){ ImGui::SetClipboardText(shownPosition().toFEN().c_str()); app_.toast("FEN copied"); }
    ImGui::SameLine();
    if (ImGui::Button("Paste FEN")){
        const char* c = ImGui::GetClipboardText();
        bool ok = false;
        Position p = Position::fromFEN(c ? c : "", &ok);
        if (!ok) app_.toast("The clipboard does not contain a valid FEN");
        else if (mode_ == GameMode::HUMAN_VS_ENGINE && game_.result == GameResult::ONGOING && !game_.moves.empty())
            app_.toast("Finish or reset the game first");
        else {
            std::string prob = enginePositionProblem(p);
            if (!prob.empty() && (mode_ != GameMode::HUMAN_VS_HUMAN || ana_)) app_.toast("Engines can't play this position: " + prob);
            else if (mode_ == GameMode::ANALYSIS) openAnalysis(p);
            else newHumanGame(p);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Edit position")) app_.openEditor(shownPosition());
    if (ImGui::Button("Open PGN...")) openPgnDialog();
    ImGui::SameLine();
    if (ImGui::Button("Save PGN...")) savePgn();
}

void GameScreen::drawAnalysis(){
    Settings& st = app_.settings();
    ImGui::SeparatorText("Engine analysis");
    float em = ImGui::GetFontSize();
    ImGui::SetNextItemWidth(em * 12);
    if (ui::engineCombo("##anaEngine", app_.registry(), st.analysisEngine)){
        app_.saveSettings();
        if (ana_) startAnalysis();
    }
    ImGui::SameLine();
    if (ana_){ if (ImGui::Button("Stop analysis")) stopAnalysis(); }
    else if (ImGui::Button("Analyse")) startAnalysis();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 5);
    int mpv = st.analysisMultiPV;
    if (ImGui::SliderInt("Lines", &mpv, 1, 5)) st.analysisMultiPV = mpv;
    if (ImGui::IsItemDeactivatedAfterEdit()){ app_.saveSettings(); if (ana_) startAnalysis(); }
    ImGui::SameLine();
    if (ImGui::Checkbox("Arrows", &st.showArrows)) app_.saveSettings();
    ImGui::BeginDisabled(review_.running());
    if (ImGui::Button("Review game")) startReview();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 6);
    ImGui::InputInt("ms / position", &reviewMs_, 100, 500);
    reviewMs_ = std::max(50, std::min(reviewMs_, 60000));
    ImGui::SameLine(); ui::helpMarker("Review: the engine evaluates every position of the game and marks "
                                      "inaccuracies (?!), mistakes (?) and blunders (??).");
    if (!ana_) return;
    if (!ana_->alive()){ ImGui::TextColored(ui::kBad, "The analysis engine terminated."); return; }
    Position shown = shownPosition();
    if (ana_->fen() != shown.toFEN()){ ImGui::TextDisabled("waiting for %s...", anaEngine_.c_str()); return; }
    EngineInfo inf = ana_->info();
    auto pvs = ana_->pvs();
    ImGui::PushFont(ui::fonts().mono);
    for (auto& L : pvs){
        if (!L.valid) continue;
        int cpW = shown.whiteToMove ? L.scoreCp : -L.scoreCp;
        int mateW = shown.whiteToMove ? L.mateIn : -L.mateIn;
        std::string sc = formatScore(cpW, L.isMate, mateW);
        if (L.lowerbound) sc += "+"; else if (L.upperbound) sc += "-";
        ImGui::TextColored(ui::kGood, "%7s", sc.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("d%-2d", L.depth);
        ImGui::SameLine();
        ImGui::TextUnformatted(pvToSan(shown, L.pv).c_str());
    }
    char stats[200];
    std::snprintf(stats, sizeof stats, "depth %d/%d  nodes %lld  nps %lld  hash %.1f%%  tbhits %lld",
                  inf.depth, inf.seldepth, inf.nodes, inf.nps, inf.hashfull < 0 ? 0.0 : inf.hashfull / 10.0, inf.tbhits);
    ImGui::TextDisabled("%s", stats);
    ImGui::PopFont();
}

void GameScreen::drawPromotionPopup(){
    if (pendingPromotion_ && !ImGui::IsPopupOpen("Promote to")) ImGui::OpenPopup("Promote to");
    if (ImGui::BeginPopupModal("Promote to", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        static const int kinds[4] = {WHITE_QUEEN, WHITE_ROOK, WHITE_BISHOP, WHITE_KNIGHT};
        static const char* names[4] = {"##Queen", "##Rook", "##Bishop", "##Knight"};   // images, no text
        float cell = ImGui::GetFontSize() * 4;
        bool white = game_.pos.whiteToMove;
        for (int i = 0; i < 4; i++){
            if (i) ImGui::SameLine();
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            if (ImGui::Button(names[i], ImVec2(cell, cell)) && pendingPromotion_){
                Move m = *pendingPromotion_; m.promo = (int8_t)kinds[i];
                pendingPromotion_.reset();
                Move real;
                if (game_.pos.uciToMove(game_.pos.moveToUCI(m), real)) humanCommitMove(real);
                ImGui::CloseCurrentPopup();
            }
            app_.pieces().draw(ImGui::GetWindowDrawList(), white ? kinds[i] : -kinds[i],
                               ImVec2(p0.x + 4, p0.y + 4), ImVec2(p0.x + cell - 4, p0.y + cell - 4));
        }
        if (ImGui::Button("Cancel")){ pendingPromotion_.reset(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

void GameScreen::drawNewGameDialog(){
    Settings& st = app_.settings();
    if (openNewGame_){ ImGui::OpenPopup("New game"); openNewGame_ = false; }
    if (!ImGui::BeginPopupModal("New game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    int mode = (int)dlgMode_;
    ImGui::RadioButton("Human vs human", &mode, (int)GameMode::HUMAN_VS_HUMAN); ImGui::SameLine();
    ImGui::RadioButton("Play vs engine", &mode, (int)GameMode::HUMAN_VS_ENGINE); ImGui::SameLine();
    ImGui::RadioButton("Analysis board", &mode, (int)GameMode::ANALYSIS);
    dlgMode_ = (GameMode)mode;
    float em = ImGui::GetFontSize();
    if (dlgMode_ == GameMode::HUMAN_VS_ENGINE){
        ImGui::SeparatorText("Opponent");
        ImGui::SetNextItemWidth(em * 20);
        if (ui::engineCombo("Engine", app_.registry(), st.opponentEngine)) app_.saveSettings();
        ImGui::RadioButton("I play White", &dlgColor_, 0); ImGui::SameLine();
        ImGui::RadioButton("I play Black", &dlgColor_, 1); ImGui::SameLine();
        ImGui::RadioButton("Random", &dlgColor_, 2);
        const EngineConfig* c = ui::findEngine(app_.registry(), st.opponentEngine);
        if (c){ const std::string* p = c->option("Ponder");
                ImGui::TextDisabled("Pondering: %s (set the engine's Ponder option in Engines)",
                                    p && *p == "true" ? "on" : "off"); }
    }
    if (dlgMode_ != GameMode::ANALYSIS){
        ImGui::SeparatorText("Time control");
        for (int i = 0; i < (int)(sizeof kTimeControls / sizeof kTimeControls[0]); i++){
            if (i) ImGui::SameLine();
            bool sel = st.tcBaseMs == kTimeControls[i].baseMs && st.tcIncMs == kTimeControls[i].incMs;
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(kTimeControls[i].label)){ st.tcBaseMs = kTimeControls[i].baseMs; st.tcIncMs = kTimeControls[i].incMs; }
            if (sel) ImGui::PopStyleColor();
        }
        float baseMin = st.tcBaseMs / 60000.f, incS = st.tcIncMs / 1000.f;
        ImGui::SetNextItemWidth(em * 6);
        if (ImGui::InputFloat("minutes", &baseMin, 1, 5, "%.1f")) st.tcBaseMs = (int)(std::max(0.1f, baseMin) * 60000);
        ImGui::SameLine(); ImGui::SetNextItemWidth(em * 6);
        if (ImGui::InputFloat("s increment", &incS, 1, 5, "%.1f")) st.tcIncMs = (int)(std::max(0.f, incS) * 1000);
    }
    ImGui::SeparatorText("Start position");
    ImGui::RadioButton("Standard", &dlgStart_, 0); ImGui::SameLine();
    ImGui::RadioButton(dlgPreset_ ? "From the editor" : "Current position", &dlgStart_, 1); ImGui::SameLine();
    ImGui::RadioButton("FEN", &dlgStart_, 2);
    if (dlgStart_ == 2){ ImGui::SetNextItemWidth(em * 30); ImGui::InputTextWithHint("##dlgfen", "paste a FEN", &dlgFen_); }
    Position start = Position::startpos();
    bool ok = true;
    if (dlgStart_ == 1) start = dlgPreset_ ? *dlgPreset_ : shownPosition();
    if (dlgStart_ == 2){ start = Position::fromFEN(dlgFen_, &ok); if (!ok) ImGui::TextColored(ui::kBad, "Invalid FEN"); }
    std::string prob = ok && dlgMode_ != GameMode::HUMAN_VS_HUMAN ? enginePositionProblem(start) : "";
    if (!prob.empty()) ImGui::TextColored(ui::kWarn, "Engines can't use this position: %s", prob.c_str());
    ImGui::Separator();
    const EngineConfig* opp = ui::findEngine(app_.registry(), st.opponentEngine);
    bool canStart = ok && prob.empty() && (dlgMode_ != GameMode::HUMAN_VS_ENGINE || opp);
    ImGui::BeginDisabled(!canStart);
    if (ImGui::Button("Start", ImVec2(em * 8, 0))){
        app_.saveSettings();
        if (dlgMode_ == GameMode::HUMAN_VS_HUMAN) newHumanGame(start);
        else if (dlgMode_ == GameMode::ANALYSIS) openAnalysis(start);
        else {
            bool white = dlgColor_ == 0 || (dlgColor_ == 2 && std::random_device{}() % 2 == 0);
            newEngineGame(*opp, start, white);
        }
        dlgPreset_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(em * 8, 0))){ dlgPreset_.reset(); ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}

// =============================================================== PGN
void GameScreen::openPgnDialog(){
    openPgn_ = true;
}

bool GameScreen::loadPgnText(const std::string& text, const std::string& source){
    auto games = parsePGN(text);
    if (games.empty()){ app_.toast("No games found in " + source); return false; }
    if (games.size() == 1){
        Game g; std::string err;
        bool ok = gameFromPGN(games[0], g, &err);
        if (!ok && g.moves.empty()){ app_.toast("Cannot load the game: " + err); return false; }
        openGame(g);
        app_.toast(ok ? "Loaded " + g.whiteName + " vs " + g.blackName : "Loaded partially: " + err);
        return true;
    }
    pgnGames_ = games;
    pgnFilter_.clear();
    openPgnList_ = true;
    return true;
}

void GameScreen::drawPgnDialogs(){
    Settings& st = app_.settings();
    float em = ImGui::GetFontSize();
    if (openPgn_){ ImGui::OpenPopup("Open PGN"); openPgn_ = false; }
    if (ImGui::BeginPopupModal("Open PGN", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextUnformatted("Open a .pgn file (multi-game files are fine) or paste PGN text.");
        ImGui::SetNextItemWidth(em * 32);
        ui::pathInput("pgnpath", pgnPath_, "path to a .pgn file", [&](){
            std::string p = filedialog::openFile("Open PGN", st.lastPgnDir, {"PGN files", "*.pgn", "All files", "*"});
            return p; });
        bool done = false;
        if (ImGui::Button("Load file")){
            std::ifstream f(fs::u8path(pgnPath_));
            if (!f) app_.toast("Cannot open " + pgnPath_);
            else {
                std::stringstream ss; ss << f.rdbuf();
                st.lastPgnDir = fs::u8path(pgnPath_).parent_path().u8string(); app_.saveSettings();
                done = loadPgnText(ss.str(), fs::u8path(pgnPath_).filename().u8string());
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Paste PGN from clipboard")){
            const char* c = ImGui::GetClipboardText();
            done = loadPgnText(c ? c : "", "the clipboard");
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) done = true;
        if (done) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (openPgnList_){ ImGui::OpenPopup("Choose a game"); openPgnList_ = false; }
    ImGui::SetNextWindowSize(ImVec2(em * 44, em * 28), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Choose a game")){
        ImGui::Text("%d games.", (int)pgnGames_.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(em * 16);
        ImGui::InputTextWithHint("##filter", "filter by player or event", &pgnFilter_);
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        if (ImGui::BeginTable("pgnlist", 5, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)){
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, em * 3);
            ImGui::TableSetupColumn("White"); ImGui::TableSetupColumn("Black");
            ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, em * 4);
            ImGui::TableSetupColumn("Event");
            ImGui::TableHeadersRow();
            auto has = [&](const std::string& s){
                if (pgnFilter_.empty()) return true;
                std::string a = s, b = pgnFilter_;
                for (auto& c : a) c = (char)std::tolower((unsigned char)c);
                for (auto& c : b) c = (char)std::tolower((unsigned char)c);
                return a.find(b) != std::string::npos;
            };
            for (int i = 0; i < (int)pgnGames_.size(); i++){
                const PgnGame& g = pgnGames_[i];
                if (!has(g.white) && !has(g.black) && !has(g.event)) continue;
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                char id[16]; std::snprintf(id, sizeof id, "%d", i + 1);
                if (ImGui::Selectable(id, false, ImGuiSelectableFlags_SpanAllColumns)){
                    Game gm; std::string err;
                    bool ok = gameFromPGN(g, gm, &err);
                    if (!ok && gm.moves.empty()) app_.toast("Cannot load the game: " + err);
                    else { openGame(gm); if (!ok) app_.toast("Loaded partially: " + err); ImGui::CloseCurrentPopup(); }
                }
                ImGui::TableNextColumn(); ImGui::TextUnformatted(g.white.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(g.black.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(g.result.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(g.event.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::EndPopup();
    }
}

void GameScreen::savePgn(){
    Settings& st = app_.settings();
    std::string path = filedialog::saveFile("Save game as PGN",
        (st.lastSaveDir.empty() ? std::string("games") : st.lastSaveDir) + "/game_" + ui::timeStamp() + ".pgn",
        {"PGN files", "*.pgn"});
    if (path.empty()){
        if (filedialog::available()) return;                 // cancelled
        std::error_code ec; fs::create_directories("games", ec);
        path = "games/game_" + ui::timeStamp() + ".pgn";
    } else { st.lastSaveDir = fs::u8path(path).parent_path().u8string(); app_.saveSettings(); }
    std::ofstream f(fs::u8path(path));
    f << game_.toPGN();
    app_.toast(f ? "Saved " + path : "Could not write " + path);
}
