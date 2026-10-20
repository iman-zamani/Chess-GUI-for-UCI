/* Chess-GUI-for-UCI — chess board widget (ImGui draw list). GPL-2.0 */
#include "ui/board_view.hpp"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

const std::vector<BoardTheme>& boardThemes(){
    static const std::vector<BoardTheme> t = {
        {"brown", IM_COL32(240,217,181,255), IM_COL32(181,136,99,255)},
        {"green", IM_COL32(238,238,210,255), IM_COL32(118,150,86,255)},
        {"blue",  IM_COL32(222,227,230,255), IM_COL32(140,162,173,255)},
        {"grey",  IM_COL32(220,220,220,255), IM_COL32(150,150,150,255)},
        {"purple",IM_COL32(232,225,240,255), IM_COL32(150,124,176,255)},
    };
    return t;
}
const BoardTheme& boardTheme(const std::string& name){
    for (auto& t : boardThemes()) if (name == t.name) return t;
    return boardThemes()[0];
}

// Same encoding ImGui-SFML uses: the GL texture name stored in an ImTextureID.
static ImTextureID textureId(const sf::Texture& t){
    ImTextureID id{};
    unsigned int gl = t.getNativeHandle();
    std::memcpy(&id, &gl, sizeof gl);
    return id;
}

bool PieceSet::load(const std::string& path){
    ok_ = tex_.loadFromFile(path);
    if (ok_){ tex_.setSmooth(true); (void)tex_.generateMipmap(); }
    return ok_;
}

void PieceSet::draw(ImDrawList* dl, int piece, ImVec2 p0, ImVec2 p1, ImU32 tint) const{
    if (!piece) return;
    int a = std::abs(piece);
    int col = a==20?0 : a==9?1 : a==4?2 : a==3?3 : a==5?4 : 5;
    if (ok_){
        int row = piece > 0 ? 0 : 1;
        ImVec2 uv0(col / 6.f, row / 2.f), uv1((col + 1) / 6.f, (row + 1) / 2.f);
        dl->AddImage(textureId(tex_), p0, p1, uv0, uv1, tint);
    } else {
        const char* letters = "KQBNRP";
        char s[2] = {letters[col], 0};
        float h = (p1.y - p0.y) * 0.7f;
        ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(h, FLT_MAX, 0, s);
        ImVec2 at(p0.x + (p1.x - p0.x - sz.x) / 2, p0.y + (p1.y - p0.y - sz.y) / 2);
        dl->AddText(ImGui::GetFont(), h, at, piece > 0 ? IM_COL32(255,255,255,255) : IM_COL32(20,20,20,255), s);
    }
}

int BoardView::squareAt(ImVec2 p) const{
    if (size_ <= 0) return -1;
    float s8 = size_ / 8;
    int x = (int)std::floor((p.x - origin_.x) / s8), y = (int)std::floor((p.y - origin_.y) / s8);
    if (x < 0 || x > 7 || y < 0 || y > 7) return -1;
    if (flipped_){ x = 7 - x; y = 7 - y; }
    return y * 8 + x;
}

ImVec2 BoardView::squareCenter(int sq) const{
    float s8 = size_ / 8;
    int x = sq % 8, y = sq / 8;
    if (flipped_){ x = 7 - x; y = 7 - y; }
    return ImVec2(origin_.x + (x + 0.5f) * s8, origin_.y + (y + 0.5f) * s8);
}

static void drawArrow(ImDrawList* dl, ImVec2 a, ImVec2 b, float sq, ImU32 c){
    float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx*dx + dy*dy);
    if (len < 1) return;
    float ux = dx / len, uy = dy / len, nx = -uy, ny = ux;
    float headLen = sq * 0.38f, headW = sq * 0.42f, shaftW = sq * 0.20f;
    ImVec2 base(b.x - ux * headLen, b.y - uy * headLen);
    dl->AddQuadFilled(ImVec2(a.x + nx*shaftW/2, a.y + ny*shaftW/2), ImVec2(base.x + nx*shaftW/2, base.y + ny*shaftW/2),
                      ImVec2(base.x - nx*shaftW/2, base.y - ny*shaftW/2), ImVec2(a.x - nx*shaftW/2, a.y - ny*shaftW/2), c);
    dl->AddTriangleFilled(b, ImVec2(base.x + nx*headW/2, base.y + ny*headW/2),
                          ImVec2(base.x - nx*headW/2, base.y - ny*headW/2), c);
}

