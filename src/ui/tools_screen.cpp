/* Chess-GUI-for-UCI — engine testing tools screen. GPL-2.0 */
#include "ui/tools_screen.hpp"
#include "gui.hpp"
#include "testing.hpp"
#include "ui/file_dialog.hpp"
#include <misc/cpp/imgui_stdlib.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

std::string ToolsScreen::saveDialog(const std::string& stem){
    return filedialog::saveFile("Save", app_.settings().lastSaveDir + "/" + stem + "_" + ui::timeStamp() + ".txt",
                                {"Text files", "*.txt", "All files", "*"});
}

void ToolsScreen::draw(){
    float em = ImGui::GetFontSize();
    ImGui::BeginChild("tools-left", ImVec2(em * 30, 0), ImGuiChildFlags_Borders);
    if (ImGui::BeginTabBar("tooltabs")){
        if (ImGui::BeginTabItem("Move generator")){ drawPerftTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Perft bisection", nullptr, selectBisectTab_ ? ImGuiTabItemFlags_SetSelected : 0)){
            drawBisectTab(); ImGui::EndTabItem();
        }
        selectBisectTab_ = false;
        if (ImGui::BeginTabItem("UCI compliance")){ drawUciCheckTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Accuracy")){ drawAccuracyTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Elo estimate", nullptr, selectEloTab_ ? ImGuiTabItemFlags_SetSelected : 0)){
            drawEloTab(); ImGui::EndTabItem();
        }
        selectEloTab_ = false;
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("tools-out", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (view_ == View::Bisect) drawBisectResult();
    else if (view_ == View::UciCheck) drawUciCheckResult();
    else if (view_ == View::Elo) drawEloResult();
    ui::jobPanel(job_, [&](){ return saveDialog("test"); }, [&](const std::string& m){ app_.toast(m); });
    ImGui::EndChild();
}

// =============================================================== move generator
void ToolsScreen::drawPerftTab(){
    Settings& st = app_.settings();
    ImGui::TextWrapped("Checks a move generator by counting leaf nodes (perft) on positions with "
                       "verified node counts.");
    ImGui::SeparatorText("This GUI's generator");
    ImGui::BeginDisabled(busy());
    if (ImGui::Button("Run self-test")) runSelfTest();
    ImGui::EndDisabled();

    ImGui::SeparatorText("Your engine (go perft)");
    ImGui::TextWrapped("Sends 'go perft <depth>' for every position and compares the engine's "
                       "'Nodes searched: N' with the expected count.");
    ImGui::SetNextItemWidth(-1);
    if (ui::engineCombo("##perftEngine", app_.registry(), st.perftEngine)) app_.saveSettings();
    ImGui::TextUnformatted("Suite (EPD with ;D1 n ;D2 n ...)");
    ui::pathInput("suite", suitePath_, "Resources/perftsuite.epd", [&](){
        return filedialog::openFile("Open perft suite", "Resources", {"EPD files", "*.epd", "All files", "*"}); });
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
    ImGui::SliderInt("Max depth", &perftDepth_, 1, 6);
    const EngineConfig* eng = ui::findEngine(app_.registry(), st.perftEngine);
    ImGui::BeginDisabled(busy() || !eng);
    if (ImGui::Button("Run perft suite on engine") && eng) runEnginePerft(*eng);
    ImGui::EndDisabled();

    std::vector<std::pair<std::string, int>> fails;
    { std::lock_guard<std::mutex> lk(resMx_); fails = suiteFailures_; }
    if (!fails.empty()){
        ImGui::SeparatorText("Mismatches from the last run");
        ImGui::TextWrapped("Bisect one to find the exact position and move the engine gets wrong.");
        ImGui::BeginChild("fails", ImVec2(0, ImGui::GetFontSize() * 10), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < fails.size() && i < 200; i++){
            ImGui::PushID((int)i);
            ImGui::BeginDisabled(busy() || !eng);
            if (ImGui::SmallButton("Bisect") && eng){
                bisectFen_ = fails[i].first; bisectDepth_ = fails[i].second;
                selectBisectTab_ = true;
                runBisect(*eng, bisectFen_, bisectDepth_);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Text("d%d  %s", fails[i].second, fails[i].first.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
}

void ToolsScreen::runSelfTest(){
    view_ = View::Log;
    std::string suitePath = suitePath_;
    int depth = 5;
    job_.start("GUI move generator self-test (verified perft suite)", [suitePath, depth](ui::Job& j){
        auto suite = loadEpdSuite(suitePath);
        j.log(suite.empty() ? "Suite file not found: using the built-in positions"
                            : "Loaded " + suitePath + ": " + std::to_string(suite.size()) + " positions");
        if (suite.empty()) suite = builtinPerftSuite();
        bool all = runInternalPerftSelfTest(suite, depth,
            [&](int i, int n, bool pass){
                j.log("[" + std::to_string(i) + "/" + std::to_string(n) + "] " + (pass ? "OK" : "FAIL"));
            }, j.abortFlag());
        if (j.stopRequested()) j.log(">>> Aborted.");
        else j.log(all ? ">>> ALL PASSED - internal generator verified." : ">>> FAILURES FOUND.");
    });
}

void ToolsScreen::runEnginePerft(const EngineConfig& cfg){
    view_ = View::Log;
    { std::lock_guard<std::mutex> lk(resMx_); suiteFailures_.clear(); }
    std::string suitePath = suitePath_;
    int maxDepth = perftDepth_;
    job_.start("Engine move-generator test: " + cfg.name, [this, cfg, suitePath, maxDepth](ui::Job& j){
        UciEngine e;
        e.setRole("Perft test");
        if (!e.start(cfg)){ j.log("FAIL: cannot start engine: " + e.lastError()); return; }
        j.log("Engine: " + e.name());
        auto suite = loadEpdSuite(suitePath);
        j.log(suite.empty() ? "Suite file not found: using the built-in verified positions"
                            : "Loaded " + suitePath + ": " + std::to_string(suite.size()) + " positions");
        if (suite.empty()) suite = builtinPerftSuite();
        j.log("Sending 'go perft <depth>' for each position, depth <= " + std::to_string(maxDepth) + "...");
        int fails = 0;
        auto res = runEnginePerftSuite(e, suite, maxDepth,
            [&](int i, int n, const PerftResult& r){
                std::string s = "[" + std::to_string(i) + "/" + std::to_string(n) + "] d" + std::to_string(r.depth)
                    + " want " + std::to_string(r.expected) + " got "
                    + (r.engineNodes < 0 ? std::string("(no perft support)") : std::to_string(r.engineNodes))
                    + (r.pass ? "  OK" : "  FAIL");
                if (!r.pass){
                    fails++; s += "  fen: " + r.fen;
                    if (r.engineNodes >= 0){
                        std::lock_guard<std::mutex> lk(resMx_);
                        // keep the shallowest failing depth per position: the quickest bisection
                        bool known = false;
                        for (auto& f : suiteFailures_) if (f.first == r.fen) known = true;
                        if (!known) suiteFailures_.push_back({r.fen, r.depth});
                    }
                }
                j.log(s);
            }, j.abortFlag());
        if (j.stopRequested()) j.log(">>> Aborted.");
        else if (!res.empty() && res.back().engineNodes < 0)
            j.log("FAIL: the engine does not answer 'go perft N' with 'Nodes searched: X'.");
        else j.log(fails ? "FAIL: " + std::to_string(fails) + " mismatches - the move generator has bugs in those positions. "
                           "Use 'Bisect' to locate them."
                         : ">>> ALL PASSED - move generator agrees with verified node counts.");
        e.quit();
    });
}

// =============================================================== perft bisection
void ToolsScreen::drawBisectTab(){
    Settings& st = app_.settings();
    ImGui::TextWrapped("Find my movegen bug: compares your engine's perft divide ('go perft N', e.g. "
                       "'e2e4: 20' per move) with this GUI's verified generator, follows the first move "
                       "whose count differs down to the position where your engine generates a wrong "
                       "set of moves, and names the missing or illegal moves.");
    ImGui::SetNextItemWidth(-1);
    if (ui::engineCombo("##bisectEngine", app_.registry(), st.perftEngine)) app_.saveSettings();
    ImGui::TextUnformatted("Start position (FEN)");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##bisectFen", &bisectFen_);
    bool ok = false;
    Position::fromFEN(bisectFen_, &ok);
    if (!ok) ImGui::TextColored(ui::kBad, "Not a valid FEN");
    if (ImGui::SmallButton("Start position")) bisectFen_ = Position::startpos().toFEN();
    ImGui::SameLine();
    if (ImGui::SmallButton("Kiwipete")) bisectFen_ = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    ImGui::SameLine();
    if (ImGui::SmallButton("Paste")){ const char* c = ImGui::GetClipboardText(); if (c) bisectFen_ = c; }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
    ImGui::SliderInt("Depth", &bisectDepth_, 1, 7);
    ImGui::SameLine(); ui::helpMarker("The depth at which the counts differ. Higher depths take longer: the GUI "
                                      "computes the reference counts itself.");
    const EngineConfig* eng = ui::findEngine(app_.registry(), st.perftEngine);
    ImGui::BeginDisabled(busy() || !eng || !ok);
    if (ImGui::Button("Run bisection") && eng) runBisect(*eng, bisectFen_, bisectDepth_);
    ImGui::EndDisabled();
}

void ToolsScreen::runBisect(const EngineConfig& cfg, const std::string& fen, int depth){
    view_ = View::Bisect;
    { std::lock_guard<std::mutex> lk(resMx_); bisect_ = BisectReport{}; bisectDone_ = false; }
    job_.start("Perft bisection: " + cfg.name + ", depth " + std::to_string(depth), [this, cfg, fen, depth](ui::Job& j){
        UciEngine e;
        e.setRole("Perft bisection");
        if (!e.start(cfg)){
            j.log("FAIL: cannot start engine: " + e.lastError());
            std::lock_guard<std::mutex> lk(resMx_);
            bisect_.error = true; bisect_.summary = "Cannot start the engine: " + e.lastError(); bisectDone_ = true;
            return;
        }
        j.log("Engine: " + e.name() + "   start: " + fen);
        auto rep = perftBisect(e, fen, depth, [&](const BisectStep& s){
            std::string line = "d" + std::to_string(s.depth) + "  expected " + std::to_string(s.expected) + ", engine "
                             + (s.engineNodes < 0 ? std::string("?") : std::to_string(s.engineNodes));
            if (!s.chosenMove.empty()) line += "  -> into " + s.chosenMove;
            line += "   " + s.fen;
            j.log(line);
            std::lock_guard<std::mutex> lk(resMx_);
            bisect_.steps.push_back(s);
        }, j.abortFlag());
        j.log(rep.found || rep.error ? "FAIL: " + rep.summary : ">>> " + rep.summary);
        std::lock_guard<std::mutex> lk(resMx_);
        bisect_ = rep; bisectDone_ = true;
        e.quit();
    });
}

void ToolsScreen::drawBisectResult(){
    BisectReport rep; bool done;
    { std::lock_guard<std::mutex> lk(resMx_); rep = bisect_; done = bisectDone_; }
    ImGui::PushFont(ui::fonts().large);
    ImGui::TextUnformatted("Perft bisection");
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0);
    if (!done) ImGui::TextColored(ui::kWarn, "running... (%d steps so far)", (int)rep.steps.size());
    else if (rep.error) ImGui::TextColored(ui::kBad, "%s", rep.summary.c_str());
    else if (rep.engineAgrees) ImGui::TextColored(ui::kGood, "%s", rep.summary.c_str());
    else ImGui::TextColored(ui::kBad, "%s", rep.summary.c_str());
    ImGui::PopTextWrapPos();
    if (done && rep.found){
        ImGui::SeparatorText("Position");
        ImGui::PushFont(ui::fonts().mono);
        ImGui::TextUnformatted(rep.bugFen.c_str());
        ImGui::PopFont();
        if (ImGui::Button("Copy FEN")){ ImGui::SetClipboardText(rep.bugFen.c_str()); app_.toast("FEN copied"); }
        ImGui::SameLine();
        if (ImGui::Button("Open in board")){
            app_.gameScreen().openAnalysis(Position::fromFEN(rep.bugFen));
            app_.go(Screen::GAME);
        }
        Position p = Position::fromFEN(rep.bugFen);
        for (auto& u : rep.missing){
            Move m; std::string san = p.uciToMove(u, m) ? p.moveToSAN(m) : u;
            ImGui::BulletText("missing: %s (%s)%s", san.c_str(), u.c_str(),
                              m.isEnPassant ? "  en passant" : m.isCastle ? "  castling" : m.promo ? "  promotion" : "");
        }
        for (auto& u : rep.extra) ImGui::BulletText("illegal, generated anyway: %s", u.c_str());
    }
    if (!rep.steps.empty() && ImGui::BeginTable("steps", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableSetupColumn("Depth", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3);
        ImGui::TableSetupColumn("Expected / engine", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 11);
        ImGui::TableSetupColumn("Next", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4);
        ImGui::TableSetupColumn("Position");
        ImGui::TableHeadersRow();
        for (auto& s : rep.steps){
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d", s.depth);
            ImGui::TableNextColumn();
            bool same = s.engineNodes >= 0 && (uint64_t)s.engineNodes == s.expected;
            ImGui::TextColored(same ? ui::kGood : ui::kBad, "%llu / %lld", (unsigned long long)s.expected, s.engineNodes);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(s.chosenMove.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(s.fen.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::Separator();
}

// =============================================================== UCI compliance
void ToolsScreen::drawUciCheckTab(){
    Settings& st = app_.settings();
    ImGui::TextWrapped("Runs %d protocol checks against the engine (each with a fresh engine process "
                       "where it matters) and reports pass / warn / fail with an explanation and the "
                       "engine log behind every result.", (int)uciCheckIds().size());
    ImGui::SetNextItemWidth(-1);
    if (ui::engineCombo("##checkEngine", app_.registry(), st.uciCheckEngine)) app_.saveSettings();
    ImGui::Checkbox("Quick (skip the 10s+0.1s time-management game)", &quickCheck_);
    const EngineConfig* eng = ui::findEngine(app_.registry(), st.uciCheckEngine);
    ImGui::BeginDisabled(busy() || !eng);
    if (ImGui::Button("Run compliance check") && eng) runUciCheck(*eng, quickCheck_);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Takes about %s.", quickCheck_ ? "10-20 seconds" : "40-60 seconds");
}

void ToolsScreen::runUciCheck(const EngineConfig& cfg, bool quick){
    view_ = View::UciCheck;
    { std::lock_guard<std::mutex> lk(resMx_); checks_.clear(); checkEngine_ = cfg.name; }
    job_.start("UCI compliance: " + cfg.name, [this, cfg, quick](ui::Job& j){
        UciCheckOptions o;
        o.quick = quick;
        o.abort = &j.abortFlag();
        o.progress = [&](const CheckResult& r){
            std::string line = "[" + std::string(checkStatusName(r.status)) + "] " + r.title
                             + "  (" + std::to_string(r.ms) + " ms)";
            j.log(r.status == CheckStatus::Pass ? ">>> " + line : line);
            std::lock_guard<std::mutex> lk(resMx_);
            checks_.push_back(r);
        };
        auto res = runUciChecks(cfg, o);
        CheckSummary s = summarize(res);
        j.log((s.fail ? "FAIL: " : ">>> ") + std::to_string(s.pass) + " passed, " + std::to_string(s.warn) + " warnings, "
              + std::to_string(s.fail) + " failed, " + std::to_string(s.skip) + " skipped.");
    });
}

void ToolsScreen::drawUciCheckResult(){
    std::vector<CheckResult> res; std::string name;
    { std::lock_guard<std::mutex> lk(resMx_); res = checks_; name = checkEngine_; }
    ImGui::PushFont(ui::fonts().large);
    ImGui::Text("UCI compliance: %s", name.c_str());
    ImGui::PopFont();
    CheckSummary s = summarize(res);
    ImGui::TextColored(ui::kGood, "%d passed", s.pass); ImGui::SameLine();
    ImGui::TextColored(ui::kWarn, "%d warnings", s.warn); ImGui::SameLine();
    ImGui::TextColored(ui::kBad, "%d failed", s.fail); ImGui::SameLine();
    ImGui::TextDisabled("%d skipped", s.skip);
    if (job_.running()){ ImGui::SameLine(); ImGui::TextColored(ui::kWarn, "running..."); }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy report")){ ImGui::SetClipboardText(checkReportText(name, res).c_str()); app_.toast("Report copied"); }
    ImGui::SameLine();
    if (ImGui::SmallButton("Save report...")){
        std::string path = saveDialog("uci_check");
        if (!path.empty()){ std::ofstream f(path); f << checkReportText(name, res); app_.toast(f ? "Saved " + path : "Could not write " + path); }
    }
    float h = ImGui::GetContentRegionAvail().y * 0.65f;
    ImGui::BeginChild("checks", ImVec2(0, h), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < res.size(); i++){
        const CheckResult& r = res[i];
        ImVec4 col = r.status == CheckStatus::Pass ? ui::kGood : r.status == CheckStatus::Warn ? ui::kWarn
                   : r.status == CheckStatus::Fail ? ui::kBad : ui::kDim;
        ImGui::PushID((int)i);
        ImGui::TextColored(col, "%s", checkStatusName(r.status));
        ImGui::SameLine(ImGui::GetFontSize() * 3.5f);
        bool open = ImGui::TreeNodeEx(r.title.c_str(), r.status == CheckStatus::Fail ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        ImGui::SameLine(); ImGui::TextDisabled("%lld ms", r.ms);
        if (open){
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(r.detail.c_str());
            ImGui::PopTextWrapPos();
            if (!r.log.empty() && ImGui::TreeNode("Engine log for this check")){
                ImGui::PushFont(ui::fonts().mono);
                size_t from = r.log.size() > 200 ? r.log.size() - 200 : 0;
                for (size_t k = from; k < r.log.size(); k++){
                    const UciLogLine& l = r.log[k];
                    ImVec4 c = l.dir == '>' ? ImVec4(0.55f, 0.80f, 1.0f, 1) : l.dir == '!' ? ImVec4(1.0f, 0.62f, 0.35f, 1)
                             : l.dir == '#' ? ui::kDim : ImVec4(0.92f, 0.92f, 0.92f, 1);
                    ImGui::TextColored(c, "%9.3f %c %s", l.tMs / 1000.0, l.dir, l.text.c_str());
                }
                ImGui::PopFont();
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// =============================================================== accuracy
void ToolsScreen::drawAccuracyTab(){
    Settings& st = app_.settings();
    bool changed = false;
    ImGui::TextWrapped("Your engine plays each test position; a reference engine (e.g. Stockfish) scores "
                       "every choice with one MultiPV search, so good alternative moves are credited at "
                       "their true centipawn gap.");
    ImGui::TextUnformatted("Engine under test");
    ImGui::SetNextItemWidth(-1);
    changed |= ui::engineCombo("##accTest", app_.registry(), st.accTestEngine);
    ImGui::TextUnformatted("Reference engine");
    ImGui::SetNextItemWidth(-1);
    changed |= ui::engineCombo("##accRef", app_.registry(), st.accRefEngine);
    float w = ImGui::GetFontSize() * 7;
    ImGui::SetNextItemWidth(w); changed |= ImGui::InputInt("Positions", &st.accCount, 25, 100);
    ImGui::SetNextItemWidth(w); changed |= ImGui::InputInt("Engine ms / move", &st.accTestMs, 50, 250);
    ImGui::SetNextItemWidth(w); changed |= ImGui::InputInt("Reference ms", &st.accRefMs, 50, 250);
    ImGui::SetNextItemWidth(w); changed |= ImGui::SliderInt("Reference MultiPV", &st.accMultiPV, 1, 8);
    st.accCount = std::max(1, std::min(st.accCount, 1000));
    st.accTestMs = std::max(10, st.accTestMs); st.accRefMs = std::max(10, st.accRefMs);
    if (changed) app_.saveSettings();
    double secs = st.accCount * (st.accTestMs + st.accRefMs) / 1000.0;
    if (secs < 90) ImGui::TextColored(ui::kWarn, "Estimated duration: about %.0f-%.0f seconds", secs, secs * 1.4);
    else ImGui::TextColored(ui::kWarn, "Estimated duration: about %.0f-%.0f minutes", secs / 60, secs * 1.4 / 60);
    const EngineConfig* te = ui::findEngine(app_.registry(), st.accTestEngine);
    const EngineConfig* re = ui::findEngine(app_.registry(), st.accRefEngine);
    ImGui::BeginDisabled(busy() || !te || !re);
    if (ImGui::Button("Start accuracy test") && te && re) runAccuracy(*te, *re);
    ImGui::EndDisabled();
}

void ToolsScreen::runAccuracy(const EngineConfig& tee, const EngineConfig& ree){
    view_ = View::Log;
    const Settings& st = app_.settings();
    int cnt = st.accCount, tms = st.accTestMs, rms = st.accRefMs, mpv = st.accMultiPV;
    job_.start("Accuracy: " + tee.name + " vs reference " + ree.name + "  (" + std::to_string(cnt) + " positions)",
               [tee, ree, cnt, tms, rms, mpv](ui::Job& j){
        UciEngine te, re;
        te.setRole("Accuracy test"); re.setRole("Reference");
        if (!te.start(tee)){ j.log("FAIL: cannot start test engine: " + te.lastError()); return; }
        if (!re.start(ree)){ j.log("FAIL: cannot start reference engine: " + re.lastError()); return; }
        te.newGame(); re.newGame();
        auto fens = loadFenList("Resources/accuracy.epd");
        j.log(fens.empty() ? "Resources/accuracy.epd not found - using the 15 built-in positions"
                           : "Loaded Resources/accuracy.epd: " + std::to_string(fens.size()) + " positions");
        if (fens.empty()) fens = defaultAccuracyPositions();
        if ((int)fens.size() > cnt) fens.resize(cnt);
        j.log("Test engine " + std::to_string(tms) + " ms/move, reference " + std::to_string(rms)
              + " ms with MultiPV " + std::to_string(mpv) + ".");
        auto rep = runAccuracyTest(te, re, fens, tms, rms, mpv,
            [&](int i, int n, const AccuracyMoveReport& m){
                std::string s = "[" + std::to_string(i) + "/" + std::to_string(n) + "] " + m.san
                    + "  loss " + std::to_string(m.cpLoss);
                if (m.rank == 0) s += "  = best move";
                else if (m.rank > 0) s += "  = choice #" + std::to_string(m.rank + 1);
                if (m.cpLoss >= 300) s += "  ??";
                if (!m.alternatives.empty()) s += "   [also fine: " + m.alternatives + "]";
                j.log(s);
            }, j.abortFlag());
        char b[220];
        std::snprintf(b, sizeof b, ">>> %d positions | Avg cp loss: %.1f | Accuracy: %.1f%% | "
                      "blunders %d, mistakes %d, inaccuracies %d", (int)rep.moves.size(), rep.avgCpLoss,
                      rep.accuracyPct, rep.blunders, rep.mistakes, rep.inaccuracies);
        j.log(b);
        te.quit(); re.quit();
    });
}

// =============================================================== Elo estimate
bool ToolsScreen::loadEloModel(){
    if (modelTried_) return modelError_.empty();
    modelTried_ = true;
    modelPath_ = findResource("elo/model.json");
    if (modelPath_.empty()){ modelError_ = "This build has no calibrated Elo model yet (Resources/elo/model.json). "
                                       "The calibration is still being produced; see docs/elo-estimator.md."; return false; }
    if (!model_.load(modelPath_, &modelError_)) return false;
    modelError_.clear();
    return true;
}

void ToolsScreen::drawEloTab(){
    Settings& st = app_.settings();
    ImGui::TextWrapped("Estimates an engine's CCRL-Blitz-equivalent Elo from how it plays test positions. "
                       "It is an estimate from positions, not a rating from games: time management and "
                       "other game skills are not measured.");
    if (!loadEloModel()){
        ImGui::PushStyleColor(ImGuiCol_Text, ui::kWarn);
        ImGui::TextWrapped("%s", modelError_.c_str());
        ImGui::PopStyleColor();
        return;
    }
    ImGui::TextDisabled("%d positions, model v%d.", (int)model_.items.size(), model_.version);
    if (!model_.validationText.empty()) ImGui::TextWrapped("Accuracy: %s.", model_.validationText.c_str());
    ImGui::SetNextItemWidth(-1);
    if (ui::engineCombo("##eloEngine", app_.registry(), st.eloEngine)) app_.saveSettings();
    int mode = eloFull_ ? 1 : 0;
    ImGui::RadioButton("Quick (adaptive: stops once the estimate is precise enough)", &mode, 0);
    ImGui::RadioButton("Full (every position)", &mode, 1);
    eloFull_ = mode == 1;
    double minutes = (eloFull_ ? model_.items.size() : (size_t)model_.maxItems) * (model_.baseMovetimeMs + 60) / 60000.0;
    ImGui::PushStyleColor(ImGuiCol_Text, ui::kDim);
    ImGui::TextWrapped("At most %.1f minutes at the calibration machine's speed; the move time per position is "
                       "scaled to this computer's speed.", minutes);
    ImGui::PopStyleColor();
    const EngineConfig* eng = ui::findEngine(app_.registry(), st.eloEngine);
    ImGui::BeginDisabled(busy() || !eng);
    if (ImGui::Button("Estimate Elo") && eng) runElo(*eng, eloFull_);
    ImGui::EndDisabled();
    ImGui::TextDisabled("The engine runs with Threads=1 and Hash=%d MB.", model_.hashMb);
}

void ToolsScreen::runElo(const EngineConfig& cfg, bool full){
    view_ = View::Elo;
    { std::lock_guard<std::mutex> lk(resMx_); elo_ = EloResult{}; eloProgress_ = EloProgress{}; eloRunning_ = true; }
    EloModel model = model_;
    job_.start("Elo estimate: " + cfg.name + (full ? " (full)" : " (quick)"), [this, cfg, full, model](ui::Job& j){
        EloOptions o;
        o.adaptive = !full;
        o.abort = &j.abortFlag();
        j.log("Measuring this computer's speed...");
        o.progress = [&](const EloProgress& p){
            { std::lock_guard<std::mutex> lk(resMx_); eloProgress_ = p; }
            if (p.answered % 10 == 0)
                j.log(std::to_string(p.answered) + " positions: " + std::to_string((int)std::lround(p.elo))
                      + " +/- " + std::to_string((int)std::lround(1.96 * p.se)) + " (measurement only)");
        };
        EloResult r = runEloEstimate(cfg, model, o);
        if (r.ok) j.log(">>> Estimated Elo " + std::to_string((int)std::lround(r.elo)) + " +/- "
                        + std::to_string((int)std::lround(r.ci95)) + " (95%), " + std::to_string(r.answered)
                        + " positions, " + std::to_string(r.movetimeMs) + " ms each");
        else j.log("FAIL: " + r.error);
        std::lock_guard<std::mutex> lk(resMx_);
        elo_ = r;
        eloRunning_ = false;
    });
}

void ToolsScreen::drawEloResult(){
    EloResult r; EloProgress p; bool running;
    { std::lock_guard<std::mutex> lk(resMx_); r = elo_; p = eloProgress_; running = eloRunning_; }
    float em = ImGui::GetFontSize();
    double shownElo = r.ok ? r.elo : p.elo, shownCi = r.ok ? r.ci95 : 1.96 * p.se;
    ImGui::PushFont(ui::fonts().large);
    if (r.ok) ImGui::Text("Estimated Elo: %ld +/- %ld (95%%), CCRL-Blitz-equivalent", std::lround(r.elo), std::lround(r.ci95));
    else if (running && p.answered) ImGui::Text("%d / %d positions: %ld +/- %ld", p.answered, p.planned,
                                                std::lround(p.elo), std::lround(1.96 * p.se));
    else if (running) ImGui::Text("Starting...");
    else ImGui::TextColored(ui::kBad, "%s", r.error.empty() ? "No result" : r.error.c_str());
    ImGui::PopFont();
    if (running && p.planned) ImGui::ProgressBar((float)p.answered / p.planned, ImVec2(-1, em * 0.4f), "");
    if (r.ok){
        ImGui::TextDisabled("%s: %d of %d positions solved, %d ms per position (hardware factor %.2f). "
                            "The interval includes the model's measured error.", r.engineName.c_str(), r.solved,
                            r.answered, r.movetimeMs, r.factor);
        ImGui::TextColored(ui::kWarn, "An estimate from positions, not from games: time management is not measured.");
    }
    // where the engine sits among the calibration engines
    if ((r.ok || p.answered) && !model_.calibration.empty()){
        ImGui::SeparatorText("Among the calibration engines");
        double lo = 1e9, hi = -1e9;
        for (auto& c : model_.calibration){ lo = std::min(lo, c.second); hi = std::max(hi, c.second); }
        lo = std::min(lo, shownElo - shownCi) - 50; hi = std::max(hi, shownElo + shownCi) + 50;
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = em * 4;
        ImGui::InvisibleButton("elo-chart", ImVec2(w, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto x = [&](double e){ return p0.x + (float)((e - lo) / (hi - lo)) * w; };
        float axis = p0.y + h * 0.6f;
        dl->AddLine(ImVec2(p0.x, axis), ImVec2(p0.x + w, axis), ImGui::GetColorU32(ui::kDim));
        for (int t = (int)std::ceil(lo / 500) * 500; t <= hi; t += 500){
            dl->AddLine(ImVec2(x(t), axis - 3), ImVec2(x(t), axis + 3), ImGui::GetColorU32(ui::kDim));
            char b[16]; std::snprintf(b, sizeof b, "%d", t);
            dl->AddText(ImVec2(x(t) - em, axis + 4), ImGui::GetColorU32(ui::kDim), b);
        }
        std::string hover;
        ImVec2 mouse = ImGui::GetIO().MousePos;
        for (auto& c : model_.calibration){
            float cx = x(c.second);
            dl->AddCircleFilled(ImVec2(cx, axis - em * 0.6f), em * 0.22f, ImGui::GetColorU32(ui::kDim));
            if (ImGui::IsItemHovered() && std::fabs(mouse.x - cx) < em * 0.3f)
                hover = c.first + ": " + std::to_string((int)std::lround(c.second));
        }
        ImU32 col = ImGui::GetColorU32(ui::kAccent);
        dl->AddRectFilled(ImVec2(x(shownElo - shownCi), axis - em * 1.5f), ImVec2(x(shownElo + shownCi), axis - em * 1.1f),
                          (col & 0x00FFFFFF) | 0x60000000);
        dl->AddLine(ImVec2(x(shownElo), axis - em * 1.8f), ImVec2(x(shownElo), axis), col, 3);
        if (!hover.empty()) ImGui::SetTooltip("%s", hover.c_str());
        ImGui::TextDisabled("Dots: the %d calibration engines (hover for names). Bar: this engine with its 95%% interval.",
                            (int)model_.calibration.size());
    }
    if (r.ok){
        ImGui::SeparatorText("By category");
        if (ImGui::BeginTable("elo-groups", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)){
            ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Elo"); ImGui::TableSetupColumn("Positions");
            ImGui::TableHeadersRow();
            for (auto& [g, v] : r.groups){
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(g.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%ld", std::lround(v.first));
                ImGui::TableNextColumn(); ImGui::Text("%d", v.second);
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Category values come from fewer positions and are less certain than the total.");
        auto save = [&](const char* ext, const std::string& text){
            std::string path = filedialog::saveFile("Save the Elo estimate",
                app_.settings().lastSaveDir + "/elo_" + ui::timeStamp() + ext, {ext[1] == 'j' ? "JSON" : "Markdown", std::string("*") + ext});
            if (path.empty()){
                if (filedialog::available()) return;
                std::error_code ec; fs::create_directories("games", ec);
                path = "games/elo_" + ui::timeStamp() + ext;
            }
            std::ofstream f(fs::u8path(path)); f << text;
            app_.toast(f ? "Saved " + path : "Could not write " + path);
        };
        if (ImGui::Button("Export JSON...")) save(".json", eloResultJson(r, model_));
        ImGui::SameLine();
        if (ImGui::Button("Export Markdown...")) save(".md", eloResultMarkdown(r, model_));
    }
    ImGui::Separator();
}
