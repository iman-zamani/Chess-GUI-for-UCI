/* Chess-GUI-for-UCI — board editor screen. GPL-2.0 */
#include "ui/editor_screen.hpp"
#include "gui.hpp"
#include "ui/widgets.hpp"
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>

static const int kPalette[12] = {WHITE_KING, WHITE_QUEEN, WHITE_ROOK, WHITE_BISHOP, WHITE_KNIGHT, WHITE_PAWN,
                                 BLACK_KING, BLACK_QUEEN, BLACK_ROOK, BLACK_BISHOP, BLACK_KNIGHT, BLACK_PAWN};

void EditorScreen::open(bool forMatch){
    forMatch_ = forMatch;
    handFrom_ = -1; handPiece_ = 0;
    fenDirty_ = false;
}

Position EditorScreen::position() const{
    Position p = pos_;
    p.castleWK = p.castleWK && p.board[60]==WHITE_KING && p.board[63]==WHITE_ROOK;
    p.castleWQ = p.castleWQ && p.board[60]==WHITE_KING && p.board[56]==WHITE_ROOK;
    p.castleBK = p.castleBK && p.board[4]==BLACK_KING && p.board[7]==BLACK_ROOK;
    p.castleBQ = p.castleBQ && p.board[4]==BLACK_KING && p.board[0]==BLACK_ROOK;
    // an en passant square is only kept if a pawn could actually have just double-pushed
    if (p.epSquare >= 0){
        int pawnSq = p.whiteToMove ? p.epSquare + 8 : p.epSquare - 8;
        int want = p.whiteToMove ? BLACK_PAWN : WHITE_PAWN;
        if (pawnSq < 0 || pawnSq > 63 || p.board[pawnSq] != want || p.board[p.epSquare]) p.epSquare = -1;
    }
    return p;
}

std::string EditorScreen::problem() const{
    return enginePositionProblem(position());
}

void EditorScreen::drawPalette(){
    float cell = ImGui::GetFontSize() * 2.8f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < 12; i++){
        if (i % 6) ImGui::SameLine();
        ImGui::PushID(i);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        char id[16]; snprintf(id, sizeof id, "piece%d", i);
        if (ImGui::InvisibleButton(id, ImVec2(cell, cell))) brush_ = kPalette[i];
        bool sel = brush_ == kPalette[i];
        ImVec2 p1(p0.x + cell, p0.y + cell);
        dl->AddRectFilled(p0, p1, sel ? IM_COL32(70,110,70,255) : ImGui::IsItemHovered() ? IM_COL32(80,80,100,255)
                                                                                          : IM_COL32(58,58,72,255), 4);
        app_.pieces().draw(dl, kPalette[i], ImVec2(p0.x + 3, p0.y + 3), ImVec2(p1.x - 3, p1.y - 3));
        ImGui::PopID();
    }
    if (ImGui::RadioButton("Hand (drag pieces)", brush_ == kHand)) brush_ = kHand;
    ImGui::SameLine();
    if (ImGui::RadioButton("Eraser", brush_ == 0)) brush_ = 0;
}