BoardInput BoardView::draw(const char* id, float size, const BoardSpec& spec, const PieceSet& pieces){
    BoardInput in;
    origin_ = ImGui::GetCursorScreenPos();
    size_ = std::floor(size / 8) * 8;                  // whole-pixel squares
    flipped_ = spec.flipped;
    ImGui::InvisibleButton(id, ImVec2(size_, size_),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    ImVec2 mouse = ImGui::GetIO().MousePos;
    if (spec.interactive){
        if (hovered) in.hovered = squareAt(mouse);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
            in.pressed = squareAt(mouse);
            pressPos_ = mouse; dragMoved_ = false; holding_ = true;
            dragSq_ = (in.pressed >= 0 && spec.draggable.size() == 64 && spec.draggable[in.pressed]) ? in.pressed : -1;
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) in.rightPressed = squareAt(mouse);
        if (holding_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            in.paintOver = squareAt(mouse);
            float mx = mouse.x - pressPos_.x, my = mouse.y - pressPos_.y;
            if (mx*mx + my*my > 16) dragMoved_ = true;
        }
        // (on the release frame ImGui has already cleared the active item)
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && holding_){
            holding_ = false;
            in.released = true;
            in.releasedOn = squareAt(mouse);
            if (dragSq_ >= 0 && in.releasedOn >= 0 && in.releasedOn != dragSq_){
                in.dropFrom = dragSq_; in.dropTo = in.releasedOn;
            }
            dragSq_ = -1; dragMoved_ = false;
        }
    }
    if (!spec.pos) return in;
    const Position& p = *spec.pos;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const BoardTheme& th = boardTheme(spec.theme);
    float s8 = size_ / 8;
    auto rectOf = [&](int sq, ImVec2& a, ImVec2& b){
        int x = sq % 8, y = sq / 8;
        if (flipped_){ x = 7 - x; y = 7 - y; }
        a = ImVec2(origin_.x + x * s8, origin_.y + y * s8); b = ImVec2(a.x + s8, a.y + s8);
    };
    ImVec2 a, b;
    for (int sq = 0; sq < 64; sq++){
        rectOf(sq, a, b);
        dl->AddRectFilled(a, b, ((sq % 8 + sq / 8) % 2) ? th.dark : th.light);
    }
    auto tint = [&](int sq, ImU32 c){ if (sq >= 0 && sq < 64){ rectOf(sq, a, b); dl->AddRectFilled(a, b, c); } };
    tint(spec.lastFrom, IM_COL32(70,130,220,70));      // origin lighter, destination stronger
    tint(spec.lastTo, IM_COL32(70,130,220,150));
    tint(spec.selected, IM_COL32(110,160,90,180));
    if (p.kingSquare(p.whiteToMove) >= 0 && p.inCheck(p.whiteToMove))
        tint(p.kingSquare(p.whiteToMove), IM_COL32(220,80,80,170));
    for (auto& t : spec.tints) tint(t.sq, t.color);
    if (spec.interactive && hovered && dragging()){
        int h = squareAt(mouse);
        if (h >= 0){ rectOf(h, a, b); dl->AddRect(a, b, IM_COL32(255,255,255,140), 0, 0, std::max(2.f, s8 * 0.04f)); }
    }
    for (const Move& m : spec.targets){
        ImVec2 c = squareCenter(m.to);
        if (p.board[m.to] || m.isEnPassant) dl->AddCircle(c, s8 * 0.42f, IM_COL32(40,90,40,150), 32, s8 * 0.07f);
        else dl->AddCircleFilled(c, s8 * 0.15f, IM_COL32(40,90,40,120), 24);
    }
    int liftSq = dragging() ? dragSq_ : -1;
    for (int sq = 0; sq < 64; sq++){
        if (!p.board[sq] || sq == liftSq || sq == spec.hiddenSquare) continue;
        rectOf(sq, a, b);
        pieces.draw(dl, p.board[sq], a, b);
    }
    if (spec.showCoords){
        float fs = std::max(9.f, s8 * 0.16f);
        for (int i = 0; i < 8; i++){
            char f[2] = {char(flipped_ ? 'h' - i : 'a' + i), 0};
            char r[2] = {char(flipped_ ? '1' + i : '8' - i), 0};
            // contrast with the square the label sits on (a1 and h8 are dark)
            ImVec2 fsz = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, f);
            dl->AddText(ImGui::GetFont(), fs, ImVec2(origin_.x + (i + 1) * s8 - fsz.x - s8 * 0.05f,
                        origin_.y + size_ - fsz.y - s8 * 0.02f), (i % 2) ? th.dark : th.light, f);
            dl->AddText(ImGui::GetFont(), fs, ImVec2(origin_.x + s8 * 0.05f, origin_.y + i * s8 + s8 * 0.02f),
                        (i % 2) ? th.light : th.dark, r);
        }
    }
    for (auto& ar : spec.arrows)
        if (ar.from >= 0 && ar.to >= 0 && ar.from != ar.to)
            drawArrow(dl, squareCenter(ar.from), squareCenter(ar.to), s8, ar.color);
    dl->AddRect(origin_, ImVec2(origin_.x + size_, origin_.y + size_), IM_COL32(90,90,110,255), 0, 0, 2);
    if (spec.dimmed) dl->AddRectFilled(origin_, ImVec2(origin_.x + size_, origin_.y + size_), IM_COL32(0,0,0,120));
    // lifted piece / ghost follows the cursor, drawn on the foreground layer
    int ghost = liftSq >= 0 ? p.board[liftSq] : spec.ghostPiece;
    if (ghost && (liftSq >= 0 || spec.ghostPiece)){
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        pieces.draw(fg, ghost, ImVec2(mouse.x - s8/2, mouse.y - s8/2), ImVec2(mouse.x + s8/2, mouse.y + s8/2),
                    IM_COL32(255,255,255,230));
    }
    return in;
}

std::string formatScore(int cp, bool isMate, int mate){
    char b[32];
    if (isMate) std::snprintf(b, sizeof b, "%sM%d", mate < 0 ? "-" : "", std::abs(mate));
    else std::snprintf(b, sizeof b, "%+.2f", cp / 100.0);
    return b;
}

void drawEvalBar(ImDrawList* dl, ImVec2 p0, ImVec2 p1, int cpWhite, bool isMate, int mateWhite, bool flipped){
    float frac = isMate ? (mateWhite > 0 ? 1.f : 0.f) : 0.5f + 0.5f * std::tanh(cpWhite / 600.0f);
    float h = p1.y - p0.y;
    dl->AddRectFilled(p0, p1, IM_COL32(30,30,30,255));
    if (!flipped) dl->AddRectFilled(ImVec2(p0.x, p0.y + h * (1 - frac)), p1, IM_COL32(230,230,230,255));
    else dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + h * frac), IM_COL32(230,230,230,255));
    dl->AddLine(ImVec2(p0.x, p0.y + h / 2), ImVec2(p1.x, p0.y + h / 2), IM_COL32(120,120,120,255), 2);
}
