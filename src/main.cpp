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

 #include<iostream>
#include<vector>
#include<string>
#include <cmath>
#include <thread>
#include <memory>
#include <SFML/Graphics.hpp>
#include "board.hpp"


#include <iostream>
#include <SFML/Graphics.hpp>
#include "board.hpp"

int main() {
    sf::VideoMode desktop = sf::VideoMode::getDesktopMode();
    float windowScale = 0.5f;
    unsigned int windowWidth = static_cast<unsigned int>(desktop.width * windowScale);
    unsigned int windowHeight = static_cast<unsigned int>(desktop.height * windowScale);
    
    int posX = (desktop.width - windowWidth) / 2;
    int posY = (desktop.height - windowHeight) / 2;
    
    sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "GUI FOR UCI", sf::Style::Default);
    window.setPosition(sf::Vector2i(posX, posY));
    
    sf::View virtualView(sf::FloatRect(0, 0, windowWidth, windowHeight));
    window.setFramerateLimit(60);
    window.setVerticalSyncEnabled(true);
    
    Board board(window);   

    bool mouseLeftButtonIsPressed = false;
    bool isDraggingPiece = false; // Tracks if we actually grabbed a piece
    sf::Sprite draggingPieceSprite;
    sf::Texture* draggingPieceTexture = nullptr;
    sf::Vector2f dragStartPos;

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed || 
                (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape)) {
                window.close();
            }  
            
            // 1. MOUSE PRESS: Detect intent
            if (!mouseLeftButtonIsPressed && event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
                sf::Vector2i mousePixel = sf::Mouse::getPosition(window);
                sf::Vector2f virtualMouse = window.mapPixelToCoords(mousePixel, virtualView);
                dragStartPos = virtualMouse;
                
                // ALWAYS flag true so the release event is guaranteed to fire
                mouseLeftButtonIsPressed = true; 

                draggingPieceTexture = board.startDragging(virtualMouse);
                if (draggingPieceTexture && draggingPieceTexture->getSize() != sf::Vector2u(0, 0)) {
                    draggingPieceSprite.setTexture(*draggingPieceTexture);
                    draggingPieceSprite.setScale(board.getSelectedPieceSpriteScale());
                    isDraggingPiece = true;
                    board.findLegalMoves();
                } else {
                    isDraggingPiece = false;
                }
            }

            // 2. MOUSE RELEASE: Evaluate drag-drop vs click-select
            if (mouseLeftButtonIsPressed && event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left) {
                sf::Vector2i mousePixel = sf::Mouse::getPosition(window);
                sf::Vector2f virtualMouse = window.mapPixelToCoords(mousePixel, virtualView);
                mouseLeftButtonIsPressed = false;

                float deltaX = std::abs(virtualMouse.x - dragStartPos.x);
                float deltaY = std::abs(virtualMouse.y - dragStartPos.y);

                if (deltaX < 5.f && deltaY < 5.f) {
                    // It's a click
                    if (isDraggingPiece) {
                        board.endDragging(virtualMouse); 
                    }
                    // Pass to the click state engine
                    board.handleSquareClick(virtualMouse);
                } else {
                    // It's a drag
                    if (isDraggingPiece) {
                        board.endDragging(virtualMouse);
                    }
                }
                isDraggingPiece = false;
            }
        }

        // 3. DRAG RENDERER
        if (mouseLeftButtonIsPressed && isDraggingPiece && draggingPieceTexture) {
            sf::Vector2i mousePixel = sf::Mouse::getPosition(window);
            sf::Vector2f virtualMouse = window.mapPixelToCoords(mousePixel, virtualView);
            sf::FloatRect bounds = draggingPieceSprite.getLocalBounds();
            sf::Vector2f scale = draggingPieceSprite.getScale();
            float w = bounds.width * scale.x;
            float h = bounds.height * scale.y;
            draggingPieceSprite.setPosition(virtualMouse.x - (w / 2), virtualMouse.y - (h / 2));
        }

        window.clear(sf::Color::Black);
        window.draw(board);
        
        // Only draw the floating sprite if we're actively holding a piece
        if (mouseLeftButtonIsPressed && isDraggingPiece) {
            window.draw(draggingPieceSprite);
        }
        
        window.display();
    }

    return 0;
}