/* Chess-GUI-for-UCI — SFML GUI. GPL-2.0 */
#include "gui.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cmath>
#include <ctime>
namespace fs = std::filesystem;

static const sf::Color COL_BG(24,24,28);
static const sf::Color COL_PANEL(38,38,46);
static const sf::Color COL_BTN(58,58,72);
static const sf::Color COL_BTN_HOT(80,80,100);
static const sf::Color COL_BTN_TOG(70,110,70);
static const sf::Color COL_LIGHT(240,217,181);
static const sf::Color COL_DARK(181,136,99);
static const sf::Color COL_SEL(110,160,90,180);
static const sf::Color COL_LAST(205,210,106,120);
static const sf::Color COL_CHECK(220,80,80,170);
static const sf::Color COL_ACCENT(120,170,255);

static float WW = 1280, WH = 800;

// ---------------------------------------------------------------- init/run
bool App::init(){
    sf::VideoMode desk = sf::VideoMode::getDesktopMode();
    WW = std::min(1280u, desk.width);  WH = std::min(800u, desk.height);
    window.create(sf::VideoMode((unsigned)WW,(unsigned)WH), "Chess GUI for UCI", sf::Style::Default);
    window.setFramerateLimit(60);
    window.setVerticalSyncEnabled(true);
    const char* fontPaths[] = {
        "Resources/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "C:\\Windows\\Fonts\\arial.ttf"
    };
    bool fontOk=false;
    for (auto p : fontPaths) if (font.loadFromFile(p)){ fontOk=true; break; }
    if (!fontOk){ fprintf(stderr,"No font found (put arial.ttf in Resources/)\n"); return false; }
    piecesOk = pieceTexture.loadFromFile("Resources/pieceTexture.png");
    if (piecesOk) pieceTexture.setSmooth(true);
    scanEngines();
    game.reset(Position::startpos());
    tc = TimeControl{300000,0};
    layout();
    return true;
}

void App::run(){
    while (window.isOpen()){
        sf::Event e;
        while (window.pollEvent(e)) handleEvent(e);
        update();
        render();
    }
    stopOpponent(); stopAnalysis();
    runner.abort();
    testAbort = true;
    if (testThread.joinable()) testThread.join();
}

// ---------------------------------------------------------------- helpers
void App::text(const std::string& s, float x, float y, unsigned size, sf::Color c, bool center){
    sf::Text t(s, font, size);
    t.setFillColor(c);
    if (center){
        auto b = t.getLocalBounds();
        t.setOrigin(b.left + b.width/2.f, b.top + b.height/2.f);
    }
    t.setPosition(x,y);
    window.draw(t);
}
static bool hit(const sf::FloatRect& r, sf::Vector2f p){ return r.contains(p); }

void App::drawButtons(){
    sf::Vector2f m = window.mapPixelToCoords(sf::Mouse::getPosition(window));
    for (auto& b : buttons){
        sf::RectangleShape r(sf::Vector2f(b.rect.width, b.rect.height));
        r.setPosition(b.rect.left, b.rect.top);
        sf::Color c = b.toggled? COL_BTN_TOG : (hit(b.rect,m)&&b.enabled ? COL_BTN_HOT : COL_BTN);
        if (!b.enabled) c = sf::Color(45,45,50);
        r.setFillColor(c);
        r.setOutlineThickness(1); r.setOutlineColor(sf::Color(90,90,110));
        window.draw(r);
        text(b.label, b.rect.left+b.rect.width/2, b.rect.top+b.rect.height/2, 
             (unsigned)std::min(18.f, b.rect.height*0.45f), 
             b.enabled? sf::Color::White : sf::Color(140,140,140), true);
    }
}

