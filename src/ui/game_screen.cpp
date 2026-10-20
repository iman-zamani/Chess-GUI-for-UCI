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
#include <thread>

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

// =============================================================== entry points
void GameScreen::resetBoardState(){
    oppStats_.clear();
    selectedSq_ = -1; legalForSelected_.clear();
    pendingPromotion_.reset(); cur_ = tree_.mainLineEnd();
    premoveFrom_ = premoveTo_ = -1;
    scrollMoves_ = true;
}

void GameScreen::newHumanGame(const Position& p){
    stopOpponent(); stopAnalysis();
    mode_ = GameMode::HUMAN_VS_HUMAN;
    game_.reset(p);
    game_.whiteName = "White"; game_.blackName = "Black";
    tree_.reset(p); tree_.white = game_.whiteName; tree_.black = game_.blackName; tree_.tags.clear();
    resetBoardState();
    tc_ = TimeControl{app_.settings().tcBaseMs, app_.settings().tcIncMs};
    wClockMs_ = bClockMs_ = tc_.baseMs;
    clocksRunning_ = true;
    lastTickMs_ = steadyMs();
}

bool GameScreen::newEngineGame(const EngineConfig& cfg, const Position& p, bool humanWhite){
    stopOpponent(); stopAnalysis();
    opp_ = std::make_unique<UciEngine>();
    opp_->setRole("Opponent");
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
    tree_.reset(p); tree_.white = game_.whiteName; tree_.black = game_.blackName; tree_.tags.clear();
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
    tree_.reset(p); tree_.white = game_.whiteName; tree_.black = game_.blackName; tree_.tags.clear();
    resetBoardState();
    clocksRunning_ = false;
}

void GameScreen::openGame(const GameTree& t){
    stopOpponent();
    mode_ = GameMode::ANALYSIS;
    tree_ = t;
    game_ = tree_.lineGame(tree_.mainLineEnd());
    resetBoardState();
    cur_ = 0;                                      // open at the start; step through with ->
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
    return clocksRunning_ || oppState_ != 0 || ana_ != nullptr || review_.running() || board_.animating();
}

// =============================================================== game flow
Position GameScreen::shownPosition() const{
    return atLiveEnd() ? game_.pos : tree_.at(cur_).pos;
}

bool GameScreen::canBranch() const{
    return mode_ == GameMode::ANALYSIS || game_.result != GameResult::ONGOING;
}

void GameScreen::goTo(int node){
    if (node < 0 || node >= tree_.size() || !tree_.at(node).alive) return;
    cur_ = node;
    selectedSq_ = -1; legalForSelected_.clear();
    scrollMoves_ = true;
}

void GameScreen::syncMainLine(){
    Game g = tree_.lineGame(tree_.mainLineEnd());
    g.whiteName = game_.whiteName; g.blackName = game_.blackName;
    if (game_.result != GameResult::ONGOING && g.result == GameResult::ONGOING && g.moves.size() == game_.moves.size()){
        g.result = game_.result; g.reason = game_.reason;   // resignation, time: not in the moves
    }
    game_ = g;
}

void GameScreen::branchMove(Move m){
    int n = tree_.add(cur_, m);
    if (tree_.onMainLine(n)) syncMainLine();       // extended the main line (analysis)
    goTo(n);
}

void GameScreen::addLine(const std::vector<Move>& line){
    if (line.empty()) return;
    bool liveGame = mode_ != GameMode::ANALYSIS && game_.result == GameResult::ONGOING;
    if (liveGame){ app_.toast("Lines can be added in analysis mode or once the game is over"); return; }
    int n = cur_;
    for (const Move& m : line) n = tree_.add(n, m);
    if (tree_.onMainLine(n)) syncMainLine();
    goTo(n);
}

bool GameScreen::humanToMove() const{
    return mode_ != GameMode::HUMAN_VS_ENGINE || game_.pos.whiteToMove == humanIsWhite_;
}

void GameScreen::commitMove(Move m){
    int end = tree_.mainLineEnd();
    if (!game_.tryMove(m)) return;
    int n = tree_.add(end, m);
    if (clocksRunning_){
        if (!game_.pos.whiteToMove) wClockMs_ += tc_.incMs; else bClockMs_ += tc_.incMs;
        tree_.at(n).clockMs = game_.pos.whiteToMove ? bClockMs_ : wClockMs_;   // the mover's clock
    }
    selectedSq_ = -1; legalForSelected_.clear(); cur_ = n;
    if (game_.result != GameResult::ONGOING) clocksRunning_ = false;
    scrollMoves_ = true;
}

