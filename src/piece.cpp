#include <iostream>
#include <SFML/Graphics.hpp>
#include <cmath>
#include "piece.hpp"

// return a const reference to the sprite for drawing
const sf::Sprite& Piece::getSprite()const{
    return sprite;
}
// set position of the sprite using a vector
void Piece::setPosition(const sf::Vector2f& position) {
    sprite.setPosition(position);
}
// set the position of the sprite
void Piece::setPosition(float x, float y) {
    pixelX = x;
    pixelY = y;
    sprite.setPosition(x, y);
}

int Piece::getType(){
    return this->pieceType;
}
sf::Vector2i Piece::getPosition(){
    return sf::Vector2(this->x,this->y);
}