void App::drawPiece(int piece, float x, float y, float size, sf::Uint8 alpha){
    if (!piece) return;
    if (piecesOk){
        int col=5;
        switch (std::abs(piece)){case 20:col=0;break;case 9:col=1;break;case 4:col=2;break;
                                  case 3:col=3;break;case 5:col=4;break;case 1:col=5;break;}
        int row = piece>0 ? 0 : 1;
        sf::Sprite sp(pieceTexture, sf::IntRect(col*150,row*150,150,150));
        sp.setScale(size/150.f, size/150.f);
        sp.setColor(sf::Color(255,255,255,alpha));
        sp.setPosition(x,y);
        window.draw(sp);
    } else {
        char c = "  ? "[0];
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

void App::drawBoard(float bx, float by, float side, const Position& p,
                    int selSq, const std::vector<Move>* legal,
                    int lastFrom, int lastTo, bool flipped, int dragSq, sf::Vector2f dPos){
    float sq = side/8;
    int checkSq = -1;
    if (p.inCheck(p.whiteToMove)) checkSq = p.kingSquare(p.whiteToMove);
    for (int i=0;i<64;i++){
        int x=i%8, y=i/8;
        int dx = flipped? 7-x : x, dy = flipped? 7-y : y;
        sf::RectangleShape r(sf::Vector2f(sq,sq));
        r.setPosition(bx+dx*sq, by+dy*sq);
        r.setFillColor(((x+y)%2)? COL_DARK : COL_LIGHT);
        window.draw(r);
        auto overlay=[&](sf::Color c){
            sf::RectangleShape o(sf::Vector2f(sq,sq));
            o.setPosition(bx+dx*sq, by+dy*sq); o.setFillColor(c); window.draw(o);
        };
        if (i==lastFrom || i==lastTo) overlay(COL_LAST);
        if (i==selSq) overlay(COL_SEL);
        if (i==checkSq) overlay(COL_CHECK);
    }
    // legal move markers
    if (legal){
        for (const Move& m : *legal){
            int x=m.to%8, y=m.to/8;
            int dx = flipped?7-x:x, dy = flipped?7-y:y;
            sf::CircleShape c(p.board[m.to]||m.isEnPassant ? sq*0.42f : sq*0.15f);
            c.setOrigin(c.getRadius(), c.getRadius());
            c.setPosition(bx+dx*sq+sq/2, by+dy*sq+sq/2);
            if (p.board[m.to]||m.isEnPassant){
                c.setFillColor(sf::Color::Transparent);
                c.setOutlineThickness(sq*0.07f);
                c.setOutlineColor(sf::Color(40,90,40,150));
            } else c.setFillColor(sf::Color(40,90,40,120));
            window.draw(c);
        }
    }
    // pieces
    for (int i=0;i<64;i++){
        if (!p.board[i] || i==dragSq) continue;
        int x=i%8, y=i/8;
        int dx = flipped?7-x:x, dy = flipped?7-y:y;
        drawPiece(p.board[i], bx+dx*sq, by+dy*sq, sq);
    }
    // coordinates
    for (int i=0;i<8;i++){
        char f = flipped? 'h'-i : 'a'+i;
        char rk = flipped? '1'+i : '8'-i;
        text(std::string(1,f), bx+i*sq+sq-10, by+side-14, 12,
             (i%2)? COL_LIGHT : COL_DARK);
        text(std::string(1,rk), bx+3, by+i*sq+2, 12,
             (i%2)? COL_LIGHT : COL_DARK);
    }
    // dragged piece on top
    if (dragSq>=0 && p.board[dragSq])
        drawPiece(p.board[dragSq], dPos.x-sq/2, dPos.y-sq/2, sq, 230);
    // border
    sf::RectangleShape br(sf::Vector2f(side,side));
    br.setPosition(bx,by); br.setFillColor(sf::Color::Transparent);
    br.setOutlineThickness(2); br.setOutlineColor(sf::Color(90,90,110));
    window.draw(br);
}

void App::drawEvalBar(float x, float y, float h, int cp, bool isMate, int mateIn){
    float frac;
    if (isMate) frac = mateIn>0 ? 1.f : 0.f;
    else frac = 0.5f + 0.5f * std::tanh(cp/600.0f);
    sf::RectangleShape black(sf::Vector2f(22, h)); black.setPosition(x,y);
    black.setFillColor(sf::Color(30,30,30)); window.draw(black);
    sf::RectangleShape white(sf::Vector2f(22, h*frac));
    white.setPosition(x, y+h*(1-frac));
    white.setFillColor(sf::Color(230,230,230)); window.draw(white);
    sf::RectangleShape mid(sf::Vector2f(22,2)); mid.setPosition(x, y+h/2);
    mid.setFillColor(sf::Color(120,120,120)); window.draw(mid);
    std::string s;
    if (isMate) s = "M" + std::to_string(std::abs(mateIn));
    else { char b[16]; snprintf(b,16,"%+.1f", cp/100.0); s=b; }
    text(s, x+11, y+h+12, 13, sf::Color::White, true);
}

void App::drawMoveList(float x, float y, float w, float h,
                       const std::vector<std::string>& sans, int startNo, bool startWhite){
    sf::RectangleShape r(sf::Vector2f(w,h)); r.setPosition(x,y);
    r.setFillColor(COL_PANEL); window.draw(r);
    float lh = 20;
    int rows = (int)(h/lh) - 1;
    // build rows of "N. white black"
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

// ---------------------------------------------------------------- engines
void App::scanEngines(){
    knownEngines.clear();
    auto add=[&](const std::string& p){
        for (auto& e : knownEngines) if (e.path==p) return;
        knownEngines.push_back({p, fs::path(p).filename().string()});
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

// ---------------------------------------------------------------- layout
static Button mk(float x,float y,float w,float h,const std::string& s,int id,bool tog=false){
    Button b; b.rect={x,y,w,h}; b.label=s; b.id=id; b.toggled=tog; return b;
}
void App::layout(){
    buttons.clear();
    float cx = WW/2;
    switch (screen){
    case Screen::MENU: {
        float w=360,h=46,y=170;
        buttons.push_back(mk(cx-w/2,y,w,h,"Play: Human vs Human",100)); y+=58;
        buttons.push_back(mk(cx-w/2,y,w,h,"Play vs Engine (you are White)",101)); y+=58;
        buttons.push_back(mk(cx-w/2,y,w,h,"Play vs Engine (you are Black)",102)); y+=58;
        buttons.push_back(mk(cx-w/2,y,w,h,"Engine vs Engine / Tournament",103)); y+=58;
        buttons.push_back(mk(cx-w/2,y,w,h,"Analysis Board",104)); y+=58;
        buttons.push_back(mk(cx-w/2,y,w,h,"Engine Testing Tools",105)); y+=70;
        // time controls
        const char* tcs[] = {"1+0","3+2","5+0","10+0","15+10","30+0"};
        int bases[] = {60000,180000,300000,600000,900000,1800000};
        int incs[]  = {0,2000,0,0,10000,0};
        float bw=70;
        for (int i=0;i<6;i++)
            buttons.push_back(mk(cx-3*bw-15+i*(bw+5), y, bw, 36, tcs[i], 110+i,
                                 tc.baseMs==bases[i] && tc.incMs==incs[i]));
        y+=60;
        buttons.push_back(mk(cx-w/2,y,w,40,"Quit",120));
        break; }
    case Screen::GAME: {
        float px = WH - 40 + 60; (void)px;
        float rx = WH + 20;            // panel starts right of board
        float bw = (WW-rx-20-8)/2;
        float y = WH-190;
        buttons.push_back(mk(rx, y, bw, 34, "New Game", 201));
        buttons.push_back(mk(rx+bw+8, y, bw, 34, "Flip (F)", 202)); y+=42;
        buttons.push_back(mk(rx, y, bw, 34, "Takeback", 203));
        buttons.push_back(mk(rx+bw+8, y, bw, 34, "Resign", 204)); y+=42;
        buttons.push_back(mk(rx, y, bw, 34, "Copy FEN", 205));
        buttons.push_back(mk(rx+bw+8, y, bw, 34, "Paste FEN", 206)); y+=42;
        buttons.push_back(mk(rx, y, bw, 34, "Save PGN", 207));
        buttons.push_back(mk(rx+bw+8, y, bw, 34, analysing? "Analysis: ON":"Analysis: OFF", 212, analysing));
        // nav
        float ny = 20;
        float nw = 40;
        buttons.push_back(mk(rx, ny, nw, 28, "|<", 208));
        buttons.push_back(mk(rx+nw+4, ny, nw, 28, "<", 209));
        buttons.push_back(mk(rx+2*(nw+4), ny, nw, 28, ">", 210));
        buttons.push_back(mk(rx+3*(nw+4), ny, nw, 28, ">|", 211));
        buttons.push_back(mk(WW-90, ny, 70, 28, "Menu", 200));
        // promotion popup handled in render/event directly (ids 250..253)
        if (pendingPromotion){
            float side = WH-40, sq=side/8;
            float pxp = 20+side/2-2*sq, pyp = 20+side/2-sq/2;
            int promos[4] = {9,5,4,3};
            const char* n[4]={"Q","R","B","N"};
            for (int i=0;i<4;i++)
                buttons.push_back(mk(pxp+i*sq, pyp, sq, sq, n[i], 250+i));
        }
        break; }
    case Screen::PICK_ENGINE: {
        text("", 0,0,1); // no-op
        float w=WW-240, y=110;
        for (size_t i=0;i<knownEngines.size() && i<12;i++){
            buttons.push_back(mk(120, y, w, 38, knownEngines[i].name + "   (" + knownEngines[i].path + ")",
                                 300+(int)i));
            y+=46;
        }
        buttons.push_back(mk(120, WH-130, w-160, 38, enginePathInput.empty()?
                             "[click to type engine path]" : enginePathInput, 380, typingPath));
        buttons.push_back(mk(WW-260, WH-130, 140, 38, "Add path", 381));
        buttons.push_back(mk(120, WH-80, 140, 38, "Back", 399));
        break; }
    case Screen::MATCH_SETUP: {
        float w=WW-240, y=110;
        for (size_t i=0;i<knownEngines.size() && i<10;i++){
            bool sel = std::find(matchSelection.begin(),matchSelection.end(),(int)i)!=matchSelection.end();
            buttons.push_back(mk(120, y, w, 36, knownEngines[i].name + "   (" + knownEngines[i].path + ")",
                                 400+(int)i, sel));
            y+=42;
        }
        y+=10;
        buttons.push_back(mk(120, y, 40, 36, "-", 450));
        buttons.push_back(mk(230, y, 40, 36, "+", 451));
        const char* tcs[] = {"1+0","3+2","5+0","10+0","15+10","30+0"};
        int bases[] = {60000,180000,300000,600000,900000,1800000};
        int incs[]  = {0,2000,0,0,10000,0};
        for (int i=0;i<6;i++)
            buttons.push_back(mk(300+i*66, y, 60, 36, tcs[i], 110+i,
                                 tc.baseMs==bases[i]&&tc.incMs==incs[i]));
        y+=54;
        buttons.push_back(mk(120, y, 220, 42, "Start", 460));
        buttons.push_back(mk(360, y, 140, 42, "Back", 499));
        break; }
    case Screen::MATCH_VIEW: {
        buttons.push_back(mk(WW-170, 20, 150, 32, runner.running()? "Abort" : "Back", 500));
        buttons.push_back(mk(WW-170, 60, 150, 32, "Save PGNs", 501));
        break; }
    case Screen::TESTS: {
        float w=500, y=150;
        buttons.push_back(mk(cx-w/2, y, w, 46, "1. Self-test GUI move generator (perft)", 600)); y+=58;
        buttons.push_back(mk(cx-w/2, y, w, 46, "2. Test YOUR engine's move generator (go perft)", 601)); y+=58;
        buttons.push_back(mk(cx-w/2, y, w, 46, "3. Accuracy test vs reference engine", 602)); y+=58;
        buttons.push_back(mk(cx-w/2, y+20, 160, 40, "Back", 699));
        break; }
    case Screen::TEST_RUN: {
        buttons.push_back(mk(WW-170, 20, 150, 32, testRunning? "Abort" : "Back", 700));
        break; }
    default: break;
    }
}

// ---------------------------------------------------------------- game logic
void App::newGame(const Position& p){
    stopOpponent();
    game.reset(p);
    selectedSq=-1; dragFromSq=-1; dragging=false;
    pendingPromotion.reset(); viewPly=-1;
    wClockMs = tc.baseMs; bClockMs = tc.baseMs;
    clocksRunning = (mode != GameMode::ANALYSIS);
    tickClock.restart();
    anaFenSent.clear();
}
void App::commitMove(Move m){
    if (game.tryMove(m)){
        // clock increments
        if (clocksRunning){
            if (!game.pos.whiteToMove) wClockMs += tc.incMs; else bClockMs += tc.incMs;
        }
        selectedSq=-1; legalForSelected.clear(); viewPly=-1;
        if (game.result != GameResult::ONGOING) clocksRunning=false;
    }
}
void App::trySquareAction(int sq){
    if (sq<0 || game.result!=GameResult::ONGOING || viewPly>=0) return;
    bool humanTurn = (mode!=GameMode::HUMAN_VS_ENGINE) || (game.pos.whiteToMove==humanIsWhite);
    if (!humanTurn || pendingPromotion) return;
    // if a piece is selected and sq is a legal destination -> move
    for (const Move& m : legalForSelected){
        if (m.to == sq){
            if (m.promo){ Move base=m; base.promo=0; pendingPromotion=base; layout(); return; }
            commitMove(m); return;
        }
    }
    // otherwise select own piece
    int v = game.pos.board[sq];
    if (v && (v>0)==game.pos.whiteToMove){
        selectedSq = sq;
        std::vector<Move> all; game.pos.generateLegal(all);
        legalForSelected.clear();
        for (auto& m : all) if (m.from==sq) legalForSelected.push_back(m);
    } else { selectedSq=-1; legalForSelected.clear(); }
}

void App::stopOpponent(){
    if (oppThread.joinable()){
        if (oppEngine) oppEngine->sendRaw("stop");
        oppThread.join();
    }
    oppThinking=false;
    { std::lock_guard<std::mutex> lk(oppMx); oppMoveReady.clear(); }
}
void App::startEngineThinkIfNeeded(){
    if (mode!=GameMode::HUMAN_VS_ENGINE || !oppEngine || !oppEngine->alive()) return;
    if (game.result!=GameResult::ONGOING || pendingPromotion) return;
    if (game.pos.whiteToMove == humanIsWhite) return;
    if (oppThinking) return;
    oppThinking = true;
    if (oppThread.joinable()) oppThread.join();
    std::string startFen = game.start.toFEN();
    std::vector<std::string> uciMoves;
    { Position p=game.start; for (auto& m : game.moves){ uciMoves.push_back(p.moveToUCI(m)); p.makeMove(m);} }
    long long wc=wClockMs, bc=bClockMs; int inc=tc.incMs;
    oppThread = std::thread([this,startFen,uciMoves,wc,bc,inc](){
        oppEngine->setPosition(startFen, uciMoves);
        std::string bm = oppEngine->goClock((int)wc,(int)bc,inc,inc);
        std::lock_guard<std::mutex> lk(oppMx);
        oppMoveReady = bm.empty()? "(none)" : bm;
        oppThinking = false;
    });
}
void App::applyEngineMoveIfReady(){
    std::string bm;
    { std::lock_guard<std::mutex> lk(oppMx); bm=oppMoveReady; oppMoveReady.clear(); }
    if (bm.empty()) return;
    Move m;
    if (bm!="(none)" && game.pos.uciToMove(bm, m)) commitMove(m);
    else if (game.result==GameResult::ONGOING){
        game.result = humanIsWhite? GameResult::WHITE_WINS : GameResult::BLACK_WINS;
        game.reason = ResultReason::ILLEGAL_MOVE;
        showToast("Engine returned invalid move '"+bm+"'");
        clocksRunning=false;
    }
}

void App::ensureAnalysisFollows(){
    if (!analysing || !anaEngine || !anaEngine->alive()) return;
    // analyse the position currently on screen (live or browsed)
    Position p = game.pos;
    if (viewPly>=0){ p=game.start; for (int i=0;i<viewPly;i++) p.makeMove(game.moves[i]); }
    std::string fen = p.toFEN();
    if (fen != anaFenSent){
        anaEngine->stopAndWait(1000);
        anaEngine->setPosition(fen, {});
        anaEngine->goInfinite();
        anaFenSent = fen;
    }
    anaEngine->pump();
}
void App::stopAnalysis(){
    if (anaEngine && anaEngine->alive()){ anaEngine->stopAndWait(1000); }
    analysing = false; anaFenSent.clear();
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
    Position p = game.pos;
    if (viewPly>=0){ p=game.start; for (int i=0;i<viewPly;i++) p.makeMove(game.moves[i]); }
    sf::Clipboard::setString(p.toFEN());
    showToast("FEN copied to clipboard");
}
void App::pasteFEN(){
    std::string fen = sf::Clipboard::getString();
    bool ok=false; Position p = Position::fromFEN(fen,&ok);
    if (ok){ newGame(p); showToast("Position set from FEN"); }
    else showToast("Clipboard does not contain a valid FEN");
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

// ---------------------------------------------------------------- tests
void App::pushTestLine(const std::string& s){
    std::lock_guard<std::mutex> lk(testMx);
    testLines.push_back(s);
}
void App::startInternalSelfTest(){
    testAbort=false; testRunning=true;
    { std::lock_guard<std::mutex> lk(testMx); testLines.clear(); }
    testTitle = "GUI move generator self-test (verified perft suite)";
    if (testThread.joinable()) testThread.join();
    testThread = std::thread([this](){
        auto suite = loadEpdSuite("Resources/perftsuite.epd");
        pushTestLine(suite.empty()
            ? "Using built-in suite (put perftsuite.epd in Resources/ for the full ~126 positions)"
            : "Loaded Resources/perftsuite.epd: " + std::to_string(suite.size()) + " positions");
        if (suite.empty()) suite = builtinPerftSuite();
        bool all = runInternalPerftSelfTest(suite, 5,
            [this](int i,int n,bool pass){
                pushTestLine("["+std::to_string(i)+"/"+std::to_string(n)+"] "+(pass?"OK":"FAIL"));
            }, testAbort);
        pushTestLine(all? ">>> ALL PASSED — internal generator verified." : ">>> FAILURES FOUND.");
        testRunning=false;
    });
}
void App::startPerftTest(const std::string& path){
    testAbort=false; testRunning=true;
    { std::lock_guard<std::mutex> lk(testMx); testLines.clear(); }
    testTitle = "Engine move-generator test: " + fs::path(path).filename().string();
    if (testThread.joinable()) testThread.join();
    testThread = std::thread([this,path](){
        UciEngine e;
        if (!e.start(path)){ pushTestLine("Cannot start engine: "+e.lastError()); testRunning=false; return; }
        pushTestLine("Engine: "+e.name());
        auto suite = loadEpdSuite("Resources/perftsuite.epd");
        pushTestLine(suite.empty()
            ? "Using built-in verified suite (add Resources/perftsuite.epd for ~126 positions)"
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
            pushTestLine(">>> Engine does not answer 'go perft N' with 'Nodes searched: X'. "
                         "Add that command to your engine to use this test.");
        else
            pushTestLine(fails? ">>> "+std::to_string(fails)+" FAILURES — your move generator has bugs in those positions."
                              : ">>> ALL PASSED — move generator agrees with verified node counts.");
        e.quit();
        testRunning=false;
    });
}
void App::startAccuracyTest(const std::string& testPath, const std::string& refPath){
    testAbort=false; testRunning=true;
    { std::lock_guard<std::mutex> lk(testMx); testLines.clear(); }
    testTitle = "Accuracy: " + fs::path(testPath).filename().string()
              + " vs reference " + fs::path(refPath).filename().string();
    if (testThread.joinable()) testThread.join();
    testThread = std::thread([this,testPath,refPath](){
        UciEngine te, re;
        if (!te.start(testPath)){ pushTestLine("Cannot start test engine: "+te.lastError()); testRunning=false; return; }
        if (!re.start(refPath)){ pushTestLine("Cannot start reference engine: "+re.lastError()); testRunning=false; return; }
        pushTestLine("Test engine: "+te.name()+"   Reference: "+re.name());
        pushTestLine("Each position: your engine 1000ms/move, reference 1500ms eval.");
        auto rep = runAccuracyTest(te, re, defaultAccuracyPositions(), 1000, 1500,
            [this](int i,int n,const AccuracyMoveReport& m){
                pushTestLine("["+std::to_string(i)+"/"+std::to_string(n)+"] played "+m.san
                    +"  cp loss "+std::to_string(m.cpLoss)
                    +(m.cpLoss==0? "  (= best move "+m.refBest+")":"  (best was "+m.refBest+")"));
            }, testAbort);
        char b[160];
        snprintf(b,160,">>> Avg centipawn loss: %.1f   Accuracy: %.1f%%   (blunders %d, mistakes %d, inaccuracies %d)",
                 rep.avgCpLoss, rep.accuracyPct, rep.blunders, rep.mistakes, rep.inaccuracies);
        pushTestLine(b);
        te.quit(); re.quit();
        testRunning=false;
    });
}

// ---------------------------------------------------------------- events
void App::handleEvent(const sf::Event& e){
    if (e.type == sf::Event::Closed){ window.close(); return; }
    if (e.type == sf::Event::Resized){
        WW = (float)e.size.width; WH = (float)e.size.height;
        window.setView(sf::View(sf::FloatRect(0,0,WW,WH)));
        layout(); return;
    }
    if (e.type == sf::Event::KeyPressed){
        if (typingPath && screen==Screen::PICK_ENGINE){
            if (e.key.code==sf::Keyboard::Enter){ onButton(381); return; }
            if (e.key.code==sf::Keyboard::Escape){ typingPath=false; layout(); return; }
        } else {
            if (e.key.code==sf::Keyboard::Escape){
                if (screen==Screen::GAME || screen==Screen::TESTS) { screen=Screen::MENU; layout(); }
                return;
            }
            if (screen==Screen::GAME){
                if (e.key.code==sf::Keyboard::F){ boardFlipped=!boardFlipped; }
                if (e.key.code==sf::Keyboard::Left)  onButton(209);
                if (e.key.code==sf::Keyboard::Right) onButton(210);
                if (e.key.code==sf::Keyboard::Up)    onButton(208);
                if (e.key.code==sf::Keyboard::Down)  onButton(211);
            }
        }
    }
    if (e.type == sf::Event::TextEntered && typingPath && screen==Screen::PICK_ENGINE){
        if (e.text.unicode==8){ if(!enginePathInput.empty()) enginePathInput.pop_back(); }
        else if (e.text.unicode>=32 && e.text.unicode<127) enginePathInput += (char)e.text.unicode;
        layout(); return;
    }
    sf::Vector2f m = window.mapPixelToCoords(sf::Mouse::getPosition(window));
    if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button==sf::Mouse::Left){
        for (auto& b : buttons)
            if (b.enabled && hit(b.rect, m)){ onButton(b.id); return; }
        if (screen==Screen::GAME && !pendingPromotion){
            float side = WH-40;
            int sq = boardSquareAt(m, 20, 20, side, boardFlipped);
            if (sq>=0){
                bool humanTurn = (mode!=GameMode::HUMAN_VS_ENGINE) || (game.pos.whiteToMove==humanIsWhite);
                int v = game.pos.board[sq];
                trySquareAction(sq);
                if (humanTurn && viewPly<0 && v && (v>0)==game.pos.whiteToMove && selectedSq==sq){
                    dragging=true; dragFromSq=sq; dragPos=m;
                }
            }
        }
    }
    if (e.type == sf::Event::MouseMoved && dragging) dragPos = m;
    if (e.type == sf::Event::MouseButtonReleased && e.mouseButton.button==sf::Mouse::Left && dragging){
        dragging=false;
        float side = WH-40;
        int sq = boardSquareAt(m, 20, 20, side, boardFlipped);
        int from = dragFromSq; dragFromSq=-1;
        if (sq>=0 && sq!=from) trySquareAction(sq);
    }
}

void App::onButton(int id){
    // menu
    if (id>=110 && id<=115){
        int bases[] = {60000,180000,300000,600000,900000,1800000};
        int incs[]  = {0,2000,0,0,10000,0};
        tc.baseMs=bases[id-110]; tc.incMs=incs[id-110]; layout(); return;
    }
    switch (id){
    case 100: mode=GameMode::HUMAN_VS_HUMAN; stopOpponent(); newGame(Position::startpos());
              screen=Screen::GAME; layout(); break;
    case 101: case 102:
        humanIsWhite = (id==101); boardFlipped = !humanIsWhite;
        mode=GameMode::HUMAN_VS_ENGINE; pickPurpose=1;
        screen=Screen::PICK_ENGINE; scanEngines(); layout(); break;
    case 103: matchSelection.clear(); screen=Screen::MATCH_SETUP; scanEngines(); layout(); break;
    case 104: mode=GameMode::ANALYSIS; pickPurpose=2;
              screen=Screen::PICK_ENGINE; scanEngines(); layout(); break;
    case 105: screen=Screen::TESTS; layout(); break;
    case 120: window.close(); break;
    // game
    case 200: stopOpponent(); stopAnalysis(); screen=Screen::MENU; layout(); break;
    case 201: newGame(Position::startpos()); layout(); break;
    case 202: boardFlipped=!boardFlipped; break;
    case 203: stopOpponent();
              game.undo();
              if (mode==GameMode::HUMAN_VS_ENGINE && game.pos.whiteToMove!=humanIsWhite) game.undo();
              viewPly=-1; selectedSq=-1; legalForSelected.clear(); pendingPromotion.reset();
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
        else if (anaEngine && anaEngine->alive()){ analysing=true; anaFenSent.clear(); layout(); }
        else { pickPurpose=2; screen=Screen::PICK_ENGINE; scanEngines(); layout(); }
        break;
    case 250: case 251: case 252: case 253:
        if (pendingPromotion){
            Move m = *pendingPromotion; pendingPromotion.reset();
            int pr[4] = {9,5,4,3};
            m.promo = (int8_t)pr[id-250];
            // find the actual legal move (captured flag etc.)
            Move real;
            if (game.pos.uciToMove(game.pos.moveToUCI(m), real)) commitMove(real);
            layout();
        }
        break;
    // engine picking
    case 380: typingPath=true; layout(); break;
    case 381: {
        typingPath=false;
        std::error_code ec;
        if (!enginePathInput.empty() && fs::exists(enginePathInput, ec)){
            knownEngines.push_back({enginePathInput, fs::path(enginePathInput).filename().string()});
            enginePathInput.clear();
            showToast("Engine added");
        } else showToast("File not found: "+enginePathInput);
        layout(); break; }
    case 399: screen = (pickPurpose>=4)? Screen::TESTS : Screen::MENU; layout(); break;
    // match setup
    case 450: matchGamesPerPair=std::max(1,matchGamesPerPair-1); break;
    case 451: matchGamesPerPair=std::min(50,matchGamesPerPair+1); break;
    case 460: {
        if (matchSelection.size()<2){ showToast("Select at least 2 engines"); break; }
        std::vector<EngineEntry> es;
        for (int i : matchSelection) es.push_back(knownEngines[i]);
        runner.startTournament(es, tc, matchGamesPerPair);
        screen=Screen::MATCH_VIEW; layout(); break; }
    case 499: screen=Screen::MENU; layout(); break;
    case 500: if (runner.running()) runner.abort();
              else { screen=Screen::MENU; }
              layout(); break;
    case 501: saveTournamentPGNs(); break;
    // tests
    case 600: startInternalSelfTest(); screen=Screen::TEST_RUN; layout(); break;
    case 601: pickPurpose=4; screen=Screen::PICK_ENGINE; scanEngines(); layout(); break;
    case 602: pickPurpose=5; screen=Screen::PICK_ENGINE; scanEngines(); layout(); break;
    case 699: screen=Screen::MENU; layout(); break;
    case 700: if (testRunning) testAbort=true;
              else { screen=Screen::TESTS; }
              layout(); break;
    default: break;
    }
    // engine list clicks
    if (id>=300 && id<400 && id-300 < (int)knownEngines.size()){
        const EngineEntry& ee = knownEngines[id-300];
        if (pickPurpose==1){
            stopOpponent();
            oppEngine = std::make_unique<UciEngine>();
            if (!oppEngine->start(ee.path)){ showToast("Failed: "+oppEngine->lastError()); return; }
            oppEngine->newGame();
            newGame(Position::startpos());
            game.whiteName = humanIsWhite? "Human" : oppEngine->name();
            game.blackName = humanIsWhite? oppEngine->name() : "Human";
            screen=Screen::GAME; layout();
        } else if (pickPurpose==2){
            stopAnalysis();
            anaEngine = std::make_unique<UciEngine>();
            if (!anaEngine->start(ee.path)){ showToast("Failed: "+anaEngine->lastError()); return; }
            anaEngine->newGame();
            analysing=true; anaFenSent.clear();
            if (mode==GameMode::ANALYSIS){ newGame(Position::startpos()); clocksRunning=false; }
            screen=Screen::GAME; layout();
        } else if (pickPurpose==4){
            startPerftTest(ee.path);
            screen=Screen::TEST_RUN; layout();
        } else if (pickPurpose==5){
            accTestEnginePath = ee.path; pickPurpose=6;
            showToast("Now pick the REFERENCE engine (e.g. Stockfish)");
            layout();
        } else if (pickPurpose==6){
            startAccuracyTest(accTestEnginePath, ee.path);
            screen=Screen::TEST_RUN; layout();
        }
    }
    if (id>=400 && id<450 && id-400 < (int)knownEngines.size()){
        int i=id-400;
        auto it = std::find(matchSelection.begin(),matchSelection.end(),i);
        if (it==matchSelection.end()) matchSelection.push_back(i); else matchSelection.erase(it);
        layout();
    }
}

// ---------------------------------------------------------------- update
void App::update(){
    if (screen==Screen::GAME){
        // clocks
        long long dt = tickClock.restart().asMilliseconds();
        if (clocksRunning && game.result==GameResult::ONGOING && !game.moves.empty()){
            if (game.pos.whiteToMove) wClockMs -= dt; else bClockMs -= dt;
            if (wClockMs<=0){ wClockMs=0; game.result=GameResult::BLACK_WINS; game.reason=ResultReason::TIMEOUT; clocksRunning=false; stopOpponent(); }
            if (bClockMs<=0){ bClockMs=0; game.result=GameResult::WHITE_WINS; game.reason=ResultReason::TIMEOUT; clocksRunning=false; stopOpponent(); }
        }
        applyEngineMoveIfReady();
        startEngineThinkIfNeeded();
        ensureAnalysisFollows();
    } else tickClock.restart();
    if (screen==Screen::MATCH_VIEW || screen==Screen::TEST_RUN){
        static sf::Clock relayout; 
        if (relayout.getElapsedTime().asMilliseconds()>300){ layout(); relayout.restart(); }
    }
}

// ---------------------------------------------------------------- render
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
        text("Chess GUI for UCI", WW/2, 70, 44, COL_ACCENT, true);
        text("Play, run engine matches & tournaments, analyse, and test your own UCI engine",
             WW/2, 120, 16, sf::Color(170,170,180), true);
        text("Time control for games & matches:", WW/2, 542, 15, sf::Color(170,170,180), true);
        break; }
    case Screen::GAME: {
        float side = WH-40;
        Position shown = game.pos;
        int lastFrom=-1, lastTo=-1;
        if (viewPly>=0){
            shown = game.start;
            for (int i=0;i<viewPly;i++) shown.makeMove(game.moves[i]);
            if (viewPly>0){ lastFrom=game.moves[viewPly-1].from; lastTo=game.moves[viewPly-1].to; }
        } else if (!game.moves.empty()){
            lastFrom=game.moves.back().from; lastTo=game.moves.back().to;
        }
        drawBoard(20,20,side, shown,
                  viewPly<0? selectedSq : -1,
                  viewPly<0? &legalForSelected : nullptr,
                  lastFrom,lastTo, boardFlipped,
                  dragging? dragFromSq : -1, dragPos);
        float rx = WH+20;
        // clocks + names (top = side away from viewer)
        bool whiteAtBottom = !boardFlipped;
        std::string topName = whiteAtBottom? game.blackName : game.whiteName;
        std::string botName = whiteAtBottom? game.whiteName : game.blackName;
        long long topClk = whiteAtBottom? bClockMs : wClockMs;
        long long botClk = whiteAtBottom? wClockMs : bClockMs;
        text(topName, rx, 64, 17);
        text(clockStr(topClk), WW-110, 60, 22,
             topClk<30000? sf::Color(255,120,120):sf::Color::White);
        text(botName, rx, WH-238, 17);
        text(clockStr(botClk), WW-110, WH-242, 22,
             botClk<30000? sf::Color(255,120,120):sf::Color::White);
        // move list
        drawMoveList(rx, 95, WW-rx-52, WH-95-250, game.sans,
                     game.start.fullmoveNumber, game.start.whiteToMove);
        // eval bar (analysis)
        if (analysing && anaEngine){
            EngineInfo i = anaEngine->lastInfo();
            int cpW = shown.whiteToMove? i.scoreCp : -i.scoreCp;
            int mateW = shown.whiteToMove? i.mateIn : -i.mateIn;
            drawEvalBar(WW-46, 95, WH-95-250-20, cpW, i.isMate, mateW);
            std::string pv = i.pv.substr(0, 60);
            text("d"+std::to_string(i.depth)+"  "+pv, rx, WH-215, 13, sf::Color(150,200,150));
        }
        // status
        std::string st;
        if (game.result!=GameResult::ONGOING)
            st = game.resultString()+"  ("+reasonStr(game.reason)+")";
        else if (viewPly>=0)
            st = "Viewing move "+std::to_string(viewPly)+"/"+std::to_string(game.moves.size())+"  (>| to return)";
        else if (mode==GameMode::HUMAN_VS_ENGINE && game.pos.whiteToMove!=humanIsWhite)
            st = "Engine is thinking...";
        else st = game.pos.whiteToMove? "White to move" : "Black to move";
        text(st, rx, WH-206+14, 15, COL_ACCENT);
        if (pendingPromotion){
            sf::RectangleShape dim(sf::Vector2f(side,side)); dim.setPosition(20,20);
            dim.setFillColor(sf::Color(0,0,0,120)); window.draw(dim);
            text("Promote to:", 20+side/2, 20+side/2 - side/8, 22, sf::Color::White, true);
        }
        break; }
    case Screen::PICK_ENGINE: {
        const char* titles[] = {"","Choose your OPPONENT engine","Choose the ANALYSIS engine","",
                                "Choose the engine to PERFT-TEST",
                                "Choose YOUR engine to accuracy-test",
                                "Choose the REFERENCE engine (e.g. Stockfish)"};
        text(titles[std::min(pickPurpose,6)], WW/2, 50, 26, COL_ACCENT, true);
        text("Engines are auto-discovered from Resources/engines/ and common Stockfish paths.",
             WW/2, 84, 14, sf::Color(160,160,170), true);
        if (knownEngines.empty())
            text("No engines found. Put UCI engine executables in Resources/engines/ or type a path below.",
                 WW/2, 200, 16, sf::Color(230,180,120), true);
        break; }
    case Screen::MATCH_SETUP: {
        text("Engine vs Engine / Tournament", WW/2, 50, 26, COL_ACCENT, true);
        text("Click engines to select them (2 = match, 3+ = round-robin tournament).",
             WW/2, 84, 14, sf::Color(160,160,170), true);
        float y = 110 + std::min(knownEngines.size(),(size_t)10)*42 + 18;
        text("Games per pairing: "+std::to_string(matchGamesPerPair), 170, y, 16);
        break; }
    case Screen::MATCH_VIEW: {
        auto s = runner.snapshot();
        float side = std::min(WH-120.f, WW*0.5f);
        drawBoard(20, 60, side, s.pos, -1, nullptr, -1, -1, false, -1, {});
        text(s.blackName + "   " + clockStr(s.bClockMs), 20, 24, 18);
        text(s.whiteName + "   " + clockStr(s.wClockMs), 20, 66+side, 18);
        float rx = side+60;
        text("Game "+std::to_string(s.gameIndex)+"/"+std::to_string(s.totalGames), rx, 60, 20, COL_ACCENT);
        drawEvalBar(rx, 95, side-140, s.scoreCpWhitePOV, s.scoreIsMate, s.mateIn);
        // standings
        float ty = 95;
        float tx = rx+60;
        text("Standings", tx, ty, 18, COL_ACCENT); ty+=30;
        for (auto& st : s.standings){
            char b[160];
            snprintf(b,160,"%-24s  +%d =%d -%d   %.1f pts", st.name.substr(0,24).c_str(),
                     st.wins, st.draws, st.losses, st.points());
            text(b, tx, ty, 15); ty+=24;
        }
        ty+=14;
        for (auto& l : s.log){ text(l, tx, ty, 13, sf::Color(170,170,180)); ty+=19; }
        if (s.result!=GameResult::ONGOING && !s.finished)
            text("Last result: "+reasonStr(s.reason), tx, ty+8, 15, COL_ACCENT);
        if (s.finished) text("Tournament finished.", tx, ty+8, 17, sf::Color(140,230,140));
        break; }
    case Screen::TESTS: {
        text("Engine Testing Tools", WW/2, 60, 30, COL_ACCENT, true);
        text("Tools for chess-engine developers. Test 2 requires your engine to support 'go perft N'\n"
             "(printing 'Nodes searched: X', like Stockfish). Expected node counts come from the\n"
             "classic perft suite and are cross-verified by this GUI's own tested generator.",
             WW/2, 110, 14, sf::Color(170,170,180), true);
        break; }
    case Screen::TEST_RUN: {
        text(testTitle, 30, 24, 20, COL_ACCENT);
        std::vector<std::string> lines;
        { std::lock_guard<std::mutex> lk(testMx); lines = testLines; }
        int maxRows = (int)((WH-90)/19);
        int first = std::max(0, (int)lines.size()-maxRows);
        float y=70;
        for (int i=first;i<(int)lines.size();i++){
            sf::Color c = sf::Color(210,210,215);
            if (lines[i].find("FAIL")!=std::string::npos) c = sf::Color(255,130,130);
            else if (lines[i].find(">>>")!=std::string::npos) c = sf::Color(140,230,140);
            text(lines[i], 30, y, 14, c); y+=19;
        }
        if (testRunning) text("running...", WW-170, 60, 14, sf::Color(230,200,120));
        break; }
    default: break;
    }
    drawButtons();
    if (!toast.empty()){
        if (toastClock.getElapsedTime().asSeconds()>3) toast.clear();
        else {
            sf::RectangleShape r(sf::Vector2f(std::min(WW-40, 620.f), 34));
            r.setPosition(WW/2 - r.getSize().x/2, WH-44);
            r.setFillColor(sf::Color(20,20,25,230));
            r.setOutlineThickness(1); r.setOutlineColor(COL_ACCENT);
            window.draw(r);
            text(toast, WW/2, WH-27, 14, sf::Color::White, true);
        }
    }
    window.display();
}
