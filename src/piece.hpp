#pragma once 


class Piece : public sf::Drawable{
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
    virtual void draw(sf::RenderTarget& target, sf::RenderStates states) const override {
        target.draw(sprite, states);
    }
public:
    Piece() = default;  
    Piece(int pieceType,int x , int y);
    // Copy constructor
    Piece(const Piece& other);
    sf::Vector2i getPosition();
    int getType();
    // two ways of updating the position using function overloading 
    void setPosition(float x, float y);
    void setPosition(const sf::Vector2f& position) ;
};