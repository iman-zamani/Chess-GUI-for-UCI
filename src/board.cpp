#include <iostream>
#include <vector>
#include <SFML/Graphics.hpp>
#include "values.hpp"
#include "board.hpp"

Board::Board(sf::RenderWindow &window,const std::string &FEN){
    // there is no piece selected at start
    this->pieceSelected = -1;
    // no piece is dragging at start 
    this->isDragging = false;
    // the board is going to be set for x: WINDOW_WIDTH_RATIO to y: WINDOW_HEIGHT_RATIO windows
    sf::Vector2u size = window.getSize();
    
    int windowWidth = size.x;
    int windowHeight = size.y; 
    // int windowWidth = 3840;
    // int windowHeight = 2160; 
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
    int H = 0;
    for (int L = 0; L < 8; ++L)
    {

        for (int M = 0; M < 8; ++M)
        {
            if ((L + M) % 2 == 1){
                this->SpriteSquares[H].setTexture(this->darkSquaresTexture);
            }
            else {
                this->SpriteSquares[H].setTexture(this->lightSquaresTexture);
            }
            H++;
        }
    }
    //--------------------------------------------------------------------------------------
    // this part is for setting the position of squares 
    int i = x * (WINDOW_WIDTH_RATIO/4);
    int j = y / 2 ;// leave half of a square from top and bottom 
    for (int k=0;k<64;k++){
        SpriteSquares[k].setPosition(i,j);
        i += x;
        // we divided the width of the window to three parts the first 1/4 of it is black , from the end of 1/4 to start of 4/4 we will show the board 
        // and the last 1/4 is black as well
        if (i>= (x*((WINDOW_WIDTH_RATIO/4) * 3) )){
            j+=y;
            i = x * (WINDOW_WIDTH_RATIO/4);
        }
    }
    
    // this part is coping the piece object . right now there is no need for writing a custom one myself but if the classes changes over time 
    // this may become necessary  
    i = 0, j = 0;
    for (char c : FEN) {
        //end of the piece position part of the FEN string 
        if (c == ' '){break;}
        switch (c) {
        case 'p': pieces.emplace_back(BLACK_PAWN, i, j); break;
        case 'P': pieces.emplace_back(WHITE_PAWN, i, j); break;
        case 'b': pieces.emplace_back(BLACK_BISHOP, i, j); break;
        case 'B': pieces.emplace_back(WHITE_BISHOP, i, j); break;
        case 'n': pieces.emplace_back(BLACK_KNIGHT, i, j); break;
        case 'N': pieces.emplace_back(WHITE_KNIGHT, i, j); break;
        case 'r': pieces.emplace_back(BLACK_ROOK, i, j); break;
        case 'R': pieces.emplace_back(WHITE_ROOK, i, j); break;
        case 'q': pieces.emplace_back(BLACK_QUEEN, i, j); break;
        case 'Q': pieces.emplace_back(WHITE_QUEEN, i, j); break;
        case 'k': pieces.emplace_back(BLACK_KING, i, j); break;
        case 'K': pieces.emplace_back(WHITE_KING, i, j); break;
        case '/':
            j++; i = 0;
            continue;
        default:
            i+=c-'0';
            continue;
        }
        
        i++;
    }
    size_t pos = FEN.find(' ');
    if (FEN[pos+1] == 'w'){
        this->isWhiteTurn = true;
    }
    else {
        this->isWhiteTurn = false;
    }
    pos+=3;
    this-> whiteKingSideCastle = false;
    this-> whiteQueenSideCastle = false;
    this-> blackKingSideCastle = false;
    this-> blackQueenSideCastle = false;
    while(FEN[pos]!=' '){
        switch (FEN[pos])
        {
        case 'K':
            this-> whiteKingSideCastle = true;
            break;
        case 'Q':
            this-> whiteQueenSideCastle = true;
            break;
        case 'k':
            this-> blackKingSideCastle = true;
            break;
        case 'q':
            this-> blackQueenSideCastle = true;
            break;
        case '-':
            break;
        default:
        throw std::invalid_argument("Error invalid FEN.");
            break;
        }
        pos++;
    }
    // if En passant is available or not 
    pos++;
    if (FEN[pos] == '-'){
        this->enPassantX = -1, this-> enPassantY = -1;
    }
    else {
        std::string temp(FEN, pos, 2);
        sf::Vector2i square = this->squareNameToXY(temp);
        this->enPassantX = square.x , this->enPassantY = square.y;
    }
    // the number of half moves from when last capture or pawn move happened 
    pos = FEN.find(' ', pos) + 1;
    int endPos = FEN.find(' ', pos);
    std::string t1(FEN, pos, endPos - pos);
    this->halfMovesFromLastCaptureOrPawnMove = std::stoi(t1);
    // the number of moves happened in  game. starts from 1 and after each black move it will be increased
    std::string t2;
    pos = endPos;
    endPos = FEN.size();
    for (int i=pos+1;i<endPos;i++){
        t2 += FEN[i];
    }
    this->moveNumber = std::stoi(t2);
    // this part only for checking if the FEN is loaded correctly 
    this->printBoardState();

}
// Method to print the current state of the board
void Board::printBoardState() {
    std::cout << "Current Board State:\n";
    std::cout << "Piece Selected: " << (pieceSelected == -1 ? "None" : std::to_string(pieceSelected)) << std::endl;
    std::cout << "Is Dragging: " << (isDragging ? "Yes" : "No") << std::endl;
    std::cout << "Is White's Turn: " << (isWhiteTurn ? "Yes" : "No") << std::endl;
    std::cout << "White King-side Castle: " << (whiteKingSideCastle ? "Available" : "Not available") << std::endl;
    std::cout << "White Queen-side Castle: " << (whiteQueenSideCastle ? "Available" : "Not available") << std::endl;
    std::cout << "Black King-side Castle: " << (blackKingSideCastle ? "Available" : "Not available") << std::endl;
    std::cout << "Black Queen-side Castle: " << (blackQueenSideCastle ? "Available" : "Not available") << std::endl;
    std::cout << "En Passant Target Square: " << (enPassantX == -1 && enPassantY == -1 ? "None" : std::to_string(enPassantX) + ", " + std::to_string(enPassantY)) << std::endl;
    std::cout << "Half Moves Since Last Capture Or Pawn Move: " << halfMovesFromLastCaptureOrPawnMove << std::endl;
    std::cout << "Move Number: " << moveNumber << std::endl;
}

