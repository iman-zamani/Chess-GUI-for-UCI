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
    int windowWidth ,  windowHeight;
public:
    Piece() = default;
    Piece(int pieceType,int x , int y);
    ~Piece(){};
    // Copy constructor
    Piece(const Piece& other);

    void draw(sf::RenderWindow &window);    
    void updateGraphicalPosition(int windowWidth , int windowHeight);
    sf::Vector2i getPiecePosition();
    void setGraphicalPositionWhileDragging(int mouseX, int  mouseY);
    void draggingReleased();
    int getPieceType();
};