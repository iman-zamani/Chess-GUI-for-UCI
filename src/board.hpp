#include "piece.hpp"

class Board{

private:
    std::vector<Piece> pieces;
    sf::Texture lightSquaresTexture;
    sf::Texture darkSquaresTexture;
    std::vector<sf::Sprite> SpriteSquares;
    // this is the piece that the user selected with the mouse . if it is -1 it means there is no piece selected
    int pieceSelected;
    //
    bool isDragging;
public:

    Board(sf::RenderWindow &window,std::string FEN);
    // if this constructor is used, it will call the default constructor (with FEN string) and give it the 
    //default starting position as the required FEN String 
    Board(sf::RenderWindow &window) : Board(window,"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") {};
    void draw(sf::RenderWindow &window); 
    void selectTargetPiece(sf::RenderWindow &window, int mouseX, int mouseY);
    bool getIsPieceDragging();
    void setIsPieceDragging(bool setIsPieceDragging);
};