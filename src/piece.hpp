#pragma once 


class Piece : public sf::Drawable{
private:
    int pieceType;
    sf::Texture texture;
    sf::Sprite  sprite;
    // ghost version of the piece 
    sf::Sprite ghostSprite;
    // the scale we need to apply to sprites so the size gets correct 
    float scale;
    // size of each square of the board
    int squareSide; 
    // from 0 to 7 
    int x , y ;
    // the coordination of  piece in pixels 
    int pixelX , pixelY;
    bool exists ,isDragging; 
    size_t windowWidth ,  windowHeight;
    // set the piece Dimensions base on window size (it is a square ) 
    int pieceDimensions;
    virtual void draw(sf::RenderTarget& target, sf::RenderStates states) const override {
        if (!isDragging){
            target.draw(sprite, states);
        }
        else {
            target.draw(ghostSprite, states);
        }
    }
    // two ways of updating the position using function overloading 
    void setPosition(float x, float y);
    void setPosition(const sf::Vector2f& position) ;
public:
    Piece() = default;  
    Piece(int pieceType,int x , int y, int squareSide);
    // Copy constructor
    Piece(const Piece& other);
    sf::Vector2i getPosition();
    int getType();
    const sf::Texture& getTexture() const;
    const float getScale()const;
    void selectPiece();
    void deselectPiece();
    bool moveTo(int destX, int destY);
};