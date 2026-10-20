/* Chess-GUI-for-UCI — engine match / tournament / SPRT screen. GPL-2.0 */
#include "ui/match_screen.hpp"
#include "gui.hpp"
#include "ui/file_dialog.hpp"
#include "ui/widgets.hpp"
#include <nlohmann/json.hpp>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
using json = nlohmann::json;

static long long steadyMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static int defaultConcurrency(){ return std::max(1, (int)std::thread::hardware_concurrency() / 2); }

// ---------------------------------------------------------------- presets file
static json toJson(const MatchSetup& m){
    return {{"name", m.name}, {"mode", (int)m.mode}, {"engines", m.engines}, {"gauntletSeeds", m.gauntletSeeds},
            {"baseSec", m.baseSec}, {"incSec", m.incSec}, {"timeMarginMs", m.timeMarginMs},
            {"gamesPerPairing", m.gamesPerPairing}, {"concurrency", m.concurrency},
            {"bookPath", m.bookPath}, {"bookPlies", m.bookPlies}, {"bookRandom", m.bookRandom}, {"seed", m.seed}, {"chess960", m.chess960},
            {"adj", {{"resign", m.adj.resign}, {"resignCp", m.adj.resignCp}, {"resignMoves", m.adj.resignMoves},
                     {"draw", m.adj.draw}, {"drawAfterMove", m.adj.drawAfterMove}, {"drawCp", m.adj.drawCp},
                     {"drawMoves", m.adj.drawMoves}, {"maxMoves", m.adj.maxMoves},
                     {"syzygyPath", m.adj.syzygyPath}, {"syzygyPieces", m.adj.syzygyPieces}}},
            {"sprt", m.sprt}, {"elo0", m.sprtCfg.elo0}, {"elo1", m.sprtCfg.elo1}, {"alpha", m.sprtCfg.alpha},
            {"beta", m.sprtCfg.beta}, {"model", m.sprtCfg.model == SprtModel::Logistic ? "logistic" : "normalized"},
            {"savePgn", m.savePgn}, {"resumable", m.resumable}};
}

static MatchSetup fromJson(const json& j){
    MatchSetup m;
    m.name = j.value("name", "");
    int mode = j.value("mode", 0);
    m.mode = mode == 1 ? TournamentMode::RoundRobin : mode == 2 ? TournamentMode::Gauntlet : TournamentMode::Match;
    m.engines = j.value("engines", std::vector<std::string>{});
    m.gauntletSeeds = std::max(1, j.value("gauntletSeeds", 1));
    m.baseSec = j.value("baseSec", 10.0); m.incSec = j.value("incSec", 0.1);
    m.timeMarginMs = j.value("timeMarginMs", 100);
    m.gamesPerPairing = std::max(2, j.value("gamesPerPairing", 100));
    m.concurrency = j.value("concurrency", 0);
    m.bookPath = j.value("bookPath", ""); m.bookPlies = j.value("bookPlies", 8);
    m.bookRandom = j.value("bookRandom", true); m.seed = j.value("seed", 1);
    m.chess960 = j.value("chess960", false);
    if (j.contains("adj") && j["adj"].is_object()){
        const json& a = j["adj"];
        m.adj.resign = a.value("resign", false); m.adj.resignCp = a.value("resignCp", 600);
        m.adj.resignMoves = a.value("resignMoves", 3);
        m.adj.draw = a.value("draw", false); m.adj.drawAfterMove = a.value("drawAfterMove", 40);
        m.adj.drawCp = a.value("drawCp", 10); m.adj.drawMoves = a.value("drawMoves", 8);
        m.adj.maxMoves = a.value("maxMoves", 0);
        m.adj.syzygyPath = a.value("syzygyPath", ""); m.adj.syzygyPieces = a.value("syzygyPieces", 0);
    }
    m.sprt = j.value("sprt", false);
    m.sprtCfg.elo0 = j.value("elo0", 0.0); m.sprtCfg.elo1 = j.value("elo1", 5.0);
    m.sprtCfg.alpha = j.value("alpha", 0.05); m.sprtCfg.beta = j.value("beta", 0.05);
    m.sprtCfg.model = j.value("model", "normalized") == "logistic" ? SprtModel::Logistic : SprtModel::Normalized;
    m.savePgn = j.value("savePgn", true); m.resumable = j.value("resumable", true);
    return m;
}

static fs::path presetsFile(){ return fs::u8path(userConfigDir()) / "match_presets.json"; }

MatchScreen::MatchScreen(App& app) : app_(app) { loadPresets(); }

void MatchScreen::loadPresets(){
    presets_.clear();
    std::ifstream f(presetsFile());
    if (!f) return;
    try {
        json j = json::parse(f);
        if (j.contains("last")) setup_ = fromJson(j["last"]);
        if (j.contains("presets")) for (auto& p : j["presets"]) presets_.push_back(fromJson(p));
        lastPgnPath_ = j.value("lastPgn", "");
        lastStatePath_ = j.value("lastState", "");
    } catch (const std::exception&) {}      // a broken file just means no presets
}

