#include <iostream>
#include <SFML/Graphics.hpp>
#include <cmath>
#include "piece.hpp"

// set position of the sprite using a vector
void Piece::setPosition(const sf::Vector2f& position) {
    sprite.setPosition(position);
}
// set the position of the sprites
void Piece::setPosition(float x, float y) {
    pixelX = x;
    pixelY = y;
    sprite.setPosition(x, y);
    ghostSprite.setPosition(x,y);
    return;
}

int Piece::getType(){
    return this->pieceType;
}
sf::Vector2i Piece::getPosition(){
    return sf::Vector2i(this->pixelX,this->pixelY);
}

// it will take the type of the piece that we want to create and it's position in the board 
Piece::Piece (int pieceType ,int x , int y, int squareSideLength){
    this->squareSide = squareSideLength;
    this -> pieceDimensions = 150;
    // set x and y 
    // check if it is in the boundaries of the board 
    if (x<8 && y < 8 && x > -1 && y > -1){
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
    
    sf::IntRect rect(i * 150,j * 150,150,150);// (left, top, width, height)
     // loading texture from file 
    this->texture = new sf::Texture();
    if (!this->texture->loadFromFile("./Resources/pieceTexture.png", rect)) {
        throw std::invalid_argument("Error loading texture from file.");
    }
    
    this->texture->setSmooth(true);
    this->sprite.setTexture(*texture);
    this->scale = static_cast<float>(squareSide) / 150;
    this->sprite.setScale(this->scale,this->scale);
    this->isDragging = false ;
    this->setPosition(x*squareSide + (squareSide/2),y*squareSide + (squareSide/2));
    // create the ghost version of the piece 
    this->ghostSprite = this->sprite;
    ghostSprite.setColor(sf::Color(255, 255, 255, 100));
    // set the piece as not selected for dragging 
    this->isDragging = false;
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
    this->scale = other.scale;
    this->squareSide = other.squareSide;
    this->pieceDimensions = other.pieceDimensions;

    // deep copy of texture
    this->texture = new sf::Texture(*other.texture);

    // apply texture to sprite
    this->sprite.setTexture(*this->texture);
    this->sprite.setPosition(other.sprite.getPosition());
    this->sprite.setScale(other.sprite.getScale());
    this->sprite.setRotation(other.sprite.getRotation());
    this->sprite.setColor(other.sprite.getColor());

    // for the ghost sprite
    this->ghostSprite = this->sprite;
    this->ghostSprite.setColor(sf::Color(255, 255, 255, 100));
}
Piece::~Piece() {
    texture = nullptr;
}
sf::Texture* Piece::getTexture() {
    
    return texture;
}
void Piece::selectPiece(){
    // we should change the texture to the ghost one showing to the user this piece is selected
    this->isDragging = true;
}
void Piece::deselectPiece(){
    // we should change the texture to the real one showing to the user this piece is deselected
    this->isDragging = false;
}
const float Piece::getScale()const{
    return this->scale;
}
bool Piece::moveTo(int destX, int destY){
    if (destX > 7 || destX < 0 || destY > 7 || destY < 0){
        return false;
    }
    this->x = destX;
    this->y = destY;
    this->pixelX = x*squareSide + (squareSide/2);
    this-> pixelY = y*squareSide + (squareSide/2);
    this->setPosition(pixelX,pixelY);
    return true;
}