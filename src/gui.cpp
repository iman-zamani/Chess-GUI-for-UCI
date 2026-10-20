/* Chess-GUI-for-UCI — application shell (window, ImGui, screens). GPL-2.0 */
#include "gui.hpp"
#include "ui/file_dialog.hpp"
#include "ui/item_hooks.hpp"
#include "ui/widgets.hpp"
#include <imgui.h>
#include <imgui-SFML.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

static long long nowMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// =============================================================== init
bool App::init(){
#ifdef _WIN32
    // Opt out of Windows DPI virtualization (which blurs by upscaling 96-dpi
    // renders); we scale the UI ourselves.
    SetProcessDPIAware();
#endif
    std::string err;
    if (!settings_.load((fs::u8path(userConfigDir()) / "settings.json").u8string(), &err))
        toast("Could not read settings.json: " + err);

    // UI scale from the screen size: 1080p -> 1x, 4K -> 2x. CHESSGUI_SCALE overrides it.
    sf::Vector2u desk = sf::VideoMode::getDesktopMode().size;
    float uiScale = std::max(1.f, std::min(3.f, std::floor(std::min(desk.x / 1280.f, desk.y / 800.f))));
    if (const char* env = std::getenv("CHESSGUI_SCALE")){
        float v = (float)std::atof(env);
        if (v >= 0.75f && v <= 4.f) uiScale = v;
    }
    unsigned w = (unsigned)(settings_.windowW > 0 ? settings_.windowW : 1280);
    unsigned h = (unsigned)(settings_.windowH > 0 ? settings_.windowH : 800);
    w = std::min(w, (unsigned)(desk.x / uiScale)); h = std::min(h, (unsigned)(desk.y / uiScale));
    window_.create(sf::VideoMode({(unsigned)(w * uiScale), (unsigned)(h * uiScale)}), "Chess GUI for UCI",
                   sf::Style::Default);
    // On macOS the requested size is in points; with Retina backing the real
    // framebuffer is 2x that. Everything below works in framebuffer pixels.
    float backing = window_.getSize().x / (w * uiScale);
    dpiScale_ = uiScale * std::max(1.f, backing);
    std::fprintf(stderr, "[hidpi] requested %ux%u, framebuffer %ux%u, backing scale %.2f\n",
                 (unsigned)(w * uiScale), (unsigned)(h * uiScale), window_.getSize().x, window_.getSize().y, backing);
    window_.setFramerateLimit(60);
    if (sf::Image icon; icon.loadFromFile("Resources/icon.png")) window_.setIcon(icon);

    imguiOk_ = ImGui::SFML::Init(window_, false);
    if (!imguiOk_){ std::fprintf(stderr, "ImGui initialisation failed\n"); return false; }
    ImGui::GetIO().IniFilename = nullptr;                 // no imgui.ini next to the binary
    setupFonts();
    if (!pieces_.load("Resources/pieceTexture@2x.png") && !pieces_.load("Resources/pieceTexture.png"))
        std::fprintf(stderr, "Piece images not found; using letters\n");
    scanEngines();
    game_.newHumanGame(Position::startpos());
    return true;
}

// ImGui draws in physical pixels; fonts are rasterized at the real pixel
// size so text stays sharp on HiDPI screens.
void App::setupFonts(){
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig fc; fc.OversampleH = 2;
    ui::Fonts& f = ui::fonts();
    f.body = io.Fonts->AddFontFromFileTTF("Resources/Cantarell-Regular.ttf", 15.f * dpiScale_, &fc);
    if (!f.body) f.body = io.Fonts->AddFontDefault();
    f.mono = io.Fonts->AddFontFromFileTTF("Resources/JetBrainsMono-Regular.ttf", 13.5f * dpiScale_, &fc);
    if (!f.mono) f.mono = f.body;
    f.large = io.Fonts->AddFontFromFileTTF("Resources/Cantarell-Regular.ttf", 24.f * dpiScale_, &fc);
    if (!f.large) f.large = f.body;
    if (!ImGui::SFML::UpdateFontTexture()) std::fprintf(stderr, "[imgui] font texture failed\n");
    ImGuiStyle st;
    if (settings_.theme == "light") ImGui::StyleColorsLight(&st); else ImGui::StyleColorsDark(&st);
    st.FrameRounding = 3; st.WindowRounding = 4; st.ChildRounding = 3; st.GrabRounding = 3; st.PopupRounding = 4;
    st.ScaleAllSizes(dpiScale_);
    ImGui::GetStyle() = st;
}

