#include <iostream>
#include <SFML/Graphics.hpp>
#include <optional>
#include<vector>
#include<string>
#include "values.hpp"
#include "piece.hpp"
#include "board.hpp"

void Board::constructor(const std::string &FEN){
    // there is no piece selected at start
    this->pieceSelected = -1;
    // reserve the elements in the pieces vector to avoid comping because of how vector handles memory 
    this->piecesVectorSize = 32;
    this->pieces.reserve(32);
    // no piece is dragging at start 
    this->isDragging = false;
    // allocate the memory for empty texture 
    this->emptyTextureToReturn = new sf::Texture();
    // theres is no piece selected so there are no legal moves as well
    // it is resized to 64 for each square
    this->legalSquaresForTargetPiece.resize(64, false);
    
    
    // the board is going to be set for x: WINDOW_WIDTH_RATIO to y: WINDOW_HEIGHT_RATIO windows
    sf::Vector2u size = this->window.getSize();
    
    int windowWidth = size.x;
    int windowHeight = size.y;
    // we will find the smaller side and we will devid it by the number of squares that we want 
    this->squareSideLength = (windowHeight>windowWidth ? windowWidth : windowHeight) / 9 ;

    
    //////////////////////////////////////////////////////////////////////////////////////////
    // loading the texture of light and dark squares and loading the Sprite of each square base on it  
    sf::IntRect rectLight(0,0,squareSideLength,squareSideLength);
     // loading texture from file 
    if (!this->lightSquaresTexture.loadFromFile("./Resources/boardColor.png", rectLight)) {
        throw std::invalid_argument("Error loading light squares texture from file.");
    }
    this->lightSquaresTexture.setSmooth(true);
    sf::IntRect rectDark(240,0,squareSideLength,squareSideLength);
     // loading texture from file 
    if (!this->darkSquaresTexture.loadFromFile("./Resources/boardColor.png", rectDark)) {
        throw std::invalid_argument("Error loading dark squares texture from file.");
    }

    this->darkSquaresTexture.setSmooth(true);
    this->spriteSquares.resize(64);
    this->setSquaresTexture();

    //////////////////////////////////////////////////////////////////////////////////////////
    // loading the texture for target pieces 
    sf::IntRect rectTargetSquare(0,0,squareSideLength,squareSideLength);
     // loading texture from file 
    if (!this->targetSquaresTexture.loadFromFile("./Resources/targetSquare.png", rectLight)) {
        throw std::invalid_argument("Error loading target square texture from file.");
    }
    targetSquaresTexture.setSmooth(true);
    ////////////////////////////////////////////////////////////////////////////////////////////
    // this part is for setting the position of squares 
    int i = squareSideLength / 2;
    int j = squareSideLength / 2;
    int squareSetPositionCounter = 0;
    for (int k=0;k<64;k++){
        this->spriteSquares[k].setPosition(i,j);
        squareSetPositionCounter++;
        i += squareSideLength;
        // we divided the width of the window to three parts the first 1/4 of it is black , from the end of 1/4 to start of 4/4 we will show the board 
        // and the last 1/4 is black as well
        if (squareSetPositionCounter>=8){
            j+=squareSideLength;
            i = squareSideLength / 2;
            squareSetPositionCounter = 0;
        }
    }
    
    
    // resizing the vector to fit all the squares 
    int piecePositionIndex = 0;
    this->piecePositions.resize(64);
    
    // this part is coping the piece object . right now there is no need for writing a custom one myself but if the classes changes over time 
    // this may become necessary  
    i = 0, j = 0;
    for (char c : FEN) {
        //end of the piece position part of the FEN string 
        if (c == ' '){break;}
        
        switch (c) {
        case 'p': pieces.emplace_back(BLACK_PAWN, i, j, squareSideLength);piecePositions[piecePositionIndex]=BLACK_PAWN; break;
        case 'P': pieces.emplace_back(WHITE_PAWN, i, j, squareSideLength);piecePositions[piecePositionIndex]=WHITE_PAWN; break;
        case 'b': pieces.emplace_back(BLACK_BISHOP, i, j, squareSideLength);piecePositions[piecePositionIndex]=BLACK_BISHOP; break;
        case 'B': pieces.emplace_back(WHITE_BISHOP, i, j, squareSideLength);piecePositions[piecePositionIndex]=WHITE_BISHOP; break;
        case 'n': pieces.emplace_back(BLACK_KNIGHT, i, j, squareSideLength);piecePositions[piecePositionIndex]=BLACK_KNIGHT; break;
        case 'N': pieces.emplace_back(WHITE_KNIGHT, i, j, squareSideLength);piecePositions[piecePositionIndex]=WHITE_KNIGHT; break;
        case 'r': pieces.emplace_back(BLACK_ROOK, i, j, squareSideLength);piecePositions[piecePositionIndex]=BLACK_ROOK; break;
        case 'R': pieces.emplace_back(WHITE_ROOK, i, j, squareSideLength);piecePositions[piecePositionIndex]=WHITE_ROOK; break;
        case 'q': pieces.emplace_back(BLACK_QUEEN, i, j, squareSideLength);piecePositions[piecePositionIndex]=BLACK_QUEEN; break;
        case 'Q': pieces.emplace_back(WHITE_QUEEN, i, j, squareSideLength);piecePositions[piecePositionIndex]=WHITE_QUEEN; break;
        case 'k': pieces.emplace_back(BLACK_KING, i, j, squareSideLength);piecePositions[piecePositionIndex]=BLACK_KING; break;
        case 'K': pieces.emplace_back(WHITE_KING, i, j, squareSideLength);piecePositions[piecePositionIndex]=WHITE_KING; break;
        case '/':
            j++; i = 0;
            continue;
        default:
            i+=c-'0';
            for (int t1=0;t1<(c-'0');t1++){
                piecePositions[piecePositionIndex]=0;
                piecePositionIndex++;
            }
            continue;
        }
        piecePositionIndex++;
        i++;
    }


    // continuing reading the FEN
    //witch side turn it is 
    size_t pos = FEN.find(' ');
    if (FEN[pos+1] == 'w'){
        this->isWhiteTurn = true;
    }
    else {
        this->isWhiteTurn = false;
    }
    
    
    // what type of castling's are available for each side 
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
}
void Board::setSquaresTexture(){
    int H = 0;
        for (int L = 0; L < 8; ++L){
    
            for (int M = 0; M < 8; ++M){
                if ((L + M) % 2 == 1){
                    this->spriteSquares[H].setTexture(this->darkSquaresTexture);
                }
                else {
                    this->spriteSquares[H].setTexture(this->lightSquaresTexture);
                }
                H++;
            }
        }
}