void MatchScreen::savePresets(){
    json arr = json::array();
    for (auto& p : presets_) arr.push_back(toJson(p));
    json j = {{"last", toJson(setup_)}, {"presets", arr}, {"lastPgn", lastPgnPath_}, {"lastState", lastStatePath_}};
    std::error_code ec; fs::create_directories(presetsFile().parent_path(), ec);
    std::ofstream f(presetsFile());
    f << j.dump(2) << "\n";
}

void MatchScreen::shutdown(){
    runner_.abort();
    stopJudge();
}

void MatchScreen::stopJudge(){
    if (judge_){ judge_->shutdown(); judge_.reset(); }
}

void MatchScreen::update(){
    if (!live_ || !judge_) return;
    auto s = runner_.snapshot();
    if (selectedGame_ < (int)s.live.size()) judge_->setFen(s.live[selectedGame_].pos.toFEN());
}

void MatchScreen::draw(){
    if (live_) drawLive(); else drawSetup();
}

// ---------------------------------------------------------------- setup
void MatchScreen::drawPresetsBlock(){
    float em = ImGui::GetFontSize();
    ImGui::SeparatorText("Presets");
    ImGui::SetNextItemWidth(em * 14);
    if (ImGui::BeginCombo("##preset", presets_.empty() ? "(none saved)" : "Load a preset...")){
        for (size_t i = 0; i < presets_.size(); i++){
            ImGui::PushID((int)i);
            if (ImGui::Selectable(presets_[i].name.c_str())){ setup_ = presets_[i]; presetName_ = presets_[i].name; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Dev vs base")){
        // the usual way to test an engine change: an SPRT of the new build against the old one
        setup_.mode = TournamentMode::Match;
        setup_.engines.resize(2);
        setup_.sprt = true;
        setup_.sprtCfg = SprtConfig{};
        setup_.baseSec = 10; setup_.incSec = 0.1;
        setup_.gamesPerPairing = std::max(setup_.gamesPerPairing, 20000);
        setup_.adj.resign = true; setup_.adj.draw = true;
        if (presetName_.empty()) presetName_ = "dev vs base";
        app_.toast("Pick the dev build as engine A and the base build as engine B");
    }
    ImGui::SameLine();
    ui::helpMarker("Dev vs base: SPRT [0, 5] normalized Elo, alpha = beta = 0.05, 10s+0.1s, resign and draw "
                   "adjudication, up to 20000 games. Pick the two builds and an opening book, then save it as a "
                   "preset to reuse it for every change you test.");
    ImGui::SetNextItemWidth(em * 10);
    ImGui::InputTextWithHint("##pname", "preset name", &presetName_);
    ImGui::SameLine();
    ImGui::BeginDisabled(presetName_.empty());
    if (ImGui::Button("Save preset")){
        MatchSetup p = setup_; p.name = presetName_;
        auto it = std::find_if(presets_.begin(), presets_.end(), [&](const MatchSetup& x){ return x.name == p.name; });
        if (it != presets_.end()) *it = p; else presets_.push_back(p);
        savePresets();
        app_.toast("Preset '" + p.name + "' saved");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    auto it = std::find_if(presets_.begin(), presets_.end(), [&](const MatchSetup& x){ return x.name == presetName_; });
    ImGui::BeginDisabled(it == presets_.end());
    if (ImGui::Button("Delete preset") && it != presets_.end()){ presets_.erase(it); savePresets(); }
    ImGui::EndDisabled();
}

void MatchScreen::drawEnginesBlock(){
    auto& engines = app_.registry().engines();
    float em = ImGui::GetFontSize();
    ImGui::SeparatorText("Engines");
    int mode = (int)setup_.mode;
    ImGui::RadioButton("Match", &mode, (int)TournamentMode::Match); ImGui::SameLine();
    ImGui::RadioButton("Round-robin", &mode, (int)TournamentMode::RoundRobin); ImGui::SameLine();
    ImGui::RadioButton("Gauntlet", &mode, (int)TournamentMode::Gauntlet);
    setup_.mode = (TournamentMode)mode;
    if (engines.empty()){
        ImGui::TextColored(ui::kWarn, "No engines configured. Add some in Engines > Manage engines.");
        return;
    }
    if (setup_.mode == TournamentMode::Match){
        setup_.engines.resize(2);
        ImGui::SetNextItemWidth(em * 16); ui::engineCombo("Engine A (dev)", app_.registry(), setup_.engines[0]);
        ImGui::SetNextItemWidth(em * 16); ui::engineCombo("Engine B (base)", app_.registry(), setup_.engines[1]);
        if (!setup_.engines[0].empty() && setup_.engines[0] == setup_.engines[1])
            ImGui::TextColored(ui::kWarn, "A and B are the same engine (fine as a sanity check: expect 0 Elo).");
        return;
    }
    // drop names from a Match setup that are blank
    setup_.engines.erase(std::remove(setup_.engines.begin(), setup_.engines.end(), std::string()), setup_.engines.end());
    if (setup_.mode == TournamentMode::Gauntlet){
        ImGui::SetNextItemWidth(em * 6);
        ImGui::InputInt("seeds", &setup_.gauntletSeeds);
        setup_.gauntletSeeds = std::max(1, setup_.gauntletSeeds);
        ImGui::SameLine(); ui::helpMarker("The first N engines ticked (#1..#N) each play all the others; the others "
                                          "do not play each other.");
    }
    for (auto& c : engines){
        auto pos = std::find(setup_.engines.begin(), setup_.engines.end(), c.name);
        bool sel = pos != setup_.engines.end();
        if (ImGui::Checkbox(c.name.c_str(), &sel)){
            if (sel) setup_.engines.push_back(c.name); else setup_.engines.erase(pos);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.path.c_str());
        auto now = std::find(setup_.engines.begin(), setup_.engines.end(), c.name);
        if (now != setup_.engines.end()){ ImGui::SameLine(); ImGui::TextDisabled("#%d", (int)(now - setup_.engines.begin()) + 1); }
    }
}

void MatchScreen::drawOpeningsBlock(){
    Settings& st = app_.settings();
    float em = ImGui::GetFontSize();
    ImGui::SeparatorText("Openings");
    ImGui::TextDisabled("Every opening is played twice with colours swapped (a game pair).");
    ImGui::SetNextItemWidth(em * 24);
    ui::pathInput("book", setup_.bookPath, "opening book: .epd, .pgn or FEN list (empty: positions below)", [&](){
        return filedialog::openFile("Open opening book", st.lastOpeningsDir,
                                    {"Opening books", "*.epd *.pgn *.fen *.txt", "All files", "*"}); });
    if (!setup_.bookPath.empty()){
        ImGui::SetNextItemWidth(em * 6); ImGui::InputInt("PGN plies", &setup_.bookPlies);
        setup_.bookPlies = std::max(0, setup_.bookPlies);
        ImGui::SameLine(); ImGui::Checkbox("random order", &setup_.bookRandom);
        if (setup_.bookRandom){ ImGui::SameLine(); ImGui::SetNextItemWidth(em * 6); ImGui::InputInt("seed", &setup_.seed); }
        std::string key = setup_.bookPath + "|" + std::to_string(setup_.bookPlies);
        if (key != bookPreviewKey_){
            bookPreviewKey_ = key;
            BookLoadResult r = loadOpeningBook(setup_.bookPath, BookFormat::Auto, setup_.bookPlies);
            bookPreviewOk_ = r.error.empty();
            bookPreview_ = bookPreviewOk_ ? std::to_string(r.openings.size()) + " openings"
                                            + (r.skipped ? ", " + std::to_string(r.skipped) + " unusable skipped" : "")
                                          : r.error;
        }
        ImGui::TextColored(bookPreviewOk_ ? ui::kGood : ui::kBad, "%s", bookPreview_.c_str());
        return;
    }
    ImGui::Checkbox("Chess960 start positions", &setup_.chess960);
    if (setup_.chess960){
        ImGui::SameLine(); ImGui::Checkbox("random order##960", &setup_.bookRandom);
        if (setup_.bookRandom){ ImGui::SameLine(); ImGui::SetNextItemWidth(em * 6); ImGui::InputInt("seed##960", &setup_.seed); }
        ImGui::TextDisabled("Each game pair gets one of the 960 positions; engines get UCI_Chess960.");
        return;
    }
    ImGui::Text("%s", openings_.empty() ? "Standard starting position" : (std::to_string(openings_.size()) + " positions").c_str());
    ImGui::SetNextItemWidth(em * 20);
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
}

void MatchScreen::drawRulesBlock(){
    float em = ImGui::GetFontSize(), w = em * 6;
    ImGui::SeparatorText("Time control and games");
    ImGui::SetNextItemWidth(w); ImGui::InputDouble("base (s)", &setup_.baseSec, 1, 10, "%.2f");
    ImGui::SameLine(); ImGui::SetNextItemWidth(w); ImGui::InputDouble("increment (s)", &setup_.incSec, 0.01, 0.1, "%.3f");
    setup_.baseSec = std::max(0.05, setup_.baseSec); setup_.incSec = std::max(0.0, setup_.incSec);
    ImGui::SameLine(); ImGui::SetNextItemWidth(w); ImGui::InputInt("margin (ms)", &setup_.timeMarginMs);
    setup_.timeMarginMs = std::max(0, setup_.timeMarginMs);
    ImGui::SameLine(); ui::helpMarker("How far a move may overrun the clock before it counts as a loss on time.");
    ImGui::SetNextItemWidth(w); ImGui::InputInt("games per pairing", &setup_.gamesPerPairing, 2, 100);
    setup_.gamesPerPairing = std::max(2, setup_.gamesPerPairing);
    int cores = (int)std::max(1u, std::thread::hardware_concurrency());
    int conc = setup_.concurrency > 0 ? setup_.concurrency : defaultConcurrency();
    ImGui::SameLine(); ImGui::SetNextItemWidth(w * 1.5f);
    if (ImGui::SliderInt("games in parallel", &conc, 1, cores)) setup_.concurrency = conc;
    ImGui::SameLine(); ui::helpMarker("Each game runs two engine processes. Keep games in parallel x 2 x engine "
                                      "threads at or below the number of CPU cores, or the engines take time from "
                                      "each other and the result gets noisier.");

    ImGui::SeparatorText("Adjudication");
    ImGui::Checkbox("resign", &setup_.adj.resign);
    ImGui::BeginDisabled(!setup_.adj.resign);
    ImGui::SameLine(); ImGui::SetNextItemWidth(w); ImGui::InputInt("cp##r", &setup_.adj.resignCp);
    ImGui::SameLine(); ImGui::SetNextItemWidth(w); ImGui::InputInt("moves each##r", &setup_.adj.resignMoves);
    ImGui::EndDisabled();
    ImGui::Checkbox("draw", &setup_.adj.draw);
    ImGui::BeginDisabled(!setup_.adj.draw);
    ImGui::SameLine(); ImGui::SetNextItemWidth(w); ImGui::InputInt("from move", &setup_.adj.drawAfterMove);
    ImGui::SameLine(); ImGui::SetNextItemWidth(w); ImGui::InputInt("|cp| <=", &setup_.adj.drawCp);
    ImGui::SameLine(); ImGui::SetNextItemWidth(w); ImGui::InputInt("moves each##d", &setup_.adj.drawMoves);
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(w); ImGui::InputInt("max moves (0 = none)", &setup_.adj.maxMoves);
    ImGui::TextUnformatted("Syzygy tablebases");
    ImGui::SetNextItemWidth(em * 20);
    ui::pathInput("Syzygy", setup_.adj.syzygyPath, "tablebase folder (empty = off)", [&](){
        return filedialog::selectFolder("Syzygy tablebase folder", setup_.adj.syzygyPath); });
    if (!setup_.adj.syzygyPath.empty()){
        ImGui::SetNextItemWidth(w); ImGui::InputInt("max pieces (0 = all)", &setup_.adj.syzygyPieces);
        setup_.adj.syzygyPieces = std::max(0, setup_.adj.syzygyPieces);
    }
    ImGui::SameLine(); ui::helpMarker("Games end as soon as they reach a position the tables cover (right after a "
                                      "capture or pawn move). Several folders: separate them with ';' on Windows, "
                                      "':' elsewhere.");
    setup_.adj.resignCp = std::max(1, setup_.adj.resignCp); setup_.adj.resignMoves = std::max(1, setup_.adj.resignMoves);
    setup_.adj.drawCp = std::max(0, setup_.adj.drawCp); setup_.adj.drawMoves = std::max(1, setup_.adj.drawMoves);
    setup_.adj.drawAfterMove = std::max(1, setup_.adj.drawAfterMove); setup_.adj.maxMoves = std::max(0, setup_.adj.maxMoves);

    if (setup_.mode == TournamentMode::Match){
        ImGui::SeparatorText("SPRT");
        ImGui::Checkbox("stop when H0 or H1 is accepted", &setup_.sprt);
        ImGui::BeginDisabled(!setup_.sprt);
        ImGui::SetNextItemWidth(w); ImGui::InputDouble("elo0", &setup_.sprtCfg.elo0, 0.5, 1, "%.2f");
        ImGui::SameLine(); ImGui::SetNextItemWidth(w); ImGui::InputDouble("elo1", &setup_.sprtCfg.elo1, 0.5, 1, "%.2f");
        ImGui::SameLine(); ImGui::SetNextItemWidth(w); ImGui::InputDouble("alpha", &setup_.sprtCfg.alpha, 0.01, 0.05, "%.3f");
        ImGui::SameLine(); ImGui::SetNextItemWidth(w); ImGui::InputDouble("beta", &setup_.sprtCfg.beta, 0.01, 0.05, "%.3f");
        int model = (int)setup_.sprtCfg.model;
        ImGui::RadioButton("normalized Elo", &model, (int)SprtModel::Normalized); ImGui::SameLine();
        ImGui::RadioButton("logistic Elo", &model, (int)SprtModel::Logistic);
        setup_.sprtCfg.model = (SprtModel)model;
        std::string v = setup_.sprtCfg.validate();
        if (!v.empty()) ImGui::TextColored(ui::kBad, "%s", v.c_str());
        else ImGui::TextDisabled("LLR bounds %.2f / %.2f. GSPRT on game pairs, as in fastchess and OpenBench.",
                                 setup_.sprtCfg.lowerBound(), setup_.sprtCfg.upperBound());
        ImGui::EndDisabled();
    }

    ImGui::SeparatorText("Output");
    ImGui::Checkbox("write games to a PGN file as they finish", &setup_.savePgn);
    ImGui::SameLine(); ImGui::Checkbox("resumable", &setup_.resumable);
    ImGui::SameLine(); ui::helpMarker("Keeps a state file next to the PGN. After a crash or Stop, \"Resume last run\" "
                                      "continues with the games already played.");
    ImGui::TextDisabled("Files: %s.pgn / .state.json", outputBase().c_str());

    ImGui::SeparatorText("Judge engine (optional)");
    ImGui::TextDisabled("An independent engine for the eval bar and arrows on the main board. It plays no games.");
    ImGui::SetNextItemWidth(em * 16);
    if (ImGui::BeginCombo("##judge", judgeEngine_.empty() ? "None" : judgeEngine_.c_str())){
        if (ImGui::Selectable("None", judgeEngine_.empty())) judgeEngine_.clear();
        for (auto& c : app_.registry().engines())
            if (ImGui::Selectable(c.name.c_str(), c.name == judgeEngine_)) judgeEngine_ = c.name;
        ImGui::EndCombo();
    }
    ImGui::SameLine(); ImGui::SetNextItemWidth(em * 5);
    ImGui::SliderInt("arrows", &judgeLines_, 1, 5);
}

std::string MatchScreen::outputBase() const{
    const Settings& st = app_.settings();
    return (st.lastSaveDir.empty() ? std::string("games") : st.lastSaveDir) + "/match_<date>";
}

void MatchScreen::drawSetup(){
    float em = ImGui::GetFontSize();
    ImGui::BeginChild("match-setup", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.5f));
    drawPresetsBlock();
    drawEnginesBlock();
    drawOpeningsBlock();
    drawRulesBlock();
    ImGui::EndChild();
    int named = (int)std::count_if(setup_.engines.begin(), setup_.engines.end(), [](const std::string& n){ return !n.empty(); });
    bool enough = named >= 2;
    ImGui::BeginDisabled(!enough);
    if (ImGui::Button("Start", ImVec2(em * 10, 0))) start(false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    bool canResume = !lastStatePath_.empty() && fs::exists(fs::u8path(lastStatePath_));
    ImGui::BeginDisabled(!canResume || !enough);
    if (ImGui::Button("Resume last run")) start(true);
    ImGui::EndDisabled();
    if (canResume && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Continues %s (the settings must match it)", lastStatePath_.c_str());
    if (runner_.snapshot().gamesTotal > 0){ ImGui::SameLine(); if (ImGui::Button("Show last run")) live_ = true; }
    if (!enough){ ImGui::SameLine(); ImGui::TextDisabled("Pick at least two engines."); }
    if (!lastError_.empty()){ ImGui::SameLine(); ImGui::TextColored(ui::kBad, "%s", lastError_.c_str()); }
}

void MatchScreen::start(bool resume){
    Settings& st = app_.settings();
    lastError_.clear();
    MatchSettings s;
    for (auto& n : setup_.engines){
        if (n.empty()) continue;
        const EngineConfig* c = ui::findEngine(app_.registry(), n);
        if (!c){ lastError_ = "Unknown engine '" + n + "'"; return; }
        s.engines.push_back(*c);
    }
    s.mode = setup_.mode;
    s.gauntletSeeds = setup_.gauntletSeeds;
    s.tc.baseMs = (int)std::lround(setup_.baseSec * 1000);
    s.tc.incMs = (int)std::lround(setup_.incSec * 1000);
    s.timeMarginMs = setup_.timeMarginMs;
    s.gamesPerPairing = setup_.gamesPerPairing;
    s.concurrency = setup_.concurrency > 0 ? setup_.concurrency : defaultConcurrency();
    s.bookPath = setup_.bookPath;
    s.bookPlies = setup_.bookPlies;
    s.bookRandom = setup_.bookRandom;
    s.seed = (uint64_t)setup_.seed;
    if (s.bookPath.empty() && !setup_.chess960) s.startFens = openings_;
    s.chess960 = setup_.chess960 && s.bookPath.empty();
    s.adj = setup_.adj;
    s.sprtEnabled = setup_.sprt && setup_.mode == TournamentMode::Match;
    s.sprt = setup_.sprtCfg;
    if (!resume){
        std::string dir = st.lastSaveDir.empty() ? std::string("games") : st.lastSaveDir;
        std::error_code ec; fs::create_directories(fs::u8path(dir), ec);
        std::string stem = dir + "/match_" + ui::timeStamp();
        lastPgnPath_ = setup_.savePgn ? stem + ".pgn" : "";
        lastStatePath_ = setup_.resumable ? stem + ".state.json" : "";
    }
    s.pgnPath = lastPgnPath_;
    s.statePath = lastStatePath_;
    std::string err;
    if (!runner_.start(s, &err)){ lastError_ = err; return; }
    lastSettings_ = s;
    savePresets();
    stopJudge();
    if (!judgeEngine_.empty()){
        if (const EngineConfig* c = ui::findEngine(app_.registry(), judgeEngine_)){
            EngineConfig cfg = *c;
            cfg.setOption("MultiPV", std::to_string(judgeLines_));
            judge_ = std::make_unique<AnalysisController>();
            if (!judge_->start(cfg, "Judge")){ app_.toast("Judge engine failed: " + judge_->lastError()); judge_.reset(); }
        }
    }
    minis_.assign(s.concurrency, BoardView());
    selectedGame_ = 0;
    flipped_ = false;
    live_ = true;
}

void MatchScreen::savePgns(){
    auto pgns = runner_.collectedPGNs();
    if (pgns.empty()){ app_.toast("No finished games yet"); return; }
    Settings& st = app_.settings();
    std::string path = filedialog::saveFile("Save games as PGN",
        (st.lastSaveDir.empty() ? std::string("games") : st.lastSaveDir) + "/match_" + ui::timeStamp() + ".pgn",
        {"PGN files", "*.pgn"});
    if (path.empty()){
        if (filedialog::available()) return;
        std::error_code ec; fs::create_directories("games", ec);
        path = "games/match_" + ui::timeStamp() + ".pgn";
    } else st.lastSaveDir = fs::u8path(path).parent_path().u8string();
    std::ofstream f(fs::u8path(path));
    for (auto& p : pgns) f << p << "\n";
    app_.toast(f ? "Saved " + path + " (" + std::to_string(pgns.size()) + " games)" : "Could not write " + path);
}

void MatchScreen::exportJson(){
    Settings& st = app_.settings();
    std::string path = filedialog::saveFile("Export results as JSON",
        (st.lastSaveDir.empty() ? std::string("games") : st.lastSaveDir) + "/match_" + ui::timeStamp() + ".json",
        {"JSON files", "*.json"});
    if (path.empty()){
        if (filedialog::available()) return;
        std::error_code ec; fs::create_directories("games", ec);
        path = "games/match_" + ui::timeStamp() + ".json";
    }
    std::ofstream f(fs::u8path(path));
    f << runner_.resultsJson() << "\n";
    app_.toast(f ? "Saved " + path : "Could not write " + path);
}

// ---------------------------------------------------------------- live view
void MatchScreen::drawStats(const MatchRunner::Snapshot& s){
    float em = ImGui::GetFontSize();
    if (s.sprt){
        ImGui::SeparatorText("SPRT");
        double span = s.llrUpper - s.llrLower;
        float frac = span > 0 ? (float)std::max(0.0, std::min(1.0, (s.llr - s.llrLower) / span)) : 0.f;
        char ov[96]; std::snprintf(ov, sizeof ov, "LLR %.2f   (%.2f, %.2f)", s.llr, s.llrLower, s.llrUpper);
        ImVec4 col = s.sprtOutcome == SprtOutcome::AcceptH1 ? ui::kGood
                   : s.sprtOutcome == SprtOutcome::AcceptH0 ? ui::kBad : ui::kAccent;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
        ImGui::ProgressBar(frac, ImVec2(-1, 0), ov);
        ImGui::PopStyleColor();
        const SprtConfig& c = lastSettings_.sprt;
        ImGui::TextDisabled("[%.2f, %.2f] %s Elo, alpha %.3f, beta %.3f", c.elo0, c.elo1, sprtModelName(c.model), c.alpha, c.beta);
        if (s.sprtOutcome == SprtOutcome::AcceptH1)
            ImGui::TextColored(ui::kGood, "H1 accepted: engine A is stronger by about elo1 or more.");
        else if (s.sprtOutcome == SprtOutcome::AcceptH0)
            ImGui::TextColored(ui::kBad, "H0 accepted: engine A is not stronger by elo1.");
    }
    ImGui::SeparatorText("Results");
    ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("pairings", 7, tf)){
        ImGui::TableSetupColumn("Pairing", ImGuiTableColumnFlags_WidthStretch);
        for (const char* h : {"W-D-L", "Elo", "nElo", "LOS", "Pairs 0-.5-1-1.5-2", "Time/crash"}) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (auto& p : s.pairings){
            // pentanomial when there are pairs; it has no variance when every pair scores the same
            EloEstimate e = eloFromPenta(p.penta);
            if (!e.valid) e = eloFromWdl(p.wdl);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s - %s", s.standings[p.a].name.c_str(), s.standings[p.b].name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d-%d-%d", p.wdl.wins, p.wdl.draws, p.wdl.losses);
            ImGui::TableNextColumn();
            if (e.valid) ImGui::Text("%+.1f +/- %.1f", e.elo, e.eloError); else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (e.valid) ImGui::Text("%+.1f +/- %.1f", e.nelo, e.neloError); else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (e.valid) ImGui::Text("%.1f%%", e.los * 100); else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            ImGui::Text("%d %d %d %d %d", p.penta.ll, p.penta.ld, p.penta.wl + p.penta.dd, p.penta.wd, p.penta.ww);
            ImGui::TableNextColumn();
            ImGui::TextColored(p.timeLosses || p.crashes ? ui::kWarn : ui::kDim, "%d / %d", p.timeLosses, p.crashes);
        }
        ImGui::EndTable();
    }
    if (s.standings.size() > 2 && ImGui::BeginTable("standings", 5, tf)){
        ImGui::TableSetupColumn("Engine", ImGuiTableColumnFlags_WidthStretch);
        for (const char* h : {"Games", "Points", "Score", "W-D-L"}) ImGui::TableSetupColumn(h, 0, em * 4.5f);
        ImGui::TableHeadersRow();
        auto table = s.standings;
        std::stable_sort(table.begin(), table.end(), [](const Standing& a, const Standing& b){ return a.points() > b.points(); });
        for (auto& t : table){
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(t.name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d", t.games());
            ImGui::TableNextColumn(); ImGui::Text("%.1f", t.points());
            ImGui::TableNextColumn(); ImGui::Text("%.1f%%", t.games() ? 100.0 * t.points() / t.games() : 0.0);
            ImGui::TableNextColumn(); ImGui::Text("%d-%d-%d", t.wins, t.draws, t.losses);
        }
        ImGui::EndTable();
    }
}

void MatchScreen::drawBoards(const MatchRunner::Snapshot& s){
    if (s.live.size() <= 1) return;
    float em = ImGui::GetFontSize();
    ImGui::SeparatorText("Games in progress (click one to watch it)");
    float cell = em * 7;
    float step = cell + ImGui::GetStyle().ItemSpacing.x;
    int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x) / step));
    if (minis_.size() < s.live.size()) minis_.resize(s.live.size());
    for (size_t i = 0; i < s.live.size(); i++){
        if (i % perRow) ImGui::SameLine();
        const LiveGame& g = s.live[i];
        ImGui::BeginGroup();
        BoardSpec spec;
        spec.pos = &g.pos; spec.interactive = false; spec.showCoords = false; spec.flipped = flipped_;
        spec.theme = app_.settings().boardTheme; spec.lastFrom = g.lastFrom; spec.lastTo = g.lastTo;
        spec.dimmed = !g.active;
        char id[32]; std::snprintf(id, sizeof id, "mini%d", (int)i);
        minis_[i].draw(id, cell, spec, app_.pieces());
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) selectedGame_ = (int)i;
        if ((int)i == selectedGame_)
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                ImGui::GetColorU32(ui::kAccent), 0, 0, 3);
        std::string label = g.active ? "#" + std::to_string(g.index + 1) + "  " + g.whiteName + " - " + g.blackName : "idle";
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cell);
        ImGui::TextDisabled("%s", label.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }
}

