/*
 * Chess-GUI-for-UCI
 * Copyright (C) 2025 Iman Zamani
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

 #include <iostream>
#include <SFML/Graphics.hpp>
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
    // 
    this->isPieceSelectedState = false;
    this->selectedPieceIdx = -1;
    this->isPromotionWaiting = false;
    
    // the board is going to be set for x: WINDOW_WIDTH_RATIO to y: WINDOW_HEIGHT_RATIO windows
    sf::Vector2f size = this->window.getView().getSize();
    int windowWidth = static_cast<int>(size.x);
    int windowHeight = static_cast<int>(size.y);
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
    return sf::Vector2i(x,y);
}
void Board::findLegalMoves() {
    if (this->pieceSelected == -1) return;

    int selectedPieceIndex = this->pieceSelected;
    int pieceType = this->pieces[selectedPieceIndex].getType();
    
    // 1. Turn enforcement
    if ((this->isWhiteTurn && pieceType < 0) || (!this->isWhiteTurn && pieceType > 0)) {
        std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
        return;
    }

    // 2. Generate pseudo-legal moves using your existing switch block
    switch (pieceType) {
        case BLACK_ROOK:   this->findLegalMovesBlackRook();   break;
        case BLACK_BISHOP: this->findLegalMovesBlackBishop(); break;
        case BLACK_KNIGHT: this->findLegalMovesBlackKnight(); break;
        case BLACK_QUEEN:  this->findLegalMovesBlackQueen();  break;
        case BLACK_KING:   this->findLegalMovesBlackKing();   break;
        case BLACK_PAWN:   this->findLegalMovesBlackPawn();   break;
        case WHITE_ROOK:   this->findLegalMovesWhiteRook();   break;
        case WHITE_BISHOP: this->findLegalMovesWhiteBishop(); break;
        case WHITE_KNIGHT: this->findLegalMovesWhiteKnight(); break;
        case WHITE_QUEEN:  this->findLegalMovesWhiteQueen();  break;
        case WHITE_KING:   this->findLegalMovesWhiteKing();   break;
        case WHITE_PAWN:   this->findLegalMovesWhitePawn();   break;
        default: return;
    }

    // Save a copy of the pseudo-legal moves list to filter through
    std::vector<bool> pseudoLegalSquares = legalSquaresForTargetPiece;
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);

    sf::Vector2i originalPos = this->pieces[selectedPieceIndex].getGridPosition();
    int originalIndex = originalPos.y * 8 + originalPos.x;
    bool isWhite = (pieceType > 0);

    // 3. Filter moves by simulating them (Make / Unmake)
    for (int targetIdx = 0; targetIdx < 64; ++targetIdx) {
        if (!pseudoLegalSquares[targetIdx]) continue;

        int targetX = targetIdx % 8;
        int targetY = targetIdx / 8;

        // Special Castling Check: Must not castle out of, through, or into check
        if ((pieceType == WHITE_KING || pieceType == BLACK_KING) && std::abs(targetX - originalPos.x) == 2) {
            // Check if starting square is under attack (Castling out of check)
            if (isSquareAttacked(originalPos.x, originalPos.y, !isWhite)) continue;

            // Check the passing square (e.g., f1 for White King-side)
            int stepX = (targetX > originalPos.x) ? 1 : -1;
            if (isSquareAttacked(originalPos.x + stepX, originalPos.y, !isWhite)) continue;
        }

        // --- Simulate Move (Make) ---
        int capturedPiece = piecePositions[targetIdx];
        piecePositions[originalIndex] = 0;
        piecePositions[targetIdx] = pieceType;
        
        // Temporarily adjust the piece's grid tracking properties
        int tempX = this->pieces[selectedPieceIndex].x;
        int tempY = this->pieces[selectedPieceIndex].y;
        this->pieces[selectedPieceIndex].x = targetX;
        this->pieces[selectedPieceIndex].y = targetY;

        // --- Evaluate Legality ---
        sf::Vector2i kingPos = findKingGridPosition(isWhite);
        bool kingIsSafe = !isSquareAttacked(kingPos.x, kingPos.y, !isWhite);

        if (kingIsSafe) {
            legalSquaresForTargetPiece[targetIdx] = true;
        }

        // --- Undo Move (Unmake) ---
        piecePositions[originalIndex] = pieceType;
        piecePositions[targetIdx] = capturedPiece;
        this->pieces[selectedPieceIndex].x = tempX;
        this->pieces[selectedPieceIndex].y = tempY;
    }
}
// white pieces 
void Board::findLegalMovesWhitePawn() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getGridPosition(); // Using the new grid position method

    // Move forward
    if (pos.y - 1 >= 0) {
        int oneStep = (pos.y - 1) * 8 + pos.x;
        if (piecePositions[oneStep] == 0) {
            legalSquaresForTargetPiece[oneStep] = true;
            // Two steps forward (only if one step is also empty)
            if (pos.y == 6) {
                int twoStep = (pos.y - 2) * 8 + pos.x;
                if (piecePositions[twoStep] == 0) {
                    legalSquaresForTargetPiece[twoStep] = true;
                }
            }
        }
    }

    // Captures & En Passant
    if (pos.y - 1 >= 0) {
        // Capture Left
        if (pos.x - 1 >= 0) {
            int captureLeft = (pos.y - 1) * 8 + (pos.x - 1);
            if (piecePositions[captureLeft] < 0 || (pos.x - 1 == enPassantX && pos.y - 1 == enPassantY)) {
                legalSquaresForTargetPiece[captureLeft] = true;
            }
        }
        // Capture Right
        if (pos.x + 1 <= 7) {
            int captureRight = (pos.y - 1) * 8 + (pos.x + 1);
            if (piecePositions[captureRight] < 0 || (pos.x + 1 == enPassantX && pos.y - 1 == enPassantY)) {
                legalSquaresForTargetPiece[captureRight] = true;
            }
        }
    }
}
void Board::findLegalMovesWhiteKnight() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getGridPosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getGridPosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getGridPosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getGridPosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getGridPosition();

    // Normal King Moves
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

    // Castling
    if (pos.x == 4 && pos.y == 7) {
        // King-side (O-O)
        if (whiteKingSideCastle && piecePositions[7 * 8 + 5] == 0 && piecePositions[7 * 8 + 6] == 0) {
            legalSquaresForTargetPiece[7 * 8 + 6] = true; // g1
        }
        // Queen-side (O-O-O)
        if (whiteQueenSideCastle && piecePositions[7 * 8 + 1] == 0 && piecePositions[7 * 8 + 2] == 0 && piecePositions[7 * 8 + 3] == 0) {
            legalSquaresForTargetPiece[7 * 8 + 2] = true; // c1
        }
    }
}

void Board::findLegalMovesBlackKing() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getGridPosition();

    // Normal King Moves
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

    // Castling
    if (pos.x == 4 && pos.y == 0) {
        // King-side (O-O)
        if (blackKingSideCastle && piecePositions[5] == 0 && piecePositions[6] == 0) {
            legalSquaresForTargetPiece[6] = true; // g8
        }
        // Queen-side (O-O-O)
        if (blackQueenSideCastle && piecePositions[1] == 0 && piecePositions[2] == 0 && piecePositions[3] == 0) {
            legalSquaresForTargetPiece[2] = true; // c8
        }
    }
}
// black pieces 
void Board::findLegalMovesBlackPawn() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getGridPosition();

    // Move forward
    if (pos.y + 1 <= 7) {
        int oneStep = (pos.y + 1) * 8 + pos.x;
        if (piecePositions[oneStep] == 0) {
            legalSquaresForTargetPiece[oneStep] = true;
            // Two steps forward
            if (pos.y == 1) {
                int twoStep = (pos.y + 2) * 8 + pos.x;
                if (piecePositions[twoStep] == 0) {
                    legalSquaresForTargetPiece[twoStep] = true;
                }
            }
        }
    }

    // Captures & En Passant
    if (pos.y + 1 <= 7) {
        // Capture Left
        if (pos.x - 1 >= 0) {
            int captureLeft = (pos.y + 1) * 8 + (pos.x - 1);
            if (piecePositions[captureLeft] > 0 || (pos.x - 1 == enPassantX && pos.y + 1 == enPassantY)) {
                legalSquaresForTargetPiece[captureLeft] = true;
            }
        }
        // Capture Right
        if (pos.x + 1 <= 7) {
            int captureRight = (pos.y + 1) * 8 + (pos.x + 1);
            if (piecePositions[captureRight] > 0 || (pos.x + 1 == enPassantX && pos.y + 1 == enPassantY)) {
                legalSquaresForTargetPiece[captureRight] = true;
            }
        }
    }
}
void Board::findLegalMovesBlackRook() {
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    sf::Vector2i pos = this->pieces[this->pieceSelected].getGridPosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getGridPosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getGridPosition();
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
    sf::Vector2i pos = this->pieces[this->pieceSelected].getGridPosition();
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


// we should call this on the start of the dragging 
// it will return the texture of the pice user is trying to drag 
sf::Texture* Board::startDragging(sf::Vector2f clickPos) {
    if (this->isPromotionWaiting) return emptyTextureToReturn;

    for (int i = 0; i < this->piecesVectorSize; i++) {
        sf::Vector2f piecePosF(static_cast<float>(this->pieces[i].getPosition().x),static_cast<float>(this->pieces[i].getPosition().y));

        float absDiffX = clickPos.x - piecePosF.x;
        float absDiffY = clickPos.y - piecePosF.y;
        if (absDiffX < 0 || absDiffY < 0){continue;}
        
        if (absDiffX < squareSideLength && absDiffY < squareSideLength) {
            int pType = this->pieces[i].getType();
            // Ensure we only start dragging if it's our piece
            if ((this->isWhiteTurn && pType > 0) || (!this->isWhiteTurn && pType < 0)) {
                // Turn off ghost of previous piece if we clicked a different friendly piece
                if (pieceSelected != -1 && pieceSelected != i) {
                    this->pieces[pieceSelected].deselectPiece();
                }
                pieceSelected = i;
                this->pieces[pieceSelected].selectPiece();
                return this->pieces[pieceSelected].getTexture();
            } else {
                // We clicked an opponent piece. Don't grab it. 
                return emptyTextureToReturn; 
            }
        }
    }
    
    // Clicked an empty square. DO NOT reset pieceSelected here. 
    // We need to keep it intact so handleSquareClick knows what to move.
    return emptyTextureToReturn;
}
// we should call this when the dragging ends  
void Board::endDragging(sf::Vector2f clickPos) {
    if (pieceSelected < 0 || pieceSelected >= piecesVectorSize){
        return;
    }
    
    this->pieces[pieceSelected].deselectPiece();
    
    // Check if the clicked position is inside the board boundaries
    int upAndLeft = squareSideLength / 2;
    int downAndRight = upAndLeft + (8 * squareSideLength);
    if (clickPos.x < upAndLeft || clickPos.y < upAndLeft || clickPos.x > downAndRight || clickPos.y > downAndRight){
        // Snap back to original position
        sf::Vector2i orig = this->pieces[pieceSelected].getGridPosition();
        this->pieces[pieceSelected].moveTo(orig.x, orig.y);
        return;
    }

    int selectedPieceType = this->pieces[pieceSelected].getType();
    float pointedSquareCoordinateX = clickPos.x - (squareSideLength / 2);
    float pointedSquareCoordinateY = clickPos.y - (squareSideLength / 2);
    int pointedSquareX = pointedSquareCoordinateX / squareSideLength;
    int pointedSquareY = pointedSquareCoordinateY / squareSideLength;
    
    int targetIdx = pointedSquareY * 8 + pointedSquareX;

    // Reject move if the destination square isn't marked as strictly legal
    if (!legalSquaresForTargetPiece[targetIdx]) {
        sf::Vector2i orig = this->pieces[pieceSelected].getGridPosition();
        this->pieces[pieceSelected].moveTo(orig.x, orig.y);
        return;
    }

    sf::Vector2i originalPos = this->pieces[pieceSelected].getGridPosition();
    int originalIdx = originalPos.y * 8 + originalPos.x;

    // Identify Special Moves
    bool isCastlingMove = (selectedPieceType == WHITE_KING || selectedPieceType == BLACK_KING) && (std::abs(pointedSquareX - originalPos.x) == 2);
    bool isPawn = (selectedPieceType == WHITE_PAWN || selectedPieceType == BLACK_PAWN);
    bool isEnPassantCapture = isPawn && (pointedSquareX == enPassantX && pointedSquareY == enPassantY) && (piecePositions[targetIdx] == 0);

    // FIX: Evaluate capture BEFORE the piece actually moves and overwrites the target square!
    bool isCapture = (piecePositions[targetIdx] != 0) || isEnPassantCapture;

    int nextEnPassantX = -1;
    int nextEnPassantY = -1;

    // --- EXECUTE EN PASSANT CAPTURE REMOVAL ---
    if (isEnPassantCapture) {
        // Find the Y coordinate of the captured pawn (it sits on the original row of the victim)
        int victimY = originalPos.y; 
        int victimIdx = victimY * 8 + pointedSquareX;

        // Clear the layout map position for the victim pawn
        piecePositions[victimIdx] = 0;

        // Erase the victim pawn object from our rendering vector
        for (int i = 0; i < this->piecesVectorSize; i++) {
            if (i == pieceSelected) continue;
            sf::Vector2i pGrid = this->pieces[i].getGridPosition();
            if (pGrid.x == pointedSquareX && pGrid.y == victimY) {
                this->pieces.erase(this->pieces.begin() + i);
                this->piecesVectorSize--;
                if (i < pieceSelected) {
                    pieceSelected--;
                }
                break;
            }
        }
    }

    // Handle Standard Capture Execution (Direct Landings)
    if (!isEnPassantCapture) {
        for (int i = 0; i < this->piecesVectorSize; i++) {
            if (i == pieceSelected) continue;
            
            sf::Vector2i pGrid = this->pieces[i].getGridPosition();
            if (pGrid.x == pointedSquareX && pGrid.y == pointedSquareY) {
                this->pieces.erase(this->pieces.begin() + i);
                this->piecesVectorSize--;
                if (i < pieceSelected) {
                    pieceSelected--;
                }
                break;
            }
        }
    }

    // Teleport our active piece to its finalized grid square coordinate
    this->pieces[pieceSelected].moveTo(pointedSquareX, pointedSquareY);

    // Update the layout positions vector permanently
    piecePositions[originalIdx] = 0;
    piecePositions[targetIdx] = selectedPieceType;

    // --- TRACK NEW EN PASSANT POTENTIALS ---
    if (isPawn && std::abs(pointedSquareY - originalPos.y) == 2) {
        nextEnPassantX = pointedSquareX;
        // En Passant square is the square the pawn skipped over
        nextEnPassantY = (originalPos.y + pointedSquareY) / 2;
    }
    this->enPassantX = nextEnPassantX;
    this->enPassantY = nextEnPassantY;

    // --- EXECUTE ROOK TELEPORTATION IF CASTLING ---
    if (isCastlingMove) {
        int rookOriginalX = (pointedSquareX == 6) ? 7 : 0;
        int rookTargetX = (pointedSquareX == 6) ? 5 : 3;
        int castlingY = originalPos.y;

        int rookOriginalIdx = castlingY * 8 + rookOriginalX;
        int rookTargetIdx = castlingY * 8 + rookTargetX;
        int rookType = piecePositions[rookOriginalIdx];

        piecePositions[rookOriginalIdx] = 0;
        piecePositions[rookTargetIdx] = rookType;

        for (int i = 0; i < this->piecesVectorSize; i++) {
            sf::Vector2i pGrid = this->pieces[i].getGridPosition();
            if (pGrid.x == rookOriginalX && pGrid.y == castlingY) {
                this->pieces[i].moveTo(rookTargetX, castlingY);
                break;
            }
        }
    }

    // --- AMEND FEN CASTLING RIGHTS ---
    if (selectedPieceType == WHITE_KING) {
        this->whiteKingSideCastle = false;
        this->whiteQueenSideCastle = false;
    } else if (selectedPieceType == BLACK_KING) {
        this->blackKingSideCastle = false;
        this->blackQueenSideCastle = false;
    }
    if (originalIdx == 7 * 8 + 7 || targetIdx == 7 * 8 + 7) this->whiteKingSideCastle = false;
    if (originalIdx == 7 * 8 + 0 || targetIdx == 7 * 8 + 0) this->whiteQueenSideCastle = false;
    if (originalIdx == 0 * 8 + 7 || targetIdx == 0 * 8 + 7) this->blackKingSideCastle = false;
    if (originalIdx == 0 * 8 + 0 || targetIdx == 0 * 8 + 0) this->blackQueenSideCastle = false;

    // --- INTERCEPT PROMOTION WAITING LOOP ---
    if (isPawn && (pointedSquareY == 0 || pointedSquareY == 7)) {
        this->isPromotionWaiting = true;
        this->promotionTargetX = pointedSquareX;
        this->promotionTargetY = pointedSquareY;
        this->promotionPawnVectorIdx = pieceSelected;
        return; // Exit early to wait for user to click the menu!
    }

    // --- UPDATE 50-MOVE RULE TRACKER ---
    if (isPawn || isCapture) {
        this->halfMovesFromLastCaptureOrPawnMove = 0;
    } else {
        //std::cout << "number of half moves passed:" << this->halfMovesFromLastCaptureOrPawnMove << std::endl;
        this->halfMovesFromLastCaptureOrPawnMove++;
    }
    // 
    char promotionChar = 0;
    if (isPawn && (pointedSquareY == 0 || pointedSquareY == 7)) promotionChar = 'q'; // Default record for now, updated on click
    std::string moveStr = formatUciMove(originalPos.x, originalPos.y, pointedSquareX, pointedSquareY, promotionChar);
    this->uciMoveHistory.push_back(moveStr);

    // --- ENFORCE TURN SWITCHING ---
    this->isWhiteTurn = !this->isWhiteTurn;
    
    // --- CHECK FOR CHECKMATE / DRAWS ---
    this->checkGameEndConditions();

    // Reset move generation states
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    pieceSelected = -1;
    this->pieceSelected = -1;
    this->isPieceSelectedState = false;
    this->selectedPieceIdx = -1;
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


bool Board::isSquareAttacked(int targetX, int targetY, bool attackedByWhite) const {
    // Check Knight attacks
    std::vector<sf::Vector2i> knightMoves = {
        {targetX + 2, targetY + 1}, {targetX + 2, targetY - 1},
        {targetX - 2, targetY + 1}, {targetX - 2, targetY - 1},
        {targetX + 1, targetY + 2}, {targetX + 1, targetY - 2},
        {targetX - 1, targetY + 2}, {targetX - 1, targetY - 2}
    };
    int targetEnemyKnight = attackedByWhite ? WHITE_KNIGHT : BLACK_KNIGHT;
    for (const auto& move : knightMoves) {
        if (move.x >= 0 && move.x < 8 && move.y >= 0 && move.y < 8) {
            if (piecePositions[move.y * 8 + move.x] == targetEnemyKnight) return true;
        }
    }

    // Check Straight Line attacks (Rook / Queen / King)
    std::vector<sf::Vector2i> straightDirs = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int enemyRook = attackedByWhite ? WHITE_ROOK : BLACK_ROOK;
    int enemyQueen = attackedByWhite ? WHITE_QUEEN : BLACK_QUEEN;
    int enemyKing = attackedByWhite ? WHITE_KING : BLACK_KING;

    for (const auto& dir : straightDirs) {
        int step = 1;
        while (true) {
            int nx = targetX + dir.x * step;
            int ny = targetY + dir.y * step;
            if (nx < 0 || nx >= 8 || ny < 0 || ny >= 8) break;

            int piece = piecePositions[ny * 8 + nx];
            if (piece != 0) {
                if (piece == enemyRook || piece == enemyQueen) return true;
                if (step == 1 && piece == enemyKing) return true; // King adjacent attack
                break; // Blocked by any other piece
            }
            step++;
        }
    }

    // Check Diagonal attacks (Bishop / Queen / King / Pawn)
    std::vector<sf::Vector2i> diagDirs = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    int enemyBishop = attackedByWhite ? WHITE_BISHOP : BLACK_BISHOP;
    int enemyPawn = attackedByWhite ? WHITE_PAWN : BLACK_PAWN;

    for (const auto& dir : diagDirs) {
        int step = 1;
        while (true) {
            int nx = targetX + dir.x * step;
            int ny = targetY + dir.y * step;
            if (nx < 0 || nx >= 8 || ny < 0 || ny >= 8) break;

            int piece = piecePositions[ny * 8 + nx];
            if (piece != 0) {
                if (piece == enemyBishop || piece == enemyQueen) return true;
                if (step == 1 && piece == enemyKing) return true;
                
                // Pawn attacks are direction-dependent and only 1 square away diagonally
                if (step == 1) {
                    if (attackedByWhite) {
                        // White pawns attack upwards relative to the board layout (y decreases for black, but here we check who attacks our square)
                        // If a White Pawn is at (targetX ± 1, targetY + 1), it attacks (targetX, targetY)
                        if (ny == targetY + 1 && piece == enemyPawn) return true;
                    } else {
                        // If a Black Pawn is at (targetX ± 1, targetY - 1), it attacks (targetX, targetY)
                        if (ny == targetY - 1 && piece == enemyPawn) return true;
                    }
                }
                break; // Blocked
            }
            step++;
        }
    }

    return false;
}

sf::Vector2i Board::findKingGridPosition(bool whiteKing) const {
    int targetKingType = whiteKing ? WHITE_KING : BLACK_KING;
    for (const auto& piece : pieces) {
        // We look for a non-ghost, matching king type
        if (piece.pieceType == targetKingType) {
            return sf::Vector2i(piece.x, piece.y); 
        }
    }
    // search the positions board directly if vector sync is off, fallback only 
    for (int idx = 0; idx < 64; ++idx) {
        if (piecePositions[idx] == targetKingType) {
            return sf::Vector2i(idx % 8, idx / 8);
        }
    }
    return sf::Vector2i(-1, -1);
}


void Board::drawBoardBackground(sf::RenderTarget& target) const {
    // draw the basic squares
    for (sf::Sprite sp : spriteSquares){
        window.draw(sp);
    }
    
    // overlay indicators for legal squares if a piece is currently selected
    if (pieceSelected != -1) {
        for (int idx = 0; idx < 64; ++idx) {
            if (legalSquaresForTargetPiece[idx]) {
                sf::Sprite targetIndicator;
                targetIndicator.setTexture(targetSquaresTexture);
                
                float scaleFactor = static_cast<float>(squareSideLength) / targetSquaresTexture.getSize().x;
                targetIndicator.setScale(scaleFactor, scaleFactor);
                
                int sx = idx % 8;
                int sy = idx / 8;
                int pixelX = sx * squareSideLength + (squareSideLength / 2);
                int pixelY = sy * squareSideLength + (squareSideLength / 2);
                targetIndicator.setPosition(pixelX, pixelY);
                
                window.draw(targetIndicator);
            }
        }
    }
}

void Board::handleSquareClick(sf::Vector2f clickPos) {
    // 1. If waiting for promotion, handle choice sub-menu clicks first
    if (this->isPromotionWaiting) {
        float upAndLeft = squareSideLength / 2;
        int clickedOption = -1;
        
        for (int i = 0; i < 4; ++i) {
            float ox = promotionTargetX * squareSideLength + upAndLeft;
            float oy = (promotionTargetY == 0) ? (i * squareSideLength + upAndLeft) : ((promotionTargetY - i) * squareSideLength + upAndLeft);
            
            if (clickPos.x >= ox && clickPos.x < ox + squareSideLength &&
                clickPos.y >= oy && clickPos.y < oy + squareSideLength) {
                clickedOption = i; 
                break;
            }
        }
        
        if (clickedOption != -1) {
            int baseType = (piecePositions[promotionTargetY * 8 + promotionTargetX] > 0) ? 1 : -1;
            int finalType = 0;
            if (clickedOption == 0) finalType = baseType * 9;  // Queen
            if (clickedOption == 1) finalType = baseType * 5;  // Rook
            if (clickedOption == 2) finalType = baseType * 4;  // Bishop
            if (clickedOption == 3) finalType = baseType * 3;  // Knight

            piecePositions[promotionTargetY * 8 + promotionTargetX] = finalType;
            this->pieces[promotionPawnVectorIdx] = Piece(finalType, promotionTargetX, promotionTargetY, squareSideLength);
            
            this->isPromotionWaiting = false;
            this->isWhiteTurn = !this->isWhiteTurn;
            std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
            
            // Turn off ghost rendering on unfreeze
            if (this->pieceSelected != -1) this->pieces[this->pieceSelected].deselectPiece();
            this->pieceSelected = -1;
            this->isPieceSelectedState = false;
            this->selectedPieceIdx = -1;
        }
        return;
    }

    // Translate absolute screen pixels into grid array coordinates
    float upAndLeft = squareSideLength / 2;
    int gridX = (clickPos.x - upAndLeft) / squareSideLength;
    int gridY = (clickPos.y - upAndLeft) / squareSideLength;

    // Check bounds and cleanly clear selection if click is off the board
    if (clickPos.x < upAndLeft || clickPos.y < upAndLeft || gridX > 7 || gridY > 7) {
        if (this->selectedPieceIdx != -1) {
            this->pieces[this->selectedPieceIdx].deselectPiece();
        }
        std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
        this->pieceSelected = -1;
        this->isPieceSelectedState = false;
        this->selectedPieceIdx = -1;
        return;
    }

    int clickedIdx = gridY * 8 + gridX;

    // --- STATE 1: DESTINATION CLICK HANDLING ---
    // If a piece was already selected and we clicked a square marked TRUE in our legality array:
    if (isPieceSelectedState && legalSquaresForTargetPiece[clickedIdx]) {
        int selectedPieceType = this->pieces[selectedPieceIdx].getType();
        sf::Vector2i originalPos = this->pieces[selectedPieceIdx].getGridPosition();
        int originalIdx = originalPos.y * 8 + originalPos.x;

        bool isPawn = (selectedPieceType == WHITE_PAWN || selectedPieceType == BLACK_PAWN);
        bool isCastlingMove = (selectedPieceType == WHITE_KING || selectedPieceType == BLACK_KING) && (std::abs(gridX - originalPos.x) == 2);
        bool isEnPassantCapture = isPawn && (gridX == enPassantX && gridY == enPassantY) && (piecePositions[clickedIdx] == 0);

        // FIX: Evaluate capture BEFORE the piece moves!
        bool isCapture = (piecePositions[clickedIdx] != 0) || isEnPassantCapture;

        // Remove Captured Pieces
        int victimY = isEnPassantCapture ? originalPos.y : gridY;
        for (int i = 0; i < this->piecesVectorSize; i++) {
            if (i == selectedPieceIdx) continue;
            sf::Vector2i pGrid = this->pieces[i].getGridPosition();
            if (pGrid.x == gridX && pGrid.y == victimY) {
                this->pieces.erase(this->pieces.begin() + i);
                this->piecesVectorSize--;
                if (i < selectedPieceIdx) selectedPieceIdx--;
                break;
            }
        }

        // Teleport piece data maps permanently
        this->pieces[selectedPieceIdx].moveTo(gridX, gridY);
        piecePositions[originalIdx] = 0;
        piecePositions[clickedIdx] = selectedPieceType;

        // Manage En Passant status markers
        int nextEnPassantX = -1, nextEnPassantY = -1;
        if (isPawn && std::abs(gridY - originalPos.y) == 2) {
            nextEnPassantX = gridX;
            nextEnPassantY = (originalPos.y + gridY) / 2;
        }
        this->enPassantX = nextEnPassantX;
        this->enPassantY = nextEnPassantY;

        // Manage Castling Teleportations
        if (isCastlingMove) {
            int rookOriginalX = (gridX == 6) ? 7 : 0;
            int rookTargetX = (gridX == 6) ? 5 : 3;
            piecePositions[gridY * 8 + rookOriginalX] = 0;
            piecePositions[gridY * 8 + rookTargetX] = (selectedPieceType > 0) ? WHITE_ROOK : BLACK_ROOK;

            for (int i = 0; i < this->piecesVectorSize; i++) {
                sf::Vector2i pGrid = this->pieces[i].getGridPosition();
                if (pGrid.x == rookOriginalX && pGrid.y == gridY) {
                    this->pieces[i].moveTo(rookTargetX, gridY);
                    break;
                }
            }
        }

        // Drop Castling Rights Flags permanently if piece moved
        if (selectedPieceType == WHITE_KING) { this->whiteKingSideCastle = false; this->whiteQueenSideCastle = false; }
        if (selectedPieceType == BLACK_KING) { this->blackKingSideCastle = false; this->blackQueenSideCastle = false; }
        if (originalIdx == 7 * 8 + 7 || clickedIdx == 7 * 8 + 7) this->whiteKingSideCastle = false;
        if (originalIdx == 7 * 8 + 0 || clickedIdx == 7 * 8 + 0) this->whiteQueenSideCastle = false;
        if (originalIdx == 0 * 8 + 7 || clickedIdx == 0 * 8 + 7) this->blackKingSideCastle = false;
        if (originalIdx == 0 * 8 + 0 || clickedIdx == 0 * 8 + 0) this->blackQueenSideCastle = false;

        // Turn off ghost texture rendering layout properties cleanly on execution
        this->pieces[selectedPieceIdx].deselectPiece();

        // --- INTERCEPT PROMOTION WAITING LOOP ---
        if (isPawn && (gridY == 0 || gridY == 7)) {
            this->isPromotionWaiting = true;
            this->promotionTargetX = gridX;
            this->promotionTargetY = gridY;
            this->promotionPawnVectorIdx = selectedPieceIdx;
            return; 
        }

        // --- UPDATE 50-MOVE RULE TRACKER ---
        if (isPawn || isCapture) {
            this->halfMovesFromLastCaptureOrPawnMove = 0;
        } else {
            //std::cout << "number of half moves passed:" << this->halfMovesFromLastCaptureOrPawnMove << std::endl;
            this->halfMovesFromLastCaptureOrPawnMove++;
        }
        char promotionChar = 0;
        if (isPawn && (gridY == 0 || gridY == 7)) promotionChar = 'q'; // Default record for now, updated on click
        std::string moveStr = formatUciMove(originalPos.x, originalPos.y, gridX, gridY, promotionChar);
        this->uciMoveHistory.push_back(moveStr);
        // --- ENFORCE TURN SWITCHING ---
        this->isWhiteTurn = !this->isWhiteTurn;
        
        // --- CHECK FOR CHECKMATE / DRAWS ---
        this->checkGameEndConditions();

        std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
        this->pieceSelected = -1;
        this->isPieceSelectedState = false;
        this->selectedPieceIdx = -1;
        return;
    }

    // --- STATE 0: HIGHLIGHT SELECTION INTERACTION ---
    int targetedVectorIdx = -1;
    for (int i = 0; i < this->piecesVectorSize; i++) {
        sf::Vector2i pGrid = this->pieces[i].getGridPosition();
        if (pGrid.x == gridX && pGrid.y == gridY) {
            targetedVectorIdx = i;
            break;
        }
    }

    if (targetedVectorIdx != -1) {
        int pType = this->pieces[targetedVectorIdx].getType();
        // Verify targeted item aligns safely with current turn
        if ((this->isWhiteTurn && pType > 0) || (!this->isWhiteTurn && pType < 0)) {
            // If another friendly piece was already selected, make sure to clear its ghost state first!
            if (this->selectedPieceIdx != -1) {
                this->pieces[this->selectedPieceIdx].deselectPiece();
            }

            this->pieceSelected = targetedVectorIdx;
            this->selectedPieceIdx = targetedVectorIdx;
            this->isPieceSelectedState = true;
            this->findLegalMoves(); 
            return;
        }
    }

    // Reset layout maps if user clicked blank spaces out of bounds or an opponent piece
    if (this->selectedPieceIdx != -1) {
        this->pieces[this->selectedPieceIdx].deselectPiece();
    }
    std::fill(legalSquaresForTargetPiece.begin(), legalSquaresForTargetPiece.end(), false);
    this->pieceSelected = -1;
    this->isPieceSelectedState = false;
    this->selectedPieceIdx = -1;
}

void Board::drawPromotionMenu(sf::RenderTarget& target) const {
    if (!isPromotionWaiting) return;

    float upAndLeft = squareSideLength / 2;
    int baseColor = (piecePositions[promotionTargetY * 8 + promotionTargetX] > 0) ? 1 : -1;
    std::vector<int> optionTypes = { baseColor * 9, baseColor * 5, baseColor * 4, baseColor * 3 };

    for (size_t i = 0; i < 4; ++i) {
        sf::RectangleShape blockFrame(sf::Vector2f(squareSideLength, squareSideLength));
        float ox = promotionTargetX * squareSideLength + upAndLeft;
        float oy = (promotionTargetY == 0) ? (i * squareSideLength + upAndLeft) : ((promotionTargetY - i) * squareSideLength + upAndLeft);
        
        blockFrame.setPosition(ox, oy);
        blockFrame.setFillColor(sf::Color(80, 80, 80, 240));
        blockFrame.setOutlineColor(sf::Color::White);
        blockFrame.setOutlineThickness(2.f);
        target.draw(blockFrame);

        // Instantiate a quick dummy Piece instance to pull standard sprite atlas cuts on-the-fly
        Piece optionVisual(optionTypes[i], promotionTargetX, promotionTargetY, squareSideLength);
        optionVisual.sprite.setPosition(ox, oy);
        target.draw(optionVisual.sprite);
    }
}

void Board::draw(sf::RenderTarget& target, sf::RenderStates states) const {
    drawBoardBackground(target);
    for (const auto& piece : pieces) {
        target.draw(piece);
    }
    // Overlay choice modal panels directly over back-rank square zones
    if (this->isPromotionWaiting) {
        drawPromotionMenu(target);
    }
}



// --- GAME END LOGIC ---

bool Board::hasAnyLegalMoves(bool forWhite) {
    int previousSelected = this->pieceSelected;
    std::vector<bool> backupLegals = this->legalSquaresForTargetPiece;
    
    bool foundMove = false;
    for (int i = 0; i < this->piecesVectorSize; ++i) {
        int pType = this->pieces[i].getType();
        if ((forWhite && pType > 0) || (!forWhite && pType < 0)) {
            this->pieceSelected = i;
            this->findLegalMoves(); 
            
            for (bool isLegal : this->legalSquaresForTargetPiece) {
                if (isLegal) {
                    foundMove = true;
                    break;
                }
            }
        }
        if (foundMove) break;
    }
    
    // Restore state cleanly so we don't mess up the UI
    this->pieceSelected = previousSelected;
    this->legalSquaresForTargetPiece = backupLegals;
    
    return foundMove;
}

std::string Board::generatePositionHash() const {
    std::string hash = "";
    for (int i = 0; i < 64; ++i) {
        hash += std::to_string(piecePositions[i]) + ",";
    }
    // Include rights that alter the true state of the board
    hash += std::to_string(whiteKingSideCastle) + std::to_string(whiteQueenSideCastle) + 
            std::to_string(blackKingSideCastle) + std::to_string(blackQueenSideCastle) + 
            std::to_string(enPassantX);
    return hash;
}

void Board::checkGameEndConditions() {
    // 1. 50-Move Rule (100 half-moves)
    if (halfMovesFromLastCaptureOrPawnMove >= 100) {
        currentGameState = GameResult::Draw_50Move;
        std::cout << "GAME OVER: Draw by 50-Move Rule\n";
        return;
    }

    // 2. 3-Fold Repetition
    std::string currentHash = generatePositionHash();
    positionHistory.push_back(currentHash);
    
    int repetitionCount = 0;
    for (const std::string& h : positionHistory) {
        if (h == currentHash) repetitionCount++;
    }
    if (repetitionCount >= 3) {
        currentGameState = GameResult::Draw_Repetition;
        std::cout << "GAME OVER: Draw by 3-Fold Repetition\n";
        return;
    }

    // 3. Checkmate & Stalemate Detection
    bool canMove = hasAnyLegalMoves(this->isWhiteTurn);
    if (!canMove) {
        sf::Vector2i kingPos = findKingGridPosition(this->isWhiteTurn);
        bool inCheck = isSquareAttacked(kingPos.x, kingPos.y, !this->isWhiteTurn);
        
        if (inCheck) {
            currentGameState = this->isWhiteTurn ? GameResult::BlackWins : GameResult::WhiteWins;
            std::cout << "GAME OVER: Checkmate! " << (this->isWhiteTurn ? "Black" : "White") << " wins.\n";
        } else {
            currentGameState = GameResult::Draw_Stalemate;
            std::cout << "GAME OVER: Draw by Stalemate\n";
        }
    }
}

std::string Board::formatUciMove(int startX, int startY, int endX, int endY, char promotion) const {
    std::string move = "";
    move += (char)('a' + startX);
    move += std::to_string(8 - startY);
    move += (char)('a' + endX);
    move += std::to_string(8 - endY);
    if (promotion != 0) move += promotion;
    return move;
}

std::string Board::getUciMoveHistoryString() const {
    std::string history = "";
    for (const auto& m : uciMoveHistory) {
        history += m + " ";
    }
    return history;
}
bool Board::applyUciMove(const std::string& moveStr) {
    if (moveStr.length() < 4) return false;
    
    int sx = moveStr[0] - 'a';
    int sy = 8 - (moveStr[1] - '0');
    int ex = moveStr[2] - 'a';
    int ey = 8 - (moveStr[3] - '0');

    // Select the piece
    this->pieceSelected = -1;
    for (int i = 0; i < piecesVectorSize; i++) {
        sf::Vector2i pGrid = pieces[i].getGridPosition();
        if (pGrid.x == sx && pGrid.y == sy) {
            this->pieceSelected = i;
            break;
        }
    }
    if (this->pieceSelected == -1) return false;

    this->findLegalMoves();
    this->selectedPieceIdx = this->pieceSelected;
    this->isPieceSelectedState = true;

    // Simulate clicking the destination square
    float upAndLeft = squareSideLength / 2.f;
    float clickX = ex * squareSideLength + upAndLeft + 1.f;
    float clickY = ey * squareSideLength + upAndLeft + 1.f;
    this->handleSquareClick(sf::Vector2f(clickX, clickY));

    // Resolve silent promotion if engine ordered it
    if (moveStr.length() == 5 && this->isPromotionWaiting) {
        char p = moveStr[4];
        int option = 0; // Default Queen
        if (p == 'r') option = 1;
        if (p == 'b') option = 2;
        if (p == 'n') option = 3;

        float px = this->promotionTargetX * squareSideLength + upAndLeft + 1.f;
        float py = (this->promotionTargetY == 0) ? (option * squareSideLength + upAndLeft + 1.f) : ((this->promotionTargetY - option) * squareSideLength + upAndLeft + 1.f);
        this->handleSquareClick(sf::Vector2f(px, py));
    }
    return true;
}