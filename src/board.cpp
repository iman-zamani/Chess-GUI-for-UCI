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


    // theres is no piece selected so there are no legal moves as well
    // it is resized to 64 for each square
    this->legalSquaresForTargetPiece.resize(64, false);
    
    
    // the board is going to be set for x: WINDOW_WIDTH_RATIO to y: WINDOW_HEIGHT_RATIO windows
    sf::Vector2u size = window.getSize();
    
    int windowWidth = size.x;
    int windowHeight = size.y; 
    // int windowWidth = 3840;
    // int windowHeight = 2160; 
    int x = windowWidth / WINDOW_WIDTH_RATIO;
    int y = windowHeight / WINDOW_HEIGHT_RATIO;
    
    
    //////////////////////////////////////////////////////////////////////////////////////////
    // loading the texture of light and dark squares and loading the Sprite of each piece base on it  
    sf::IntRect rectLight(0,0,x,y);
     // loading texture from file 
    if (!this->lightSquaresTexture.loadFromFile("./Resources/boardColor.png", rectLight)) {
        throw std::invalid_argument("Error loading light squares texture from file.");
    }
    this->lightSquaresTexture.setSmooth(true);
    sf::IntRect rectDark(240,0,x,y);
     // loading texture from file 
    if (!this->darkSquaresTexture.loadFromFile("./Resources/boardColor.png", rectDark)) {
        throw std::invalid_argument("Error loading dark squares texture from file.");
    }

    this->lightSquaresTexture.setSmooth(true);
    this->spriteSquares.resize(64);
    this->squareTextureSetToNormal();

    //////////////////////////////////////////////////////////////////////////////////////////
    // loading the texture for target pieces 
    sf::IntRect rectTargetSquare(0,0,x,y);
     // loading texture from file 
    if (!this->targetSquaresTexture.loadFromFile("./Resources/targetSquare.png", rectLight)) {
        throw std::invalid_argument("Error loading target square texture from file.");
    }
    targetSquaresTexture.setSmooth(true);
    ////////////////////////////////////////////////////////////////////////////////////////////
    // this part is for setting the position of squares 
    int i = x * (WINDOW_WIDTH_RATIO/4);
    int j = y / 2 ;// leave half of a square from top and bottom 
    for (int k=0;k<64;k++){
        this->spriteSquares[k].setPosition(i,j);
        i += x;
        // we divided the width of the window to three parts the first 1/4 of it is black , from the end of 1/4 to start of 4/4 we will show the board 
        // and the last 1/4 is black as well
        if (i>= (x*((WINDOW_WIDTH_RATIO/4) * 3) )){
            j+=y;
            i = x * (WINDOW_WIDTH_RATIO/4);
        }
    }
    
    
    // resizing the vector to fit all the squares 
    int piecePositionIndex = 0;
    this->piecePositions.resize(64);
    squareTextureSetToNormal();
    
    // this part is coping the piece object . right now there is no need for writing a custom one myself but if the classes changes over time 
    // this may become necessary  
    i = 0, j = 0;
    for (char c : FEN) {
        //end of the piece position part of the FEN string 
        if (c == ' '){break;}
        switch (c) {
        case 'p': pieces.emplace_back(BLACK_PAWN, i, j);piecePositions[piecePositionIndex]=BLACK_PAWN; break;
        case 'P': pieces.emplace_back(WHITE_PAWN, i, j);piecePositions[piecePositionIndex]=WHITE_PAWN; break;
        case 'b': pieces.emplace_back(BLACK_BISHOP, i, j);piecePositions[piecePositionIndex]=BLACK_BISHOP; break;
        case 'B': pieces.emplace_back(WHITE_BISHOP, i, j);piecePositions[piecePositionIndex]=WHITE_BISHOP; break;
        case 'n': pieces.emplace_back(BLACK_KNIGHT, i, j);piecePositions[piecePositionIndex]=BLACK_KNIGHT; break;
        case 'N': pieces.emplace_back(WHITE_KNIGHT, i, j);piecePositions[piecePositionIndex]=WHITE_KNIGHT; break;
        case 'r': pieces.emplace_back(BLACK_ROOK, i, j);piecePositions[piecePositionIndex]=BLACK_ROOK; break;
        case 'R': pieces.emplace_back(WHITE_ROOK, i, j);piecePositions[piecePositionIndex]=WHITE_ROOK; break;
        case 'q': pieces.emplace_back(BLACK_QUEEN, i, j);piecePositions[piecePositionIndex]=BLACK_QUEEN; break;
        case 'Q': pieces.emplace_back(WHITE_QUEEN, i, j);piecePositions[piecePositionIndex]=WHITE_QUEEN; break;
        case 'k': pieces.emplace_back(BLACK_KING, i, j);piecePositions[piecePositionIndex]=BLACK_KING; break;
        case 'K': pieces.emplace_back(WHITE_KING, i, j);piecePositions[piecePositionIndex]=WHITE_KING; break;
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
   
   
    // this part only for checking if the FEN is loaded correctly 
    this->printBoardState();

}
// set texture for sprite of each square to normal 
void Board::squareTextureSetToNormal(){
int H = 0;
    for (int L = 0; L < 8; ++L)
    {

        for (int M = 0; M < 8; ++M)
        {
            if ((L + M) % 2 == 1){
                this->spriteSquares[H].setTexture(this->darkSquaresTexture);
            }
            else {
                this->spriteSquares[H].setTexture(this->lightSquaresTexture);
            }
            H++;
        }
    }}
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
    
    // if(isDragging && this->pieceSelected != -1){
    //     // the position of the mouse
    //     sf::Vector2i position = sf::Mouse::getPosition(window);

    //     this->pieces[this->pieceSelected].setGraphicalPositionWhileDragging(position.x,position.y);
        
    // }
    // else {
    //     this->squareTextureSetToNormal();
    // }
    this->squareTextureSetToNormal();

    if (this->pieceSelected != -1){
        for (int i=0;i<64;i++){
            if (legalSquaresForTargetPiece[i]){spriteSquares[i].setTexture(targetSquaresTexture);}
        }
    }

    for (sf::Sprite sp : spriteSquares){
        window.draw(sp);
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

    if (this->legalSquaresForTargetPiece[x+y*8]) {
        // there is no need to select a new piece 
        // because the selected square can be a target square for the 
        // previous selected piece 
        return ;
    }
    int n = pieces.size();
    for (int i=0;i<n;i++){
        
        if (pieces[i].getPiecePosition().x == x and pieces[i].getPiecePosition().y == y ){
            // ther is no need to declare a new variable 
            // the n variable is not going to be used ever again  
            n = i;
            break;
        }
    }
    
    // this will save the index of the selected piece 
    // and if the user clicked on a place with no piece it 
    //  will deselect the selected piece unless the old selected piece 
    //  can move to that square 
    if (n == (int)pieces.size()){
        if (!this->legalSquaresForTargetPiece[x+y*8]) {this->pieceSelected = -1;}
        return ;
    }
    else {
        this->pieceSelected = n;
        this->findLegalMoves();
        return ;
    } 
}
// if a piece in board is getting dragged (in the moment)
bool Board::getIsPieceDragging(){
    return isDragging;
}

void Board::setIsPieceDragging(bool setIsPieceDragging,int mouseX,int mouseY){
    this->isDragging = setIsPieceDragging;
    if (!setIsPieceDragging && pieceSelected!=-1){
        //this will set the graphical position of the piece and coordinations of the piece 
        sf::Vector2i oldPosPieceSelected =  this->pieces[this->pieceSelected].getPiecePosition();
        // set the starting square of the piece to empty 
        piecePositions[oldPosPieceSelected.x + (oldPosPieceSelected.y*8)] = 0;
        this->pieces[this->pieceSelected].draggingReleased(mouseX,mouseY);
        //this will check if it is a capture 
        // and if so deletes the other piece
        sf::Vector2i posPieceSelected =  this->pieces[this->pieceSelected].getPiecePosition();
        //set the target square to the correct piece type 
        piecePositions[posPieceSelected.x + (posPieceSelected.y*8)] = this->pieces[this->pieceSelected].getPieceType();
        int n = pieces.size();
        for (int i=0;i<n;i++){
            if (i == this->pieceSelected){continue;}
            sf::Vector2i pos= this->pieces[i].getPiecePosition();
            if (pos.x == posPieceSelected.x && pos.y == posPieceSelected.y){
               std::vector<Piece> copyVector1(pieces);
               pieces.resize(i);
               for (int j=i+1;j<n;j++){
                pieces.push_back(copyVector1[j]);
               }
               pieceSelected = -1;
                break;
            }
        }
    }
    return ;
}

void Board::dragPiece(int mouseX,int mouseY){
    // if there is a piece selected 
    // set the position of it to the mouse position
    if (pieceSelected != -1){
        pieces[pieceSelected].setGraphicalPositionWhileDragging(mouseX,mouseY);
    }  
    return ;
}

void Board::placePiece(int mouseX,int mouseY){
    if (pieceSelected != -1){
        sf::Vector2i start = pieces[pieceSelected].getPiecePosition();
        pieces[pieceSelected].draggingReleased(mouseX,mouseY);
        sf::Vector2i end = pieces[pieceSelected].getPiecePosition();
        
        if ((start.x != end.x ) || (start.y != end.y)){ 
            applyMove((int)start.x,(int)start.y,(int)end.x,(int)end.y);
            std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
            }
    }
    return ;
}

// it will get the square name in string like (f6 or d2) and it will convert it to x and y 
sf::Vector2i Board::squareNameToXY(const std::string &square){
    if (square.size() != 2){throw std::invalid_argument("Error invalid square.");}
    // in the board we store the y in opposite order so we need to ( 8 - value )
    int x = square[0] - 'a';
    int y = 8 - (square[1] - '0');
    return sf::Vector2(x,y);
}

// saving the legal moves that can be made with the given piece (piece index)
void Board::findLegalMoves(){
    int pieceType = this->pieces[this->pieceSelected].getPieceType();
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
    sf::Vector2i pos =  this->pieces[this->pieceSelected].getPiecePosition();
   
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPiecePosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPiecePosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPiecePosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPiecePosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPiecePosition();
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
    sf::Vector2i pos =  this->pieces[this->pieceSelected].getPiecePosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPiecePosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPiecePosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPiecePosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPiecePosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getPiecePosition();
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

// this function will only apply the move and the integrity of the move should be checked before passing to this function 
// this function will update the board class in all of the aspects 
bool Board::applyMove(int startX,int startY,int endX,int endY){
     //std::cout<<"start X: "<<startX<<" Y: "<<startY<<std::endl;
      //std::cout<<"end X: "<<endX<<" Y: "<<endY<<std::endl;
    // check to see if there is any piece available in starting position  
    if (this->piecePositions[startX+startY*8] == 0){
        return false ;
    }
    int n = this->pieces.size();
    for (int i=0;i<n;i++){
        sf::Vector2i pos  = pieces[i].getPiecePosition();
        if (pos.x == startX && pos.y == startY){
            n = i ;
            break;
        }
    }
    
    
    // add the move number if it was black turn
    if (!this->isWhiteTurn) this->moveNumber++;
    //change the turn 
    this->isWhiteTurn = !this->isWhiteTurn;
    
    // increment the half move number (we will set it to zero if there was a capture or pawn move later )
    this->halfMovesFromLastCaptureOrPawnMove++;

    //-------------------
    //check to see if it is a capture 
    if (this->piecePositions[endX+endY*8] != 0){
        
        this->halfMovesFromLastCaptureOrPawnMove = 0 ;
        // find the index of captures piece 
        int indexOfCapturedPiece =0;
        for (int i=0;i<pieces.size();i++){
            sf::Vector2i pos  = pieces[i].getPiecePosition();
            if (i != n && pos.x == endX && pos.y == endY){
                indexOfCapturedPiece = i ;
                break;
            }
        }
        if (indexOfCapturedPiece == 0){
            throw std::invalid_argument("Error conflict with board aspects");
        }
        int capturedPieceType = pieces[indexOfCapturedPiece].getPieceType();
        switch (capturedPieceType)
        {
        case BLACK_ROOK:
            if (endX == 0 && endY==0){
                this->blackQueenSideCastle = false ;
            }
            else if (endX == 7 && endY==0){
                this->blackKingSideCastle = false ;
            }
            break;
        case WHITE_ROOK:
            if (endX == 0 && endY==7){
                this->whiteQueenSideCastle = false ;
            }
            else if (endX == 7 && endY==7){
                this->whiteKingSideCastle = false ;
            }
            break;
        default:
            break;
        }
        // remove the captured piece from the vector 
        std::vector<Piece> copyVector1(this->pieces);
        this->pieces.resize(indexOfCapturedPiece);
        for (int j=indexOfCapturedPiece+1;j<copyVector1.size();j++){
            pieces.push_back(copyVector1[j]);
        }
    }
    this->piecePositions[endX+endY*8] = this->piecePositions[startX+startY*8];
    this->piecePositions[startX+startY*8] = 0;
    this->pieces[n].setPiecePosition(endX,endY);
    return true;
    //check to see if it is an en passant
    //else
     if (endX == this->enPassantX && endY == this->enPassantY){
        this->halfMovesFromLastCaptureOrPawnMove = 0 ;
        int pieceType = pieces[n].getPieceType();
        if (pieceType<0){
            int indexOfCapturedPawn =0;
            for (int i=0;i<pieces.size();i++){
                sf::Vector2i pos  = pieces[i].getPiecePosition();
                if (i != n && pos.x == endX && pos.y == endY+1){
                    indexOfCapturedPawn = i ;
                    break;
                }
            }
            sf::Vector2i posPawn = pieces[indexOfCapturedPawn].getPiecePosition();
            piecePositions[posPawn.x+posPawn.y*8] = 0;
            // remove the captured pawn from the vector 
            std::vector<Piece> copyVector1(this->pieces);
            this->pieces.resize(indexOfCapturedPawn);
            for (int j=indexOfCapturedPawn+1;j<pieces.size();j++){
                pieces.push_back(copyVector1[j]);
            }
        } 
        else if (pieceType > 0){
            int indexOfCapturedPawn =0;
            for (int i=0;i<pieces.size();i++){
                sf::Vector2i pos  = pieces[i].getPiecePosition();
                if (i != n && pos.x == endX && pos.y == endY-1){
                    indexOfCapturedPawn = i ;
                    break;
                }
            }
            sf::Vector2i posPawn = pieces[indexOfCapturedPawn].getPiecePosition();
            piecePositions[posPawn.x+posPawn.y*8] = 0;
            // remove the captured pawn from the vector 
            std::vector<Piece> copyVector1(this->pieces);
            this->pieces.resize(indexOfCapturedPawn);
            for (int j=indexOfCapturedPawn+1;j<pieces.size();j++){
                pieces.push_back(copyVector1[j]);
            }
        }

    }
    else {
        int pieceType = this->pieces[n].getPieceType();
        // handel special scenarios like castle or an passant 
        switch (pieceType)
        {
        case BLACK_PAWN:
            if ((endY - startY) == 2 && startY == 1){
                this->enPassantX = endX;
                this->enPassantY = endY-1;
            }
            this->halfMovesFromLastCaptureOrPawnMove = 0;
            break;
        case WHITE_PAWN:
            if ((startY - endY) == 2 && startY == 6){
                this->enPassantX = endX;
                this->enPassantY = endY+1;
            }
            this->halfMovesFromLastCaptureOrPawnMove = 0;
            break;
        case BLACK_KING:
            // short castle 
            if (this->blackKingSideCastle && startX == 4 && startY ==0  && endX == 6 && endY==0){
                // move the rook as well
                this->applyMove(7,0,5,0);
            }
            // long castle 
            if (this->blackQueenSideCastle && startX == 4 && startY ==0  && endX == 2 && endY==0){
                 // move the rook as well
                this->applyMove(0,0,3,0);
            }
            this->blackKingSideCastle = false;
            this->blackQueenSideCastle = false;
            break;
        case WHITE_KING:
             // short castle 
            if (this->blackKingSideCastle && startX == 4 && startY == 7 && endX == 6 && endY==7){
                // move the rook as well
                this->applyMove(7,7,5,7);
            }
            // long castle 
            if (this->blackQueenSideCastle && startX == 4 && startY ==7  && endX == 2 && endY==7){
                 // move the rook as well
                this->applyMove(0,7,3,7);
            }
            this->whiteKingSideCastle = false;
            this->whiteQueenSideCastle = false;
            break;
        case BLACK_ROOK:
            if (startX == 0 and startY == 0){
                this->blackQueenSideCastle = false;
            }
            else if(startX == 0 and startY == 7){
                this->blackKingSideCastle = false;
            }
            break;
        case WHITE_ROOK:
            if (startX == 7 and startY == 0){
                this->blackQueenSideCastle = false;
            }
            else if(startX == 7 and startY == 7){
                this->blackKingSideCastle = false;
            }
            break;
        default:
            this->enPassantX = -1;
            this->enPassantY = -1;
            break;
        }
    }
    return true;
}