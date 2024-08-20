#include <iostream>
#include <SFML/Graphics.hpp>
#include "values.hpp"
#include "piece.hpp"
#include <cmath>


// it will take the type of the piece that we want to create and it's position in the board 
Piece::Piece (int pieceType ,int x , int y){
    // set x and y 
    // check if it is in the boundaries of the board 
    if (x<8 and y < 8 and x > -1 and y > -1){
        this->x = x;
        this->y = y;
    }
    else {
        throw std::invalid_argument("x and y values must be greater than zero and smaller than 8.");
    }
    int pieceTypeWithoutColor = abs(pieceType);
    //set pieceType
    // if it is a valid piece type base on pieceValues.hpp file 
    if (pieceTypeWithoutColor == 1 || pieceTypeWithoutColor == 3 || pieceTypeWithoutColor == 4 || pieceTypeWithoutColor == 5 || pieceTypeWithoutColor == 9 || pieceTypeWithoutColor == 20){
        this->pieceType = pieceType;
        
    }
    else {
        throw std::invalid_argument("not a valid Pice type");
    }
    // ------
    this->exists =true;
    //--------
    // find the coordination of the piece in pieceTexture.png
    int i = 0 , j  = 0;
    // determine if the piece is black or white
    if (pieceType < 0 ){j = 1;}
    // determine the type itself 
    switch (pieceTypeWithoutColor)
    {
    case 20:
        i = 0;
        break;
    case 9:
        i = 1;
        break;
    case 4:
        i = 2;
        break;
    case 3:
        i = 3;
        break;
    case 5:
        i = 4;
        break;
    case 1:
        i = 5;
        break;
    
    default:
        break;
    }
    
    sf::IntRect rect(i * 333,j * 333,333,333);// (left, top, width, height)
     // loading texture from file 
    if (!this->texture.loadFromFile("./Resources/piceTexture.png", rect)) {
        throw std::invalid_argument("Error loading texture from file.");
    }
    this->texture.setSmooth(true);
    this->sprite.setTexture(this->texture);
    this->sprite.setScale(pieceToBoardScale,pieceToBoardScale);
    this->isDragging = false ;
    
}
void Piece::updateGraphicalPosition(int windowWidth , int windowHeight){
    int chunksWidth = windowWidth / 16;
    int chunksHeight = windowHeight / 9;
    this->pixelX = (this->x * chunksWidth ) + chunksWidth * 4;
    this->pixelY = (this->y * chunksHeight) + (chunksHeight/2);
    return ;
}

void Piece::draw(sf::RenderWindow &window){
    sf::Vector2u size = window.getSize();
    if (this->windowWidth != size.x || this->windowHeight != size.y){
        this->windowHeight = size.y, this->windowWidth = size.x;
        this->updateGraphicalPosition(size.x,size.y);
        this->sprite.setPosition(pixelX,pixelY);
    }
    window.draw(this->sprite);
}


Piece::Piece(const Piece& other) {
    this->pieceType = other.pieceType;
    this->x = other.x;
    this->y = other.y;
    this->pixelX = other.pixelX;
    this->pixelY = other.pixelY;
    this->exists = other.exists;
    this->isDragging = other.isDragging;
    this->windowWidth = other.windowWidth;
    this->windowHeight = other.windowHeight;

    // Copy the texture
    this->texture = other.texture;
    this->texture.setSmooth(true);
    // Set the texture to the sprite
    this->sprite.setTexture(this->texture);
    
    // If there is any specific transformation applied to the original sprite, we replicate it
    this->sprite.setPosition(other.sprite.getPosition());
    this->sprite.setScale(other.sprite.getScale());
    this->sprite.setRotation(other.sprite.getRotation());
    this->sprite.setColor(other.sprite.getColor());
}
sf::Vector2i Piece::getPiecePosition(){
    return sf::Vector2(this->x,this->y);
}
void Piece::setGraphicalPositionWhileDragging(int mouseX, int mouseY){
    this->pixelX = mouseX - (sprite.getLocalBounds().width / 3);
    this->pixelY = mouseY - (sprite.getLocalBounds().height / 3);
    this->sprite.setPosition(pixelX, pixelY);
}
int Piece::getPieceType(){
    return this->pieceType;
}