void MatchScreen::drawLive(){
    auto s = runner_.snapshot();
    float em = ImGui::GetFontSize();
    if (selectedGame_ >= (int)s.live.size()) selectedGame_ = 0;
    LiveGame g = s.live.empty() ? LiveGame{} : s.live[selectedGame_];
    // clocks tick between the runner's posts
    long long wc = g.wClockMs, bc = g.bClockMs;
    if (g.active && s.running && g.postedAtMs > 0){
        long long el = steadyMs() - g.postedAtMs;
        if (el > 0){ if (g.pos.whiteToMove) wc -= el; else bc -= el; }
    }
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float side = std::min(avail.y - ImGui::GetFrameHeightWithSpacing() * 2.5f, avail.x * 0.45f);
    side = std::max(side, em * 8);
    float barW = em * 1.2f;

    ImGui::BeginGroup();
    auto player = [&](bool white){
        bool toMove = g.active && g.pos.whiteToMove == white;
        ImGui::PushFont(ui::fonts().large);
        ImGui::TextColored(toMove ? ImVec4(1, 1, 1, 1) : ui::kDim, "%s   %s",
                           ui::clockText(white ? wc : bc).c_str(), (white ? g.whiteName : g.blackName).c_str());
        ImGui::PopFont();
    };
    player(flipped_);
    // eval bar: the judge if there is one, else the players' own reports
    int cpW = g.scoreCpWhite, mateW = g.mateWhite; bool isMate = g.scoreIsMate;
    std::vector<BoardArrow> arrows;
    std::string judgeLine;
    if (judge_){
        auto jp = judge_->pvs();
        if (!judge_->alive()) judgeLine = "The judge engine terminated.";
        else if (!jp.empty() && jp[0].valid && judge_->fen() == g.pos.toFEN()){
            cpW = g.pos.whiteToMove ? jp[0].scoreCp : -jp[0].scoreCp;
            isMate = jp[0].isMate;
            mateW = g.pos.whiteToMove ? jp[0].mateIn : -jp[0].mateIn;
            static const int alpha[5] = {210, 120, 85, 62, 45};
            for (int r = std::min((int)jp.size(), 5) - 1; r >= 0; r--){
                Move m;
                if (jp[r].valid && g.pos.uciToMove(jp[r].firstMove(), m))
                    arrows.push_back({m.from, m.to, IM_COL32(70,140,220,alpha[r])});
            }
            Move bm;
            judgeLine = "Judge " + judge_->engineName() + ": best "
                      + (g.pos.uciToMove(jp[0].firstMove(), bm) ? g.pos.moveToSAN(bm) : jp[0].firstMove())
                      + " " + formatScore(cpW, isMate, mateW) + " (depth " + std::to_string(jp[0].depth) + ")";
        } else judgeLine = "Judge: waiting for analysis...";
    }
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(barW - ImGui::GetStyle().ItemSpacing.x, side));
    drawEvalBar(ImGui::GetWindowDrawList(), p0, ImVec2(p0.x + barW - ImGui::GetStyle().ItemSpacing.x, p0.y + side),
                cpW, isMate, mateW, flipped_);
    ImGui::SameLine();
    BoardSpec spec;
    spec.pos = &g.pos; spec.flipped = flipped_; spec.interactive = false;
    spec.theme = app_.settings().boardTheme; spec.showCoords = app_.settings().showCoordinates;
    spec.lastFrom = g.lastFrom; spec.lastTo = g.lastTo;
    spec.arrows = arrows;
    board_.draw("match-board", side, spec, app_.pieces());
    player(!flipped_);
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginChild("match-side", ImVec2(0, 0));
    ImGui::PushFont(ui::fonts().large);
    ImGui::Text("%d / %d games", s.gamesDone, s.gamesTotal);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s, %s%s", tournamentModeName(lastSettings_.mode), lastSettings_.tc.label().c_str(),
                        s.gamesResumed ? (", " + std::to_string(s.gamesResumed) + " resumed").c_str() : "");
    ImGui::ProgressBar(s.gamesTotal ? (float)s.gamesDone / s.gamesTotal : 0.f, ImVec2(-1, em * 0.4f), "");
    if (runner_.running()){ if (ImGui::Button("Stop")) runner_.abort(); }
    else if (ImGui::Button("Back to setup")){ stopJudge(); live_ = false; }
    ImGui::SameLine(); if (ImGui::Button("Save PGNs...")) savePgns();
    ImGui::SameLine(); if (ImGui::Button("Export JSON...")) exportJson();
    ImGui::SameLine(); if (ImGui::Button("Flip")) flipped_ = !flipped_;
    if (!lastPgnPath_.empty()) ImGui::TextDisabled("Writing %s", lastPgnPath_.c_str());
    if (!judgeLine.empty())
        ImGui::TextColored(judgeLine.find("terminated") != std::string::npos ? ui::kBad : ui::kGood, "%s", judgeLine.c_str());
    if (s.finished) ImGui::TextColored(ui::kGood, "Finished.");
    else if (!s.running) ImGui::TextColored(ui::kWarn, "Stopped.%s", lastStatePath_.empty() ? "" : " Resume continues from the state file.");
    else if (!s.status.empty()) ImGui::TextDisabled("%s", s.status.c_str());
    drawStats(s);
    drawBoards(s);
    if (ImGui::CollapsingHeader("Search stats (watched game)")){
        ImGui::TextUnformatted(g.whiteName.c_str());
        ui::searchStats("w", g.stats, true);
        ImGui::TextUnformatted(g.blackName.c_str());
        ui::searchStats("b", g.stats, false);
    }
    ImGui::SeparatorText("Log");
    ImGui::BeginChild("matchlog", ImVec2(0, std::max(em * 6, ImGui::GetContentRegionAvail().y)), ImGuiChildFlags_Borders);
    ImGui::PushFont(ui::fonts().mono);
    bool atEnd = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1;
    for (auto& l : s.log) ImGui::TextUnformatted(l.c_str());
    if (atEnd) ImGui::SetScrollHereY(1.0f);
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::EndChild();
}
