#pragma once 


class Piece{
private:
    int pieceType;
    sf::Texture texture;
    sf::Sprite  sprite;
    // from 0 to 7 
    int x , y ;
    // the coordination of  piece in pixels 
    int pixelX , pixelY;
    bool exists ,isDragging; 
    size_t windowWidth ,  windowHeight;
    // set the piece Dimensions base on window size (it is a square ) 
    int pieceDimensions;
public:
    Piece() ;
    // Copy constructor
    Piece(const Piece& other);
    sf::Vector2i getPosition();
    int getType();
    const sf::Sprite& getSprite() const;
    // two ways of updating the position using function overloading 
    void setPosition(float x, float y);
    void setPosition(const sf::Vector2f& position) ;
};