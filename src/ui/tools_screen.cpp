/* Chess-GUI-for-UCI — engine testing tools screen. GPL-2.0 */
#include "ui/tools_screen.hpp"
#include "gui.hpp"
#include "testing.hpp"
#include "ui/file_dialog.hpp"
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

void ToolsScreen::draw(){
    Settings& st = app_.settings();
    float em = ImGui::GetFontSize();
    ImGui::BeginChild("tools-left", ImVec2(em * 30, 0), ImGuiChildFlags_Borders);
    if (ImGui::BeginTabBar("tooltabs")){
        if (ImGui::BeginTabItem("Move generator")){ drawPerftTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Accuracy")){ drawAccuracyTab(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("tools-out", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ui::jobPanel(job_,
        [&](){ return filedialog::saveFile("Save test log", st.lastSaveDir + "/test_" + ui::timeStamp() + ".txt",
                                           {"Text files", "*.txt", "All files", "*"}); },
        [&](const std::string& m){ app_.toast(m); });
    ImGui::EndChild();
}

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
}

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

void ToolsScreen::runSelfTest(){
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
    std::string suitePath = suitePath_;
    int maxDepth = perftDepth_;
    job_.start("Engine move-generator test: " + cfg.name, [cfg, suitePath, maxDepth](ui::Job& j){
        UciEngine e;
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
                if (!r.pass){ fails++; s += "  fen: " + r.fen; }
                j.log(s);
            }, j.abortFlag());
        if (j.stopRequested()) j.log(">>> Aborted.");
        else if (!res.empty() && res.back().engineNodes < 0)
            j.log("FAIL: the engine does not answer 'go perft N' with 'Nodes searched: X'.");
        else j.log(fails ? "FAIL: " + std::to_string(fails) + " mismatches - the move generator has bugs in those positions."
                         : ">>> ALL PASSED - move generator agrees with verified node counts.");
        e.quit();
    });
}

void ToolsScreen::runAccuracy(const EngineConfig& tee, const EngineConfig& ree){
    const Settings& st = app_.settings();
    int cnt = st.accCount, tms = st.accTestMs, rms = st.accRefMs, mpv = st.accMultiPV;
    job_.start("Accuracy: " + tee.name + " vs reference " + ree.name + "  (" + std::to_string(cnt) + " positions)",
               [tee, ree, cnt, tms, rms, mpv](ui::Job& j){
        UciEngine te, re;
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
