#pragma once
#include <iostream>
#include <SFML/Graphics.hpp>
#include <optional>
#include<vector>
#include<string>
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
    //
    void setSquaresTexture();
    void constructor(const std::string &FEN);
    //draw the board squares 
    void drawBoardBackground(sf::RenderTarget& target) const {
        for (sf::Sprite sp : spriteSquares){
            window.draw(sp);
        }
    }
    // draw all the elements 
    virtual void draw(sf::RenderTarget& target, sf::RenderStates states) const override {
        drawBoardBackground(target);
        for (const auto& piece : pieces) {
            target.draw(piece);
        }
    }
    sf::Vector2i squareNameToXY(const std::string &square);
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
};