sf::Vector2i Board::squareNameToXY(const std::string &square){
    if (square.size() != 2){throw std::invalid_argument("Error invalid square.");}
    // in the board we store the y in opposite order so we need to ( 8 - value )
    int x = square[0] - 'a';
    int y = 8 - (square[1] - '0');
    return sf::Vector2(x,y);
}
void Board::findLegalMoves(){
    int pieceType = this->pieces[this->pieceSelected].getType();
    switch (pieceType)
    {
    //black pieces 
    case BLACK_ROOK:
        this->findLegalMovesBlackRook();
        break;
    case BLACK_BISHOP:
        this->findLegalMovesBlackBishop();
        break;
    case BLACK_KNIGHT:
        this->findLegalMovesBlackKnight();
        break;
    case BLACK_QUEEN:
        this->findLegalMovesBlackQueen();
        break;
    case BLACK_KING:
        this->findLegalMovesBlackKing();
        break;
    case BLACK_PAWN:
        this->findLegalMovesBlackPawn();
        break;
    // white pieces 
    case WHITE_ROOK:
        this->findLegalMovesWhiteRook();
        break;
    case WHITE_BISHOP:
        this->findLegalMovesWhiteBishop();
        break;
    case WHITE_KNIGHT:
        this->findLegalMovesWhiteKnight();
        break;
    case WHITE_QUEEN:
        this->findLegalMovesWhiteQueen();
        break;
    case WHITE_KING:
        this->findLegalMovesWhiteKing();
        break;
    case WHITE_PAWN:
        this->findLegalMovesWhitePawn();
        break;
    
    default:
        break;
    }
}
// white pieces 
void Board::findLegalMovesWhitePawn(){
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos =  this->pieces[this->pieceSelected].getPosition();
   
    // square in front of the pawn
    int squareInFrontOfPawn = ((pos.y-1) * 8) + pos.x;
    if (piecePositions[squareInFrontOfPawn] == 0){
        legalSquaresForTargetPiece[squareInFrontOfPawn] = true;
        // if it is in staring position 
         if (pos.y == 6){
            int twoSquaresInFrontOfPawn = ((pos.y-2) * 8) + pos.x;
            // if two squares in front of it are free
            if (piecePositions[twoSquaresInFrontOfPawn] == 0){
                legalSquaresForTargetPiece[twoSquaresInFrontOfPawn] = true;
            }
        }
    }
    // if captures available 
    // is the square exists and there is an enemy piece there 
    if (squareInFrontOfPawn%8 == 0 and piecePositions[squareInFrontOfPawn+1]<0){
        legalSquaresForTargetPiece[squareInFrontOfPawn+1] = true;
    }
    else if(squareInFrontOfPawn % 8 == 7 and piecePositions[squareInFrontOfPawn-1]<0){
        legalSquaresForTargetPiece[squareInFrontOfPawn-1] = true;
    }
    else {
        if(piecePositions[squareInFrontOfPawn+1]<0)legalSquaresForTargetPiece[squareInFrontOfPawn+1] = true;
        if(piecePositions[squareInFrontOfPawn-1]<0)legalSquaresForTargetPiece[squareInFrontOfPawn-1] = true;
    }
    return ;
}

