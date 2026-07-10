/* Chess-GUI-for-UCI — SFML GUI. GPL-2.0 */
#include "gui.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace fs = std::filesystem;

static const sf::Color COL_BG(24,24,28);
static const sf::Color COL_PANEL(38,38,46);
static const sf::Color COL_BTN(58,58,72);
static const sf::Color COL_BTN_HOT(80,80,100);
static const sf::Color COL_BTN_TOG(70,110,70);
static const sf::Color COL_LIGHT(240,217,181);
static const sf::Color COL_DARK(181,136,99);
static const sf::Color COL_SEL(110,160,90,180);
static const sf::Color COL_LAST_FROM(70,130,220,70);    // last move: origin (lighter)
static const sf::Color COL_LAST_TO(70,130,220,150);     // last move: destination (stronger)
static const sf::Color COL_PRE_FROM(220,60,60,70);      // premove origin
static const sf::Color COL_PRE_TO(220,60,60,150);       // premove destination
static const sf::Color COL_CHECK(220,80,80,170);
static const sf::Color COL_ACCENT(120,170,255);
static const sf::Color COL_DIM(170,170,180);
static const sf::Color COL_WARN(230,180,120);
static const sf::Color COL_ERR(255,130,130);
static const sf::Color COL_GOOD(140,230,140);