void GameScreen::humanCommitMove(Move m){
    if (!atLiveEnd()){ branchMove(m); return; }
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
    if (sq < 0 || pendingPromotion_) return;
    bool live = atLiveEnd();
    if (live && (game_.result != GameResult::ONGOING || !humanToMove())) return;
    if (!live && !canBranch()) return;             // a game in progress: history is view-only
    Position pos = shownPosition();
    // castling: onto the rook (Chess960 style) or the king's target square
    for (const Move& m : legalForSelected_)
        if (m.isCastle && m.to != sq && pos.castleKingTarget(m) == sq && sq != m.from){
            bool plain = false;
            for (const Move& o : legalForSelected_) if (!o.isCastle && o.to == sq) plain = true;
            if (!plain){ humanCommitMove(m); return; }
        }
    for (const Move& m : legalForSelected_){
        if (m.to == sq){
            if (m.promo){ Move base = m; base.promo = 0; pendingPromotion_ = base; return; }
            humanCommitMove(m);
            return;
        }
    }
    int v = pos.board[sq];
    if (v && (v > 0) == pos.whiteToMove){
        selectedSq_ = sq;
        std::vector<Move> all; pos.generateLegal(all);
        legalForSelected_.clear();
        for (auto& m : all) if (m.from == sq) legalForSelected_.push_back(m);
    } else { selectedSq_ = -1; legalForSelected_.clear(); }
}