void Board::findLegalMovesWhiteKnight() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPosition();
    std::vector<sf::Vector2i> moves = {
        {pos.x + 2, pos.y + 1}, {pos.x + 2, pos.y - 1},
        {pos.x - 2, pos.y + 1}, {pos.x - 2, pos.y - 1},
        {pos.x + 1, pos.y + 2}, {pos.x + 1, pos.y - 2},
        {pos.x - 1, pos.y + 2}, {pos.x - 1, pos.y - 2}
    };

    for (const auto& move : moves) {
        if (move.x >= 0 && move.x < 8 && move.y >= 0 && move.y < 8) {
            int index = move.y * 8 + move.x;
            if (piecePositions[index] <= 0) { 
                legalSquaresForTargetPiece[index] = true;
            }
        }
    }
    return;
}

void Board::findLegalMovesWhiteBishop() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPosition();
    std::vector<sf::Vector2i> directions = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    for (const auto& dir : directions) {
        sf::Vector2i newPos = pos;
        while (true) {
            newPos += dir;
            if (newPos.x < 0 || newPos.x >= 8 || newPos.y < 0 || newPos.y >= 8)
                break;
            int index = newPos.y * 8 + newPos.x;
            if (piecePositions[index] > 0)
                break;
            legalSquaresForTargetPiece[index] = true;
            if (piecePositions[index] < 0)
                break;
        }
    }
}

void Board::findLegalMovesWhiteRook() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPosition();
    std::vector<sf::Vector2i> directions = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    for (const auto& dir : directions) {
        sf::Vector2i newPos = pos;
        while (true) {
            newPos += dir;
            if (newPos.x < 0 || newPos.x >= 8 || newPos.y < 0 || newPos.y >= 8)
                break;
            int index = newPos.y * 8 + newPos.x;
            if (piecePositions[index] > 0)
                break;
            legalSquaresForTargetPiece[index] = true;
            if (piecePositions[index] < 0)
                break;
        }
    }
}

void Board::findLegalMovesWhiteQueen() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPosition();
    std::vector<sf::Vector2i> directions = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}, // rook moves
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1} // bishop moves
    };

    for (const auto& dir : directions) {
        sf::Vector2i newPos = pos;
        while (true) {
            newPos += dir;
            if (newPos.x < 0 || newPos.x >= 8 || newPos.y < 0 || newPos.y >= 8)
                break;
            int index = newPos.y * 8 + newPos.x;
            if (piecePositions[index] > 0) // blocked by white piece
                break;
            legalSquaresForTargetPiece[index] = true;
            if (piecePositions[index] < 0) // capture black piece and stop
                break;
        }
    }
}

void Board::findLegalMovesWhiteKing() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPosition();
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx == 0 && dy == 0) continue;
            sf::Vector2i newPos = pos + sf::Vector2i(dx, dy);
            if (newPos.x >= 0 && newPos.x < 8 && newPos.y >= 0 && newPos.y < 8) {
                int index = newPos.y * 8 + newPos.x;
                if (piecePositions[index] <= 0)
                    legalSquaresForTargetPiece[index] = true;
            }
        }
    }
}

