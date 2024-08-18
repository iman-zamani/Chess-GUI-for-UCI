#include <iostream>
#include <vector>
#include <SFML/Graphics.hpp>
#include "values.hpp"
#include "board.hpp"

board::board(sf::RenderWindow &window,std::string FEN){
    // the board is going to be set for x: WINDOW_WIDTH_RATIO to y: WINDOW_HEIGHT_RATIO windows
    sf::Vector2u size = window.getSize();
    
    int windowWidth = size.x;
    int windowHeight = size.y; 
    int x = windowWidth / WINDOW_WIDTH_RATIO;
    int y = windowHeight / WINDOW_HEIGHT_RATIO;
    
    //----------------------------------------------------------------------------------------
    // loading the texture of light and dark squares and loading the Sprite of each piece base on it  
    sf::IntRect rectLight(0,0,x,y);
     // loading texture from file 
    if (!this->lightSquaresTexture.loadFromFile("./Resources/boardColor.png", rectLight)) {
        throw std::invalid_argument("Error loading texture from file.");
    }
    this->lightSquaresTexture.setSmooth(true);
    sf::IntRect rectDark(240,0,x,y);
     // loading texture from file 
    if (!this->darkSquaresTexture.loadFromFile("./Resources/boardColor.png", rectDark)) {
        throw std::invalid_argument("Error loading texture from file.");
    }

    this->lightSquaresTexture.setSmooth(true);
    this->SpriteSquares.resize(64);
    for (int i=0;i<64;i++){
        if (i%2==0){
            this->SpriteSquares[i].setTexture(this->lightSquaresTexture);
        }
        else {
            this->SpriteSquares[i].setTexture(this->darkSquaresTexture);
        }
    }
    
    //--------------------------------------------------------------------------------------
    // this part is for setting the position of squares 
    int i = x * (WINDOW_WIDTH_RATIO/4);
    int j = y / 2 ;// leave half of a square from top and bottom 
    for (sf::Sprite sp : SpriteSquares){
        sp.setPosition(i,j);
        i += x;
        // we divided the width of the window to three parts the first 1/4 of it is black , from the end of 1/4 to start of 4/4 we will show the board 
        // and the last 1/4 is black as well
        if (i> (x*((WINDOW_WIDTH_RATIO/4) * 3) )){
            j++;
            i = x * (WINDOW_WIDTH_RATIO/4);
        }
    }
    
    // this part is coping the piece object . right now there is no need for writing a custom one myself but if the classes changes over time 
    // this may become necessary  
    i = 0, j = 0;
    piece tempPiece;
    for (char c : FEN) {
        //end of the piece position part of the FEN string 
        if (c == ' '){break;}
        switch (c) {
        case 'p': tempPiece = piece(BLACK_PAWN, i, j); break;
        case 'P': tempPiece = piece(WHITE_PAWN, i, j); break;
        case 'b': tempPiece = piece(BLACK_BISHOP, i, j); break;
        case 'B': tempPiece = piece(WHITE_BISHOP, i, j); break;
        case 'n': tempPiece = piece(BLACK_KNIGHT, i, j); break;
        case 'N': tempPiece = piece(WHITE_KNIGHT, i, j); break;
        case 'r': tempPiece = piece(BLACK_ROOK, i, j); break;
        case 'R': tempPiece = piece(WHITE_ROOK, i, j); break;
        case 'q': tempPiece = piece(BLACK_QUEEN, i, j); break;
        case 'Q': tempPiece = piece(WHITE_QUEEN, i, j); break;
        case 'k': tempPiece = piece(BLACK_KING, i, j); break;
        case 'K': tempPiece = piece(WHITE_KING, i, j); break;
        case '/':
            j++; i = 0;
            continue;
        default:
            continue;
        }
        this->pieces.push_back(tempPiece);
        i++;
    }
    
}

void board::draw(sf::RenderWindow &window){
    for (sf::Sprite sp : SpriteSquares){
        window.draw(sp);
    }
    for (piece pieceIt : pieces){
        pieceIt.draw(window);
    }
    return ;
}