void GameScreen::tryExecutePremove(){
    if (premoveFrom_ < 0 || premoveTo_ < 0){ premoveFrom_ = premoveTo_ = -1; return; }
    int from = premoveFrom_, to = premoveTo_;
    premoveFrom_ = premoveTo_ = -1;
    if (game_.result != GameResult::ONGOING || !atLiveEnd() || !humanToMove()) return;
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
    int ply = (int)game_.moves.size();
    bool white = game_.pos.whiteToMove;
    oppThread_ = std::thread([this, startFen, moves, wc, bc, inc, ply, white](){
        opp_->setPosition(startFen, moves);
        auto t0 = std::chrono::steady_clock::now();
        std::string bm = opp_->goClock((int)wc, (int)bc, inc, inc);
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        { std::lock_guard<std::mutex> lk(oppMx_);
          oppMoveReady_ = bm.empty() ? "(none)" : bm;
          oppMoveStat_ = MoveStat::from(opp_->lastInfo(), ply, white, ms); }
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
    int ply = (int)game_.moves.size() + 1;                // the reply to the predicted move
    bool white = !game_.pos.whiteToMove;
    oppThread_ = std::thread([this, startFen, moves, wc, bc, inc, ply, white](){
        opp_->setPosition(startFen, moves);
        auto t0 = std::chrono::steady_clock::now();
        std::string bm = opp_->goPonderWait((int)wc, (int)bc, inc, inc);
        if (oppDiscard_){ oppState_ = 0; return; }        // ponder miss: the result is garbage
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        { std::lock_guard<std::mutex> lk(oppMx_);
          oppMoveReady_ = bm.empty() ? "(none)" : bm;
          oppMoveStat_ = MoveStat::from(opp_->lastInfo(), ply, white, ms); }   // includes pondering time
        oppState_ = 0;
    });
}

void GameScreen::applyEngineMoveIfReady(){
    std::string bm;
    MoveStat stat;
    { std::lock_guard<std::mutex> lk(oppMx_); bm = oppMoveReady_; oppMoveReady_.clear(); stat = oppMoveStat_; }
    if (bm.empty()) return;
    Move m;
    if (bm != "(none)" && game_.pos.uciToMove(bm, m)){
        stat.ply = (int)game_.moves.size();
        oppStats_.push_back(stat);
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
    applyReview();
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
    std::vector<int> nodes = tree_.mainLine();
    if (nodes.empty()){ app_.toast("No moves to review"); return; }
    const EngineConfig* c = ui::findEngine(app_.registry(), app_.settings().analysisEngine);
    if (!c){ app_.toast("Pick an analysis engine first"); return; }
    EngineConfig cfg = *c;
    cfg.setOption("MultiPV", "2");
    cfg.setOption("Threads", "1");
    std::string startFen = tree_.start().toFEN();
    std::vector<std::string> moves;
    std::vector<std::string> sans;
    for (int n : nodes){ moves.push_back(tree_.at(tree_.at(n).parent).pos.moveToUCI(tree_.at(n).move)); sans.push_back(tree_.at(n).san); }
    int startNo = tree_.start().fullmoveNumber;
    bool startWhite = tree_.start().whiteToMove;
    int ms = reviewMs_;
    int threads = reviewThreads_ > 0 ? reviewThreads_ : std::max(1, std::min(4, (int)std::thread::hardware_concurrency() / 2));
    { std::lock_guard<std::mutex> lk(reviewMx_); reviewOut_ = ReviewOut{}; }
    reviewSummary_.clear();
    review_.start("Game review: " + game_.whiteName + " vs " + game_.blackName,
                  [this, cfg, startFen, moves, sans, nodes, startNo, startWhite, ms, threads](ui::Job& j){
        int N = (int)moves.size();
        // per position (0..N): best and second-best score for the side to move, and the best move
        std::vector<int> best(N + 1, 0), second(N + 1, 0), mate(N + 1, 0);
        std::vector<bool> hasSecond(N + 1, false), isMate(N + 1, false), ok(N + 1, false);
        std::vector<std::string> bestMove(N + 1);
        std::atomic<int> done{0};
        std::string engineName;
        std::mutex nameMx;
        auto sc = [](const PvLine& l){ return l.isMate ? (l.mateIn > 0 ? 32000 - l.mateIn : -32000 - l.mateIn) : l.scoreCp; };
        auto worker = [&](int k){
            UciEngine e;
            e.setRole("Review " + std::to_string(k + 1));
            if (!e.start(cfg)){ j.log("FAIL: cannot start engine: " + e.lastError()); return; }
            { std::lock_guard<std::mutex> lk(nameMx); engineName = e.name(); }
            e.newGame();
            for (int i = k; i <= N && !j.stopRequested() && e.alive(); i += threads){
                e.setPosition(startFen, std::vector<std::string>(moves.begin(), moves.begin() + i));
                e.goMovetime(ms);
                auto pvs = e.lastPvs();
                if (!pvs.empty() && pvs[0].valid){
                    best[i] = sc(pvs[0]); isMate[i] = pvs[0].isMate; mate[i] = pvs[0].mateIn;
                    bestMove[i] = pvs[0].firstMove(); ok[i] = true;
                    if (pvs.size() > 1 && pvs[1].valid){ second[i] = sc(pvs[1]); hasSecond[i] = true; }
                }
                int d = ++done;
                if (d % 10 == 0) j.log(std::to_string(d) + " / " + std::to_string(N + 1) + " positions");
            }
            e.quit();
        };
        j.log(std::to_string(N + 1) + " positions, " + std::to_string(ms) + " ms each, " + std::to_string(threads)
              + " engine" + (threads > 1 ? "s" : "") + " in parallel");
        std::vector<std::thread> pool;
        for (int k = 0; k < threads; k++) pool.emplace_back(worker, k);
        for (auto& t : pool) t.join();
        if (j.stopRequested()){ j.log("Stopped."); return; }
        ReviewOut out;
        out.nodes = nodes;
        out.engine = engineName;
        out.cpW.assign(N + 1, 0); out.mateW.assign(N + 1, 0); out.isMate.assign(N + 1, false); out.nag.assign(N, 0);
        bool wtm = startWhite;
        for (int i = 0; i <= N; i++){                          // White's point of view of each position
            out.cpW[i] = wtm ? best[i] : -best[i];
            out.isMate[i] = isMate[i];
            out.mateW[i] = wtm ? mate[i] : -mate[i];
            wtm = !wtm;
        }
        auto win = [](int cp){ return cpToWinPct(std::max(-1500, std::min(1500, cp))); };
        double accW = 0, accB = 0; int cw = 0, cb = 0;
        int cnt[7] = {0};
        int num = startNo; wtm = startWhite;
        for (int i = 0; i < N; i++){
            if (!ok[i] || !ok[i + 1]){ if (!wtm) num++; wtm = !wtm; continue; }
            double before = win(best[i]), after = win(-best[i + 1]);   // the mover's winning chances
            double drop = std::max(0.0, before - after);
            int nag = reviewMark(before, after, moves[i] == bestMove[i], hasSecond[i], hasSecond[i] ? win(second[i]) : 0);
            out.nag[i] = nag;
            cnt[nag]++;
            double acc = moveAccuracyFromWinDrop(drop);
            if (wtm){ accW += acc; cw++; } else { accB += acc; cb++; }
            char b[200];
            int whiteCp = out.cpW[i + 1];
            static const char* sym[7] = {"", " !", " ?", " !!", " ??", "", " ?!"};
            std::snprintf(b, sizeof b, "%d.%s %-8s %+.2f  win %% %.0f -> %.0f%s", num, wtm ? "" : "..", sans[i].c_str(),
                          whiteCp / 100.0, before, after, sym[nag]);
            j.log(b);
            if (!wtm) num++;
            wtm = !wtm;
        }
        char b[300];
        std::snprintf(b, sizeof b, "Accuracy: White %.1f%%, Black %.1f%%.  Blunders ?? %d, mistakes ? %d, inaccuracies ?! %d, "
                      "good moves ! %d, !! %d", cw ? accW / cw : 0.0, cb ? accB / cb : 0.0, cnt[4], cnt[2], cnt[6], cnt[1], cnt[3]);
        out.summary = b;
        j.log(std::string(">>> ") + b);
        out.ready = true;
        std::lock_guard<std::mutex> lk(reviewMx_);
        reviewOut_ = out;
    });
}

void GameScreen::applyReview(){
    ReviewOut r;
    { std::lock_guard<std::mutex> lk(reviewMx_); if (!reviewOut_.ready) return; r = reviewOut_; reviewOut_.ready = false; }
    if (tree_.mainLine() != r.nodes){ app_.toast("The game changed during the review; results not applied"); return; }
    auto set = [&](GameNode& n, int i){
        n.hasEval = true; n.evalMate = r.isMate[i]; n.mateIn = r.mateW[i]; n.evalCp = r.cpW[i];
    };
    set(tree_.at(0), 0);
    for (size_t i = 0; i < r.nodes.size(); i++){
        GameNode& n = tree_.at(r.nodes[i]);
        set(n, (int)i + 1);
        n.nags.erase(std::remove_if(n.nags.begin(), n.nags.end(), [](int g){ return g >= 1 && g <= 6; }), n.nags.end());
        if (r.nag[i]) n.nags.insert(n.nags.begin(), r.nag[i]);
    }
    tree_.tags["Annotator"] = r.engine + " (Chess-GUI-for-UCI review)";
    reviewSummary_ = r.summary;
}

// =============================================================== drawing
void GameScreen::handleKeys(){
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) flipped_ = !flipped_;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && cur_ > 0) goTo(tree_.at(cur_).parent);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !tree_.at(cur_).children.empty()) goTo(tree_.at(cur_).children[0]);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false) || ImGui::IsKeyPressed(ImGuiKey_Home, false)) goTo(0);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false) || ImGui::IsKeyPressed(ImGuiKey_End, false)) goTo(tree_.lineEnd(cur_));
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) savePgn();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) openPgnDialog();
}