// black pieces 
void Board::findLegalMovesBlackPawn(){
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos =  this->pieces[this->pieceSelected].getPosition();
    // square in front of the pawn
    int squareInFrontOfPawn = ((pos.y+1) * 8) + pos.x;
    if (piecePositions[squareInFrontOfPawn] == 0){
        legalSquaresForTargetPiece[squareInFrontOfPawn] = true;
        // if it is in staring position 
         if (pos.y == 1){
            int twoSquaresInFrontOfPawn = ((pos.y+2) * 8) + pos.x;
            // if two squares in front of it are free
            if (piecePositions[twoSquaresInFrontOfPawn] == 0){
                legalSquaresForTargetPiece[twoSquaresInFrontOfPawn] = true;
            }
        }
    }
    // if captures available 
    // is the square exists and there is an enemy piece there 
    if (squareInFrontOfPawn%8 == 0 and piecePositions[squareInFrontOfPawn+1]>0){
        legalSquaresForTargetPiece[squareInFrontOfPawn+1] = true;
    }
    else if(squareInFrontOfPawn % 8 == 7 and piecePositions[squareInFrontOfPawn-1]>0){
        legalSquaresForTargetPiece[squareInFrontOfPawn-1] = true;
    }
    else {
        if(piecePositions[squareInFrontOfPawn+1]>0)legalSquaresForTargetPiece[squareInFrontOfPawn+1] = true;
        if(piecePositions[squareInFrontOfPawn-1]>0)legalSquaresForTargetPiece[squareInFrontOfPawn-1] = true;
    }
    return ;
}

void Board::findLegalMovesBlackRook() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPosition();
    std::vector<sf::Vector2i> directions = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    for (const auto& dir : directions) {
        sf::Vector2i newPos = pos;
        while (true) {
            newPos += dir;
            if (newPos.x < 0 || newPos.x >= 8 || newPos.y < 0 || newPos.y >= 8)
                break;
            int index = newPos.y * 8 + newPos.x;
            if (piecePositions[index] < 0)
                break;
            legalSquaresForTargetPiece[index] = true;
            if (piecePositions[index] > 0)
                break;
        }
    }
}

void Board::findLegalMovesBlackKnight() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPosition();
    std::vector<sf::Vector2i> moves = {
        {pos.x + 2, pos.y + 1}, {pos.x + 2, pos.y - 1},
        {pos.x - 2, pos.y + 1}, {pos.x - 2, pos.y - 1},
        {pos.x + 1, pos.y + 2}, {pos.x + 1, pos.y - 2},
        {pos.x - 1, pos.y + 2}, {pos.x - 1, pos.y - 2}
    };

    for (const auto& move : moves) {
        if (move.x >= 0 && move.x < 8 && move.y >= 0 && move.y < 8) {
            int index = move.y * 8 + move.x;
            if (piecePositions[index] >= 0) { // assuming 0 is empty and positive numbers are white pieces
                legalSquaresForTargetPiece[index] = true;
            }
        }
    }
}

void Board::findLegalMovesBlackQueen() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPosition();
    std::vector<sf::Vector2i> directions = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}, // rook moves
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1} // bishop moves
    };

    for (const auto& dir : directions) {
        sf::Vector2i newPos = pos;
        while (true) {
            newPos += dir;
            if (newPos.x < 0 || newPos.x >= 8 || newPos.y < 0 || newPos.y >= 8)
                break;
            int index = newPos.y * 8 + newPos.x;
            if (piecePositions[index] < 0) // blocked by black piece
                break;
            legalSquaresForTargetPiece[index] = true;
            if (piecePositions[index] > 0) // capture white piece and stop
                break;
        }
    }
}

void Board::findLegalMovesBlackBishop() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPosition();
    std::vector<sf::Vector2i> directions = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    for (const auto& dir : directions) {
        sf::Vector2i newPos = pos;
        while (true) {
            newPos += dir;
            if (newPos.x < 0 || newPos.x >= 8 || newPos.y < 0 || newPos.y >= 8)
                break;
            int index = newPos.y * 8 + newPos.x;
            if (piecePositions[index] < 0)
                break;
            legalSquaresForTargetPiece[index] = true;
            if (piecePositions[index] > 0)
                break;
        }
    }
}

