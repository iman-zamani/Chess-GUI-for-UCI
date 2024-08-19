#pragma once 


class piece{
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
    piece() = default;
    piece(int pieceType,int x , int y);
    ~piece(){};
    // Copy constructor
    piece(const piece& other);

    void draw(sf::RenderWindow &window);    
    void updateGraphicalPosition(int windowWidth , int windowHeight);
};