void GameScreen::draw(){
    handleKeys();
    Settings& st = app_.settings();
    float em = ImGui::GetFontSize();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float barW = ana_ ? em * 1.2f : 0;
    float split = (float)std::max(0.30, std::min(0.80, st.boardSplit));
    float side = std::min(avail.y, avail.x * split - barW);
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
    if (cur_ > 0){ spec.lastFrom = tree_.at(cur_).move.from; spec.lastTo = tree_.at(cur_).move.to; }
    bool live = atLiveEnd() && game_.result == GameResult::ONGOING;
    bool editable = live || (!atLiveEnd() && canBranch()) || (atLiveEnd() && mode_ == GameMode::ANALYSIS);
    if (editable){ spec.selected = selectedSq_; spec.targets = legalForSelected_; }
    if (premoveFrom_ >= 0) spec.tints.push_back({premoveFrom_, IM_COL32(220,60,60,70)});
    if (premoveTo_ >= 0) spec.tints.push_back({premoveTo_, IM_COL32(220,60,60,150)});
    spec.draggable.assign(64, false);
    if (editable && !pendingPromotion_){
        bool mine = live && mode_ == GameMode::HUMAN_VS_ENGINE ? humanIsWhite_ : shown.whiteToMove;
        for (int sq = 0; sq < 64; sq++){
            int v = shown.board[sq];
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
    spec.animateMs = st.moveAnimationMs;
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

    // ---- splitter: drag to resize the board ----
    ImGui::SameLine(0, em * 0.2f);
    ImVec2 sp = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("board-splitter", ImVec2(em * 0.5f, std::max(side, em * 4)));
    bool spHot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (spHot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.x != 0)
        st.boardSplit = std::max(0.30, std::min(0.80, st.boardSplit + ImGui::GetIO().MouseDelta.x / std::max(1.f, avail.x)));
    if (ImGui::IsItemDeactivated()) app_.saveSettings();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(sp.x + em * 0.25f, sp.y + side * 0.4f), ImVec2(sp.x + em * 0.25f, sp.y + side * 0.6f),
                                        ImGui::GetColorU32(spHot ? ui::kAccent : ui::kDim), 2);

    // ---- side panel ----
    ImGui::SameLine();
    ImGui::BeginChild("game-side", ImVec2(0, 0));
    drawPlayers(true);
    // room below the move list: nav, status, player, 3 control rows, analysis header/rows, lines
    float rowH = ImGui::GetFrameHeightWithSpacing();
    float lineH = ImGui::GetTextLineHeightWithSpacing();
    float reserve = rowH * 8.5f + ui::fonts().large->FontSize * 1.3f
                  + (ana_ ? lineH * (std::max(1, st.analysisMultiPV) + 2) : 0)
                  + (mode_ == GameMode::HUMAN_VS_ENGINE ? rowH : 0)
                  + (statsOpen_ ? lineH * 2 + em * 7 + rowH : 0)
                  + (reviewSummary_.empty() ? 0 : lineH * 2.2f);
    { int evals = 0; for (int n : tree_.mainLine()) evals += tree_.at(n).hasEval;
      if (evals >= 1) reserve += em * 4 + ImGui::GetStyle().ItemSpacing.y * 2; }
    drawMoveList(std::max(em * 6, ImGui::GetContentRegionAvail().y - reserve));
    // status line
    std::string status;
    if (game_.result != GameResult::ONGOING && atLiveEnd()){
        status = game_.resultString();
        if (game_.reason != ResultReason::NONE) status += "  (" + resultReasonText(game_.reason) + ")";
    } else if (!atLiveEnd())
        status = (tree_.onMainLine(cur_) ? "Viewing move " : "Variation, move ") + std::to_string(tree_.depth(cur_))
               + (canBranch() ? "" : "  (Down arrow: back to the game)");
    else if (mode_ == GameMode::HUMAN_VS_ENGINE && !humanToMove())
        status = premoveTo_ >= 0 ? "Engine is thinking... (premove set)" : "Engine is thinking...";
    else status = game_.pos.whiteToMove ? "White to move" : "Black to move";
    ImGui::TextColored(ui::kAccent, "%s", status.c_str());
    drawPlayers(false);
    drawControls();
    drawAnalysis();
    statsOpen_ = false;
    if (mode_ == GameMode::HUMAN_VS_ENGINE && ImGui::CollapsingHeader("Engine search stats")){
        statsOpen_ = true;
        ui::searchStats("opp", oppStats_, !humanIsWhite_);
    }
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

static const char* nagSymbol(int n){
    switch (n){ case 1: return "!"; case 2: return "?"; case 3: return "!!"; case 4: return "??"; case 5: return "!?"; case 6: return "?!"; }
    return nullptr;
}
static ImVec4 nagColour(const std::vector<int>& nags){
    for (int n : nags){
        if (n == 4) return ui::kBad;
        if (n == 2) return ImVec4(0.95f, 0.60f, 0.35f, 1);
        if (n == 6) return ui::kWarn;
        if (n == 1 || n == 3) return ui::kGood;
    }
    return ImVec4(0, 0, 0, 0);
}

// Places the next item of a wrapped line of moves.
static void flowNext(float width){
    float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    float x = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
    if (x + width <= right) ImGui::SameLine();
}

void GameScreen::moveToken(int node, bool numbered){
    const GameNode& n = tree_.at(node);
    const Position& before = tree_.at(n.parent).pos;
    std::string label;
    if (before.whiteToMove) label = std::to_string(before.fullmoveNumber) + ". ";
    else if (numbered) label = std::to_string(before.fullmoveNumber) + "... ";
    label += n.san;
    for (int g : n.nags){ const char* sym = nagSymbol(g); if (sym) label += sym; }
    ImGui::PushID(node);
    ImVec2 sz = ImGui::CalcTextSize(label.c_str());
    flowNext(sz.x + ImGui::GetStyle().FramePadding.x * 2);
    ImVec4 col = nagColour(n.nags);
    bool main = tree_.onMainLine(node);
    if (col.w > 0) ImGui::PushStyleColor(ImGuiCol_Text, col);
    else if (!main) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.78f, 0.85f, 1));
    if (ImGui::Selectable(label.c_str(), node == cur_, 0, sz)) goTo(node);
    if (col.w > 0 || !main) ImGui::PopStyleColor();
    if (node == cur_ && scrollMoves_){ ImGui::SetScrollHereY(0.7f); scrollMoves_ = false; }
    if (n.hasEval && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", formatScore(n.evalCp, n.evalMate, n.mateIn).c_str());
    if (ImGui::BeginPopupContextItem("move-menu")){
        bool liveGame = mode_ != GameMode::ANALYSIS && game_.result == GameResult::ONGOING;
        ImGui::BeginDisabled(main || liveGame);
        if (ImGui::MenuItem("Make this the main line")){
            tree_.promote(node);
            Game g = tree_.lineGame(tree_.mainLineEnd());
            g.whiteName = game_.whiteName; g.blackName = game_.blackName;
            game_ = g;
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(liveGame && main);
        if (ImGui::MenuItem("Delete from here")){
            int parent = n.parent;
            bool wasMain = main;
            tree_.remove(node);
            if (wasMain){
                Game g = tree_.lineGame(tree_.mainLineEnd());
                g.whiteName = game_.whiteName; g.blackName = game_.blackName;
                game_ = g;
            }
            if (!tree_.at(cur_).alive || tree_.depth(cur_) > tree_.depth(parent)) goTo(parent);
        }
        ImGui::EndDisabled();
        if (ImGui::MenuItem("Edit comment...")){ commentNode_ = node; commentBuf_ = n.comment; openComment_ = true; }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    if (!n.comment.empty()){
        std::string c = "{" + n.comment + "}";
        flowNext(ImGui::CalcTextSize(c.c_str()).x);
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", c.c_str());
        ImGui::PopTextWrapPos();
    }
}

void GameScreen::drawVariation(int first, int nesting){
    float em = ImGui::GetFontSize();
    ImGui::Indent(em * 1.2f);
    ImGui::TextDisabled("(");
    moveToken(first, true);
    int q = first;
    bool numbered = !tree_.at(first).comment.empty();
    while (!tree_.at(q).children.empty()){
        const auto& kids = tree_.at(q).children;
        moveToken(kids[0], numbered);
        numbered = !tree_.at(kids[0]).comment.empty();
        for (size_t i = 1; i < kids.size(); i++){ drawVariation(kids[i], nesting + 1); numbered = true; }
        if (kids.size() > 1) ImGui::Dummy(ImVec2(0, 0));
        q = kids[0];
    }
    flowNext(em);
    ImGui::TextDisabled(")");
    ImGui::Unindent(em * 1.2f);
}

void GameScreen::drawMoveList(float height){
    ImGui::BeginChild("moves", ImVec2(0, height), ImGuiChildFlags_Borders);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetFontSize() * 0.3f, ImGui::GetStyle().ItemSpacing.y));
    if (!tree_.at(0).comment.empty()){ ImGui::PushTextWrapPos(0); ImGui::TextDisabled("{%s}", tree_.at(0).comment.c_str()); ImGui::PopTextWrapPos(); }
    if (tree_.at(0).children.empty()) ImGui::TextDisabled("No moves yet.");
    else {
        ImGui::Dummy(ImVec2(0, 0));
        int p = 0;
        bool numbered = true;
        while (!tree_.at(p).children.empty()){
            const auto& kids = tree_.at(p).children;
            moveToken(kids[0], numbered);
            numbered = !tree_.at(kids[0]).comment.empty();
            for (size_t i = 1; i < kids.size(); i++){ drawVariation(kids[i], 1); numbered = true; }
            if (kids.size() > 1) ImGui::Dummy(ImVec2(0, 0));   // the main line goes on below the variations
            p = kids[0];
        }
        if (game_.result != GameResult::ONGOING){ flowNext(ImGui::GetFontSize() * 3); ImGui::TextDisabled("%s", game_.resultString().c_str()); }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    // navigation
    if (ImGui::Button("|<")) goTo(0);
    ImGui::SameLine();
    if (ImGui::Button(" < ") && cur_ > 0) goTo(tree_.at(cur_).parent);
    ImGui::SameLine();
    if (ImGui::Button(" > ") && !tree_.at(cur_).children.empty()) goTo(tree_.at(cur_).children[0]);
    ImGui::SameLine();
    if (ImGui::Button(">|")) goTo(tree_.lineEnd(cur_));
    ImGui::SameLine();
    if (ImGui::Button("Flip")) flipped_ = !flipped_;
    // comment editor
    if (openComment_){ ImGui::OpenPopup("Comment"); openComment_ = false; }
    if (ImGui::BeginPopupModal("Comment", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        float em = ImGui::GetFontSize();
        ImGui::InputTextMultiline("##comment", &commentBuf_, ImVec2(em * 24, em * 5));
        if (ImGui::Button("OK")){
            if (commentNode_ >= 0 && commentNode_ < tree_.size()) tree_.at(commentNode_).comment = commentBuf_;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void GameScreen::drawControls(){
    if (ImGui::Button("New game...")) showNewGameDialog(mode_);
    ImGui::SameLine();
    ImGui::BeginDisabled(game_.moves.empty());
    if (ImGui::Button("Takeback")){
        stopOpponent();
        game_.undo(); tree_.remove(tree_.mainLineEnd());
        if (mode_ == GameMode::HUMAN_VS_ENGINE && !humanToMove()){ game_.undo(); tree_.remove(tree_.mainLineEnd()); }
        auto keep = oppStats_;
        resetBoardState();
        for (auto& s : keep) if (s.ply < (int)game_.moves.size()) oppStats_.push_back(s);
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
    ImGui::SetNextItemWidth(em * 5);
    ImGui::InputInt("ms", &reviewMs_, 100, 500);
    reviewMs_ = std::max(50, std::min(reviewMs_, 60000));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 4);
    ImGui::InputInt("engines", &reviewThreads_, 1, 1);
    reviewThreads_ = std::max(0, std::min(reviewThreads_, 16));
    ImGui::SameLine(); ui::helpMarker("Review: the engine (single-threaded, several in parallel; 0 = automatic) "
                                      "evaluates every position of the main line for the given time. Each move "
                                      "is judged by how much it lowers the mover's winning chances (Lichess "
                                      "win-% model): ?! 10, ? 20, ?\? 30 points or more. ! marks the only good "
                                      "move (the next best is 15+ points worse), !! the same with a 40+ point "
                                      "gap in a position not already won. Evaluations and marks go into the "
                                      "game, and Save PGN writes them as [%eval] and NAGs.");
    if (!reviewSummary_.empty()){ ImGui::PushTextWrapPos(0); ImGui::TextColored(ui::kGood, "%s", reviewSummary_.c_str()); ImGui::PopTextWrapPos(); }
    drawEvalGraph();
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
        // the line, one clickable move at a time: a click adds it up to there as a variation
        std::istringstream ss(L.pv);
        std::string u;
        Position p = shown;
        std::vector<Move> line;
        ImGui::PushID(L.multipv);
        for (int k = 0; k < 12 && ss >> u; k++){
            Move m;
            if (!p.uciToMove(u, m)) break;
            std::string lab = (p.whiteToMove ? std::to_string(p.fullmoveNumber) + ". "
                                             : (k == 0 ? std::to_string(p.fullmoveNumber) + "... " : "")) + p.moveToSAN(m);
            line.push_back(m);
            ImVec2 sz = ImGui::CalcTextSize(lab.c_str());
            if (k == 0) ImGui::SameLine(); else flowNext(sz.x);
            ImGui::PushID(k);
            if (ImGui::Selectable(lab.c_str(), false, 0, sz)) addLine(line);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to add the line up to here");
            ImGui::PopID();
            p.makeMove(m);
        }
        ImGui::PopID();
    }
    char stats[200];
    std::snprintf(stats, sizeof stats, "depth %d/%d  nodes %lld  nps %lld  hash %.1f%%  tbhits %lld",
                  inf.depth, inf.seldepth, inf.nodes, inf.nps, inf.hashfull < 0 ? 0.0 : inf.hashfull / 10.0, inf.tbhits);
    ImGui::TextDisabled("%s", stats);
    ImGui::PopFont();
}

// White's winning chances over the main line, from the review or [%eval] tags; a click jumps there.
void GameScreen::drawEvalGraph(){
    std::vector<int> line = tree_.mainLine();
    line.insert(line.begin(), 0);
    int have = 0;
    for (int n : line) have += tree_.at(n).hasEval;
    if (have < 2) return;
    float em = ImGui::GetFontSize();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = em * 4;
    ImGui::InvisibleButton("eval-graph", ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(40, 40, 40, 255));
    dl->AddLine(ImVec2(p0.x, p0.y + h / 2), ImVec2(p0.x + w, p0.y + h / 2), IM_COL32(90, 90, 90, 255));
    int N = (int)line.size();
    auto x = [&](int i){ return p0.x + (N > 1 ? w * i / (N - 1) : 0); };
    auto y = [&](const GameNode& n){
        double wp = n.evalMate ? (n.mateIn > 0 ? 100 : 0) : cpToWinPct(n.evalCp);
        return p0.y + h * (float)(1 - wp / 100);
    };
    std::vector<ImVec2> pts;
    for (int i = 0; i < N; i++) if (tree_.at(line[i]).hasEval) pts.push_back(ImVec2(x(i), y(tree_.at(line[i]))));
    // White's share filled from the bottom
    for (size_t i = 1; i < pts.size(); i++)
        dl->AddQuadFilled(ImVec2(pts[i - 1].x, p0.y + h), pts[i - 1], pts[i], ImVec2(pts[i].x, p0.y + h), IM_COL32(230, 230, 230, 200));
    for (int i = 1; i < N; i++){
        ImVec4 c = nagColour(tree_.at(line[i]).nags);
        if (c.w > 0 && tree_.at(line[i]).hasEval)
            dl->AddCircleFilled(ImVec2(x(i), y(tree_.at(line[i]))), em * 0.18f, ImGui::GetColorU32(c));
    }
    auto it = std::find(line.begin(), line.end(), cur_);
    if (it != line.end()){ float cx = x((int)(it - line.begin())); dl->AddLine(ImVec2(cx, p0.y), ImVec2(cx, p0.y + h), ImGui::GetColorU32(ui::kAccent), 2); }
    if (ImGui::IsItemHovered()){
        int i = N > 1 ? (int)std::lround((ImGui::GetIO().MousePos.x - p0.x) / w * (N - 1)) : 0;
        i = std::max(0, std::min(N - 1, i));
        const GameNode& n = tree_.at(line[i]);
        if (n.hasEval) ImGui::SetTooltip("%s%s  %s", i ? (std::to_string(tree_.at(n.parent).pos.fullmoveNumber)
                                         + (tree_.at(n.parent).pos.whiteToMove ? ". " : "... ")).c_str() : "", i ? n.san.c_str() : "start",
                                         formatScore(n.evalCp, n.evalMate, n.mateIn).c_str());
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) goTo(line[i]);
    }
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
    ImGui::RadioButton("FEN", &dlgStart_, 2); ImGui::SameLine();
    ImGui::RadioButton("Chess960", &dlgStart_, 3);
    if (dlgStart_ == 2){ ImGui::SetNextItemWidth(em * 30); ImGui::InputTextWithHint("##dlgfen", "paste a FEN (X-FEN / Shredder-FEN for Chess960)", &dlgFen_); }
    if (dlgStart_ == 3){
        ImGui::SetNextItemWidth(em * 8);
        if (ImGui::InputInt("position number (0-959)", &dlg960_)) dlg960_ = std::clamp(dlg960_, 0, 959);
        ImGui::SameLine();
        if (ImGui::Button("Random")) dlg960_ = (int)(std::random_device{}() % 960);
        ImGui::SameLine(); ImGui::TextDisabled("%s", Position::chess960Start(dlg960_).toFEN().substr(0, 8).c_str());
        ImGui::TextDisabled("Castle by moving the king onto its rook or to its target square.");
    }
    Position start = Position::startpos();
    bool ok = true;
    if (dlgStart_ == 1) start = dlgPreset_ ? *dlgPreset_ : shownPosition();
    if (dlgStart_ == 2){ start = Position::fromFEN(dlgFen_, &ok); if (!ok) ImGui::TextColored(ui::kBad, "Invalid FEN"); }
    if (dlgStart_ == 3) start = Position::chess960Start(dlg960_);
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
        GameTree t; std::string err;
        bool ok = gameTreeFromPGN(games[0], t, &err);
        if (!ok && t.mainLine().empty()){ app_.toast("Cannot load the game: " + err); return false; }
        openGame(t);
        app_.toast(ok ? "Loaded " + t.white + " vs " + t.black : "Loaded partially: " + err);
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
                    GameTree gt; std::string err;
                    bool ok = gameTreeFromPGN(g, gt, &err);
                    if (!ok && gt.mainLine().empty()) app_.toast("Cannot load the game: " + err);
                    else { openGame(gt); if (!ok) app_.toast("Loaded partially: " + err); ImGui::CloseCurrentPopup(); }
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
    tree_.white = game_.whiteName; tree_.black = game_.blackName;
    if (game_.result != GameResult::ONGOING) tree_.result = game_.resultString();
    std::ofstream f(fs::u8path(path));
    f << tree_.toPGN();
    app_.toast(f ? "Saved " + path : "Could not write " + path);
}
