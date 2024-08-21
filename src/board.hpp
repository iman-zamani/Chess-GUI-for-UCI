#include "piece.hpp"

class Board{

private:
    std::vector<Piece> pieces;
    sf::Texture lightSquaresTexture;
    sf::Texture darkSquaresTexture;
    std::vector<sf::Sprite> spriteSquares;
    sf::Texture targetSquaresTexture;
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
    // this vectors stores the legal squares that the selected piece can go 
    // for each square if it is true that piece can go to that square 
    std::vector<bool> legalSquaresForTargetPiece;
    //
    void squareTextureSetToNormal();
    
public:

    Board(sf::RenderWindow &window,const std::string &FEN);
    // if this constructor is used, it will call the default constructor (with FEN string) and give it the 
    //default starting position as the required FEN String 
    Board(sf::RenderWindow &window) : Board(window,"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") {};
    void draw(sf::RenderWindow &window); 
    void selectTargetPiece(sf::RenderWindow &window, int mouseX, int mouseY);
    bool getIsPieceDragging();
    void setIsPieceDragging(bool setIsPieceDragging,int mouseX,int mouseY);
    sf::Vector2i squareNameToXY(const std::string &square);
    void printBoardState();
    // this method will find the legal moves for the selected piece 
    void findLegalMoves();
};