void Board::findLegalMovesBlackKing() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPosition();
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx == 0 && dy == 0) continue;
            sf::Vector2i newPos = pos + sf::Vector2i(dx, dy);
            if (newPos.x >= 0 && newPos.x < 8 && newPos.y >= 0 && newPos.y < 8) {
                int index = newPos.y * 8 + newPos.x;
                if (piecePositions[index] >= 0)
                    legalSquaresForTargetPiece[index] = true;
            }
        }
    }
}

// we should call this on the start of the dragging 
// it will return the texture of the pice user is trying to drag 
sf::Texture* Board::startDragging(sf::Vector2f clickPos) {
    //temp fix, this is not optimized way to find the piece
    for (int i = 0; i < this->piecesVectorSize; i++) {
        sf::Vector2f piecePosF(static_cast<float>(this->pieces[i].getPosition().x),static_cast<float>(this->pieces[i].getPosition().y));

        float absDiffX = clickPos.x - piecePosF.x;
        float absDiffY = clickPos.y - piecePosF.y;
        if (absDiffX <0 || absDiffY < 0){continue;}
        if (absDiffX < squareSideLength && absDiffY < squareSideLength) {
            pieceSelected = i;
            this->pieces[pieceSelected].selectPiece();
            return this->pieces[pieceSelected].getTexture();
        }
    }
    // reset the selected piece 
    pieceSelected = -1;
    return emptyTextureToReturn;
}

// we should call this when the dragging ends  
void Board::endDragging(sf::Vector2f clickPos) {
    // if there is no piece selected to release it 
    if (pieceSelected < 0 || pieceSelected >= piecesVectorSize){
        return;
    }
    // piece is released so we should deselect it no matter if we need to apply a move as well or not 
    this->pieces[pieceSelected].deselectPiece();
    // check if the clicked position is in the board range 
    {
        int upAndLeft = squareSideLength / 2;
        int downAndRight = upAndLeft + (8*squareSideLength);
        if (clickPos.x < upAndLeft || clickPos.y < upAndLeft || clickPos.x > downAndRight || clickPos.y > downAndRight){
            return;
        }
    }
    int selectedPieceType = this->pieces[pieceSelected].getType();
    // we should find which square user is pointing to
    float pointedSquareCoordinateX = clickPos.x - (squareSideLength / 2);
    float pointedSquareCoordinateY = clickPos.y - (squareSideLength / 2);
    int pointedSquareX = pointedSquareCoordinateX / squareSideLength;
    int pointedSquareY = pointedSquareCoordinateY / squareSideLength;
    // we should find if this is a capture
    //temp fix, this is not optimized way to find the piece
    for (int i = 0; i < this->piecesVectorSize; i++) {
        
        sf::Vector2f piecePosF(static_cast<float>(this->pieces[i].getPosition().x),static_cast<float>(this->pieces[i].getPosition().y));

        float absDiffX = clickPos.x - piecePosF.x;
        float absDiffY = clickPos.y - piecePosF.y;
        if (absDiffX <0 || absDiffY < 0){continue;}
        if ((absDiffX >= 0 || absDiffY >= 0) && (absDiffX < squareSideLength && absDiffY < squareSideLength)) {
            // we will skip the pieces with the same color, if they are the same color they will be both negative or positive
            // so if we multiply them together the result will be positive 
            if ((this->pieces[i].getType()*selectedPieceType) > 0){continue;}
            // move the selected piece 
            this->pieces[pieceSelected].moveTo(pointedSquareX,pointedSquareY);
            // delete the captured piece 
            this->pieces.erase(this->pieces.begin() + i);
            // reset the selected piece tracker 
            this->piecesVectorSize--;
            pieceSelected = -1;
            return ;
        }
    }
    // it is not a capture 
    // the user is pointing to an empty square
    this->pieces[pieceSelected].moveTo(pointedSquareX,pointedSquareY);
    return;
}
const sf::Vector2f Board::getSelectedPieceSpriteScale()const{
    if (pieceSelected < 0 || pieceSelected >= piecesVectorSize){
        sf::Vector2f tempVector(0, 0);
        return tempVector;
    }
    const float pieceScale = this->pieces[pieceSelected].getScale();
    sf::Vector2f tempVector(pieceScale, pieceScale);
    return tempVector;
}
void Board::placeThePiece(sf::Vector2f clickPos){
    // To Do
}
