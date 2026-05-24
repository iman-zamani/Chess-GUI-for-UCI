/*
 * Chess-GUI-for-UCI
 * Copyright (C) 2025 Iman Zamani
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

 #pragma once
#include "piece.hpp"



class Board : public sf::Drawable{

private:
    // store the window reference to avoid passing as a parameter each time 
    sf::RenderWindow& window;
    std::vector<Piece> pieces;
    sf::Texture lightSquaresTexture;
    sf::Texture darkSquaresTexture;
    std::vector<sf::Sprite> spriteSquares;
    // texture for squares that the selected piece can go
    sf::Texture targetSquaresTexture;
    int squareSideLength;
    // this is the piece that the user selected with the mouse . if it is -1 it means there is no piece selected
    int pieceSelected;
    // the size of piece vector fro tracking how many pieces are in the board 
    int piecesVectorSize;
    //
    bool isDragging;
    // board present qualities 
    bool isWhiteTurn;
    bool whiteKingSideCastle;
    bool whiteQueenSideCastle;
    bool blackKingSideCastle;
    bool blackQueenSideCastle;
    // if -1 there is no Ee passant available 
    //and if it is available the position of where the pawn would go if Ee passant happens 
    int enPassantX , enPassantY;
    // the number of half moves that have been passed from last paw movement or capture 
    int halfMovesFromLastCaptureOrPawnMove;
    int moveNumber;
    // this vector has a size of 64 (for each square) and it will hold the pieceType that exist in each square 
    //and if no piece is in that square it will store 0
    std::vector<int> piecePositions;
    
    // this vectors stores the legal squares that the selected piece can go 
    // for each square if it is true that piece can go to that square 
    std::vector<bool> legalSquaresForTargetPiece;
    // this texture is empty and .getSize() on it will always return sf::Vector2u(0, 0)
    sf::Texture* emptyTextureToReturn;

    // all vars about the promotion of pawns 
    bool isPieceSelectedState; // true if a piece is waiting for a target destination click
    int selectedPieceIdx;       // tracks which piece vector index is highlighted
    bool isPromotionWaiting;    // Freezes main inputs when a pawn reaches the end
    int promotionTargetX;       // Remembers where the pawn landed during selection
    int promotionTargetY;       // Remembers where the pawn landed during selection
    int promotionPawnVectorIdx; // Remembers which piece index to mutate into a chosen type
    std::vector<sf::Sprite> promotionOptionSprites;
    std::vector<sf::Texture*> promotionOptionTextures;

    // methods 
    void setSquaresTexture();
    void constructor(const std::string &FEN);
    //draw the board squares 
    void drawBoardBackground(sf::RenderTarget& target, sf::RenderStates states) const;
    // draw all the elements 
    virtual void draw(sf::RenderTarget& target, sf::RenderStates states) const override;
    sf::Vector2i squareNameToXY(const std::string &square);
    bool isSquareAttacked(int targetX, int targetY, bool attackedByWhite) const;
    sf::Vector2i findKingGridPosition(bool whiteKing) const;
public:

    Board(sf::RenderWindow &win,const std::string &FEN): window(win){this->constructor(FEN);};
    // if this constructor is used, it will call the default constructor (with FEN string) and give it the 
    //default starting position as the required FEN String 
    Board(sf::RenderWindow &win) : Board(win,"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") {};
    void draw(sf::RenderWindow &window); 
    void selectTargetPiece(sf::RenderWindow &window, int mouseX, int mouseY);
    bool getIsPieceDragging();
    void setIsPieceDragging(bool setIsPieceDragging,int mouseX,int mouseY);
    
    void printBoardState();
    // this method will find the legal moves for the selected piece 
    void findLegalMoves();
    // apply a move by the x and y of starting position and the target square, it will return false if move is not possible  
    // this function wont check if the move is legal or not , as long as ther is a piece in starting position it will not return false 
    bool applyMove(int startX,int startY,int endX,int endY); 
    // it will drag the selected piece with the mouse 
    void dragPiece(int mouseX,int mouseY);
    // place the piece where the user intended 
    void placePiece(int mouseX,int mouseY);
    // start dragging with returning dragging pieces texture
    sf::Texture* startDragging(sf::Vector2f clickPos);
    // end dragging 
    void endDragging(sf::Vector2f clickPos);
    const sf::Vector2f getSelectedPieceSpriteScale()const;
    void placeThePiece(sf::Vector2f clickPos);
    void handleSquareClick(sf::Vector2f clickPos);
    void drawPromotionMenu(sf::RenderTarget& target, sf::RenderStates states) const;
    // 
    bool getIsWhiteTurn() const { return isWhiteTurn; }
    std::string getUciMoveHistoryString() const;
    bool applyUciMove(const std::string& moveStr);
    // 
    enum class GameResult { Ongoing, WhiteWins, BlackWins, Draw_Stalemate, Draw_50Move, Draw_Repetition };
    GameResult getGameState() const { return currentGameState; }
private:
    // methods to get legal moves for each piece type
    // white pieces 
    void findLegalMovesWhitePawn();
    void findLegalMovesWhiteRook();
    void findLegalMovesWhiteKnight();
    void findLegalMovesWhiteBishop();
    void findLegalMovesWhiteQueen();
    void findLegalMovesWhiteKing();
    // black pieces 
    void findLegalMovesBlackPawn();
    void findLegalMovesBlackRook();
    void findLegalMovesBlackKnight();
    void findLegalMovesBlackBishop();
    void findLegalMovesBlackQueen();
    void findLegalMovesBlackKing();
    // --- GAME END TRACKING ---
    GameResult currentGameState = GameResult::Ongoing;
    std::vector<std::string> positionHistory;

    // Helper functions for end-game detection
    bool hasAnyLegalMoves(bool forWhite);
    std::string generatePositionHash() const;
    void checkGameEndConditions();
    // uci compatibility 
    std::vector<std::string> uciMoveHistory;
    std::string formatUciMove(int startX, int startY, int endX, int endY, char promotion = 0) const;
};