// =============================================================== services
void App::scanEngines(){
    std::string file = (fs::u8path(userConfigDir()) / "engines.json").u8string();
    std::string err;
    if (!registry_.load(file, &err)) toast("Could not read engines.json: " + err);
    registry_.discover("Resources/engines");
    std::error_code ec;
    for (const char* p : {"/usr/games/stockfish", "/usr/bin/stockfish", "/usr/local/bin/stockfish",
                          "/opt/homebrew/bin/stockfish", "C:\\stockfish\\stockfish.exe"})
        if (fs::exists(p, ec) && !registry_.findByPath(p)){
            EngineConfig c; c.path = p; c.name = fs::path(p).filename().string(); c.discovered = true;
            registry_.add(c);
        }
}

void App::saveEngines(){
    std::string err;
    if (!registry_.save((fs::u8path(userConfigDir()) / "engines.json").u8string(), &err))
        toast("Could not save engines.json: " + err);
}

void App::saveSettings(){
    std::string err;
    if (!settings_.save((fs::u8path(userConfigDir()) / "settings.json").u8string(), &err))
        toast("Could not save settings.json: " + err);
}

void App::toast(const std::string& s){ toastText_ = s; toastClock_.restart(); }

void App::go(Screen s){
    if (screen_ == Screen::GAME && s != Screen::GAME) game_.leave();
    if (s == Screen::ENGINES) scanEngines();
    screen_ = s;
}

void App::openEditor(const Position& p){
    editor_.setPosition(p);
    editor_.open(false);
    go(Screen::EDITOR);
}

void App::openEditorForMatch(){
    editor_.open(true);
    go(Screen::EDITOR);
}

void App::editorDone(EditorAction a, const Position& p){
    switch (a){
    case EditorAction::PlayHuman: game_.newHumanGame(p); go(Screen::GAME); break;
    case EditorAction::PlayEngine: go(Screen::GAME); game_.showNewGameDialog(GameMode::HUMAN_VS_ENGINE, p); break;
    case EditorAction::Analyse: game_.openAnalysis(p); go(Screen::GAME); break;
    case EditorAction::AddToMatch:
        match_.addOpening(p.toFEN());
        toast("Position added to the match (" + std::to_string(match_.openingCount()) + " total)");
        go(Screen::MATCH); break;
    case EditorAction::Cancel: go(Screen::MATCH); break;
    }
}

bool App::animating() const{
    return (screen_ == Screen::GAME && game_.animating()) || match_.animating() || tools_.busy()
        || !toastText_.empty();
}

// =============================================================== main loop
void App::processEvent(const sf::Event& ev){
    if (ev.is<sf::Event::Closed>()){ window_.close(); return; }
    if (const auto* r = ev.getIf<sf::Event::Resized>())
        window_.setView(sf::View(sf::FloatRect({0, 0}, {(float)r->size.x, (float)r->size.y})));
    ImGui::SFML::ProcessEvent(window_, ev);
}

void App::beginImGuiFrame(){
    uiitems::beginFrame();
    sf::Vector2u ws = window_.getSize();
    if (!script_.empty()){
        // scripted run: drive ImGui's IO directly, so a scripted button hold
        // isn't overwritten by the real (idle) mouse every frame
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)ws.x, (float)ws.y);
        io.DeltaTime = std::max(1e-4f, frameClock_.restart().asSeconds());
        if (scriptMouse_) io.MousePos = *scriptMouse_;
        io.MouseDown[0] = scriptButtonDown_;
        ImGui::NewFrame();
    } else ImGui::SFML::Update(window_, frameClock_.restart());
}

