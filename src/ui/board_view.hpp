/* Chess-GUI-for-UCI — chess board widget (ImGui draw list). GPL-2.0 */
#pragma once
#include "chess.hpp"
#include <SFML/Graphics/Texture.hpp>
#include <imgui.h>
#include <string>
#include <vector>

// Piece sets: "classic" (the built-in sprite sheet), the folders in Resources/pieces/
// and in <config dir>/pieces/ (user sets: wK.svg ... bP.svg, or a pieces.png sheet).
struct PieceSetInfo { std::string name, path; bool user = false; };
std::vector<PieceSetInfo> pieceSets();
std::string userPieceDir();

// Board colour schemes. Index stored in settings by name.
struct BoardTheme { const char* name; ImU32 light, dark; };
const std::vector<BoardTheme>& boardThemes();
const BoardTheme& boardTheme(const std::string& name);

// The piece sprite sheet: 6x2 cells, K Q B N R P; white row then black row.
class PieceSet {
public:
    bool load(const std::string& path);                       // a 6x2 sprite sheet (PNG)
    // A folder with wK.svg ... bP.svg (rasterized at `cell` pixels) or a pieces.png sprite sheet.
    bool loadDir(const std::string& dir, int cell = 256);
    bool ok() const { return ok_; }
    // Draws `piece` (values.hpp encoding) into the square [p0, p1].
    void draw(ImDrawList* dl, int piece, ImVec2 p0, ImVec2 p1, ImU32 tint = IM_COL32_WHITE) const;
private:
    sf::Texture tex_;
    bool ok_ = false;
};

struct BoardArrow { int from = -1, to = -1; ImU32 color = 0; };
struct BoardTint  { int sq = -1; ImU32 color = 0; };

// What to draw this frame.
struct BoardSpec {
    const Position* pos = nullptr;
    bool flipped = false;
    bool showCoords = true;
    std::string theme = "brown";
    int lastFrom = -1, lastTo = -1;
    int selected = -1;
    std::vector<Move> targets;           // legal-move dots for the selected piece
    std::vector<BoardTint> tints;        // extra square highlights (premoves, ...)
    std::vector<BoardArrow> arrows;      // drawn in order (last on top)
    bool interactive = true;
    // Which squares may be picked up and dragged (default: none).
    std::vector<bool> draggable;         // 64 entries, or empty
    int hiddenSquare = -1;               // piece on this square is not drawn
    int ghostPiece = 0;                  // drawn under the cursor (editor hand tool)
    bool dimmed = false;                 // dark overlay (e.g. promotion choice)
    int animateMs = 0;                   // slide the piece of a new lastFrom->lastTo move (0 = off)
};

// What the user did this frame.
struct BoardInput {
    int pressed = -1;                    // left button went down on this square
    int rightPressed = -1;
    int hovered = -1;
    int paintOver = -1;                  // square under the cursor while the left button is held
    int dropFrom = -1, dropTo = -1;      // a drag ended on another square
    bool released = false;               // left button released (anywhere)
    int releasedOn = -1;                 // square under the cursor at release (-1 = off board)
};

class BoardView {
public:
    // Draws the board as a `size` x `size` pixel item at the cursor position.
    BoardInput draw(const char* id, float size, const BoardSpec& spec, const PieceSet& pieces);
    bool dragging() const { return dragSq_ >= 0 && dragMoved_; }
    bool animating() const;              // a move is sliding: keep drawing frames
    int squareAt(ImVec2 p) const;        // screen position -> square (-1 outside)
    ImVec2 squareCenter(int sq) const;
    float squareSize() const { return size_ / 8; }
private:
    ImVec2 origin_{0, 0};
    float size_ = 0;
    bool flipped_ = false;
    int dragSq_ = -1;
    bool dragMoved_ = false;
    bool holding_ = false;               // left button went down on the board and is held
    ImVec2 pressPos_{0, 0};
    // move animation
    std::string shownKey_;               // position drawn last frame
    int shownBoard_[64] = {0};
    int animFrom_ = -1, animTo_ = -1, animPiece_ = 0, animMs_ = 0;
    double animStart_ = 0;
    int droppedOn_ = -1;                 // a drag just ended here: that move needs no slide
};

// Eval bar (white share from the bottom); cp from White's point of view.
void drawEvalBar(ImDrawList* dl, ImVec2 p0, ImVec2 p1, int cpWhite, bool isMate, int mateWhite, bool flipped);
std::string formatScore(int cp, bool isMate, int mate);   // "+0.35", "M3", "-M2"
