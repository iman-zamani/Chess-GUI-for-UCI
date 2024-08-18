#pragma once 


class piece{
private:
    int pieceType;
    sf::Texture texture;
    sf::Sprite  sprite;
    // from 0 to 7 
    int x , y ;
    bool exists ;
public:
    piece(int pieceType,int x , int y);
    ~piece(){};
    void draw();

    
};