void App::run(){
    while (window_.isOpen()){
        bool active = animating() || scriptPos_ < script_.size();
        // idle: sleep until input arrives (or redraw a few times a second)
        if (!active){
            if (const std::optional ev = window_.waitEvent(sf::milliseconds(250))) processEvent(*ev);
        }
        while (const std::optional ev = window_.pollEvent()) processEvent(*ev);
        if (!window_.isOpen()) break;
        if (scriptPos_ < script_.size()) stepScript();
        if (!window_.isOpen()) break;
        if (screen_ == Screen::GAME) game_.update();
        match_.update();
        beginImGuiFrame();
        drawFrame();
        window_.clear(sf::Color(24, 24, 28));
        ImGui::SFML::Render(window_);
        if (!pendingShot_.empty()){
            sf::Texture shot;
            if (shot.resize(window_.getSize())){
                shot.update(window_);
                if (!shot.copyToImage().saveToFile(pendingShot_))
                    std::fprintf(stderr, "[script] could not save %s\n", pendingShot_.c_str());
            }
            pendingShot_.clear();
        }
        window_.display();
    }
    // remember the window size in requested-size units
    sf::Vector2u ws = window_.getSize();
    if (ws.x > 0 && dpiScale_ > 0){
        settings_.windowW = (int)(ws.x / dpiScale_); settings_.windowH = (int)(ws.y / dpiScale_);
        saveSettings();
    }
    game_.shutdown();
    match_.shutdown();
    tools_.job().abort();
    ImGui::SFML::Shutdown();
}

void App::handleGlobalKeys(){
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && screen_ != Screen::HOME) go(Screen::HOME);
}

void App::drawFrame(){
    handleGlobalKeys();
    drawMenuBar();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##screen", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (screen_ != Screen::HOME && screen_ != Screen::GAME){
        static const char* titles[] = {"", "", "Engine match / tournament", "Engine testing tools",
                                       "Board editor", "Engines"};
        if (ImGui::Button("< Home")) go(Screen::HOME);
        ImGui::SameLine();
        ImGui::PushFont(ui::fonts().large);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(titles[(int)screen_]);
        ImGui::PopFont();
        ImGui::Separator();
    }
    switch (screen_){
    case Screen::HOME: drawHome(); break;
    case Screen::GAME: game_.draw(); break;
    case Screen::MATCH: match_.draw(); break;
    case Screen::TOOLS: tools_.draw(); break;
    case Screen::EDITOR: editor_.draw(); break;
    case Screen::ENGINES: {
        EngineManagerWindow::Callbacks cb;
        cb.save = [this](){ saveEngines(); };
        if (filedialog::available())
            cb.browseFile = [this](const std::string& t){
                std::string p = filedialog::openFile(t, settings_.lastEngineDir);
                if (!p.empty()){ settings_.lastEngineDir = fs::u8path(p).parent_path().u8string(); saveSettings(); }
                return p;
            };
        engineManager_.draw(registry_, cb);
        break; }
    }
    ImGui::End();
    drawAbout();
    drawToast();
}

