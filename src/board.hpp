#include "piece.hpp"

class board{

private:
    std::vector<piece> pieces;
    sf::Texture lightSquaresTexture;
    sf::Texture darkSquaresTexture;
    std::vector<sf::Sprite> SpriteSquares;
public:
    board(sf::RenderWindow &window,std::string FEN);
    // if this constructor is used, it will call the default constructor (with FEN string) and give it the 
    //default starting position as the required FEN String 
    board(sf::RenderWindow &window) : board(window,"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") {};
    void draw(sf::RenderWindow &window); 
};