void EditorScreen::draw(){
    float avail = ImGui::GetContentRegionAvail().y;
    float side = std::min(avail - ImGui::GetFrameHeightWithSpacing(), ImGui::GetContentRegionAvail().x * 0.62f);

    ImGui::BeginGroup();
    BoardSpec spec;
    spec.pos = &pos_;
    spec.flipped = flipped_;
    spec.theme = app_.settings().boardTheme;
    spec.showCoords = app_.settings().showCoordinates;
    spec.hiddenSquare = handFrom_;
    spec.ghostPiece = handFrom_ >= 0 ? handPiece_ : 0;
    BoardInput in = board_.draw("editor-board", side, spec, app_.pieces());
    if (in.pressed >= 0){
        if (brush_ == kHand){
            if (pos_.board[in.pressed]){ handFrom_ = in.pressed; handPiece_ = pos_.board[in.pressed]; }
        } else pos_.board[in.pressed] = brush_;
        fenDirty_ = false;
    }
    if (in.paintOver >= 0 && brush_ != kHand) pos_.board[in.paintOver] = brush_;
    if (in.rightPressed >= 0){ pos_.board[in.rightPressed] = 0; fenDirty_ = false; }
    if (in.released && handFrom_ >= 0){
        pos_.board[handFrom_] = 0;
        if (in.releasedOn >= 0) pos_.board[in.releasedOn] = handPiece_;   // off the board = removed
        handFrom_ = -1; handPiece_ = 0;
    }
    ImGui::TextDisabled("Left-click paints (drag to paint), right-click erases. The hand tool drags pieces; "
                        "drop one off the board to remove it.");
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginChild("editor-side", ImVec2(0, 0));
    ImGui::SeparatorText("Pieces");
    drawPalette();
    if (ImGui::Button("Clear board")){ pos_ = Position(); pos_.whiteToMove = true; }
    ImGui::SameLine();
    if (ImGui::Button("Start position")) pos_ = Position::startpos();
    ImGui::SameLine();
    if (ImGui::Button("Flip board")) flipped_ = !flipped_;

    ImGui::SeparatorText("Position");
    int stm = pos_.whiteToMove ? 0 : 1;
    if (ImGui::RadioButton("White to move", &stm, 0)) pos_.whiteToMove = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Black to move", &stm, 1)) pos_.whiteToMove = false;
    ImGui::Checkbox("White O-O", &pos_.castleWK); ImGui::SameLine();
    ImGui::Checkbox("White O-O-O", &pos_.castleWQ);
    ImGui::Checkbox("Black O-O", &pos_.castleBK); ImGui::SameLine();
    ImGui::Checkbox("Black O-O-O", &pos_.castleBQ);
    std::string ep = pos_.epSquare >= 0 ? squareName(pos_.epSquare) : "";
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4);
    if (ImGui::InputTextWithHint("En passant square", "-", &ep)) pos_.epSquare = nameToSquare(ep);

    ImGui::SeparatorText("FEN");
    if (!fenDirty_) fenEdit_ = position().toFEN();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##fen", &fenEdit_)) fenDirty_ = true;
    if (fenDirty_){
        bool ok = false;
        Position p = Position::fromFEN(fenEdit_, &ok);
        if (ok){ if (ImGui::Button("Load this FEN")){ pos_ = p; fenDirty_ = false; } }
        else ImGui::TextColored(ui::kBad, "Not a valid FEN yet");
    }
    if (ImGui::Button("Copy FEN")){ ImGui::SetClipboardText(position().toFEN().c_str()); app_.toast("FEN copied"); }
    ImGui::SameLine();
    if (ImGui::Button("Paste FEN")){
        const char* c = ImGui::GetClipboardText();
        bool ok = false;
        Position p = Position::fromFEN(c ? c : "", &ok);
        if (ok){ pos_ = p; fenDirty_ = false; app_.toast("Position loaded from the clipboard"); }
        else app_.toast("The clipboard does not contain a valid FEN");
    }

    ImGui::SeparatorText("Use it");
    std::string prob = problem();
    if (!prob.empty()) ImGui::TextColored(ui::kWarn, "Engines can't use this position yet: %s", prob.c_str());
    else ImGui::TextColored(ui::kGood, "Position OK");
    ImGui::BeginDisabled(!prob.empty());
    if (forMatch_){
        if (ImGui::Button("Done - add to the match")) app_.editorDone(EditorAction::AddToMatch, position());
    } else {
        if (ImGui::Button("Play from here (human vs human)")) app_.editorDone(EditorAction::PlayHuman, position());
        if (ImGui::Button("Play vs engine from here")) app_.editorDone(EditorAction::PlayEngine, position());
        if (ImGui::Button("Analyse from here")) app_.editorDone(EditorAction::Analyse, position());
        if (ImGui::Button("Use in an engine match")) app_.editorDone(EditorAction::AddToMatch, position());
    }
    ImGui::EndDisabled();
    if (forMatch_ && ImGui::Button("Cancel")) app_.editorDone(EditorAction::Cancel, position());
    ImGui::EndChild();
}