void App::drawMenuBar(){
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("Game")){
        if (ImGui::MenuItem("New game...")){ go(Screen::GAME); game_.showNewGameDialog(GameMode::HUMAN_VS_ENGINE); }
        if (ImGui::MenuItem("Human vs human")){ game_.newHumanGame(Position::startpos()); go(Screen::GAME); }
        if (ImGui::MenuItem("Analysis board")){ game_.openAnalysis(Position::startpos()); go(Screen::GAME); }
        if (ImGui::MenuItem("Open PGN...")){ go(Screen::GAME); game_.openPgnDialog(); }
        if (ImGui::MenuItem("Board editor")) openEditor(Position::startpos());
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) window_.close();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Engines")){
        if (ImGui::MenuItem("Manage engines...")) go(Screen::ENGINES);
        if (ImGui::MenuItem("Engine match / tournament...")) go(Screen::MATCH);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")){
        if (ImGui::MenuItem("Engine testing tools")) go(Screen::TOOLS);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")){
        if (ImGui::MenuItem("Flip board", "F", false, screen_ == Screen::GAME)) game_.flip();
        if (ImGui::MenuItem("Show coordinates", nullptr, &settings_.showCoordinates)) saveSettings();
        if (ImGui::BeginMenu("Board colours")){
            for (auto& t : boardThemes())
                if (ImGui::MenuItem(t.name, nullptr, settings_.boardTheme == t.name)){ settings_.boardTheme = t.name; saveSettings(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Theme")){
            for (const char* t : {"dark", "light"})
                if (ImGui::MenuItem(t, nullptr, settings_.theme == t)){ settings_.theme = t; saveSettings(); setupFonts(); }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")){
        if (ImGui::MenuItem("About")) showAbout_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void App::drawHome(){
    float em = ImGui::GetFontSize();
    float w = em * 24;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPosX(std::max(0.f, (avail.x - w) / 2));
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0, em));
    ImGui::PushFont(ui::fonts().large);
    ImGui::TextColored(ui::kAccent, "Chess GUI for UCI");
    ImGui::PopFont();
    ImGui::TextDisabled("Play, analyse, run engine matches, and test your own UCI engine.");
    ImGui::Dummy(ImVec2(0, em * 0.5f));
    ImVec2 bs(w, em * 2.4f);
    if (ImGui::Button("Play vs engine...", bs)){ go(Screen::GAME); game_.showNewGameDialog(GameMode::HUMAN_VS_ENGINE); }
    if (ImGui::Button("Human vs human", bs)){ game_.newHumanGame(Position::startpos()); go(Screen::GAME); }
    if (ImGui::Button("Analysis board", bs)){ game_.openAnalysis(Position::startpos()); go(Screen::GAME); }
    if (ImGui::Button("Open PGN...", bs)){ go(Screen::GAME); game_.openPgnDialog(); }
    if (ImGui::Button("Board editor", bs)) openEditor(Position::startpos());
    ImGui::Dummy(ImVec2(0, em * 0.5f));
    ImGui::TextDisabled("For engine developers");
    if (ImGui::Button("Engine match / tournament", bs)) go(Screen::MATCH);
    if (ImGui::Button("Engine testing tools", bs)) go(Screen::TOOLS);
    if (ImGui::Button("Engines...", bs)) go(Screen::ENGINES);
    ImGui::Dummy(ImVec2(0, em * 0.5f));
    size_t n = registry_.engines().size();
    if (n == 0) ImGui::TextColored(ui::kWarn, "No engines yet: add one in Engines..., or drop executables\n"
                                              "into Resources/engines/ next to the program.");
    else ImGui::TextDisabled("%d engine%s configured.", (int)n, n == 1 ? "" : "s");
    if (ImGui::Button("Quit", ImVec2(w, em * 1.8f))) window_.close();
    ImGui::EndGroup();
}

void App::drawAbout(){
    if (showAbout_){ ImGui::OpenPopup("About"); showAbout_ = false; }
    if (ImGui::BeginPopupModal("About", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextUnformatted("Chess GUI for UCI");
        ImGui::TextDisabled("GPL-2.0. A GUI for playing, analysing and testing UCI chess engines.");
        ImGui::Separator();
        ImGui::TextUnformatted("Third-party components:");
        ImGui::BulletText("SFML 3 (zlib), Dear ImGui and ImGui-SFML (MIT)");
        ImGui::BulletText("nlohmann/json (MIT), portable-file-dialogs (WTFPL)");
        ImGui::BulletText("Cantarell and JetBrains Mono fonts (SIL OFL 1.1)");
        ImGui::Text("Settings and engines are stored in:");
        ImGui::TextDisabled("%s", userConfigDir().c_str());
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::drawToast(){
    if (toastText_.empty()) return;
    if (toastClock_.getElapsedTime().asSeconds() > 3.5f){ toastText_.clear(); return; }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x / 2, vp->WorkPos.y + vp->WorkSize.y - 20),
                            ImGuiCond_Always, ImVec2(0.5f, 1.f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("##toast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoInputs);
    ImGui::TextUnformatted(toastText_.c_str());
    ImGui::End();
}

// =============================================================== scripted input
bool App::loadScript(const std::string& path){
    std::string err;
    script_.clear(); scriptPos_ = 0;
    if (!loadUiScript(path, script_, &err)){ std::fprintf(stderr, "[script] %s\n", err.c_str()); return false; }
    return true;
}

ImVec2 App::logicalToPixels(float x, float y) const{
    return ImVec2(x * dpiScale_, y * dpiScale_);
}

BoardView* App::activeBoard(){
    if (screen_ == Screen::GAME) return &game_.board();
    if (screen_ == Screen::EDITOR) return &editor_.board();
    return nullptr;
}

static bool keyFromName(const std::string& n, sf::Keyboard::Key& k){
    using K = sf::Keyboard::Key;
    static const std::pair<const char*, K> named[] = {
        {"Left",K::Left},{"Right",K::Right},{"Up",K::Up},{"Down",K::Down},
        {"Escape",K::Escape},{"Enter",K::Enter},{"Space",K::Space},{"Tab",K::Tab},
        {"Backspace",K::Backspace},{"Delete",K::Delete},{"Home",K::Home},{"End",K::End},
    };
    for (auto& p : named) if (n == p.first){ k = p.second; return true; }
    if (n.size()==1 && n[0]>='A' && n[0]<='Z'){ k = (K)((int)K::A + (n[0]-'A')); return true; }
    return false;
}

static ImGuiKey imguiKey(sf::Keyboard::Key k){
    using K = sf::Keyboard::Key;
    switch (k){
    case K::Enter: return ImGuiKey_Enter;   case K::Escape: return ImGuiKey_Escape;
    case K::Tab: return ImGuiKey_Tab;       case K::Backspace: return ImGuiKey_Backspace;
    case K::Delete: return ImGuiKey_Delete; case K::Left: return ImGuiKey_LeftArrow;
    case K::Right: return ImGuiKey_RightArrow; case K::Up: return ImGuiKey_UpArrow;
    case K::Down: return ImGuiKey_DownArrow; case K::Home: return ImGuiKey_Home;
    case K::End: return ImGuiKey_End;       case K::Space: return ImGuiKey_Space;
    default:
        if (k >= K::A && k <= K::Z) return (ImGuiKey)(ImGuiKey_A + ((int)k - (int)K::A));
        return ImGuiKey_None;
    }
}

void App::scriptEvent(const ScriptEvent& e){
    ImGuiIO& io = ImGui::GetIO();
    switch (e.kind){
    case ScriptEvent::MousePressed:  scriptButtonDown_ = true; break;
    case ScriptEvent::MouseReleased: scriptButtonDown_ = false; break;
    case ScriptEvent::KeyPressed: {
        ImGuiKey k = imguiKey(e.key);
        if (k == ImGuiKey_None) break;
        if (e.ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(k, true); io.AddKeyEvent(k, false);
        if (e.ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, false);
        break; }
    case ScriptEvent::TextEntered: io.AddInputCharacter((unsigned)e.unicode); break;
    default: break;
    }
}

// Executes the next due step; at most one step per frame so the UI gets a
// frame between inputs, like a real user.
void App::stepScript(){
    if (nowMs() < scriptWaitUntil_) return;
    const UiScriptStep s = script_[scriptPos_++];        // copy: steps may insert more steps
    ScriptEvent e;
    auto insert = [&](std::vector<UiScriptStep> steps){
        script_.insert(script_.begin() + (long)scriptPos_, steps.begin(), steps.end());
    };
    auto step = [](const char* op, std::vector<float> nums = {}){ UiScriptStep x; x.op = op; x.nums = nums; return x; };
    auto squarePoint = [&](const std::string& name, ImVec2& out){
        BoardView* b = activeBoard();
        int sq = nameToSquare(name);
        if (!b || sq < 0){
            std::fprintf(stderr, "[script] no board or bad square '%s'\n", name.c_str());
            scriptFailed_ = true; return false;
        }
        out = b->squareCenter(sq);
        out = ImVec2(out.x / dpiScale_, out.y / dpiScale_);
        return true;
    };
    if (s.op == "wait") scriptWaitUntil_ = nowMs() + (long long)s.nums[0];
    else if (s.op == "move") scriptMouse_ = logicalToPixels(s.nums[0], s.nums[1]);
    else if (s.op == "click"){
        // like a user: hover for a frame, then press, then release on the next frame
        scriptMouse_ = logicalToPixels(s.nums[0], s.nums[1]);
        insert({step("_press"), step("_release")});
    }
    else if (s.op == "_press"){ e.kind = ScriptEvent::MousePressed; scriptEvent(e); }
    else if (s.op == "_release"){ e.kind = ScriptEvent::MouseReleased; scriptEvent(e); }
    else if (s.op == "drag"){
        scriptMouse_ = logicalToPixels(s.nums[0], s.nums[1]);
        insert({step("_press"), step("move", {(s.nums[0] + s.nums[2]) / 2, (s.nums[1] + s.nums[3]) / 2}),
                step("move", {s.nums[2], s.nums[3]}), step("_release")});
    }
    else if (s.op == "click-square"){
        ImVec2 p;
        if (squarePoint(s.arg, p)) insert({step("click", {p.x, p.y})});
    }
    else if (s.op == "drag-square"){
        std::istringstream ss(s.arg); std::string a, b; ss >> a >> b;
        ImVec2 p, q;
        if (squarePoint(a, p) && squarePoint(b, q)) insert({step("drag", {p.x, p.y, q.x, q.y})});
    }
    else if (s.op == "key"){
        if (keyFromName(s.arg, e.key)){ e.kind = ScriptEvent::KeyPressed; e.ctrl = s.ctrl; scriptEvent(e); }
        else std::fprintf(stderr, "[script] unknown key %s\n", s.arg.c_str());
    }
    else if (s.op == "text"){
        for (unsigned char c : s.arg){ e.kind = ScriptEvent::TextEntered; e.unicode = c; scriptEvent(e); }
    }
    else if (s.op == "click-item" || s.op == "wait-item"){
        auto items = uiitems::find(s.arg);
        if (s.op == "wait-item"){
            if (items.empty()){
                if (!scriptItemDeadline_) scriptItemDeadline_ = nowMs() + (long long)s.nums[0];
                if (nowMs() < scriptItemDeadline_){ scriptPos_--; return; }   // try again next frame
                std::fprintf(stderr, "[script] no widget '%s' after %.0f ms\n", s.arg.c_str(), s.nums[0]);
                scriptFailed_ = true;
            }
            scriptItemDeadline_ = 0;
        } else if (items.empty()){
            std::fprintf(stderr, "[script] no widget labelled '%s'\n", s.arg.c_str());
            scriptFailed_ = true;
        } else {
            float cx = (items[0].x0 + items[0].x1) / 2, cy = (items[0].y0 + items[0].y1) / 2;
            insert({step("click", {cx / dpiScale_, cy / dpiScale_})});
        }
    }
    else if (s.op == "shot") pendingShot_ = s.arg;
    else if (s.op == "expect"){
        std::string fen = game_.shownFen();
        if (fen.compare(0, s.arg.size(), s.arg) != 0){
            std::fprintf(stderr, "[script] expected position '%s' but have '%s'\n", s.arg.c_str(), fen.c_str());
            scriptFailed_ = true;
        }
    }
    else if (s.op == "quit") window_.close();
}