static float WW = 1280, WH = 800;   // LOGICAL size; window pixels = logical * UIS
static float UIS = 1.f;             // UI scale factor (1..3, auto-detected)
static float snapPx(float v){ return std::round(v*UIS)/UIS; }  // land on physical pixels
static long long nowMsSteady(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// =============================================================== AnalysisController
bool AnalysisController::start(const std::string& path,
        const std::vector<std::pair<std::string,std::string>>& opts){
    eng = std::make_unique<UciEngine>();
    if (!eng->start(path)){ err = eng->lastError(); eng.reset(); return false; }
    for (auto& o : opts) eng->setOption(o.first, o.second);
    eng->newGame();
    stop_ = false;
    th = std::thread([this](){ loop(); });
    return true;
}
void AnalysisController::loop(){
    std::string current;
    bool searching = false;
    while (!stop_){
        if (!eng->alive()){ std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
        std::string want;
        { std::lock_guard<std::mutex> lk(mx); want = target; }
        if (!want.empty() && want != current){
            if (searching) eng->stopAndWait(2000);
            eng->setPosition(want, {});
            eng->goInfinite();
            searching = true;
            current = want;
            continue;
        }
        eng->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (eng && eng->alive() && searching) eng->stopAndWait(800);
}
void AnalysisController::shutdown(){
    stop_ = true;
    if (th.joinable()) th.join();
    if (eng) eng->quit();
    eng.reset();
}
void AnalysisController::setFen(const std::string& fen){
    std::lock_guard<std::mutex> lk(mx);
    target = fen;
}
EngineInfo AnalysisController::info(){ return eng ? eng->lastInfo() : EngineInfo{}; }
std::vector<PvLine> AnalysisController::pvs(){ return eng ? eng->lastPvs() : std::vector<PvLine>{}; }
bool AnalysisController::alive(){ return eng && eng->alive(); }
std::string AnalysisController::engineName(){ return eng? eng->name() : "engine"; }

// =============================================================== init / run
bool App::init(){
#ifdef _WIN32
    // Opt out of Windows DPI virtualization (which blurs by upscaling 96-dpi
    // renders); we do our own scaling below.
    SetProcessDPIAware();
#endif
    sf::Vector2u desk = desktopSize();
    // UI scale: fit a 1280x800 logical canvas to the screen. 1080p -> 1x,
    // 4K -> 2x, 5K+ -> 3x. Override with the CHESSGUI_SCALE env var (e.g. 1.5).
    float fit = std::min(desk.x/1280.f, desk.y/800.f);
    UIS = std::max(1.f, std::min(3.f, std::floor(fit)));
    if (const char* env = std::getenv("CHESSGUI_SCALE")){
        float v = (float)atof(env);
        if (v >= 0.75f && v <= 4.f) UIS = v;
    }
    WW = std::min(1280.f, desk.x / UIS);
    WH = std::min(800.f,  desk.y / UIS);
    window.create(VM((unsigned)(WW*UIS),(unsigned)(WH*UIS)),
                  "Chess GUI for UCI", sf::Style::Default);
    // --- Retina self-check ---------------------------------------------------
    // On macOS, VideoMode sizes are in points. If Retina backing was granted,
    // the real framebuffer is scale-x bigger than what we asked for.
    {
        float bs = window.getSize().x / (WW*UIS);   // backing scale (1.0 or 2.0)
        fprintf(stderr, "[hidpi] requested %.0fx%.0f, framebuffer %ux%u, backing scale %.2f\n",
                WW*UIS, WH*UIS, window.getSize().x, window.getSize().y, bs);
    #ifdef __APPLE__
        if (bs < 1.5f)
            fprintf(stderr, "[hidpi] NO Retina backing! Check SFML version and Info.plist.\n");
    #endif
    }
    window.setView(sf::View(FR(0,0,WW,WH)));   // draw in logical units
    window.setFramerateLimit(60);
    const char* fontPaths[] = {
        "Resources/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "C:\\Windows\\Fonts\\arial.ttf"
    };
    bool fontOk=false;
    for (auto p : fontPaths) if (loadFont(font, p)){ fontOk=true; break; }
    if (!fontOk){ fprintf(stderr,"No font found (put arial.ttf in Resources/)\n"); return false; }
    // Prefer the high-resolution sheet: bitmaps only stay sharp when they are
    // DOWNscaled. 150px pieces upscaled to ~190px on HiDPI screens is exactly
    // the "blurry pieces" effect.
    piecesOk = pieceTexture.loadFromFile("Resources/pieceTexture@2x.png");
    if (!piecesOk) piecesOk = pieceTexture.loadFromFile("Resources/pieceTexture.png");
    if (piecesOk){
        pieceTexture.setSmooth(true);
        pieceCell = (int)(pieceTexture.getSize().x / 6);
        if (pieceCell <= 0) pieceCell = 150;
    }
    loadOverrides();
    scanEngines();
    game.reset(Position::startpos());
    editPos = Position::startpos();
    tc = TimeControl{300000,0};
    layout();
    return true;
}

int App::activityLevel(){
    if (dragging || editorPainting || handFrom>=0) return 2;
    bool bg = false;
    if (!toast.empty() && toastClock.getElapsedTime().asSeconds() < 3.2f) bg = true;
    if (screen==Screen::GAME &&
        (clocksRunning || oppState!=0 || (analysing && anaCtl))) bg = true;
    if (screen==Screen::MATCH_VIEW && (runner.running() || judgeCtl)) bg = true;
    if (screen==Screen::TEST_RUN && testRunning) bg = true;
    return bg ? 1 : 0;
}

void App::run(){
    sf::Clock bgClock;
    while (window.isOpen()){
        bool hadEvent = false;
        UiEvent e;
        while (pollUiEvent(window, e)){ handleEvent(e); hadEvent = true; }
        update();
        int act = activityLevel();
        if (act==2 || hadEvent || forceRedraw){
            render(); forceRedraw = false; bgClock.restart();
        } else if (act==1){
            if (bgClock.getElapsedTime().asMilliseconds() >= 80){ render(); bgClock.restart(); }
            else sf::sleep(sf::milliseconds(15));
        } else {
            sf::sleep(sf::milliseconds(40));
        }
    }
    stopOpponent(); stopAnalysis();
    if (judgeCtl){ judgeCtl->shutdown(); judgeCtl.reset(); }
    runner.abort();
    testAbort = true;
    if (testThread.joinable()) testThread.join();
}

// =============================================================== drawing helpers
void App::text(const std::string& s, float x, float y, unsigned size, sf::Color c, bool center){
    // Rasterize glyphs at (at least) the physical pixel size and scale down:
    // k >= UIS means the texture never gets upscaled on screen -> crisp text
    // at every UI scale; on 1x displays it supersamples.
    int k = std::max(2, (int)std::lround(UIS));
    sf::Text t = makeText(font, s, size*(unsigned)k);
    t.setScale({1.f/k, 1.f/k});
    t.setFillColor(c);
    if (center){
        auto b = t.getLocalBounds();
        t.setOrigin({std::round(rectL(b) + rectW(b)/2.f),
                     std::round(rectT(b) + rectH(b)/2.f)});
    }
    t.setPosition({snapPx(x), snapPx(y)});
    window.draw(t);
}
static bool hit(const sf::FloatRect& r, sf::Vector2f p){ return r.contains(p); }

void App::drawButtons(){
    sf::Vector2f m = window.mapPixelToCoords(sf::Mouse::getPosition(window));
    for (auto& b : buttons){
        sf::RectangleShape r(sf::Vector2f(rectW(b.rect), rectH(b.rect)));
        r.setPosition({std::round(rectL(b.rect)), std::round(rectT(b.rect))});
        sf::Color c = b.toggled? COL_BTN_TOG : (hit(b.rect,m)&&b.enabled ? COL_BTN_HOT : COL_BTN);
        if (!b.enabled) c = sf::Color(45,45,50);
        r.setFillColor(c);
        r.setOutlineThickness(1); r.setOutlineColor(sf::Color(90,90,110));
        window.draw(r);
        text(b.label, rectL(b.rect)+rectW(b.rect)/2, rectT(b.rect)+rectH(b.rect)/2,
             (unsigned)std::min(17.f, rectH(b.rect)*0.45f),
             b.enabled? sf::Color::White : sf::Color(140,140,140), true);
    }
}

void App::drawPiece(int piece, float x, float y, float size, std::uint8_t alpha){
    if (!piece) return;
    if (piecesOk){
        int col=5;
        switch (std::abs(piece)){case 20:col=0;break;case 9:col=1;break;case 4:col=2;break;
                                  case 3:col=3;break;case 5:col=4;break;case 1:col=5;break;}
        int row = piece>0 ? 0 : 1;
        sf::Sprite sp(pieceTexture, IR(col*pieceCell,row*pieceCell,pieceCell,pieceCell));
        sp.setScale({size/(float)pieceCell, size/(float)pieceCell});
        sp.setColor(sf::Color(255,255,255,alpha));
        sp.setPosition({snapPx(x), snapPx(y)});
        window.draw(sp);
    } else {
        char c='P';
        switch (std::abs(piece)){case 20:c='K';break;case 9:c='Q';break;case 4:c='B';break;
                                  case 3:c='N';break;case 5:c='R';break;case 1:c='P';break;}
        text(std::string(1,c), x+size/2, y+size/2, (unsigned)(size*0.6f),
             piece>0? sf::Color::White : sf::Color(40,40,40), true);
    }
}

int App::boardSquareAt(sf::Vector2f m, float bx, float by, float side, bool flipped) const{
    float sq = side/8;
    int x = (int)std::floor((m.x-bx)/sq);
    int y = (int)std::floor((m.y-by)/sq);
    if (x<0||x>7||y<0||y>7) return -1;
    if (flipped){ x=7-x; y=7-y; }
    return y*8+x;
}

void App::drawSquareTint(int sq, float bx, float by, float side, bool flipped, sf::Color c){
    if (sq<0||sq>63) return;
    float s8 = side/8;
    int x=sq%8, y=sq/8;
    if (flipped){ x=7-x; y=7-y; }
    sf::RectangleShape o(sf::Vector2f(s8,s8));
    o.setPosition({bx+x*s8, by+y*s8});
    o.setFillColor(c);
    window.draw(o);
}

void App::drawBoard(float bx, float by, float side, const Position& p,
                    int selSq, const std::vector<Move>* legal,
                    int lastFrom, int lastTo, bool flipped, int dragSq, sf::Vector2f dPos){
    float sq = side/8;
    int checkSq = -1;
    if (p.kingSquare(p.whiteToMove)>=0 && p.inCheck(p.whiteToMove))
        checkSq = p.kingSquare(p.whiteToMove);
    for (int i=0;i<64;i++){
        int x=i%8, y=i/8;
        int dx = flipped? 7-x : x, dy = flipped? 7-y : y;
        sf::RectangleShape r(sf::Vector2f(sq,sq));
        r.setPosition({bx+dx*sq, by+dy*sq});
        r.setFillColor(((x+y)%2)? COL_DARK : COL_LIGHT);
        window.draw(r);
    }
    // origin lighter, destination stronger — direction reads at a glance
    if (lastFrom>=0) drawSquareTint(lastFrom, bx,by,side,flipped, COL_LAST_FROM);
    if (lastTo>=0)   drawSquareTint(lastTo,   bx,by,side,flipped, COL_LAST_TO);
    if (selSq>=0)    drawSquareTint(selSq,    bx,by,side,flipped, COL_SEL);
    if (checkSq>=0)  drawSquareTint(checkSq,  bx,by,side,flipped, COL_CHECK);
    if (legal){
        for (const Move& m : *legal){
            int x=m.to%8, y=m.to/8;
            int dx = flipped?7-x:x, dy = flipped?7-y:y;
            sf::CircleShape c(p.board[m.to]||m.isEnPassant ? sq*0.42f : sq*0.15f);
            c.setOrigin({c.getRadius(), c.getRadius()});
            c.setPosition({bx+dx*sq+sq/2, by+dy*sq+sq/2});
            if (p.board[m.to]||m.isEnPassant){
                c.setFillColor(sf::Color::Transparent);
                c.setOutlineThickness(sq*0.07f);
                c.setOutlineColor(sf::Color(40,90,40,150));
            } else c.setFillColor(sf::Color(40,90,40,120));
            window.draw(c);
        }
    }
    for (int i=0;i<64;i++){
        if (!p.board[i] || i==dragSq) continue;
        int x=i%8, y=i/8;
        int dx = flipped?7-x:x, dy = flipped?7-y:y;
        drawPiece(p.board[i], bx+dx*sq, by+dy*sq, sq);
    }
    for (int i=0;i<8;i++){
        char f = flipped? 'h'-i : 'a'+i;
        char rk = flipped? '1'+i : '8'-i;
        text(std::string(1,f), bx+i*sq+sq-10, by+side-14, 12, (i%2)? COL_LIGHT : COL_DARK);
        text(std::string(1,rk), bx+3, by+i*sq+2, 12, (i%2)? COL_LIGHT : COL_DARK);
    }
    if (dragSq>=0 && p.board[dragSq])
        drawPiece(p.board[dragSq], dPos.x-sq/2, dPos.y-sq/2, sq, 230);
    sf::RectangleShape br(sf::Vector2f(side,side));
    br.setPosition({bx,by}); br.setFillColor(sf::Color::Transparent);
    br.setOutlineThickness(2); br.setOutlineColor(sf::Color(90,90,110));
    window.draw(br);
}

void App::drawArrow(float bx, float by, float side, int from, int to,
                    bool flipped, sf::Color c){
    if (from<0||from>63||to<0||to>63||from==to) return;
    float sq = side/8;
    auto center=[&](int s)->sf::Vector2f{
        int x=s%8, y=s/8;
        if (flipped){ x=7-x; y=7-y; }
        return { bx + x*sq + sq/2, by + y*sq + sq/2 };
    };
    sf::Vector2f a = center(from), b = center(to);
    sf::Vector2f d = b-a;
    float len = std::sqrt(d.x*d.x + d.y*d.y);
    if (len < 1) return;
    sf::Vector2f u = d / len;
    float headLen = sq*0.38f, headW = sq*0.42f, shaftW = sq*0.20f;
    float ang = std::atan2(d.y, d.x) * 180.f / 3.14159265f;
    sf::RectangleShape shaft(sf::Vector2f(len-headLen, shaftW));
    shaft.setOrigin({0, shaftW/2});
    shaft.setPosition(a); setRotDeg(shaft, ang);
    shaft.setFillColor(c);
    window.draw(shaft);
    sf::Vector2f tipBase = b - u*headLen;
    sf::Vector2f nrm(-u.y, u.x);
    sf::ConvexShape head(3);
    head.setPoint(0, b);
    head.setPoint(1, tipBase + nrm*(headW/2));
    head.setPoint(2, tipBase - nrm*(headW/2));
    head.setFillColor(c);
    window.draw(head);
}

void App::drawPvArrows(const std::vector<PvLine>& pvs, const Position& shown,
                       float bx, float by, float side, bool flipped, int count){
    // weakest first so the best line's arrow sits on top; opacity encodes rank
    static const std::uint8_t A[5] = {210, 120, 85, 62, 45};
    int n = std::min((int)pvs.size(), std::min(count, 5));
    for (int r=n-1; r>=0; r--){
        if (!pvs[r].valid) continue;
        Move m;
        if (!shown.uciToMove(pvs[r].firstMove(), m)) continue;
        drawArrow(bx,by,side, m.from, m.to, flipped, sf::Color(70,140,220, A[r]));
    }
}

void App::drawEvalBar(float x, float y, float h, int cp, bool isMate, int mateIn){
    float frac;
    if (isMate) frac = mateIn>0 ? 1.f : 0.f;
    else frac = 0.5f + 0.5f * std::tanh(cp/600.0f);
    sf::RectangleShape black(sf::Vector2f(22, h)); black.setPosition({x,y});
    black.setFillColor(sf::Color(30,30,30)); window.draw(black);
    sf::RectangleShape white(sf::Vector2f(22, h*frac));
    white.setPosition({x, y+h*(1-frac)});
    white.setFillColor(sf::Color(230,230,230)); window.draw(white);
    sf::RectangleShape mid(sf::Vector2f(22,2)); mid.setPosition({x, y+h/2});
    mid.setFillColor(sf::Color(120,120,120)); window.draw(mid);
    std::string s;
    if (isMate) s = "M" + std::to_string(std::abs(mateIn));
    else { char b[16]; snprintf(b,16,"%+.1f", cp/100.0); s=b; }
    text(s, x+11, y+h+12, 13, sf::Color::White, true);
}

void App::drawMoveList(float x, float y, float w, float h,
                       const std::vector<std::string>& sans, int startNo, bool startWhite){
    sf::RectangleShape r(sf::Vector2f(w,h)); r.setPosition({x,y});
    r.setFillColor(COL_PANEL); window.draw(r);
    float lh = 20;
    int rows = (int)(h/lh) - 1;
    if (rows < 1) return;
    std::vector<std::string> lines;
    int num = startNo; size_t i=0;
    if (!startWhite && !sans.empty()){
        lines.push_back(std::to_string(num)+"... "+sans[0]); num++; i=1;
    }
    for (; i<sans.size(); i+=2){
        std::string ln = std::to_string(num)+". "+sans[i];
        if (i+1<sans.size()) ln += "  "+sans[i+1];
        lines.push_back(ln); num++;
    }
    int first = std::max(0, (int)lines.size()-rows);
    float yy = y+6;
    for (int k=first; k<(int)lines.size(); k++){
        text(lines[k], x+8, yy, 14, sf::Color(210,210,215));
        yy += lh;
    }
}

// =============================================================== engines & overrides
void App::scanEngines(){
    knownEngines.clear();
    auto add=[&](const std::string& p){
        for (auto& e : knownEngines) if (e.path==p) return;
        knownEngines.push_back({p, fs::path(p).filename().string(), {}});
    };
    std::error_code ec;
    if (fs::exists("Resources/engines", ec))
        for (auto& e : fs::directory_iterator("Resources/engines", ec))
            if (e.is_regular_file()) add(e.path().string());
    for (const char* p : {"/usr/games/stockfish","/usr/bin/stockfish",
                          "/usr/local/bin/stockfish","/opt/homebrew/bin/stockfish",
                          "C:\\stockfish\\stockfish.exe"})
        if (fs::exists(p, ec)) add(p);
}
EngineEntry App::makeEntry(int idx){
    EngineEntry e = knownEngines[idx];
    auto it = engineOverrides.find(e.path);
    if (it != engineOverrides.end()) e.options = it->second;
    return e;
}
void App::loadOverrides(){
    engineOverrides.clear();
    std::ifstream f("engine_options.cfg");
    std::string line;
    while (std::getline(f, line)){
        size_t t1 = line.find('\t'); if (t1==std::string::npos) continue;
        size_t t2 = line.find('\t', t1+1); if (t2==std::string::npos) continue;
        engineOverrides[line.substr(0,t1)].push_back(
            { line.substr(t1+1, t2-t1-1), line.substr(t2+1) });
    }
}
void App::saveOverrides(){
    std::ofstream f("engine_options.cfg");
    for (auto& [path, opts] : engineOverrides)
        for (auto& [n,v] : opts)
            f << path << '\t' << n << '\t' << v << '\n';
}
void App::setOverride(const std::string& path, const std::string& name, const std::string& val){
    auto& v = engineOverrides[path];
    for (auto& p : v) if (p.first==name){ p.second=val; saveOverrides(); return; }
    v.push_back({name,val});
    saveOverrides();
}
std::string App::overrideOrDefault(const std::string& path, const UciOption& o){
    auto it = engineOverrides.find(path);
    if (it != engineOverrides.end())
        for (auto& p : it->second) if (p.first==o.name) return p.second;
    return o.defVal;
}

// =============================================================== layout
static Button mk(float x,float y,float w,float h,const std::string& s,int id,bool tog=false){
    Button b; b.rect=FR(x,y,w,h); b.label=s; b.id=id; b.toggled=tog; return b;
}
static const char* TC_LABELS[7] = {"1+0","3+0","3+2","5+0","10+0","15+10","30+0"};
static const int   TC_BASE[7]   = {60000,180000,180000,300000,600000,900000,1800000};
static const int   TC_INC[7]    = {0,0,2000,0,0,10000,0};
static const int   PALETTE[12]  = { WHITE_KING,WHITE_QUEEN,WHITE_BISHOP,WHITE_KNIGHT,WHITE_ROOK,WHITE_PAWN,
                                    BLACK_KING,BLACK_QUEEN,BLACK_BISHOP,BLACK_KNIGHT,BLACK_ROOK,BLACK_PAWN };

void App::layout(){
    buttons.clear();
    paletteRects.clear();
    float cx = WW/2;
    forceRedraw = true;
    switch (screen){
    case Screen::MENU: {
        float w=380,h=44,y=132;
        buttons.push_back(mk(cx-w/2,y,w,h,"Play: Human vs Human",100)); y+=52;
        buttons.push_back(mk(cx-w/2,y,w-118,h,"Play vs Engine",101));
        buttons.push_back(mk(cx-w/2+w-110,y,110,h, menuPlayWhite? "You: White":"You: Black",107,!menuPlayWhite)); y+=52;
        buttons.push_back(mk(cx-w/2,y,w,h,"Engine vs Engine / Tournament",103)); y+=52;
        buttons.push_back(mk(cx-w/2,y,w,h,"Analysis Board",104)); y+=52;
        buttons.push_back(mk(cx-w/2,y,w,h,"Load PGN  (analyse a game)",108)); y+=52;
        buttons.push_back(mk(cx-w/2,y,w,h,"Board Editor / Set Position",106)); y+=52;
        buttons.push_back(mk(cx-w/2,y,w,h,"Engine Testing Tools",105));
        y+=44+64;                                   // clear gap before the TC block
        float bw=66;
        for (int i=0;i<7;i++)
            buttons.push_back(mk(cx-3.5f*bw-15+i*(bw+5), y, bw, 34, TC_LABELS[i], 110+i,
                                 tc.baseMs==TC_BASE[i] && tc.incMs==TC_INC[i]));
        y+=58;
        buttons.push_back(mk(cx-w/2,y,w,38,"Quit",120));
        break; }
    case Screen::GAME: {
        float side = WH-40;
        float rx = side + 40;
        float pw = WW - rx - 20;
        float ny = 16, nw = 40;
        buttons.push_back(mk(rx, ny, nw, 28, "|<", 208));
        buttons.push_back(mk(rx+nw+4, ny, nw, 28, "<", 209));
        buttons.push_back(mk(rx+2*(nw+4), ny, nw, 28, ">", 210));
        buttons.push_back(mk(rx+3*(nw+4), ny, nw, 28, ">|", 211));
        buttons.push_back(mk(rx+4*(nw+4)+6, ny, 58, 28, "PV:"+std::to_string(arrowsN), 213));
        buttons.push_back(mk(rx+pw-70, ny, 70, 28, "Menu", 200));
        float bw = (pw-8)/2;
        float bh = 34, gap = 8;
        int rows = (mode==GameMode::ANALYSIS) ? 5 : 4;
        gameButtonsTop = WH - 20 - (rows*bh + (rows-1)*gap);
        float y = gameButtonsTop;
        buttons.push_back(mk(rx, y, bw, bh, "New Game", 201));
        buttons.push_back(mk(rx+bw+8, y, bw, bh, "Flip (F)", 202)); y+=bh+gap;
        buttons.push_back(mk(rx, y, bw, bh, "Takeback", 203));
        buttons.push_back(mk(rx+bw+8, y, bw, bh, "Resign", 204)); y+=bh+gap;
        buttons.push_back(mk(rx, y, bw, bh, "Copy FEN", 205));
        buttons.push_back(mk(rx+bw+8, y, bw, bh, "Paste FEN", 206)); y+=bh+gap;
        buttons.push_back(mk(rx, y, bw, bh, "Save PGN", 207));
        buttons.push_back(mk(rx+bw+8, y, bw, bh, analysing? "Analysis: ON":"Analysis: OFF", 212, analysing)); y+=bh+gap;
        if (mode==GameMode::ANALYSIS)
            buttons.push_back(mk(rx, y, pw, bh, "Run game review (engine annotates every move)", 215));
        if (pendingPromotion){
            float sq=side/8;
            float pxp = 20+side/2-2*sq, pyp = 20+side/2-sq/2;
            const char* n[4]={"Q","R","B","N"};
            for (int i=0;i<4;i++)
                buttons.push_back(mk(pxp+i*sq, pyp, sq, sq, n[i], 250+i));
        }
        break; }
    case Screen::PICK_ENGINE: {
        float w = WW-240, y=110;
        for (size_t i=0;i<knownEngines.size() && i<12;i++){
            buttons.push_back(mk(120, y, w-110, 38,
                knownEngines[i].name + "   (" + knownEngines[i].path + ")", 300+(int)i));
            buttons.push_back(mk(120+w-104, y, 104, 38, "Options...", 340+(int)i));
            y+=46;
        }
        buttons.push_back(mk(120, WH-130, w-160, 38, enginePathInput.empty()?
                             "[click here, then type an engine path]" : enginePathInput,
                             380, textFocus==TextTarget::ENGINE_PATH));
        buttons.push_back(mk(WW-260, WH-130, 140, 38, "Add path", 381));
        buttons.push_back(mk(120, WH-80, 140, 38, "Back", 399));
        break; }
    case Screen::MATCH_SETUP: {
        float w=WW-240, y=96;
        for (size_t i=0;i<knownEngines.size() && i<6;i++){
            bool sel = std::find(matchSelection.begin(),matchSelection.end(),(int)i)!=matchSelection.end();
            buttons.push_back(mk(120, y, w, 34,
                knownEngines[i].name + "   (" + knownEngines[i].path + ")", 400+(int)i, sel));
            y+=40;
        }
        y+=24;                                     // label line: games / TC
        buttons.push_back(mk(120, y, 36, 32, "-", 450));
        buttons.push_back(mk(212, y, 36, 32, "+", 451));
        for (int i=0;i<7;i++)
            buttons.push_back(mk(330+i*62, y, 56, 32, TC_LABELS[i], 110+i,
                                 tc.baseMs==TC_BASE[i]&&tc.incMs==TC_INC[i]));
        y+=58;                                     // label line: start positions
        std::string fenLabel = matchFenInput.empty()
            ? "[click, then type a FEN and press Enter to add]" : matchFenInput;
        buttons.push_back(mk(120, y, w-460, 32, fenLabel, 452, textFocus==TextTarget::MATCH_FEN));
        buttons.push_back(mk(120+w-452, y, 100, 32, "Paste-add", 454));
        buttons.push_back(mk(120+w-344, y, 112, 32, "Add in editor", 455));
        buttons.push_back(mk(120+w-224, y, 104, 32, "Clear ("+std::to_string(matchFens.size())+")", 453));
        y+=38;
        std::string fpLabel = filePathInput.empty()
            ? "[openings file path (one FEN per line) - click to type]" : filePathInput;
        buttons.push_back(mk(120, y, w-460, 32, fpLabel, 458,
                             textFocus==TextTarget::FILE_PATH && filePurpose==1));
        buttons.push_back(mk(120+w-452, y, 100, 32, "Load file", 461));
        y+=54;                                     // label line: judge
        std::string jl = judgePath.empty()? "Pick judge engine..."
                        : ("Judge: "+fs::path(judgePath).filename().string());
        buttons.push_back(mk(120, y, 230, 32, jl, 456, !judgePath.empty()));
        buttons.push_back(mk(358, y, 90, 32, "No judge", 457));
        buttons.push_back(mk(456, y, 96, 32, "Arrows: "+std::to_string(arrowsN), 459));
        y+=50;
        buttons.push_back(mk(120, y, 220, 40, "Start", 460));
        buttons.push_back(mk(360, y, 140, 40, "Back", 499));
        break; }
    case Screen::MATCH_VIEW: {
        buttons.push_back(mk(WW-170, 16, 150, 32, runner.running()? "Abort" : "Back", 500));
        buttons.push_back(mk(WW-170, 56, 150, 32, "Save PGNs", 501));
        buttons.push_back(mk(WW-170, 96, 150, 32, "Copy log", 502));
        buttons.push_back(mk(WW-170, 136, 150, 32, "Flip board", 503, matchFlipped));
        break; }
    case Screen::TESTS: {
        float w=520, y=170;
        buttons.push_back(mk(cx-w/2, y, w, 46, "1. Self-test GUI move generator (perft)", 600)); y+=58;
        buttons.push_back(mk(cx-w/2, y, w, 46, "2. Test YOUR engine's move generator (go perft)", 601)); y+=58;
        buttons.push_back(mk(cx-w/2, y, w, 46, "3. Accuracy test vs reference engine", 602)); y+=58;
        buttons.push_back(mk(cx-w/2, y+20, 160, 40, "Back", 699));
        break; }
    case Screen::ACC_SETUP: {
        float y=150, x0=340;
        const int counts[5] = {25,100,250,500,1000};
        for (int i=0;i<5;i++)
            buttons.push_back(mk(x0+i*96, y, 88, 34, std::to_string(counts[i]), 620+i, accCount==counts[i]));
        y+=76;
        const int tms[4] = {100,250,500,1000};
        for (int i=0;i<4;i++)
            buttons.push_back(mk(x0+i*96, y, 88, 34, std::to_string(tms[i])+"ms", 630+i, accTestMs==tms[i]));
        y+=76;
        const int rms[4] = {250,500,1000,2000};
        for (int i=0;i<4;i++)
            buttons.push_back(mk(x0+i*96, y, 88, 34, std::to_string(rms[i])+"ms", 640+i, accRefMs==rms[i]));
        y+=76;
        const int mpv[3] = {1,3,5};
        for (int i=0;i<3;i++)
            buttons.push_back(mk(x0+i*96, y, 88, 34, std::to_string(mpv[i]), 650+i, accMultiPV==mpv[i]));
        y+=80;
        buttons.push_back(mk(x0, y, 260, 42, "Continue: pick engines", 660));
        buttons.push_back(mk(x0+280, y, 140, 42, "Back", 661));
        break; }
    case Screen::TEST_RUN: {
        buttons.push_back(mk(WW-170, 16, 150, 32, testRunning? "Abort" : "Back", 700));
        buttons.push_back(mk(WW-170, 56, 150, 32, "Copy log", 701));
        buttons.push_back(mk(WW-170, 96, 150, 32, "Save log", 702));
        break; }
    case Screen::EDITOR: {
        float side = WH-110;
        float rx = side + 40;
        float pw = WW - rx - 20;
        float cell = std::min(pw/6.f, 54.f);
        float py = 56;
        for (int i=0;i<12;i++){
            float x = rx + (i%6)*cell, yy = py + (i/6)*cell;
            buttons.push_back(mk(x, yy, cell-2, cell-2, "", 800+i, editorBrush==PALETTE[i]));
            paletteRects.push_back(FR(x, yy, cell-2, cell-2));
        }
        float y = py + 2*cell + 10;
        buttons.push_back(mk(rx, y, 110, 32, "Hand", 813, editorBrush==BRUSH_HAND));
        buttons.push_back(mk(rx+118, y, 110, 32, "Eraser", 812, editorBrush==0));
        buttons.push_back(mk(rx+236, y, 110, 32, "Clear board", 825));
        buttons.push_back(mk(rx+354, y, 96, 32, "Start pos", 826)); y+=44;
        buttons.push_back(mk(rx, y, 180, 32, editPos.whiteToMove? "White to move":"Black to move", 820)); y+=40;
        buttons.push_back(mk(rx, y, 88, 30, "W O-O", 821, editPos.castleWK));
        buttons.push_back(mk(rx+94, y, 88, 30, "W O-O-O", 822, editPos.castleWQ));
        buttons.push_back(mk(rx+188, y, 88, 30, "B O-O", 823, editPos.castleBK));
        buttons.push_back(mk(rx+282, y, 88, 30, "B O-O-O", 824, editPos.castleBQ)); y+=44;
        buttons.push_back(mk(rx, y, 150, 32, "Copy FEN", 827));
        buttons.push_back(mk(rx+158, y, 150, 32, "Paste FEN", 828)); y+=54;
        if (editorReturn == Screen::MATCH_SETUP){
            buttons.push_back(mk(rx, y, 310, 38, "Done - add position to the match", 835)); y+=46;
            buttons.push_back(mk(rx, y, 150, 34, "Cancel", 899));
        } else {
            buttons.push_back(mk(rx, y, 230, 34, "Play from here (HvH)", 830));
            buttons.push_back(mk(rx+238, y, 150, 34, editorPlayWhite? "You: White":"You: Black", 834, !editorPlayWhite)); y+=42;
            buttons.push_back(mk(rx, y, 230, 34, "Play vs engine from here", 831)); y+=42;
            buttons.push_back(mk(rx, y, 230, 34, "Analyse from here", 832)); y+=42;
            buttons.push_back(mk(rx, y, 230, 34, "Use in engine match", 833)); y+=48;
            buttons.push_back(mk(rx, y, 120, 34, "Back", 899));
        }
        break; }
    case Screen::ENGINE_OPTIONS: {
        float y = 96;
        int rows = std::min((int)((WH-190)/44), 10);
        for (int r=0; r<rows; r++){
            int oi = optScroll + r;
            if (oi >= (int)optsList.size()) break;
            const UciOption& o = optsList[oi];
            std::string cur = overrideOrDefault(optsPath, o);
            float xc = 430;
            if (o.type=="check"){
                buttons.push_back(mk(xc, y, 120, 34, cur=="true"?"true":"false", 900+r, cur=="true"));
            } else if (o.type=="spin"){
                buttons.push_back(mk(xc, y, 36, 34, "-", 900+r));
                bool editing = (textFocus==TextTarget::OPTION_VALUE && editingOption==oi);
                buttons.push_back(mk(xc+42, y, 120, 34, editing? optionEditBuffer : cur, 940+r, editing));
                buttons.push_back(mk(xc+168, y, 36, 34, "+", 920+r));
            } else if (o.type=="combo"){
                buttons.push_back(mk(xc, y, 220, 34, cur, 900+r));
            } else if (o.type=="string"){
                bool editing = (textFocus==TextTarget::OPTION_VALUE && editingOption==oi);
                buttons.push_back(mk(xc, y, 300, 34, editing? optionEditBuffer :
                                     (cur.empty()? "[empty - click to type]" : cur), 940+r, editing));
            }
            y += 44;
        }
        buttons.push_back(mk(WW-170, 96, 150, 34, "Scroll up", 950));
        buttons.push_back(mk(WW-170, 138, 150, 34, "Scroll down", 951));
        buttons.push_back(mk(WW-170, 196, 150, 34, "Reset defaults", 955));
        buttons.push_back(mk(WW-170, WH-70, 150, 38, "Done", 999));
        break; }
    case Screen::PGN_LOAD: {
        float w = WW-240, y=170;
        std::string fp = filePathInput.empty()
            ? "[PGN file path - click to type]" : filePathInput;
        buttons.push_back(mk(120, y, w-160, 38, fp, 1101,
                             textFocus==TextTarget::FILE_PATH && filePurpose==2));
        buttons.push_back(mk(WW-260, y, 140, 38, "Load file", 1102)); y+=58;
        buttons.push_back(mk(120, y, 320, 38, "Paste PGN text from clipboard", 1103)); y+=76;
        buttons.push_back(mk(120, y, 140, 38, "Back", 1199));
        break; }
    case Screen::PGN_LIST: {
        float w = WW-240, y=110;
        int rows = std::min((int)((WH-200)/44), 12);
        for (int r=0; r<rows; r++){
            int gi = pgnScroll + r;
            if (gi >= (int)pgnGames.size()) break;
            const PgnGame& g = pgnGames[gi];
            buttons.push_back(mk(120, y, w, 38,
                std::to_string(gi+1)+".  "+g.white+"  vs  "+g.black+"    "+g.result, 1000+r));
            y+=44;
        }
        buttons.push_back(mk(WW-170, WH-170, 150, 34, "Scroll up", 1050));
        buttons.push_back(mk(WW-170, WH-130, 150, 34, "Scroll down", 1051));
        buttons.push_back(mk(120, WH-80, 140, 38, "Back", 1099));
        break; }
    default: break;
    }
}

// =============================================================== game logic
Position App::shownPosition() const{
    if (viewPly < 0) return game.pos;
    Position p = game.start;
    for (int i=0;i<viewPly && i<(int)game.moves.size();i++) p.makeMove(game.moves[i]);
    return p;
}
void App::newGame(const Position& p){
    stopOpponent();
    game.reset(p);
    selectedSq=-1; dragFromSq=-1; dragging=false;
    pendingPromotion.reset(); viewPly=-1;
    premoveFrom=premoveTo=-1;
    wClockMs = tc.baseMs; bClockMs = tc.baseMs;
    clocksRunning = (mode != GameMode::ANALYSIS);
    tickClock.restart();
    forceRedraw = true;
}
void App::commitMove(Move m){
    if (game.tryMove(m)){
        if (clocksRunning){
            if (!game.pos.whiteToMove) wClockMs += tc.incMs; else bClockMs += tc.incMs;
        }
        selectedSq=-1; legalForSelected.clear(); viewPly=-1;
        if (game.result != GameResult::ONGOING) clocksRunning=false;
        forceRedraw = true;
    }
}
void App::humanCommitMove(Move m){
    // pondering: tell the engine whether the human played its predicted move
    if (mode==GameMode::HUMAN_VS_ENGINE && oppState==2 && oppEngine && oppEngine->alive()){
        std::string u = game.pos.moveToUCI(m);
        if (u == oppPonderMove){
            oppEngine->sendRaw("ponderhit");          // ponder becomes the real search
            oppState = 3;
        } else {
            oppDiscard = true;                        // wrong guess: discard that search
            oppEngine->sendRaw("stop");
        }
    }
    commitMove(m);
}
void App::trySquareAction(int sq){
    if (sq<0 || game.result!=GameResult::ONGOING || viewPly>=0) return;
    bool humanTurn = (mode!=GameMode::HUMAN_VS_ENGINE) || (game.pos.whiteToMove==humanIsWhite);
    if (!humanTurn || pendingPromotion) return;
    for (const Move& m : legalForSelected){
        if (m.to == sq){
            if (m.promo){ Move base=m; base.promo=0; pendingPromotion=base; layout(); return; }
            humanCommitMove(m); return;
        }
    }
    int v = game.pos.board[sq];
    if (v && (v>0)==game.pos.whiteToMove){
        selectedSq = sq;
        std::vector<Move> all; game.pos.generateLegal(all);
        legalForSelected.clear();
        for (auto& m : all) if (m.from==sq) legalForSelected.push_back(m);
    } else { selectedSq=-1; legalForSelected.clear(); }
    forceRedraw = true;
}
void App::tryExecutePremove(){
    if (premoveFrom<0 || premoveTo<0){ premoveFrom=premoveTo=-1; return; }
    int from=premoveFrom, to=premoveTo;
    premoveFrom=premoveTo=-1;
    if (game.result!=GameResult::ONGOING || viewPly>=0) return;
    if (mode==GameMode::HUMAN_VS_ENGINE && game.pos.whiteToMove!=humanIsWhite) return;
    std::string u = squareName(from)+squareName(to);
    Move m;
    bool ok = game.pos.uciToMove(u, m);
    if (!ok && std::abs(game.pos.board[from])==1 && (to/8==0 || to/8==7))
        ok = game.pos.uciToMove(u+"q", m);            // premoved promotion: auto-queen
    if (ok) humanCommitMove(m);
}

void App::stopOpponent(){
    oppDiscard = true;
    if (oppThread.joinable()){
        if (oppEngine) oppEngine->sendRaw("stop");
        for (int i=0;i<60 && oppState!=0;i++) sf::sleep(sf::milliseconds(25));
        if (oppState!=0 && oppEngine) oppEngine->quit();   // hung engine: kill it
        oppThread.join();
    }
    oppState = 0;
    oppDiscard = false;
    oppPonderMove.clear();
    { std::lock_guard<std::mutex> lk(oppMx); oppMoveReady.clear(); }
}
void App::startEngineThinkIfNeeded(){
    if (mode!=GameMode::HUMAN_VS_ENGINE || !oppEngine || !oppEngine->alive()) return;
    if (game.result!=GameResult::ONGOING || pendingPromotion) return;
    if (game.pos.whiteToMove == humanIsWhite) return;
    if (oppState != 0) return;                        // idle only (not think/ponder)
    oppState = 1;
    if (oppThread.joinable()) oppThread.join();
    std::string startFen = game.start.toFEN();
    std::vector<std::string> uciMoves;
    { Position p=game.start; for (auto& m : game.moves){ uciMoves.push_back(p.moveToUCI(m)); p.makeMove(m);} }
    long long wc=wClockMs, bc=bClockMs; int inc=tc.incMs;
    oppThread = std::thread([this,startFen,uciMoves,wc,bc,inc](){
        oppEngine->setPosition(startFen, uciMoves);
        std::string bm = oppEngine->goClock((int)wc,(int)bc,inc,inc);
        { std::lock_guard<std::mutex> lk(oppMx);
          oppMoveReady = bm.empty()? "(none)" : bm; }
        oppState = 0;
    });
}
void App::startPonderIfPossible(){
    if (!oppPonderEnabled || mode!=GameMode::HUMAN_VS_ENGINE) return;
    if (!oppEngine || !oppEngine->alive() || oppState!=0) return;
    if (game.result!=GameResult::ONGOING) return;
    if (game.pos.whiteToMove != humanIsWhite) return;   // ponder happens on the human's turn
    std::string hint = oppEngine->lastPonder();
    Move pm;
    if (hint.empty() || !game.pos.uciToMove(hint, pm)) return;
    oppPonderMove = hint;
    oppDiscard = false;
    oppState = 2;
    if (oppThread.joinable()) oppThread.join();
    std::string startFen = game.start.toFEN();
    std::vector<std::string> uciMoves;
    { Position p=game.start; for (auto& m : game.moves){ uciMoves.push_back(p.moveToUCI(m)); p.makeMove(m);} }
    uciMoves.push_back(hint);                           // engine thinks about the reply to its guess
    long long wc=wClockMs, bc=bClockMs; int inc=tc.incMs;
    oppThread = std::thread([this,startFen,uciMoves,wc,bc,inc](){
        oppEngine->setPosition(startFen, uciMoves);
        std::string bm = oppEngine->goPonderWait((int)wc,(int)bc,inc,inc);
        if (oppDiscard){ oppState = 0; return; }        // ponder miss: result is garbage
        { std::lock_guard<std::mutex> lk(oppMx);
          oppMoveReady = bm.empty()? "(none)" : bm; }   // ponderhit: this is the real move
        oppState = 0;
    });
}
void App::applyEngineMoveIfReady(){
    std::string bm;
    { std::lock_guard<std::mutex> lk(oppMx); bm=oppMoveReady; oppMoveReady.clear(); }
    if (bm.empty()) return;
    Move m;
    if (bm!="(none)" && game.pos.uciToMove(bm, m)){
        commitMove(m);
        startPonderIfPossible();                        // think on the human's time
        tryExecutePremove();                            // may immediately answer (ponderhit-aware)
    }
    else if (game.result==GameResult::ONGOING){
        game.result = humanIsWhite? GameResult::WHITE_WINS : GameResult::BLACK_WINS;
        game.reason = ResultReason::ILLEGAL_MOVE;
        showToast("Engine returned invalid move '"+bm+"'");
        clocksRunning=false;
    }
    forceRedraw = true;
}

void App::startAnalysis(const std::string& path){
    stopAnalysis();
    anaCtl = std::make_unique<AnalysisController>();
    std::vector<std::pair<std::string,std::string>> opts;
    auto it = engineOverrides.find(path);
    if (it != engineOverrides.end()) opts = it->second;
    opts.push_back({"MultiPV", std::to_string(arrowsN)});
    if (!anaCtl->start(path, opts)){
        showToast("Analysis engine failed: " + anaCtl->lastError());
        anaCtl.reset();
        return;
    }
    anaEnginePath = path;
    analysing = true;
    anaCtl->setFen(shownPosition().toFEN());
}
void App::stopAnalysis(){
    if (anaCtl){ anaCtl->shutdown(); anaCtl.reset(); }
    analysing = false;
}

void App::savePGN(){
    std::error_code ec; fs::create_directories("games", ec);
    char name[64]; std::time_t t=std::time(nullptr);
    std::strftime(name,64,"games/game_%Y%m%d_%H%M%S.pgn",std::localtime(&t));
    std::ofstream f(name);
    f << game.toPGN();
    showToast(std::string("Saved ")+name);
}
void App::copyFEN(){
    sf::Clipboard::setString(shownPosition().toFEN());
    showToast("FEN copied to clipboard");
}
void App::pasteFEN(){
    std::string fen = sf::Clipboard::getString();
    bool ok=false; Position p = Position::fromFEN(fen,&ok);
    if (!ok){ showToast("Clipboard does not contain a valid FEN"); return; }
    std::string prob = enginePositionProblem(p);
    bool engineInvolved = (mode!=GameMode::HUMAN_VS_HUMAN) || analysing;
    if (!prob.empty() && engineInvolved){
        showToast("Engines cannot play this position: "+prob);
        return;
    }
    newGame(p);
    showToast(prob.empty()? "Position set from FEN"
                          : "Position set (note: "+prob+" - engines would reject it)");
}
void App::saveTournamentPGNs(){
    auto pgns = runner.collectedPGNs();
    if (pgns.empty()){ showToast("No finished games yet"); return; }
    std::error_code ec; fs::create_directories("games", ec);
    char name[64]; std::time_t t=std::time(nullptr);
    std::strftime(name,64,"games/tournament_%Y%m%d_%H%M%S.pgn",std::localtime(&t));
    std::ofstream f(name);
    for (auto& p : pgns) f << p << "\n";
    showToast(std::string("Saved ")+name+" ("+std::to_string(pgns.size())+" games)");
}

// =============================================================== board editor
std::string App::editorFEN() const{
    Position p = editPos;
    p.castleWK = p.castleWK && p.board[60]==WHITE_KING && p.board[63]==WHITE_ROOK;
    p.castleWQ = p.castleWQ && p.board[60]==WHITE_KING && p.board[56]==WHITE_ROOK;
    p.castleBK = p.castleBK && p.board[4]==BLACK_KING && p.board[7]==BLACK_ROOK;
    p.castleBQ = p.castleBQ && p.board[4]==BLACK_KING && p.board[0]==BLACK_ROOK;
    p.halfmoveClock = 0; p.fullmoveNumber = 1;
    return p.toFEN();
}
std::string App::editorProblem() const{
    // same limits UCI engines assume (piece counts, kings, pawn ranks, checks)
    return enginePositionProblem(editPos);
}

// =============================================================== PGN analysis
bool App::loadPGNText(const std::string& text, const std::string& sourceLabel){
    auto games = parsePGN(text);
    if (games.empty()){ showToast("No games found in "+sourceLabel); return false; }
    pgnGames = games;
    pgnScroll = 0;
    if (pgnGames.size()==1){ openPgnGame(0); }
    else { screen = Screen::PGN_LIST; layout();
           showToast(std::to_string(pgnGames.size())+" games loaded - pick one"); }
    return true;
}
void App::openPgnGame(int idx){
    if (idx<0 || idx>=(int)pgnGames.size()) return;
    Game g;
    std::string err;
    bool ok = gameFromPGN(pgnGames[idx], g, &err);
    if (!ok && g.moves.empty()){ showToast("Cannot load game: "+err); return; }
    stopOpponent();
    mode = GameMode::ANALYSIS;
    game = g;
    clocksRunning = false;
    selectedSq=-1; legalForSelected.clear(); pendingPromotion.reset();
    premoveFrom=premoveTo=-1;
    viewPly = game.moves.empty()? -1 : 0;      // open at the start, step through with >
    boardFlipped = false;
    screen = Screen::GAME;
    if (!ok) showToast("Loaded partially: "+err);
    else showToast("Loaded: "+game.whiteName+" vs "+game.blackName
                   +" ("+std::to_string(game.moves.size())+" plies)");
    layout();
}

// =============================================================== tests
void App::pushTestLine(const std::string& s){
    std::lock_guard<std::mutex> lk(testMx);
    testLines.push_back(s);
}
std::string App::copyAllTestLines(){
    std::lock_guard<std::mutex> lk(testMx);
    std::string all = testTitle + "\n";
    for (auto& l : testLines) all += l + "\n";
    return all;
}
void App::startInternalSelfTest(){
    testAbort=false; testRunning=true; testReturn=Screen::TESTS;
    { std::lock_guard<std::mutex> lk(testMx); testLines.clear(); }
    testTitle = "GUI move generator self-test (verified perft suite)";
    if (testThread.joinable()) testThread.join();
    testThread = std::thread([this](){
        auto suite = loadEpdSuite("Resources/perftsuite.epd");
        pushTestLine(suite.empty()
            ? "Using built-in suite (put perftsuite.epd in Resources/ for the 100-position file)"
            : "Loaded Resources/perftsuite.epd: " + std::to_string(suite.size()) + " positions");
        if (suite.empty()) suite = builtinPerftSuite();
        bool all = runInternalPerftSelfTest(suite, 5,
            [this](int i,int n,bool pass){
                pushTestLine("["+std::to_string(i)+"/"+std::to_string(n)+"] "+(pass?"OK":"FAIL"));
            }, testAbort);
        pushTestLine(all? ">>> ALL PASSED - internal generator verified." : ">>> FAILURES FOUND.");
        testRunning=false;
    });
}
void App::startPerftTest(const EngineEntry& ee){
    testAbort=false; testRunning=true; testReturn=Screen::TESTS;
    { std::lock_guard<std::mutex> lk(testMx); testLines.clear(); }
    testTitle = "Engine move-generator test: " + ee.name;
    if (testThread.joinable()) testThread.join();
    testThread = std::thread([this,ee](){
        UciEngine e;
        if (!e.start(ee.path)){ pushTestLine("Cannot start engine: "+e.lastError()); testRunning=false; return; }
        for (auto& o : ee.options) e.setOption(o.first, o.second);
        pushTestLine("Engine: "+e.name());
        auto suite = loadEpdSuite("Resources/perftsuite.epd");
        pushTestLine(suite.empty()
            ? "Using built-in verified suite (add Resources/perftsuite.epd for 100 positions)"
            : "Loaded perftsuite.epd: " + std::to_string(suite.size()) + " positions");
        if (suite.empty()) suite = builtinPerftSuite();
        pushTestLine("Sending 'go perft <depth>' for each position...");
        int fails=0;
        auto res = runEnginePerftSuite(e, suite, 5,
            [this,&fails](int i,int n,const PerftResult& r){
                std::string s = "["+std::to_string(i)+"/"+std::to_string(n)+"] d"
                    +std::to_string(r.depth)+" want "+std::to_string(r.expected)+" got "
                    +(r.engineNodes<0? std::string("(no perft support)") : std::to_string(r.engineNodes))
                    +(r.pass? "  OK":"  FAIL");
                if (!r.pass){ fails++; s += "   fen: "+r.fen; }
                pushTestLine(s);
            }, testAbort);
        if (!res.empty() && res.back().engineNodes<0)
            pushTestLine(">>> Engine does not answer 'go perft N' with 'Nodes searched: X'.");
        else
            pushTestLine(fails? ">>> "+std::to_string(fails)+" FAILURES - your move generator has bugs in those positions."
                              : ">>> ALL PASSED - move generator agrees with verified node counts.");
        e.quit();
        testRunning=false;
    });
}
void App::startAccuracyTest(const EngineEntry& tee, const EngineEntry& ree){
    testAbort=false; testRunning=true; testReturn=Screen::TESTS;
    { std::lock_guard<std::mutex> lk(testMx); testLines.clear(); }
    testTitle = "Accuracy: " + tee.name + " vs reference " + ree.name
              + "  (" + std::to_string(accCount) + " positions)";
    if (testThread.joinable()) testThread.join();
    int cnt=accCount, tms=accTestMs, rms=accRefMs, mpv=accMultiPV;
    testThread = std::thread([this,tee,ree,cnt,tms,rms,mpv](){
        UciEngine te, re;
        if (!te.start(tee.path)){ pushTestLine("Cannot start test engine: "+te.lastError()); testRunning=false; return; }
        if (!re.start(ree.path)){ pushTestLine("Cannot start reference engine: "+re.lastError()); testRunning=false; return; }
        for (auto& o : tee.options) te.setOption(o.first, o.second);
        for (auto& o : ree.options) re.setOption(o.first, o.second);
        te.newGame(); re.newGame();
        auto fens = loadFenList("Resources/accuracy.epd");
        pushTestLine(fens.empty()
            ? "Resources/accuracy.epd not found - using the 15 built-in positions"
            : "Loaded Resources/accuracy.epd: " + std::to_string(fens.size()) + " positions");
        if (fens.empty()) fens = defaultAccuracyPositions();
        if ((int)fens.size() > cnt) fens.resize(cnt);
        pushTestLine("Test engine "+std::to_string(tms)+"ms/move, reference "
                     +std::to_string(rms)+"ms with MultiPV "+std::to_string(mpv)
                     +" (2nd/3rd-best moves are credited at their true cp gap).");
        auto rep = runAccuracyTest(te, re, fens, tms, rms, mpv,
            [this](int i,int n,const AccuracyMoveReport& m){
                std::string s = "["+std::to_string(i)+"/"+std::to_string(n)+"] "+m.san
                    +"  loss "+std::to_string(m.cpLoss);
                if (m.rank==0) s += "  = best move";
                else if (m.rank>0) s += "  = choice #"+std::to_string(m.rank+1);
                if (!m.alternatives.empty()) s += "   [also fine: "+m.alternatives+"]";
                pushTestLine(s);
            }, testAbort);
        char b[200];
        snprintf(b,200,">>> %d positions | Avg cp loss: %.1f | Accuracy: %.1f%% | blunders %d, mistakes %d, inaccuracies %d",
                 (int)rep.moves.size(), rep.avgCpLoss, rep.accuracyPct,
                 rep.blunders, rep.mistakes, rep.inaccuracies);
        pushTestLine(b);
        te.quit(); re.quit();
        testRunning=false;
    });
}
void App::startGameReview(const std::string& enginePath){
    if (game.moves.empty()){ showToast("No moves to review"); return; }
    testAbort=false; testRunning=true; testReturn=Screen::GAME;
    { std::lock_guard<std::mutex> lk(testMx); testLines.clear(); }
    testTitle = "Game review: " + game.whiteName + " vs " + game.blackName;
    if (testThread.joinable()) testThread.join();
    std::string startFen = game.start.toFEN();
    std::vector<std::string> uciMoves;
    std::vector<std::string> sans = game.sans;
    int startNo = game.start.fullmoveNumber;
    bool startWhite = game.start.whiteToMove;
    { Position p=game.start; for (auto& m : game.moves){ uciMoves.push_back(p.moveToUCI(m)); p.makeMove(m);} }
    testThread = std::thread([this,enginePath,startFen,uciMoves,sans,startNo,startWhite](){
        UciEngine e;
        if (!e.start(enginePath)){ pushTestLine("Cannot start engine: "+e.lastError()); testRunning=false; return; }
        auto it = engineOverrides.find(enginePath);
        if (it != engineOverrides.end())
            for (auto& o : it->second) e.setOption(o.first, o.second);
        e.setOption("MultiPV","1");
        e.newGame();
        pushTestLine("Engine: "+e.name()+"  (400ms per position, "
                     +std::to_string(uciMoves.size()+1)+" positions)");
        int N = (int)uciMoves.size();
        std::vector<int> score(N+1, 0);              // mover POV at each ply
        auto sc=[&](const EngineInfo& i)->int{
            if (i.isMate) return i.mateIn>0? 32000-i.mateIn : -32000-i.mateIn;
            return i.scoreCp;
        };
        for (int i=0;i<=N && !testAbort && e.alive(); i++){
            std::vector<std::string> mv(uciMoves.begin(), uciMoves.begin()+i);
            e.setPosition(startFen, mv);
            e.goMovetime(400);
            score[i] = sc(e.lastInfo());
        }
        long long lossW=0, lossB=0; int cw=0, cb=0;
        double accW=0, accB=0;
        int num=startNo; bool wtm=startWhite;
        for (int i=0;i<N && !testAbort; i++){
            int loss = score[i] + score[i+1];         // POV flips each ply
            if (loss<0) loss=0; if (loss>1000) loss=1000;
            const char* mark = loss>=300? " ??" : loss>=100? " ?" : loss>=50? " ?!" : "";
            int whiteCp = wtm? score[i] : -score[i];
            char b[200];
            snprintf(b,200,"%d.%s %-8s eval %+.2f  loss %d%s",
                     num, wtm?"":"..", sans[i].c_str(), whiteCp/100.0, loss, mark);
            pushTestLine(b);
            double acc = moveAccuracyFromWinDrop(
                cpToWinPct(score[i]) - cpToWinPct(-score[i+1]));
            if (wtm){ lossW+=loss; accW+=acc; cw++; } else { lossB+=loss; accB+=acc; cb++; }
            if (!wtm) num++;
            wtm=!wtm;
        }
        if (cw){ char b[160];
            snprintf(b,160,">>> White: avg cp loss %.1f, accuracy %.1f%%",
                     (double)lossW/cw, accW/cw);
            pushTestLine(b); }
        if (cb){ char b[160];
            snprintf(b,160,">>> Black: avg cp loss %.1f, accuracy %.1f%%",
                     (double)lossB/cb, accB/cb);
            pushTestLine(b); }
        if (!e.alive()) pushTestLine(">>> Engine terminated during review.");
        e.quit();
        testRunning=false;
    });
    screen=Screen::TEST_RUN; layout();
}

// =============================================================== text input
void App::commitTextInput(){
    TextTarget t = textFocus;
    textFocus = TextTarget::NONE;
    if (t == TextTarget::ENGINE_PATH){
        std::error_code ec;
        if (!enginePathInput.empty() && fs::exists(enginePathInput, ec)){
            knownEngines.push_back({enginePathInput, fs::path(enginePathInput).filename().string(), {}});
            enginePathInput.clear();
            showToast("Engine added");
        } else if (!enginePathInput.empty())
            showToast("File not found: "+enginePathInput);
    } else if (t == TextTarget::MATCH_FEN){
        if (!matchFenInput.empty()){
            bool ok=false; Position p = Position::fromFEN(matchFenInput,&ok);
            std::string prob = ok ? enginePositionProblem(p) : "";
            if (!ok) showToast("That FEN is invalid - not added");
            else if (!prob.empty()) showToast("Not added: "+prob);
            else { matchFens.push_back(matchFenInput); matchFenInput.clear();
                   showToast("Position added ("+std::to_string(matchFens.size())+" total)"); }
        }
    } else if (t == TextTarget::FILE_PATH){
        if (!filePathInput.empty()){
            std::ifstream f(filePathInput);
            if (!f){ showToast("Cannot open file: "+filePathInput); }
            else if (filePurpose==1){
                int added=0, bad=0;
                std::string line;
                while (std::getline(f,line)){
                    while (!line.empty() && (line.back()=='\r'||line.back()==' ')) line.pop_back();
                    if (line.empty()||line[0]=='#') continue;
                    bool ok=false; Position p = Position::fromFEN(line,&ok);
                    if (ok && enginePositionProblem(p).empty()){ matchFens.push_back(line); added++; }
                    else bad++;
                }
                showToast("Added "+std::to_string(added)+" positions"
                          +(bad? " ("+std::to_string(bad)+" invalid skipped)":""));
                filePathInput.clear();
            } else if (filePurpose==2){
                std::stringstream ss; ss << f.rdbuf();
                if (loadPGNText(ss.str(), fs::path(filePathInput).filename().string()))
                    filePathInput.clear();
            }
        }
    } else if (t == TextTarget::OPTION_VALUE && editingOption>=0 && editingOption<(int)optsList.size()){
        const UciOption& o = optsList[editingOption];
        std::string v = optionEditBuffer;
        if (o.type=="spin"){
            long long n = atoll(v.c_str());
            if (n < o.minV) n = o.minV;
            if (n > o.maxV && o.maxV>o.minV) n = o.maxV;
            v = std::to_string(n);
        }
        setOverride(optsPath, o.name, v);
        editingOption = -1;
    }
    layout();
}

// =============================================================== events
void App::handleEvent(const UiEvent& e){
    if (e.kind == UiEvent::Closed){ window.close(); return; }
    if (e.kind == UiEvent::Resized){
        WW = e.width  / UIS;                   // stay in logical units
        WH = e.height / UIS;
        window.setView(sf::View(FR(0,0,WW,WH)));
        layout(); return;
    }
    if (e.kind == UiEvent::KeyPressed){
        if (textFocus != TextTarget::NONE){
            if (e.key==sf::Keyboard::Key::Enter){ commitTextInput(); return; }
            if (e.key==sf::Keyboard::Key::Escape){ textFocus=TextTarget::NONE; editingOption=-1; layout(); return; }
            if (e.key==sf::Keyboard::Key::V && (e.ctrl || e.sys)){
                std::string clip = sf::Clipboard::getString();
                if (textFocus==TextTarget::ENGINE_PATH) enginePathInput += clip;
                else if (textFocus==TextTarget::MATCH_FEN) matchFenInput += clip;
                else if (textFocus==TextTarget::FILE_PATH) filePathInput += clip;
                else if (textFocus==TextTarget::OPTION_VALUE) optionEditBuffer += clip;
                layout(); return;
            }
            return;
        }
        if (e.key==sf::Keyboard::Key::Escape){
            if (screen==Screen::GAME || screen==Screen::TESTS || screen==Screen::EDITOR
                || screen==Screen::PGN_LOAD || screen==Screen::PGN_LIST || screen==Screen::ACC_SETUP)
                { screen=Screen::MENU; layout(); }
            return;
        }
        if (screen==Screen::GAME){
            if (e.key==sf::Keyboard::Key::F){ boardFlipped=!boardFlipped; forceRedraw=true; }
            if (e.key==sf::Keyboard::Key::Left)  onButton(209);
            if (e.key==sf::Keyboard::Key::Right) onButton(210);
            if (e.key==sf::Keyboard::Key::Up)    onButton(208);
            if (e.key==sf::Keyboard::Key::Down)  onButton(211);
        }
        if (screen==Screen::MATCH_VIEW && e.key==sf::Keyboard::Key::F)
            { matchFlipped=!matchFlipped; layout(); }
    }
    if (e.kind == UiEvent::TextEntered && textFocus != TextTarget::NONE){
        std::string* buf = nullptr;
        if (textFocus==TextTarget::ENGINE_PATH) buf=&enginePathInput;
        else if (textFocus==TextTarget::MATCH_FEN) buf=&matchFenInput;
        else if (textFocus==TextTarget::FILE_PATH) buf=&filePathInput;
        else if (textFocus==TextTarget::OPTION_VALUE) buf=&optionEditBuffer;
        if (buf){
            if (e.unicode==8){ if(!buf->empty()) buf->pop_back(); }
            else if (e.unicode==13 || e.unicode==10){ commitTextInput(); return; }
            else if (e.unicode>=32 && e.unicode<127) *buf += (char)e.unicode;
            layout();
        }
        return;
    }
    sf::Vector2f m = window.mapPixelToCoords(sf::Mouse::getPosition(window));
    if (e.kind == UiEvent::MousePressed && e.button==sf::Mouse::Button::Left){
        for (auto& b : buttons)
            if (b.enabled && hit(b.rect, m)){ onButton(b.id); return; }
        if (textFocus != TextTarget::NONE){ textFocus=TextTarget::NONE; editingOption=-1; layout(); }
        if (screen==Screen::GAME && !pendingPromotion){
            float side = WH-40;
            int sq = boardSquareAt(m, 20, 20, side, boardFlipped);
            if (sq>=0){
                bool humanTurn = (mode!=GameMode::HUMAN_VS_ENGINE) || (game.pos.whiteToMove==humanIsWhite);
                if (humanTurn){
                    int v = game.pos.board[sq];
                    trySquareAction(sq);
                    if (viewPly<0 && v && (v>0)==game.pos.whiteToMove && selectedSq==sq){
                        dragging=true; dragFromSq=sq; dragPos=m;
                    }
                } else if (mode==GameMode::HUMAN_VS_ENGINE && viewPly<0
                           && game.result==GameResult::ONGOING){
                    // premove. Second click is a DESTINATION whenever the gesture is
                    // geometrically plausible for the picked-up piece - including onto
                    // one's own pieces (recaptures). Otherwise it re-selects/cancels.
                    int v = game.pos.board[sq];
                    bool own = v && (v>0)==humanIsWhite;
                    if (premoveFrom>=0 && premoveTo<0){
                        if (sq==premoveFrom){ premoveFrom=-1; }               // click again = cancel
                        else if (premovePlausible(game.pos, premoveFrom, sq)){
                            premoveTo=sq;                                     // set (own square OK)
                        } else if (own){                                      // implausible: re-select
                            premoveFrom=sq;
                            dragging=true; dragFromSq=-2; dragPos=m;          // -2 = premove drag
                        } else premoveFrom=-1;
                    } else {
                        premoveFrom=-1; premoveTo=-1;
                        if (own){ premoveFrom=sq; dragging=true; dragFromSq=-2; dragPos=m; }
                    }
                    forceRedraw=true;
                }
            }
        }
        if (screen==Screen::EDITOR){
            float side = WH-110;
            int sq = boardSquareAt(m, 20, 56, side, false);
            if (sq>=0){
                if (editorBrush==BRUSH_HAND){
                    if (editPos.board[sq]){ handFrom=sq; handPiece=editPos.board[sq];
                                            editPos.board[sq]=0; }
                } else {
                    editPos.board[sq] = editorBrush;
                    editorPainting = true;
                }
                forceRedraw = true;
            }
        }
    }
    if (e.kind == UiEvent::MousePressed && e.button==sf::Mouse::Button::Right){
        if (screen==Screen::EDITOR){
            float side = WH-110;
            int sq = boardSquareAt(m, 20, 56, side, false);
            if (sq>=0){ editPos.board[sq]=0; forceRedraw=true; }
        }
        if (screen==Screen::GAME && (premoveFrom>=0 || premoveTo>=0)){
            premoveFrom=premoveTo=-1; forceRedraw=true;    // right-click cancels premove
        }
    }
    if (e.kind == UiEvent::MouseMoved){
        if (dragging) dragPos = m;
        if (handFrom>=0) forceRedraw=true;                  // ghost piece follows cursor
        if (editorPainting && screen==Screen::EDITOR){
            float side = WH-110;
            int sq = boardSquareAt(m, 20, 56, side, false);
            if (sq>=0){ editPos.board[sq]=editorBrush; forceRedraw=true; }
        }
    }
    if (e.kind == UiEvent::MouseReleased && e.button==sf::Mouse::Button::Left){
        editorPainting = false;
        if (screen==Screen::EDITOR && handFrom>=0){
            float side = WH-110;
            int sq = boardSquareAt(m, 20, 56, side, false);
            editPos.board[sq>=0? sq : handFrom] = handPiece;   // drop, or snap back
            handFrom=-1; handPiece=0;
            forceRedraw=true;
        }
        if (dragging){
            bool premoveDrag = (dragFromSq==-2);
            dragging=false;
            float side = WH-40;
            int sq = boardSquareAt(m, 20, 20, side, boardFlipped);
            int from = dragFromSq; dragFromSq=-1;
            if (premoveDrag){
                if (sq>=0 && sq!=premoveFrom && premovePlausible(game.pos, premoveFrom, sq))
                    premoveTo=sq;
            } else if (sq>=0 && sq!=from) trySquareAction(sq);
            forceRedraw = true;
        }
    }
}

// =============================================================== buttons
void App::onButton(int id){
    forceRedraw = true;
    if (id>=110 && id<=116){
        tc.baseMs=TC_BASE[id-110]; tc.incMs=TC_INC[id-110]; layout(); return;
    }
    switch (id){
    // ---- menu ----
    case 100: mode=GameMode::HUMAN_VS_HUMAN; stopOpponent();
              newGame(Position::startpos());
              screen=Screen::GAME; layout(); break;
    case 101:
        humanIsWhite = menuPlayWhite; boardFlipped = !humanIsWhite;
        mode=GameMode::HUMAN_VS_ENGINE; pickPurpose=1; oneShotStart.reset();
        screen=Screen::PICK_ENGINE; scanEngines(); layout(); break;
    case 107: menuPlayWhite=!menuPlayWhite; layout(); break;
    case 103: matchSelection.clear(); screen=Screen::MATCH_SETUP; scanEngines(); layout(); break;
    case 104: mode=GameMode::ANALYSIS; pickPurpose=2; oneShotStart.reset();
              screen=Screen::PICK_ENGINE; scanEngines(); layout(); break;
    case 105: screen=Screen::TESTS; layout(); break;
    case 106: editorReturn=Screen::MENU; screen=Screen::EDITOR; layout(); break;
    case 108: filePurpose=2; filePathInput.clear(); screen=Screen::PGN_LOAD; layout(); break;
    case 120: window.close(); break;
    // ---- game ----
    case 200: stopOpponent(); stopAnalysis(); screen=Screen::MENU; layout(); break;
    case 201: newGame(game.start); layout(); break;   // restart THIS game's start position
    case 202: boardFlipped=!boardFlipped; break;
    case 203: stopOpponent();
              game.undo();
              if (mode==GameMode::HUMAN_VS_ENGINE && game.pos.whiteToMove!=humanIsWhite) game.undo();
              viewPly=-1; selectedSq=-1; legalForSelected.clear(); pendingPromotion.reset();
              premoveFrom=premoveTo=-1;
              clocksRunning = game.result==GameResult::ONGOING && mode!=GameMode::ANALYSIS;
              layout(); break;
    case 204: if (game.result==GameResult::ONGOING){
                  bool resignerWhite = (mode==GameMode::HUMAN_VS_ENGINE)? humanIsWhite : game.pos.whiteToMove;
                  game.result = resignerWhite? GameResult::BLACK_WINS : GameResult::WHITE_WINS;
                  game.reason = ResultReason::RESIGNATION; clocksRunning=false; stopOpponent();
              } break;
    case 205: copyFEN(); break;
    case 206: if (game.result!=GameResult::ONGOING || game.moves.empty() ||
                  mode!=GameMode::HUMAN_VS_ENGINE) pasteFEN();
              else showToast("Finish or reset the game first"); break;
    case 207: savePGN(); break;
    case 208: viewPly = 0; break;
    case 209: { int cur = viewPly<0 ? (int)game.moves.size() : viewPly;
                viewPly = std::max(0, cur-1); } break;
    case 210: if (viewPly>=0){ viewPly++; if (viewPly>=(int)game.moves.size()) viewPly=-1; } break;
    case 211: viewPly = -1; break;
    case 212:
        if (analysing){ stopAnalysis(); layout(); }
        else if (!anaEnginePath.empty()){ startAnalysis(anaEnginePath); layout(); }
        else { pickPurpose=2; screen=Screen::PICK_ENGINE; scanEngines(); layout(); }
        break;
    case 213: arrowsN = arrowsN%5 + 1;
              if (analysing && !anaEnginePath.empty()) startAnalysis(anaEnginePath);
              layout(); break;
    case 215: if (!anaEnginePath.empty()) startGameReview(anaEnginePath);
              else { pickPurpose=8; screen=Screen::PICK_ENGINE; scanEngines(); layout(); }
              break;
    case 250: case 251: case 252: case 253:
        if (pendingPromotion){
            Move m = *pendingPromotion; pendingPromotion.reset();
            int pr[4] = {9,5,4,3};
            m.promo = (int8_t)pr[id-250];
            Move real;
            if (game.pos.uciToMove(game.pos.moveToUCI(m), real)) humanCommitMove(real);
            layout();
        }
        break;
    // ---- engine picking ----
    case 380: textFocus=TextTarget::ENGINE_PATH; layout(); break;
    case 381: commitTextInput(); break;
    case 399: screen = (pickPurpose==7)? Screen::MATCH_SETUP
                     : (pickPurpose==8)? Screen::GAME
                     : (pickPurpose>=4)? Screen::TESTS : Screen::MENU;
              layout(); break;
    // ---- match setup ----
    case 450: matchGamesPerPair=std::max(1,matchGamesPerPair-1); break;
    case 451: matchGamesPerPair=std::min(50,matchGamesPerPair+1); break;
    case 452: textFocus=TextTarget::MATCH_FEN; layout(); break;
    case 453: matchFens.clear(); showToast("Position list cleared - standard start"); layout(); break;
    case 454: { std::string c = sf::Clipboard::getString();
                bool ok=false; Position p = Position::fromFEN(c,&ok);
                std::string prob = ok ? enginePositionProblem(p) : "";
                if (!ok) showToast("Clipboard does not contain a valid FEN");
                else if (!prob.empty()) showToast("Not added: "+prob);
                else { matchFens.push_back(c);
                       showToast("Position added ("+std::to_string(matchFens.size())+" total)"); }
                layout(); break; }
    case 455: editorReturn=Screen::MATCH_SETUP; screen=Screen::EDITOR; layout(); break;
    case 456: pickPurpose=7; screen=Screen::PICK_ENGINE; scanEngines(); layout(); break;
    case 457: judgePath.clear(); showToast("No judge engine"); layout(); break;
    case 458: textFocus=TextTarget::FILE_PATH; filePurpose=1; layout(); break;
    case 459: arrowsN = arrowsN%5 + 1; layout(); break;
    case 461: filePurpose=1;
              if (textFocus!=TextTarget::FILE_PATH) textFocus=TextTarget::FILE_PATH;
              commitTextInput(); break;
    case 460: {
        if (matchSelection.size()<2){ showToast("Select at least 2 engines"); break; }
        std::vector<EngineEntry> es;
        for (int i : matchSelection) es.push_back(makeEntry(i));
        runner.startTournament(es, tc, matchGamesPerPair, matchFens);
        if (judgeCtl){ judgeCtl->shutdown(); judgeCtl.reset(); }
        if (!judgePath.empty()){
            judgeCtl = std::make_unique<AnalysisController>();
            std::vector<std::pair<std::string,std::string>> opts;
            auto it = engineOverrides.find(judgePath);
            if (it != engineOverrides.end()) opts = it->second;
            opts.push_back({"MultiPV", std::to_string(arrowsN)});
            if (!judgeCtl->start(judgePath, opts)){
                showToast("Judge engine failed: "+judgeCtl->lastError());
                judgeCtl.reset();
            }
        }
        matchFlipped=false;
        screen=Screen::MATCH_VIEW; layout(); break; }
    case 499: screen=Screen::MENU; layout(); break;
    case 500: if (runner.running()) runner.abort();
              else { if (judgeCtl){ judgeCtl->shutdown(); judgeCtl.reset(); }
                     screen=Screen::MENU; }
              layout(); break;
    case 501: saveTournamentPGNs(); break;
    case 502: { auto s = runner.snapshot();
                std::string all;
                for (auto& st : s.standings){
                    char b[160];
                    snprintf(b,160,"%s  +%d =%d -%d  %.1f pts\n", st.name.c_str(),
                             st.wins, st.draws, st.losses, st.points());
                    all += b;
                }
                for (auto& l : s.log) all += l + "\n";
                sf::Clipboard::setString(all);
                showToast("Standings + log copied to clipboard"); break; }
    case 503: matchFlipped=!matchFlipped; layout(); break;
    // ---- tests ----
    case 600: startInternalSelfTest(); screen=Screen::TEST_RUN; layout(); break;
    case 601: pickPurpose=4; screen=Screen::PICK_ENGINE; scanEngines(); layout(); break;
    case 602: screen=Screen::ACC_SETUP; layout(); break;
    case 620: case 621: case 622: case 623: case 624:
        { const int c[5]={25,100,250,500,1000}; accCount=c[id-620]; layout(); break; }
    case 630: case 631: case 632: case 633:
        { const int c[4]={100,250,500,1000}; accTestMs=c[id-630]; layout(); break; }
    case 640: case 641: case 642: case 643:
        { const int c[4]={250,500,1000,2000}; accRefMs=c[id-640]; layout(); break; }
    case 650: case 651: case 652:
        { const int c[3]={1,3,5}; accMultiPV=c[id-650]; layout(); break; }
    case 660: pickPurpose=5; screen=Screen::PICK_ENGINE; scanEngines(); layout(); break;
    case 661: screen=Screen::TESTS; layout(); break;
    case 699: screen=Screen::MENU; layout(); break;
    case 700: if (testRunning) testAbort=true;
              else { screen=testReturn; }
              layout(); break;
    case 701: sf::Clipboard::setString(copyAllTestLines());
              showToast("Log copied to clipboard"); break;
    case 702: { std::error_code ec; fs::create_directories("logs", ec);
                char name[64]; std::time_t t=std::time(nullptr);
                std::strftime(name,64,"logs/test_%Y%m%d_%H%M%S.txt",std::localtime(&t));
                std::ofstream f(name); f << copyAllTestLines();
                showToast(std::string("Saved ")+name); break; }
    // ---- editor ----
    case 800: case 801: case 802: case 803: case 804: case 805:
    case 806: case 807: case 808: case 809: case 810: case 811:
        editorBrush = PALETTE[id-800]; layout(); break;
    case 812: editorBrush=0; layout(); break;
    case 813: editorBrush=BRUSH_HAND; layout(); break;
    case 820: editPos.whiteToMove=!editPos.whiteToMove; layout(); break;
    case 821: editPos.castleWK=!editPos.castleWK; layout(); break;
    case 822: editPos.castleWQ=!editPos.castleWQ; layout(); break;
    case 823: editPos.castleBK=!editPos.castleBK; layout(); break;
    case 824: editPos.castleBQ=!editPos.castleBQ; layout(); break;
    case 825: { Position p; p.whiteToMove=true; editPos=p;
                editPos.castleWK=editPos.castleWQ=editPos.castleBK=editPos.castleBQ=false;
                layout(); break; }
    case 826: editPos = Position::startpos(); layout(); break;
    case 827: { std::string pr = editorProblem();
                if (!pr.empty()){ showToast(pr); break; }
                sf::Clipboard::setString(editorFEN());
                showToast("FEN copied: "+editorFEN()); break; }
    case 828: { std::string c = sf::Clipboard::getString();
                bool ok=false; Position p = Position::fromFEN(c,&ok);
                if (ok){ editPos=p; showToast("Position loaded from FEN"); }
                else showToast("Clipboard does not contain a valid FEN");
                layout(); break; }
    case 830: case 831: case 832: case 833: case 835: {
        std::string pr = editorProblem();
        if (!pr.empty()){ showToast(pr); break; }
        bool ok=false;
        Position p = Position::fromFEN(editorFEN(), &ok);
        if (!ok){ showToast("Position is not valid"); break; }
        if (id==830){ mode=GameMode::HUMAN_VS_HUMAN; stopOpponent();
                      newGame(p); screen=Screen::GAME; }
        else if (id==831){ humanIsWhite=editorPlayWhite; boardFlipped=!humanIsWhite;
                           mode=GameMode::HUMAN_VS_ENGINE; pickPurpose=1;
                           oneShotStart=p;                       // consumed by the pick, once
                           screen=Screen::PICK_ENGINE; scanEngines(); }
        else if (id==832){ mode=GameMode::ANALYSIS; pickPurpose=2;
                           oneShotStart=p;
                           screen=Screen::PICK_ENGINE; scanEngines(); }
        else { matchFens.push_back(p.toFEN());
               screen=Screen::MATCH_SETUP; editorReturn=Screen::MENU;
               showToast("Position added to the match ("+std::to_string(matchFens.size())+" total)"); }
        layout(); break; }
    case 834: editorPlayWhite=!editorPlayWhite; layout(); break;
    case 899: screen = (editorReturn==Screen::MATCH_SETUP)? Screen::MATCH_SETUP : Screen::MENU;
              editorReturn=Screen::MENU; layout(); break;
    // ---- options editor ----
    case 950: optScroll=std::max(0,optScroll-5); layout(); break;
    case 951: optScroll=std::min(std::max(0,(int)optsList.size()-5), optScroll+5); layout(); break;
    case 955: engineOverrides.erase(optsPath); saveOverrides();
              showToast("Options reset to engine defaults"); layout(); break;
    case 999: screen=Screen::PICK_ENGINE; layout(); break;
    // ---- pgn ----
    case 1101: textFocus=TextTarget::FILE_PATH; filePurpose=2; layout(); break;
    case 1102: filePurpose=2;
               if (textFocus!=TextTarget::FILE_PATH) textFocus=TextTarget::FILE_PATH;
               commitTextInput(); break;
    case 1103: loadPGNText(sf::Clipboard::getString(), "clipboard"); break;
    case 1199: screen=Screen::MENU; layout(); break;
    case 1050: pgnScroll=std::max(0,pgnScroll-8); layout(); break;
    case 1051: pgnScroll=std::min(std::max(0,(int)pgnGames.size()-8), pgnScroll+8); layout(); break;
    case 1099: screen=Screen::MENU; layout(); break;
    default: break;
    }
    if (id>=1000 && id<1050 && screen==Screen::PGN_LIST){
        openPgnGame(pgnScroll + (id-1000));
    }
    // engine list clicks / configure
    if (id>=300 && id<340 && id-300 < (int)knownEngines.size() && screen==Screen::PICK_ENGINE){
        EngineEntry ee = makeEntry(id-300);
        if (pickPurpose==1){
            stopOpponent();
            oppEngine = std::make_unique<UciEngine>();
            if (!oppEngine->start(ee.path)){ showToast("Failed: "+oppEngine->lastError()); return; }
            for (auto& o : ee.options) oppEngine->setOption(o.first, o.second);
            oppPonderEnabled = false;
            for (auto& o : ee.options)
                if (o.first=="Ponder" && o.second=="true") oppPonderEnabled = true;
            oppEngine->newGame();
            newGame(oneShotStart? *oneShotStart : Position::startpos());
            oneShotStart.reset();
            game.whiteName = humanIsWhite? "Human" : oppEngine->name();
            game.blackName = humanIsWhite? oppEngine->name() : "Human";
            screen=Screen::GAME; layout();
        } else if (pickPurpose==2){
            startAnalysis(ee.path);
            if (mode==GameMode::ANALYSIS){
                newGame(oneShotStart? *oneShotStart : Position::startpos());
                oneShotStart.reset();
                clocksRunning=false;
                game.whiteName="White"; game.blackName="Black";
            }
            screen=Screen::GAME; layout();
        } else if (pickPurpose==4){
            startPerftTest(ee);
            screen=Screen::TEST_RUN; layout();
        } else if (pickPurpose==5){
            accTestEngine = ee; pickPurpose=6;
            showToast("Now pick the REFERENCE engine (e.g. Stockfish)");
            layout();
        } else if (pickPurpose==6){
            startAccuracyTest(accTestEngine, ee);
            screen=Screen::TEST_RUN; layout();
        } else if (pickPurpose==7){
            judgePath = ee.path;
            showToast("Judge engine: "+ee.name);
            screen=Screen::MATCH_SETUP; layout();
        } else if (pickPurpose==8){
            anaEnginePath = ee.path;
            startGameReview(ee.path);
        }
    }
    if (id>=340 && id<380 && id-340 < (int)knownEngines.size() && screen==Screen::PICK_ENGINE){
        const EngineEntry& ee = knownEngines[id-340];
        UciEngine probe;
        showToast("Querying "+ee.name+" for its options...");
        if (!probe.start(ee.path)){ showToast("Failed to query engine: "+probe.lastError()); return; }
        optsPath = ee.path;
        optsList = probe.options();
        probe.quit();
        optsList.erase(std::remove_if(optsList.begin(), optsList.end(),
            [](const UciOption& o){ return o.type=="button"; }), optsList.end());
        optScroll = 0; editingOption = -1;
        screen=Screen::ENGINE_OPTIONS; layout();
    }
    if (id>=400 && id<450 && id-400 < (int)knownEngines.size() && screen==Screen::MATCH_SETUP){
        int i=id-400;
        auto it = std::find(matchSelection.begin(),matchSelection.end(),i);
        if (it==matchSelection.end()) matchSelection.push_back(i); else matchSelection.erase(it);
        layout();
    }
    if (screen==Screen::ENGINE_OPTIONS){
        auto optAt=[&](int row)->int{ int oi=optScroll+row; return oi<(int)optsList.size()? oi : -1; };
        if (id>=900 && id<920){
            int oi = optAt(id-900);
            if (oi>=0){
                const UciOption& o = optsList[oi];
                std::string cur = overrideOrDefault(optsPath, o);
                if (o.type=="check") setOverride(optsPath, o.name, cur=="true"?"false":"true");
                else if (o.type=="spin"){
                    long long n = atoll(cur.c_str()) - 1;
                    if (n < o.minV) n = o.minV;
                    setOverride(optsPath, o.name, std::to_string(n));
                }
                else if (o.type=="combo" && !o.vars.empty()){
                    int k=0;
                    for (size_t v=0; v<o.vars.size(); v++) if (o.vars[v]==cur){ k=(int)v; break; }
                    setOverride(optsPath, o.name, o.vars[(k+1)%o.vars.size()]);
                }
                layout();
            }
        }
        if (id>=920 && id<940){
            int oi = optAt(id-920);
            if (oi>=0 && optsList[oi].type=="spin"){
                const UciOption& o = optsList[oi];
                long long n = atoll(overrideOrDefault(optsPath,o).c_str()) + 1;
                if (o.maxV>o.minV && n>o.maxV) n=o.maxV;
                setOverride(optsPath, o.name, std::to_string(n));
                layout();
            }
        }
        if (id>=940 && id<950){
            int oi = optAt(id-940);
            if (oi>=0){
                editingOption = oi;
                optionEditBuffer = overrideOrDefault(optsPath, optsList[oi]);
                textFocus = TextTarget::OPTION_VALUE;
                layout();
            }
        }
    }
}

// =============================================================== update
void App::update(){
    if (screen==Screen::GAME){
        long long dt = tickClock.restart().asMilliseconds();
        if (clocksRunning && game.result==GameResult::ONGOING && !game.moves.empty()){
            if (game.pos.whiteToMove) wClockMs -= dt; else bClockMs -= dt;
            if (wClockMs<=0){ wClockMs=0; game.result=GameResult::BLACK_WINS; game.reason=ResultReason::TIMEOUT; clocksRunning=false; stopOpponent(); forceRedraw=true; }
            if (bClockMs<=0){ bClockMs=0; game.result=GameResult::WHITE_WINS; game.reason=ResultReason::TIMEOUT; clocksRunning=false; stopOpponent(); forceRedraw=true; }
        }
        applyEngineMoveIfReady();
        startEngineThinkIfNeeded();
        if (analysing && anaCtl) anaCtl->setFen(shownPosition().toFEN());
    } else tickClock.restart();
    if (screen==Screen::MATCH_VIEW && judgeCtl)
        judgeCtl->setFen(runner.snapshot().pos.toFEN());
    if (screen==Screen::MATCH_VIEW || screen==Screen::TEST_RUN){
        static sf::Clock relayout;
        if (relayout.getElapsedTime().asMilliseconds()>400){ layout(); relayout.restart(); }
    }
}

// =============================================================== render
static std::string clockStr(long long ms){
    if (ms<0) ms=0;
    long long s = ms/1000;
    char b[32];
    if (s>=3600) snprintf(b,32,"%lld:%02lld:%02lld",s/3600,(s/60)%60,s%60);
    else snprintf(b,32,"%02lld:%02lld",s/60,s%60);
    return b;
}
static std::string reasonStr(ResultReason r){
    switch(r){
    case ResultReason::CHECKMATE: return "checkmate";
    case ResultReason::STALEMATE: return "stalemate";
    case ResultReason::FIFTY_MOVE: return "50-move rule";
    case ResultReason::REPETITION: return "threefold repetition";
    case ResultReason::INSUFFICIENT: return "insufficient material";
    case ResultReason::RESIGNATION: return "resignation";
    case ResultReason::TIMEOUT: return "time forfeit";
    case ResultReason::ILLEGAL_MOVE: return "illegal move";
    case ResultReason::AGREEMENT: return "agreement";
    default: return "";
    }
}

void App::render(){
    window.clear(COL_BG);
    switch (screen){
    case Screen::MENU: {
        text("Chess GUI for UCI", WW/2, 60, 42, COL_ACCENT, true);
        text("Play, run engine matches & tournaments, analyse games, and test your own UCI engine",
             WW/2, 104, 15, COL_DIM, true);
        float tcY = 132 + 6*52 + 44 + 64;
        text("Time control for games & matches:", WW/2, tcY - 26, 14, COL_DIM, true);
        break; }
    case Screen::GAME: {
        float side = WH-40;
        float rx = side + 40;
        float pw = WW - rx - 20;
        Position shown = shownPosition();
        int lastFrom=-1, lastTo=-1;
        if (viewPly>0){ lastFrom=game.moves[viewPly-1].from; lastTo=game.moves[viewPly-1].to; }
        else if (viewPly<0 && !game.moves.empty()){
            lastFrom=game.moves.back().from; lastTo=game.moves.back().to;
        }
        drawBoard(20,20,side, shown,
                  viewPly<0? selectedSq : -1,
                  viewPly<0? &legalForSelected : nullptr,
                  lastFrom,lastTo, boardFlipped,
                  dragging && dragFromSq>=0? dragFromSq : -1, dragPos);
        // premove tint (red, origin lighter than destination)
        if (premoveFrom>=0) drawSquareTint(premoveFrom, 20,20,side, boardFlipped, COL_PRE_FROM);
        if (premoveTo>=0)   drawSquareTint(premoveTo,   20,20,side, boardFlipped, COL_PRE_TO);
        // engine suggestion arrows
        EngineInfo ai;
        std::vector<PvLine> apvs;
        bool haveAna = analysing && anaCtl;
        if (haveAna){ ai = anaCtl->info(); apvs = anaCtl->pvs(); }
        std::string bestSan;
        if (haveAna && !apvs.empty() && apvs[0].valid){
            drawPvArrows(apvs, shown, 20,20,side, boardFlipped, arrowsN);
            Move bm;
            if (shown.uciToMove(apvs[0].firstMove(), bm)) bestSan = shown.moveToSAN(bm);
        } else if (haveAna && ai.valid && !ai.pv.empty()){
            std::string tok = ai.pv.substr(0, ai.pv.find(' '));
            Move bm;
            if (shown.uciToMove(tok, bm)){
                drawArrow(20,20,side, bm.from, bm.to, boardFlipped, sf::Color(70,140,220,210));
                bestSan = shown.moveToSAN(bm);
            }
        }
        // ---- right panel bands ----
        float buttonsTop = gameButtonsTop>0? gameButtonsTop : WH-20-4*34-3*8;
        float listTop = 84;
        float infoTop = buttonsTop - 96;
        float listBot = infoTop - 8;
        bool whiteAtBottom = !boardFlipped;
        std::string topName = whiteAtBottom? game.blackName : game.whiteName;
        std::string botName = whiteAtBottom? game.whiteName : game.blackName;
        long long topClk = whiteAtBottom? bClockMs : wClockMs;
        long long botClk = whiteAtBottom? wClockMs : bClockMs;
        text(topName, rx, 56, 16);
        text(clockStr(topClk), rx+pw-86, 54, 20,
             topClk<30000? COL_ERR : sf::Color::White);
        drawMoveList(rx, listTop, pw-32, listBot-listTop, game.sans,
                     game.start.fullmoveNumber, game.start.whiteToMove);
        if (haveAna){
            int cpW = 0, mateW = 0;
            bool valid = ai.valid;
            if (valid){
                cpW = shown.whiteToMove? ai.scoreCp : -ai.scoreCp;
                mateW = shown.whiteToMove? ai.mateIn : -ai.mateIn;
            }
            drawEvalBar(rx+pw-24, listTop, listBot-listTop-18, cpW, valid&&ai.isMate, mateW);
            if (!anaCtl->alive())
                text("Analysis engine terminated - toggle Analysis to restart", rx, infoTop, 13, COL_ERR);
            else if (!valid)
                text("waiting for analysis from "+anaCtl->engineName()+"...", rx, infoTop, 13, COL_WARN);
            else {
                char sb[16];
                std::string sc = ai.isMate ? ("M"+std::to_string(std::abs(ai.mateIn)))
                                           : (snprintf(sb,16,"%+.2f",cpW/100.0), std::string(sb));
                text("Best: "+(bestSan.empty()? std::string("?") : bestSan)
                     +"   depth "+std::to_string(ai.depth)+"   "+sc, rx, infoTop, 14, COL_GOOD);
                text(ai.pv.substr(0,70), rx, infoTop+20, 13, sf::Color(150,200,150));
            }
        }
        std::string st;
        if (game.result!=GameResult::ONGOING)
            st = game.resultString()+"  ("+reasonStr(game.reason)+")";
        else if (viewPly>=0)
            st = "Viewing move "+std::to_string(viewPly)+"/"+std::to_string(game.moves.size())+"  (>| returns to live)";
        else if (mode==GameMode::HUMAN_VS_ENGINE && game.pos.whiteToMove!=humanIsWhite)
            st = (premoveFrom>=0&&premoveTo>=0)? "Engine is thinking... (premove set)"
                                               : "Engine is thinking...";
        else st = game.pos.whiteToMove? "White to move" : "Black to move";
        text(st, rx, infoTop+42, 14, COL_ACCENT);
        text(botName, rx, infoTop+68, 16);
        text(clockStr(botClk), rx+pw-86, infoTop+66, 20,
             botClk<30000? COL_ERR : sf::Color::White);
        if (pendingPromotion){
            sf::RectangleShape dim(sf::Vector2f(side,side)); dim.setPosition({20,20});
            dim.setFillColor(sf::Color(0,0,0,120)); window.draw(dim);
            text("Promote to:", 20+side/2, 20+side/2 - side/8, 22, sf::Color::White, true);
        }
        break; }
    case Screen::PICK_ENGINE: {
        const char* titles[] = {"","Choose your OPPONENT engine","Choose the ANALYSIS engine","",
                                "Choose the engine to PERFT-TEST",
                                "Choose YOUR engine to accuracy-test",
                                "Choose the REFERENCE engine (e.g. Stockfish)",
                                "Choose the JUDGE engine (eval bar & arrows)",
                                "Choose the engine for the game review"};
        text(titles[std::min(pickPurpose,8)], WW/2, 46, 25, COL_ACCENT, true);
        text("Engines are auto-discovered from Resources/engines/. 'Options...' edits an engine's UCI settings.",
             WW/2, 80, 14, COL_DIM, true);
        if (knownEngines.empty())
            text("No engines found. Put UCI engine executables in Resources/engines/ or type a path below.",
                 WW/2, 200, 16, COL_WARN, true);
        break; }
    case Screen::MATCH_SETUP: {
        text("Engine vs Engine / Tournament", WW/2, 40, 24, COL_ACCENT, true);
        text("Click engines to select (2 = match, 3+ = round-robin). Colors alternate; with multiple",
             WW/2, 68, 13, COL_DIM, true);
        float listN = (float)std::min(knownEngines.size(),(size_t)6);
        float rowY = 96 + listN*40 + 24;
        text("openings, each one is played with both colors before moving to the next.", WW/2, 82, 13, COL_DIM, true);
        text("Games per pairing:", 120, rowY-20, 13, COL_DIM);
        text(std::to_string(matchGamesPerPair), 182, rowY+6, 17, sf::Color::White, true);
        text("Time control:", 330, rowY-20, 13, COL_DIM);
        std::string posLabel = "Start positions: " + (matchFens.empty()
            ? std::string("standard opening")
            : std::to_string(matchFens.size())+" loaded");
        text(posLabel, 120, rowY+38, 13, matchFens.empty()? COL_DIM : COL_GOOD);
        text("Judge engine (independent eval bar + move arrows; plays no games):",
             120, rowY+130, 13, COL_DIM);
        break; }
    case Screen::MATCH_VIEW: {
        auto s = runner.snapshot();
        float side = std::min(WH-120.f, WW*0.48f);
        // live clocks: tick the mover's clock between snapshot posts
        long long wc = s.wClockMs, bc = s.bClockMs;
        if (!s.finished && s.result==GameResult::ONGOING && s.postedAtMs>0){
            long long el = nowMsSteady() - s.postedAtMs;
            if (el>0){ if (s.pos.whiteToMove) wc-=el; else bc-=el; }
            if (wc<0)wc=0; if (bc<0)bc=0;
        }
        drawBoard(20, 56, side, s.pos, -1, nullptr, s.lastFrom, s.lastTo, matchFlipped, -1, {});
        std::string tn = matchFlipped? s.whiteName : s.blackName;
        std::string bn = matchFlipped? s.blackName : s.whiteName;
        long long tclk = matchFlipped? wc : bc;
        long long bclk = matchFlipped? bc : wc;
        text(tn + "   " + clockStr(tclk), 20, 24, 17);
        text(bn + "   " + clockStr(bclk), 20, 62+side, 17);
        // judge arrows on the live board
        if (judgeCtl){
            auto jp = judgeCtl->pvs();
            if (!jp.empty() && jp[0].valid)
                drawPvArrows(jp, s.pos, 20, 56, side, matchFlipped, arrowsN);
        }
        float rx = side + 56;
        text("Game "+std::to_string(s.gameIndex)+"/"+std::to_string(s.totalGames), rx, 24, 19, COL_ACCENT);
        // eval bar: judge if present (independent), else the players' own reports
        int cpW = s.scoreCpWhitePOV; bool isM = s.scoreIsMate; int mIn = s.mateIn;
        std::string judgeLine;
        if (judgeCtl){
            auto jp = judgeCtl->pvs();
            if (!judgeCtl->alive()) judgeLine = "Judge engine terminated";
            else if (!jp.empty() && jp[0].valid){
                cpW = s.pos.whiteToMove? jp[0].scoreCp : -jp[0].scoreCp;
                isM = jp[0].isMate;
                mIn = s.pos.whiteToMove? jp[0].mateIn : -jp[0].mateIn;
                Move bm;
                std::string san = s.pos.uciToMove(jp[0].firstMove(), bm)
                                  ? s.pos.moveToSAN(bm) : jp[0].firstMove();
                judgeLine = "Judge "+judgeCtl->engineName()+":  best "+san
                           +"  (depth "+std::to_string(jp[0].depth)+")";
            } else judgeLine = "Judge: waiting for analysis...";
        }
        drawEvalBar(rx, 56, side-90, cpW, isM, mIn);
        float tx = rx+52;
        float maxTx = WW-190;
        float ty = 56;
        text("Standings", tx, ty, 17, COL_ACCENT); ty+=28;
        for (auto& st : s.standings){
            char b[160];
            snprintf(b,160,"%-22s +%d =%d -%d  %.1f", st.name.substr(0,22).c_str(),
                     st.wins, st.draws, st.losses, st.points());
            text(b, tx, ty, 14); ty+=22;
        }
        ty+=10;
        if (!judgeLine.empty()){
            text(judgeLine.substr(0,(size_t)((maxTx-tx)/7)), tx, ty, 14,
                 judgeLine.find("terminated")!=std::string::npos? COL_ERR : COL_GOOD);
            ty+=24;
        }
        for (auto& l : s.log){
            std::string ln = l;
            if (ln.size() > (size_t)((maxTx-tx)/7)) ln = ln.substr(0,(size_t)((maxTx-tx)/7))+"...";
            text(ln, tx, ty, 14, COL_DIM); ty+=20;
        }
        if (s.result!=GameResult::ONGOING && !s.finished)
            text("Last result: "+reasonStr(s.reason), tx, ty+6, 14, COL_ACCENT);
        if (s.finished) text("Tournament finished.", tx, ty+6, 16, COL_GOOD);
        break; }
    case Screen::TESTS: {
        text("Engine Testing Tools", WW/2, 56, 28, COL_ACCENT, true);
        text("Tools for chess-engine developers. Test 2 requires your engine to support 'go perft N'\n"
             "(printing 'Nodes searched: X', like Stockfish). Test 3 measures move quality against a\n"
             "reference engine over up to 1000 positions, with credit for good alternative moves.",
             WW/2, 112, 14, COL_DIM, true);
        break; }
    case Screen::ACC_SETUP: {
        text("Accuracy Test Settings", WW/2, 56, 26, COL_ACCENT, true);
        float y=150;
        text("Number of positions", 90, y+8, 15);
        text("(1000-position suite ships in Resources/accuracy.epd)", 90, y+28, 11, COL_DIM); y+=76;
        text("Your engine: time per move", 90, y+8, 15); y+=76;
        text("Reference: time per position", 90, y+8, 15);
        text("(one MultiPV search scores all candidate moves fairly)", 90, y+28, 11, COL_DIM); y+=76;
        text("Reference MultiPV", 90, y+8, 15);
        text("(alternatives shown & credited at their true cp gap)", 90, y+28, 11, COL_DIM);
        char est[120];
        double mins = accCount * (accTestMs + accRefMs) / 60000.0;
        snprintf(est,120,"Estimated duration: about %.0f-%.0f minutes", mins, mins*1.4);
        text(est, WW/2, y+140, 14, COL_WARN, true);
        break; }
    case Screen::TEST_RUN: {
        text(testTitle, 30, 20, 19, COL_ACCENT);
        std::vector<std::string> lines;
        { std::lock_guard<std::mutex> lk(testMx); lines = testLines; }
        int maxRows = (int)((WH-80)/20);
        int first = std::max(0, (int)lines.size()-maxRows);
        float y=60;
        float maxW = WW-200;
        for (int i=first;i<(int)lines.size();i++){
            sf::Color c = sf::Color(210,210,215);
            if (lines[i].find("FAIL")!=std::string::npos || lines[i].find(" ??")!=std::string::npos) c = COL_ERR;
            else if (lines[i].find(">>>")!=std::string::npos) c = COL_GOOD;
            std::string ln = lines[i];
            if (ln.size() > (size_t)(maxW/7)) ln = ln.substr(0,(size_t)(maxW/7))+"...";
            text(ln, 30, y, 14, c); y+=20;
        }
        if (testRunning) text("running...", WW-160, 140, 14, COL_WARN);
        break; }
    case Screen::EDITOR: {
        float side = WH-110;
        text("Board Editor", 20, 14, 24, COL_ACCENT);
        text("Left-click paints (drag to paint), right-click erases. Hand tool drags pieces.",
             20, WH-44, 14, COL_DIM);
        drawBoard(20, 56, side, editPos, -1, nullptr, -1, -1, false, handFrom, dragPos);
        // hand-tool ghost follows the cursor
        if (handFrom>=0 && handPiece){
            sf::Vector2f mp = window.mapPixelToCoords(sf::Mouse::getPosition(window));
            drawPiece(handPiece, mp.x-(side/8)/2, mp.y-(side/8)/2, side/8, 230);
        }
        float rx = side + 40;
        text("Piece palette:", rx, 34, 15, COL_DIM);
        std::string pr = editorProblem();
        float cell = std::min((WW-rx-20)/6.f,54.f);
        float probY = 56 + 2*cell + 10 + 44 + 40 + 44 + 54 - 20;
        if (!pr.empty()) text("Cannot use yet: "+pr, rx, probY, 14, COL_WARN);
        else text("Position OK - FEN: "+editorFEN().substr(0,50)+"...", rx, probY, 13, COL_GOOD);
        break; }
    case Screen::ENGINE_OPTIONS: {
        text("UCI options - "+fs::path(optsPath).filename().string(), WW/2, 34, 23, COL_ACCENT, true);
        text("Saved automatically and applied every time this engine starts (play, analysis, matches, tests).",
             WW/2, 66, 13, COL_DIM, true);
        float y = 96;
        int rows = std::min((int)((WH-190)/44), 10);
        for (int r=0; r<rows; r++){
            int oi = optScroll + r;
            if (oi >= (int)optsList.size()) break;
            const UciOption& o = optsList[oi];
            text(o.name, 60, y+8, 15);
            std::string meta = o.type;
            if (o.type=="spin") meta += "  ["+std::to_string(o.minV)+".."+std::to_string(o.maxV)+"]";
            bool overridden = false;
            auto it = engineOverrides.find(optsPath);
            if (it!=engineOverrides.end())
                for (auto& p : it->second) if (p.first==o.name) overridden=true;
            if (overridden) meta += "   (modified)";
            text(meta, 60, y+8+16, 12, overridden? COL_GOOD : sf::Color(130,130,140));
            y += 44;
        }
        if ((int)optsList.size() > rows)
            text(std::to_string(optScroll+1)+"-"+std::to_string(std::min(optScroll+rows,(int)optsList.size()))
                 +" of "+std::to_string(optsList.size()), WW-95, 76, 13, COL_DIM, true);
        if (optsList.empty())
            text("This engine advertises no UCI options.", WW/2, 160, 16, COL_WARN, true);
        text("Ponder is fully supported in Human-vs-Engine: enable it here and the engine thinks on your time.",
             60, WH-56, 13, COL_DIM);
        break; }
    case Screen::PGN_LOAD: {
        text("Load PGN for analysis", WW/2, 56, 26, COL_ACCENT, true);
        text("Load a .pgn file (multi-game files supported) or paste PGN text. The game opens on the\n"
             "analysis board: step through with arrow keys, toggle Analysis for engine evaluation and\n"
             "arrows, or press 'Run game review' for a full engine annotation of every move.",
             WW/2, 110, 14, COL_DIM, true);
        break; }
    case Screen::PGN_LIST: {
        text("Choose a game ("+std::to_string(pgnGames.size())+" in file)", WW/2, 56, 24, COL_ACCENT, true);
        if ((int)pgnGames.size() > 12)
            text(std::to_string(pgnScroll+1)+"-"+std::to_string(std::min(pgnScroll+12,(int)pgnGames.size()))
                 +" of "+std::to_string(pgnGames.size()), WW-95, 80, 13, COL_DIM, true);
        break; }
    default: break;
    }
    drawButtons();
    if (screen==Screen::EDITOR && !paletteRects.empty()){
        for (int i=0;i<12 && i<(int)paletteRects.size();i++){
            auto& r = paletteRects[i];
            drawPiece(PALETTE[i], rectL(r)+2, rectT(r)+2, rectW(r)-4);
        }
    }
    if (!toast.empty()){
        if (toastClock.getElapsedTime().asSeconds()>3) toast.clear();
        else {
            sf::RectangleShape r(sf::Vector2f(std::min(WW-40, 640.f), 34));
            r.setPosition({WW/2 - r.getSize().x/2, WH-44});
            r.setFillColor(sf::Color(20,20,25,230));
            r.setOutlineThickness(1); r.setOutlineColor(COL_ACCENT);
            window.draw(r);
            text(toast, WW/2, WH-27, 14, sf::Color::White, true);
        }
    }
    window.display();
}
