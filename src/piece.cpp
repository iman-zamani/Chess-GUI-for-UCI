#include <SFML/Graphics.hpp>
#include "values.hpp"
#include "piece.hpp"
#include <cmath>


// it will take the type of the piece that we want to create and it's position in the board 
piece::piece (int pieceType ,int x , int y){
    // set x and y 
    // is it in the boundaries of the board 
    if (x<8 and y < 8 and x > -1 and y > -1){
        this->x = x;
        this->y = y;
    }
    else {
        throw std::invalid_argument("x and y values must be greater than zero and smaller than 8.");
    }
    //set pieceType
    // if it is a valid piece type base on pieceValues.hpp file 
    if (abs(pieceType) == 1 || abs(pieceType) == 3 || abs(pieceType) == 4 || abs(pieceType) == 5 || abs(pieceType) == 9 || abs(pieceType) == 20){
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
    switch (abs(pieceType))
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
    sf::IntRect rect(i * 333,j * 333,333,333);
     // loading texture from file 
    if (!this->texture.loadFromFile("./Resources/piceTexture.png", rect)) {
        throw std::invalid_argument("Error loading texture from file.");
    }
    this->texture.setSmooth(true);
    this->sprite.setTexture(this->texture);
    this->sprite.setScale(pieceToBoardScale,pieceToBoardScale);
    this->isDragging = false ;
    
}
void piece::updateGraphicalPosition(int windowWidth , int windowHeight){
    int chunksWidth = windowWidth / 16;
    int chunksHeight = windowHeight / 9;
    this->pixelX = (this->x * chunksWidth ) + chunksWidth * 4;
    this->pixelY = (this->y * chunksHeight) + (chunksHeight/2);
    return ;
}

void piece::draw(sf::RenderWindow &window){
    sf::Vector2u size = window.getSize();
    int windowWidth = size.x;
    int windowHeight = size.y;
    if (this->windowWidth != windowWidth || this->windowHeight != windowHeight){
        this->updateGraphicalPosition(windowWidth,windowHeight);
    }
    this->sprite.setPosition(pixelX,pixelY);
    window.draw(this->sprite);
}