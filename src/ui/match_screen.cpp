/* Chess-GUI-for-UCI — engine match / tournament screen. GPL-2.0 */
#include "ui/match_screen.hpp"
#include "gui.hpp"
#include "testing.hpp"
#include "ui/file_dialog.hpp"
#include "ui/widgets.hpp"
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static long long steadyMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void MatchScreen::shutdown(){
    runner_.abort();
    stopJudge();
}

void MatchScreen::stopJudge(){
    if (judge_){ judge_->shutdown(); judge_.reset(); }
}

void MatchScreen::update(){
    if (live_ && judge_) judge_->setFen(runner_.snapshot().pos.toFEN());
}

void MatchScreen::draw(){
    if (live_) drawLive(); else drawSetup();
}

void MatchScreen::loadOpenings(const std::string& path){
    std::ifstream f(fs::u8path(path));
    if (!f){ app_.toast("Cannot open " + path); return; }
    int added = 0, bad = 0;
    std::string line;
    while (std::getline(f, line)){
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        bool ok = false; Position p = Position::fromFEN(line, &ok);
        if (ok && enginePositionProblem(p).empty()){ openings_.push_back(line); added++; }
        else bad++;
    }
    app_.toast("Added " + std::to_string(added) + " positions" + (bad ? " (" + std::to_string(bad) + " invalid skipped)" : ""));
}

