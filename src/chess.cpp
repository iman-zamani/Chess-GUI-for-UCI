/* Chess-GUI-for-UCI — core chess logic. GPL-2.0 */
#include "chess.hpp"
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <algorithm>

static const int KNIGHT_D[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
static const int KING_D[8][2]   = {{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
static const int BISHOP_D[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
static const int ROOK_D[4][2]   = {{1,0},{-1,0},{0,1},{0,-1}};

std::string squareName(int sq){
    if (sq < 0 || sq > 63) return "-";
    std::string s; s += char('a' + sq%8); s += char('0' + (8 - sq/8)); return s;
}
int nameToSquare(const std::string& n){
    if (n.size()!=2 || n[0]<'a'||n[0]>'h'||n[1]<'1'||n[1]>'8') return -1;
    return (8 - (n[1]-'0'))*8 + (n[0]-'a');
}
static char pieceChar(int p){
    int a = std::abs(p); char c='?';
    switch(a){case 1:c='p';break;case 3:c='n';break;case 4:c='b';break;
              case 5:c='r';break;case 9:c='q';break;case 20:c='k';break;}
    return p>0 ? (char)std::toupper(c) : c;
}
static int charPiece(char c){
    int v=0;
    switch(std::tolower(c)){case 'p':v=1;break;case 'n':v=3;break;case 'b':v=4;break;
        case 'r':v=5;break;case 'q':v=9;break;case 'k':v=20;break;default:return 0;}
    return std::isupper(c)? v : -v;
}

Position Position::startpos(){
    return fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

Position Position::fromFEN(const std::string& fen, bool* ok){
    Position p; if (ok) *ok = false;
    std::istringstream ss(fen);
    std::string placement, stm, castling, ep; int hm=0, fm=1;
    if (!(ss >> placement >> stm)) return p;
    if (!(ss >> castling)) castling = "-";
    if (!(ss >> ep)) ep = "-";
    ss >> hm; ss >> fm;
    int sq = 0;
    for (char c : placement){
        if (c == '/') continue;
        if (std::isdigit((unsigned char)c)) { sq += c-'0'; continue; }
        int v = charPiece(c);
        if (!v || sq > 63) return p;
        p.board[sq++] = v;
    }
    if (sq != 64) return p;
    p.whiteToMove = (stm == "w");
    p.castleWK = castling.find('K')!=std::string::npos;
    p.castleWQ = castling.find('Q')!=std::string::npos;
    p.castleBK = castling.find('k')!=std::string::npos;
    p.castleBQ = castling.find('q')!=std::string::npos;
    p.epSquare = nameToSquare(ep);
    p.halfmoveClock = hm; p.fullmoveNumber = fm>0?fm:1;
    // sanity: both kings present
    if (p.kingSquare(true)<0 || p.kingSquare(false)<0) return p;
    if (ok) *ok = true;
    return p;
}

std::string Position::toFEN() const{
    std::string s;
    for (int y=0;y<8;y++){
        int empty=0;
        for (int x=0;x<8;x++){
            int v = board[y*8+x];
            if (!v){empty++;continue;}
            if (empty){s+=std::to_string(empty);empty=0;}
            s += pieceChar(v);
        }
        if (empty) s+=std::to_string(empty);
        if (y<7) s+='/';
    }
    s += whiteToMove? " w " : " b ";
    std::string c;
    if (castleWK)c+='K'; if(castleWQ)c+='Q'; if(castleBK)c+='k'; if(castleBQ)c+='q';
    s += c.empty()? "-" : c;
    s += " " + squareName(epSquare);
    s += " " + std::to_string(halfmoveClock) + " " + std::to_string(fullmoveNumber);
    return s;
}
std::string Position::repetitionKey() const{
    std::string f = toFEN();
    // strip the two counters
    size_t p = f.rfind(' '); f = f.substr(0,p); p = f.rfind(' '); return f.substr(0,p);
}

int Position::kingSquare(bool white) const{
    int k = white? WHITE_KING : BLACK_KING;
    for (int i=0;i<64;i++) if (board[i]==k) return i;
    return -1;
}

bool Position::squareAttacked(int sq, bool byWhite) const{
    int x = sq%8, y = sq/8;
    // pawns: white pawns attack "upwards" (towards y-1 from their square => they sit at y+1)
    int py = byWhite ? y+1 : y-1;
    if (py>=0 && py<8){
        int pv = byWhite? WHITE_PAWN : BLACK_PAWN;
        if (x>0 && board[py*8+x-1]==pv) return true;
        if (x<7 && board[py*8+x+1]==pv) return true;
    }
    int nv = byWhite? WHITE_KNIGHT : BLACK_KNIGHT;
    for (auto& d : KNIGHT_D){
        int nx=x+d[0], ny=y+d[1];
        if (nx>=0&&nx<8&&ny>=0&&ny<8 && board[ny*8+nx]==nv) return true;
    }
    int kv = byWhite? WHITE_KING : BLACK_KING;
    for (auto& d : KING_D){
        int nx=x+d[0], ny=y+d[1];
        if (nx>=0&&nx<8&&ny>=0&&ny<8 && board[ny*8+nx]==kv) return true;
    }
    int bv = byWhite? WHITE_BISHOP : BLACK_BISHOP;
    int rv = byWhite? WHITE_ROOK   : BLACK_ROOK;
    int qv = byWhite? WHITE_QUEEN  : BLACK_QUEEN;
    for (auto& d : BISHOP_D){
        int nx=x, ny=y;
        while (true){
            nx+=d[0]; ny+=d[1];
            if (nx<0||nx>7||ny<0||ny>7) break;
            int v = board[ny*8+nx];
            if (!v) continue;
            if (v==bv || v==qv) return true;
            break;
        }
    }
    for (auto& d : ROOK_D){
        int nx=x, ny=y;
        while (true){
            nx+=d[0]; ny+=d[1];
            if (nx<0||nx>7||ny<0||ny>7) break;
            int v = board[ny*8+nx];
            if (!v) continue;
            if (v==rv || v==qv) return true;
            break;
        }
    }
    return false;
}

void Position::generatePseudo(std::vector<Move>& out) const{
    bool w = whiteToMove;
    int sign = w? 1 : -1;
    for (int sq=0;sq<64;sq++){
        int v = board[sq];
        if (!v || (v>0)!=w) continue;
        int x=sq%8, y=sq/8, a=std::abs(v);
        auto push=[&](int to, int promo=0, bool ep=false, bool dbl=false){
            Move m; m.from=(int8_t)sq; m.to=(int8_t)to; m.promo=(int8_t)promo;
            m.isEnPassant=ep; m.isDoublePush=dbl;
            m.captured = ep ? (int8_t)(w?BLACK_PAWN:WHITE_PAWN) : (int8_t)board[to];
            out.push_back(m);
        };
        if (a==1){ // pawn
            int dy = w? -1 : 1;
            int fy = y+dy;
            if (fy>=0 && fy<8){
                bool promoRank = (fy==0 || fy==7);
                auto pawnPush=[&](int to,bool ep=false,bool dbl=false){
                    if (promoRank){ for(int pr : {9,5,4,3}) push(to,pr,ep,dbl); }
                    else push(to,0,ep,dbl);
                };
                if (!board[fy*8+x]){
                    pawnPush(fy*8+x);
                    int startRank = w? 6 : 1;
                    if (y==startRank && !board[(y+2*dy)*8+x])
                        push((y+2*dy)*8+x,0,false,true);
                }
                for (int dx : {-1,1}){
                    int nx=x+dx;
                    if (nx<0||nx>7) continue;
                    int to = fy*8+nx;
                    int tv = board[to];
                    if (tv && (tv>0)!=w) pawnPush(to);
                    else if (to==epSquare && !tv) pawnPush(to,true);
                }
            }
        } else if (a==3){
            for (auto& d:KNIGHT_D){
                int nx=x+d[0],ny=y+d[1];
                if (nx<0||nx>7||ny<0||ny>7) continue;
                int tv=board[ny*8+nx];
                if (!tv || (tv>0)!=w) push(ny*8+nx);
            }
        } else if (a==20){
            for (auto& d:KING_D){
                int nx=x+d[0],ny=y+d[1];
                if (nx<0||nx>7||ny<0||ny>7) continue;
                int tv=board[ny*8+nx];
                if (!tv || (tv>0)!=w) push(ny*8+nx);
            }
            // castling (king on original square implied by rights)
            int rank = w? 7 : 0;
            if (sq == rank*8+4 && !inCheck(w)){
                bool ks = w? castleWK : castleBK;
                bool qs = w? castleWQ : castleBQ;
                int rook = w? WHITE_ROOK : BLACK_ROOK;
                if (ks && board[rank*8+5]==0 && board[rank*8+6]==0 && board[rank*8+7]==rook
                    && !squareAttacked(rank*8+5,!w) && !squareAttacked(rank*8+6,!w)){
                    Move m; m.from=(int8_t)sq; m.to=(int8_t)(rank*8+6); m.isCastle=true; out.push_back(m);
                }
                if (qs && board[rank*8+3]==0 && board[rank*8+2]==0 && board[rank*8+1]==0 && board[rank*8+0]==rook
                    && !squareAttacked(rank*8+3,!w) && !squareAttacked(rank*8+2,!w)){
                    Move m; m.from=(int8_t)sq; m.to=(int8_t)(rank*8+2); m.isCastle=true; out.push_back(m);
                }
            }
        } else { // sliders
            const int (*dirs)[2]; int n;
            if (a==4){dirs=BISHOP_D;n=4;}
            else if (a==5){dirs=ROOK_D;n=4;}
            else {static const int QD[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};dirs=QD;n=8;}
            for (int i=0;i<n;i++){
                int nx=x,ny=y;
                while (true){
                    nx+=dirs[i][0]; ny+=dirs[i][1];
                    if (nx<0||nx>7||ny<0||ny>7) break;
                    int tv=board[ny*8+nx];
                    if (!tv){ push(ny*8+nx); continue; }
                    if ((tv>0)!=w) push(ny*8+nx);
                    break;
                }
            }
        }
        (void)sign;
    }
}

void Position::makeMove(const Move& m){
    bool w = whiteToMove;
    int v = board[m.from];
    // halfmove clock
    if (std::abs(v)==1 || board[m.to]!=0 || m.isEnPassant) halfmoveClock=0; else halfmoveClock++;
    // en passant capture
    if (m.isEnPassant){
        int capSq = w? m.to+8 : m.to-8;
        board[capSq]=0;
    }
    board[m.to] = m.promo? (w? m.promo : -m.promo) : v;
    board[m.from]=0;
    // castling rook
    if (m.isCastle){
        int rank = w?7:0;
        if (m.to == rank*8+6){ board[rank*8+5]=board[rank*8+7]; board[rank*8+7]=0; }
        else                 { board[rank*8+3]=board[rank*8+0]; board[rank*8+0]=0; }
    }
    // castling rights updates
    auto touch=[&](int sq){
        if (sq==60) {castleWK=false;castleWQ=false;}
        if (sq==4)  {castleBK=false;castleBQ=false;}
        if (sq==63) castleWK=false;
        if (sq==56) castleWQ=false;
        if (sq==7)  castleBK=false;
        if (sq==0)  castleBQ=false;
    };
    touch(m.from); touch(m.to);
    // ep square
    epSquare = m.isDoublePush ? (w? m.from-8 : m.from+8) : -1;
    if (!w) fullmoveNumber++;
    whiteToMove = !w;
}

void Position::generateLegal(std::vector<Move>& out) const{
    std::vector<Move> pseudo; pseudo.reserve(64);
    generatePseudo(pseudo);
    out.clear(); out.reserve(pseudo.size());
    for (const Move& m : pseudo){
        Position p = *this;
        p.makeMove(m);
        if (!p.inCheck(whiteToMove)) out.push_back(m);
    }
}

uint64_t Position::perft(int depth) const{
    if (depth<=0) return 1;
    std::vector<Move> moves; generateLegal(moves);
    if (depth==1) return moves.size();
    uint64_t n=0;
    for (const Move& m : moves){
        Position p=*this; p.makeMove(m);
        n += p.perft(depth-1);
    }
    return n;
}

std::string Position::moveToUCI(const Move& m) const{
    std::string s = squareName(m.from)+squareName(m.to);
    if (m.promo){
        switch(m.promo){case 9:s+='q';break;case 5:s+='r';break;case 4:s+='b';break;case 3:s+='n';break;}
    }
    return s;
}
bool Position::uciToMove(const std::string& uci, Move& out) const{
    std::vector<Move> legal; generateLegal(legal);
    for (const Move& m : legal)
        if (moveToUCI(m)==uci){ out=m; return true; }
    return false;
}

std::string Position::moveToSAN(const Move& m) const{
    int v = board[m.from], a=std::abs(v);
    std::string s;
    if (m.isCastle){ s = (m.to%8==6)? "O-O" : "O-O-O"; }
    else {
        if (a!=1) s += (char)std::toupper(pieceChar(a));
        // disambiguation
        if (a!=1){
            std::vector<Move> legal; generateLegal(legal);
            bool amb=false, sameFile=false, sameRank=false;
            for (const Move& o : legal){
                if (o.to==m.to && o.from!=m.from && std::abs(board[o.from])==a){
                    amb=true;
                    if (o.from%8==m.from%8) sameFile=true;
                    if (o.from/8==m.from/8) sameRank=true;
                }
            }
            if (amb){
                if (!sameFile) s += char('a'+m.from%8);
                else if (!sameRank) s += char('0'+(8-m.from/8));
                else s += squareName(m.from);
            }
        }
        bool cap = m.captured!=0 || m.isEnPassant;
        if (a==1 && cap) s += char('a'+m.from%8);
        if (cap) s += 'x';
        s += squareName(m.to);
        if (m.promo){ s += '='; s += (char)std::toupper(pieceChar(m.promo)); }
    }
    Position p=*this; p.makeMove(m);
    if (p.inCheck(p.whiteToMove)){
        std::vector<Move> reply; p.generateLegal(reply);
        s += reply.empty()? '#' : '+';
    }
    return s;
}
bool Position::sanToMove(const std::string& sanIn, Move& out) const{
    // strip annotations
    std::string san;
    for (char c : sanIn) if (c!='+'&&c!='#'&&c!='!'&&c!='?') san+=c;
    std::vector<Move> legal; generateLegal(legal);
    for (const Move& m : legal){
        std::string s = moveToSAN(m), t;
        for (char c : s) if (c!='+'&&c!='#') t+=c;
        if (t==san) { out=m; return true; }
    }
    return false;
}

// ---------------- Game ----------------
void Game::reset(const Position& p){
    start=p; pos=p; moves.clear(); sans.clear(); repKeys.clear();
    repKeys.push_back(p.repetitionKey());
    result=GameResult::ONGOING; reason=ResultReason::NONE;
}
bool Game::tryMove(const Move& m){
    if (result!=GameResult::ONGOING) return false;
    Move legal;
    if (!pos.uciToMove(pos.moveToUCI(m), legal)) return false;
    sans.push_back(pos.moveToSAN(legal));
    moves.push_back(legal);
    pos.makeMove(legal);
    repKeys.push_back(pos.repetitionKey());
    updateStatus();
    return true;
}
void Game::undo(){
    if (moves.empty()) return;
    moves.pop_back(); sans.pop_back(); repKeys.pop_back();
    pos = start;
    for (const Move& m : moves) pos.makeMove(m);
    result=GameResult::ONGOING; reason=ResultReason::NONE;
}
static bool insufficientMaterial(const Position& p){
    int minors=0;
    for (int i=0;i<64;i++){
        int a=std::abs(p.board[i]);
        if (a==0||a==20) continue;
        if (a==1||a==5||a==9) return false;
        minors++;
    }
    return minors<=1; // K vs K, or K+minor vs K
}
void Game::updateStatus(){
    std::vector<Move> legal; pos.generateLegal(legal);
    if (legal.empty()){
        if (pos.inCheck(pos.whiteToMove)){
            result = pos.whiteToMove? GameResult::BLACK_WINS : GameResult::WHITE_WINS;
            reason = ResultReason::CHECKMATE;
        } else { result=GameResult::DRAW; reason=ResultReason::STALEMATE; }
        return;
    }
    if (pos.halfmoveClock>=100){ result=GameResult::DRAW; reason=ResultReason::FIFTY_MOVE; return; }
    if (insufficientMaterial(pos)){ result=GameResult::DRAW; reason=ResultReason::INSUFFICIENT; return; }
    int rep=0;
    const std::string& k = repKeys.back();
    for (const auto& s : repKeys) if (s==k) rep++;
    if (rep>=3){ result=GameResult::DRAW; reason=ResultReason::REPETITION; }
}
std::string Game::resultString() const{
    switch(result){
        case GameResult::WHITE_WINS: return "1-0";
        case GameResult::BLACK_WINS: return "0-1";
        case GameResult::DRAW: return "1/2-1/2";
        default: return "*";
    }
}
std::string Game::toPGN() const{
    std::ostringstream o;
    o << "[Event \"Chess-GUI-for-UCI\"]\n[White \""<<whiteName<<"\"]\n[Black \""<<blackName<<"\"]\n";
    std::string sf = start.toFEN();
    if (sf != Position::startpos().toFEN())
        o << "[SetUp \"1\"]\n[FEN \""<<sf<<"\"]\n";
    o << "[Result \""<<resultString()<<"\"]\n\n";
    int num = start.fullmoveNumber;
    bool w = start.whiteToMove;
    for (size_t i=0;i<sans.size();i++){
        if (w) o << num << ". ";
        else if (i==0) o << num << "... ";
        o << sans[i] << " ";
        if (!w) num++;
        w=!w;
        if (i%10==9) o << "\n";
    }
    o << resultString() << "\n";
    return o.str();
}

std::string enginePositionProblem(const Position& p){
    int wc=0,bc=0,wp=0,bp=0,wk=0,bk=0;
    for (int i=0;i<64;i++){
        int v=p.board[i];
        if (!v) continue;
        if (v>0){ wc++; if (v==WHITE_PAWN) wp++; if (v==WHITE_KING) wk++; }
        else    { bc++; if (v==BLACK_PAWN) bp++; if (v==BLACK_KING) bk++; }
        if (std::abs(v)==1 && (i/8==0 || i/8==7))
            return "pawns cannot stand on the 1st or 8th rank";
    }
    if (wk!=1 || bk!=1) return "each side needs exactly one king";
    if (wp>8 || bp>8) return "more than 8 pawns on one side";
    if (wc>16 || bc>16) return "more than 16 pieces on one side - UCI engines cannot handle this";
    if (p.inCheck(!p.whiteToMove))
        return std::string(p.whiteToMove? "Black":"White") + " is in check but it isn't their turn";
    return "";
}

bool premovePlausible(const Position& p, int from, int to){
    if (from<0||from>63||to<0||to>63||from==to) return false;
    int v = p.board[from];
    if (!v) return false;
    int a = std::abs(v);
    int fx=from%8, fy=from/8, tx=to%8, ty=to/8;
    int dx=tx-fx, dy=ty-fy, ax=std::abs(dx), ay=std::abs(dy);
    switch (a){
    case 1: {                                   // pawn
        int dir = v>0? -1 : 1;
        if (dy==dir && ax==1) return true;      // capture / recapture
        if (dx==0 && dy==dir) return true;      // push
        int startRank = v>0? 6 : 1;
        if (dx==0 && fy==startRank && dy==2*dir) return true;
        return false; }
    case 3: return (ax==1&&ay==2)||(ax==2&&ay==1);
    case 4: return ax==ay;
    case 5: return ax==0||ay==0;
    case 9: return ax==ay||ax==0||ay==0;
    case 20:
        if (ax<=1 && ay<=1) return true;
        return ay==0 && ax==2 && fx==4 && fy==(v>0?7:0);   // castling gesture
    }
    return false;
}

// ---------------- PGN import ----------------
std::vector<PgnGame> parsePGN(const std::string& text){
    std::vector<PgnGame> games;
    PgnGame cur; bool inGame=false, sawMoves=false;
    std::istringstream ss(text);
    std::string line;
    auto flush=[&](){
        if (inGame && (sawMoves || !cur.movetext.empty())) games.push_back(cur);
        cur = PgnGame{}; inGame=false; sawMoves=false;
    };
    while (std::getline(ss, line)){
        while (!line.empty() && (line.back()=='\r'||line.back()==' ')) line.pop_back();
        if (!line.empty() && line[0]=='['){
            if (sawMoves) flush();               // header after movetext = next game
            inGame = true;
            size_t sp = line.find(' ');
            size_t q1 = line.find('"');
            size_t q2 = line.rfind('"');
            if (sp!=std::string::npos && q1!=std::string::npos && q2>q1){
                std::string key = line.substr(1, sp-1);
                std::string val = line.substr(q1+1, q2-q1-1);
                if (key=="White") cur.white = val;
                else if (key=="Black") cur.black = val;
                else if (key=="Result") cur.result = val;
                else if (key=="Event") cur.event = val;
                else if (key=="FEN") cur.fenTag = val;
            }
            continue;
        }
        if (line.empty()) continue;
        inGame = true; sawMoves = true;
        cur.movetext += line + " ";
    }
    flush();
    return games;
}

bool gameFromPGN(const PgnGame& pg, Game& out, std::string* err){
    Position start = Position::startpos();
    if (!pg.fenTag.empty()){
        bool ok=false;
        Position p = Position::fromFEN(pg.fenTag,&ok);
        if (ok) start = p;
        else { if (err) *err = "Invalid FEN tag"; return false; }
    }
    out.reset(start);
    out.whiteName = pg.white; out.blackName = pg.black;
    const std::string& s = pg.movetext;
    size_t i=0, n=s.size();
    int ply=0, depth=0;
    while (i<n){
        char ch = s[i];
        if (ch==' '||ch=='\t'){ i++; continue; }
        if (ch=='{'){ while (i<n && s[i]!='}') i++; i++; continue; }        // comment
        if (ch=='('){ depth++; i++;                                          // variation
            while (i<n && depth>0){ if(s[i]=='(')depth++; if(s[i]==')')depth--; i++; }
            continue; }
        if (ch=='$'){ while (i<n && s[i]!=' ') i++; continue; }              // NAG
        size_t j=i;
        while (j<n && s[j]!=' ' && s[j]!='{' && s[j]!='(') j++;
        std::string tok = s.substr(i, j-i);
        i = j;
        if (tok.empty()) continue;
        if (tok=="1-0"||tok=="0-1"||tok=="1/2-1/2"||tok=="*") break;
        // strip leading move numbers "12." / "12..."
        size_t k=0; while (k<tok.size() && (std::isdigit((unsigned char)tok[k])||tok[k]=='.')) k++;
        tok = tok.substr(k);
        if (tok.empty()) continue;
        Move m;
        if (!out.pos.sanToMove(tok, m)){
            if (err) *err = "Illegal/unreadable move '"+tok+"' at ply "+std::to_string(ply+1);
            return false;
        }
        out.tryMove(m);
        ply++;
    }
    // keep PGN's stated result if the moves alone did not decide the game
    if (out.result==GameResult::ONGOING){
        if (pg.result=="1-0") out.result=GameResult::WHITE_WINS;
        else if (pg.result=="0-1") out.result=GameResult::BLACK_WINS;
        else if (pg.result=="1/2-1/2") out.result=GameResult::DRAW;
        if (out.result!=GameResult::ONGOING) out.reason=ResultReason::AGREEMENT;
    }
    return true;
}