void Board::draw(sf::RenderWindow &window){
    
    for (sf::Sprite sp : SpriteSquares){
        window.draw(sp);
    }
    if(isDragging && this->pieceSelected != -1){
        // the position of the mouse
        sf::Vector2i position = sf::Mouse::getPosition(window);

        this->pieces[this->pieceSelected].setGraphicalPositionWhileDragging(position.x,position.y);
    }
    int n = pieces.size();
    for (int i=0;i<n;i++){
        this->pieces[i].draw(window);
    }
    return ;
}
// this is for selecting a piece so so the legal moves for that piece gets displayed 
void Board::selectTargetPiece(sf::RenderWindow &window, int mouseX, int mouseY){
    sf::Vector2u size = window.getSize();
    int chunksWidth = size.x / WINDOW_WIDTH_RATIO;
    int chunksHeight = size.y / WINDOW_HEIGHT_RATIO;
    // converting pixel position to board indexes 
    int x = (mouseX - (chunksWidth*4) )/ (chunksWidth);
    int y = (mouseY - (chunksHeight/2)) / chunksHeight;


    int n = pieces.size();
    for (int i=0;i<n;i++){
        
        if (pieces[i].getPiecePosition().x == x and pieces[i].getPiecePosition().y == y ){
            // ther is no need to declare a new variable 
            // the n variable is not going to be used ever again  
            n = i;
        }
    }
    // this will save the index of the selected piece 
    // and if the user clicked on a place with no piece it 
    //  will deselect the selected piece 
    if (n == pieces.size()){
        this->pieceSelected -1;
        return ;
    }
    else {
        this->pieceSelected = n;
        return ;
    } 
}
// if a piece in board is getting dragged (in the moment)
bool Board::getIsPieceDragging(){
    return isDragging;
}

void Board::setIsPieceDragging(bool setIsPieceDragging){
    this->isDragging = setIsPieceDragging;
}


sf::Vector2i Board::squareNameToXY(const std::string &square){
    if (square.size() != 2){throw std::invalid_argument("Error invalid square.");}
    // in the board we store the y in opposite order so we need to ( 8 - value )
    int x = square[0] - 'a';
    int y = 8 - (square[1] - '0');
    return sf::Vector2(x,y);
}