void MatchScreen::drawSetup(){
    Settings& st = app_.settings();
    float em = ImGui::GetFontSize();
    auto& engines = app_.registry().engines();
    ImGui::TextWrapped("Pick 2 engines for a match or 3+ for a round-robin tournament. Colours alternate; "
                       "with several openings, each one is played with both colours before the next.");
    ImGui::SeparatorText("Engines");
    if (engines.empty()) ImGui::TextColored(ui::kWarn, "No engines configured. Add some in Engines > Manage engines.");
    for (auto& c : engines){
        bool sel = std::find(selected_.begin(), selected_.end(), c.name) != selected_.end();
        if (ImGui::Checkbox(c.name.c_str(), &sel)){
            if (sel) selected_.push_back(c.name);
            else selected_.erase(std::remove(selected_.begin(), selected_.end(), c.name), selected_.end());
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.path.c_str());
    }
    ImGui::SeparatorText("Games and time control");
    ImGui::SetNextItemWidth(em * 7);
    if (ImGui::InputInt("Games per pairing", &st.matchGamesPerPair)) {}
    st.matchGamesPerPair = std::max(1, std::min(st.matchGamesPerPair, 10000));
    float baseMin = st.tcBaseMs / 60000.f, incS = st.tcIncMs / 1000.f;
    ImGui::SetNextItemWidth(em * 6);
    if (ImGui::InputFloat("minutes", &baseMin, 0.5f, 1, "%.2f")) st.tcBaseMs = (int)(std::max(0.05f, baseMin) * 60000);
    ImGui::SameLine(); ImGui::SetNextItemWidth(em * 6);
    if (ImGui::InputFloat("s increment", &incS, 0.1f, 1, "%.2f")) st.tcIncMs = (int)(std::max(0.f, incS) * 1000);

    ImGui::SeparatorText("Start positions");
    ImGui::Text("%s", openings_.empty() ? "Standard starting position" : (std::to_string(openings_.size()) + " positions").c_str());
    ImGui::SetNextItemWidth(em * 28);
    bool enter = ImGui::InputTextWithHint("##fen", "FEN to add (Enter)", &fenInput_, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Add") || enter){
        bool ok = false; Position p = Position::fromFEN(fenInput_, &ok);
        std::string prob = ok ? enginePositionProblem(p) : "";
        if (!ok) app_.toast("That FEN is invalid");
        else if (!prob.empty()) app_.toast("Not added: " + prob);
        else { openings_.push_back(fenInput_); fenInput_.clear(); }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add in editor")) app_.openEditorForMatch();
    ImGui::SameLine();
    if (ImGui::Button("Clear")) openings_.clear();
    ImGui::SetNextItemWidth(em * 28);
    ui::pathInput("openings", openingsPath_, "file with one FEN per line", [&](){
        return filedialog::openFile("Open positions file", st.lastOpeningsDir, {"FEN/EPD files", "*.fen *.epd *.txt", "All files", "*"}); });
    ImGui::SameLine();
    if (ImGui::Button("Load file")){
        loadOpenings(openingsPath_);
        st.lastOpeningsDir = fs::u8path(openingsPath_).parent_path().u8string();
    }

    ImGui::SeparatorText("Judge engine (optional)");
    ImGui::TextDisabled("An independent engine for the eval bar and arrows. It plays no games.");
    ImGui::SetNextItemWidth(em * 16);
    if (ImGui::BeginCombo("##judge", judgeEngine_.empty() ? "None" : judgeEngine_.c_str())){
        if (ImGui::Selectable("None", judgeEngine_.empty())) judgeEngine_.clear();
        for (auto& c : engines) if (ImGui::Selectable(c.name.c_str(), c.name == judgeEngine_)) judgeEngine_ = c.name;
        ImGui::EndCombo();
    }
    ImGui::SameLine(); ImGui::SetNextItemWidth(em * 5);
    ImGui::SliderInt("Arrows", &judgeLines_, 1, 5);

    ImGui::Spacing();
    ImGui::BeginDisabled(selected_.size() < 2);
    if (ImGui::Button("Start", ImVec2(em * 10, 0))) start();
    ImGui::EndDisabled();
    if (selected_.size() < 2){ ImGui::SameLine(); ImGui::TextDisabled("Select at least two engines."); }
    if (runner_.snapshot().totalGames > 0){ ImGui::SameLine(); if (ImGui::Button("Show last match")) live_ = true; }
}

void MatchScreen::start(){
    Settings& st = app_.settings();
    app_.saveSettings();
    std::vector<EngineConfig> es;
    for (auto& n : selected_) if (const EngineConfig* c = ui::findEngine(app_.registry(), n)) es.push_back(*c);
    if (es.size() < 2){ app_.toast("Select at least two engines"); return; }
    runner_.startTournament(es, TimeControl{st.tcBaseMs, st.tcIncMs}, st.matchGamesPerPair, openings_);
    stopJudge();
    if (!judgeEngine_.empty()){
        if (const EngineConfig* c = ui::findEngine(app_.registry(), judgeEngine_)){
            EngineConfig cfg = *c;
            cfg.setOption("MultiPV", std::to_string(judgeLines_));
            judge_ = std::make_unique<AnalysisController>();
            if (!judge_->start(cfg, "Judge")){ app_.toast("Judge engine failed: " + judge_->lastError()); judge_.reset(); }
        }
    }
    flipped_ = false;
    live_ = true;
}

void MatchScreen::savePgns(){
    auto pgns = runner_.collectedPGNs();
    if (pgns.empty()){ app_.toast("No finished games yet"); return; }
    Settings& st = app_.settings();
    std::string path = filedialog::saveFile("Save games as PGN",
        (st.lastSaveDir.empty() ? std::string("games") : st.lastSaveDir) + "/tournament_" + ui::timeStamp() + ".pgn",
        {"PGN files", "*.pgn"});
    if (path.empty()){
        if (filedialog::available()) return;
        std::error_code ec; fs::create_directories("games", ec);
        path = "games/tournament_" + ui::timeStamp() + ".pgn";
    } else st.lastSaveDir = fs::u8path(path).parent_path().u8string();
    std::ofstream f(fs::u8path(path));
    for (auto& p : pgns) f << p << "\n";
    app_.toast(f ? "Saved " + path + " (" + std::to_string(pgns.size()) + " games)" : "Could not write " + path);
}

void MatchScreen::drawLive(){
    auto s = runner_.snapshot();
    float em = ImGui::GetFontSize();
    // clocks tick between snapshot posts
    long long wc = s.wClockMs, bc = s.bClockMs;
    if (!s.finished && s.result == GameResult::ONGOING && s.postedAtMs > 0){
        long long el = steadyMs() - s.postedAtMs;
        if (el > 0){ if (s.pos.whiteToMove) wc -= el; else bc -= el; }
    }
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float side = std::min(avail.y - ImGui::GetFrameHeightWithSpacing() * 2, avail.x * 0.55f);
    float barW = em * 1.2f;

    ImGui::BeginGroup();
    auto player = [&](bool white){
        bool toMove = !s.finished && s.result == GameResult::ONGOING && s.pos.whiteToMove == white;
        ImGui::PushFont(ui::fonts().large);
        ImGui::TextColored(toMove ? ImVec4(1, 1, 1, 1) : ui::kDim, "%s   %s",
                           ui::clockText(white ? wc : bc).c_str(), (white ? s.whiteName : s.blackName).c_str());
        ImGui::PopFont();
    };
    player(flipped_);
    // eval bar: judge if present, else the players' own reports
    int cpW = s.scoreCpWhitePOV, mateW = s.mateIn; bool isMate = s.scoreIsMate;
    std::vector<BoardArrow> arrows;
    std::string judgeLine;
    if (judge_){
        auto jp = judge_->pvs();
        if (!judge_->alive()) judgeLine = "The judge engine terminated.";
        else if (!jp.empty() && jp[0].valid && judge_->fen() == s.pos.toFEN()){
            cpW = s.pos.whiteToMove ? jp[0].scoreCp : -jp[0].scoreCp;
            isMate = jp[0].isMate;
            mateW = s.pos.whiteToMove ? jp[0].mateIn : -jp[0].mateIn;
            static const int alpha[5] = {210, 120, 85, 62, 45};
            for (int r = std::min((int)jp.size(), 5) - 1; r >= 0; r--){
                Move m;
                if (jp[r].valid && s.pos.uciToMove(jp[r].firstMove(), m))
                    arrows.push_back({m.from, m.to, IM_COL32(70,140,220,alpha[r])});
            }
            Move bm;
            judgeLine = "Judge " + judge_->engineName() + ": best "
                      + (s.pos.uciToMove(jp[0].firstMove(), bm) ? s.pos.moveToSAN(bm) : jp[0].firstMove())
                      + " " + formatScore(cpW, isMate, mateW) + " (depth " + std::to_string(jp[0].depth) + ")";
        } else judgeLine = "Judge: waiting for analysis...";
    }
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(barW - ImGui::GetStyle().ItemSpacing.x, side));
    drawEvalBar(ImGui::GetWindowDrawList(), p0, ImVec2(p0.x + barW - ImGui::GetStyle().ItemSpacing.x, p0.y + side),
                cpW, isMate, mateW, flipped_);
    ImGui::SameLine();
    BoardSpec spec;
    spec.pos = &s.pos; spec.flipped = flipped_; spec.interactive = false;
    spec.theme = app_.settings().boardTheme; spec.showCoords = app_.settings().showCoordinates;
    spec.lastFrom = s.lastFrom; spec.lastTo = s.lastTo;
    spec.arrows = arrows;
    board_.draw("match-board", side, spec, app_.pieces());
    player(!flipped_);
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginChild("match-side", ImVec2(0, 0));
    ImGui::PushFont(ui::fonts().large);
    ImGui::Text("Game %d / %d", s.gameIndex, s.totalGames);
    ImGui::PopFont();
    if (runner_.running()){ if (ImGui::Button("Abort")) runner_.abort(); }
    else if (ImGui::Button("Back to setup")){ stopJudge(); live_ = false; }
    ImGui::SameLine();
    if (ImGui::Button("Save PGNs...")) savePgns();
    ImGui::SameLine();
    if (ImGui::Button("Copy standings")){
        std::string all;
        for (auto& stn : s.standings){
            char b[200];
            std::snprintf(b, sizeof b, "%s  +%d =%d -%d  %.1f pts\n", stn.name.c_str(), stn.wins, stn.draws, stn.losses, stn.points());
            all += b;
        }
        for (auto& l : s.log) all += l + "\n";
        ImGui::SetClipboardText(all.c_str());
        app_.toast("Standings and log copied");
    }
    ImGui::SameLine();
    if (ImGui::Button("Flip")) flipped_ = !flipped_;
    if (!judgeLine.empty()) ImGui::TextColored(judgeLine.find("terminated") != std::string::npos ? ui::kBad : ui::kGood,
                                               "%s", judgeLine.c_str());
    ImGui::SeparatorText("Standings");
    if (ImGui::BeginTable("standings", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)){
        ImGui::TableSetupColumn("Engine", ImGuiTableColumnFlags_WidthStretch);
        for (const char* h : {"+", "=", "-", "Points"}) ImGui::TableSetupColumn(h, ImGuiTableColumnFlags_WidthFixed, em * 3.5f);
        ImGui::TableHeadersRow();
        auto table = s.standings;
        std::stable_sort(table.begin(), table.end(), [](const Standing& a, const Standing& b){ return a.points() > b.points(); });
        for (auto& stn : table){
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(stn.name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d", stn.wins);
            ImGui::TableNextColumn(); ImGui::Text("%d", stn.draws);
            ImGui::TableNextColumn(); ImGui::Text("%d", stn.losses);
            ImGui::TableNextColumn(); ImGui::Text("%.1f", stn.points());
        }
        ImGui::EndTable();
    }
    if (ImGui::CollapsingHeader("Search stats (this game)")){
        ImGui::TextUnformatted(s.whiteName.c_str());
        ui::searchStats("w", s.stats, true);
        ImGui::TextUnformatted(s.blackName.c_str());
        ui::searchStats("b", s.stats, false);
    }
    if (s.finished) ImGui::TextColored(ui::kGood, "Finished.");
    else if (s.result != GameResult::ONGOING) ImGui::Text("Last result: %s", resultReasonText(s.reason).c_str());
    ImGui::SeparatorText("Log");
    ImGui::BeginChild("matchlog", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::PushFont(ui::fonts().mono);
    for (auto& l : s.log) ImGui::TextUnformatted(l.c_str());
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::EndChild();
}
