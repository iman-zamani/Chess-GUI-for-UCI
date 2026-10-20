/* Chess-GUI-for-UCI — board editor screen. GPL-2.0 */
#pragma once
#include "chess.hpp"
#include "ui/board_view.hpp"
#include <string>

class App;

// Set up any position: paint pieces, drag them with the hand tool, set side
// to move and castling rights, or type/paste a FEN. The position can then be
// played, analysed, or added to an engine match.
class EditorScreen {
public:
    explicit EditorScreen(App& app) : app_(app) {}
    void open(bool forMatch);              // forMatch: "Done" adds the position to the match
    void draw();
    Position position() const;             // castling rights cleaned to match the pieces
    std::string problem() const;           // "" if engines can use the position
private:
    void drawPalette();
    App& app_;
    Position pos_ = Position::startpos();
    BoardView board_;
    int brush_ = WHITE_PAWN;               // piece value, 0 = eraser, kHand = hand tool
    static constexpr int kHand = 99;
    int handFrom_ = -1, handPiece_ = 0;
    bool flipped_ = false;
    bool forMatch_ = false;
    std::string fenEdit_;
    bool fenDirty_ = false;                // fenEdit_ being typed by the user
};
