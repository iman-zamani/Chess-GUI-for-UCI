#include<iostream>
#include<vector>
#include<string>
#include <cmath>
#include <thread>
#include <memory>
#include <SFML/Graphics.hpp>
#include "board.hpp"

int main(){
    // Get the desktop resolution
    sf::VideoMode desktop = sf::VideoMode::getDesktopMode();
    unsigned int screenWidth = desktop.width;
    unsigned int screenHeight = desktop.height;
    // we launch the window with 1/4 of the screen area , and in the middle of the screen 
    float windowScale = 0.5f;
    unsigned int windowWidth = static_cast<unsigned int>(screenWidth * windowScale);
    unsigned int windowHeight = static_cast<unsigned int>(screenHeight * windowScale);
    // center position of the screen 
    int posX = static_cast<int>((screenWidth - windowWidth) / 2);
    int posY = static_cast<int>((screenHeight - windowHeight) / 2);
    
    sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "GUI FOR UCI", sf::Style::Default);
    window.setPosition(sf::Vector2i(posX, posY));
    
    // we need a virtual view so we can translate coordination to the ones we are rendering 
    sf::View virtualView(sf::FloatRect(0, 0, windowWidth, windowHeight));

    // set frame limit so we overload the hardware for no reason 
    window.setFramerateLimit(60);
    // Set vertical sync to prevent screen tearing
    window.setVerticalSyncEnabled(true);
    
    bool mouseLeftButtonIsPressed = false;
    Board board(window);   

    sf::Sprite draggingPieceSprite;
    sf::Texture* draggingPieceTexture ;
    // Main loop
    while (window.isOpen())
    {
        // Process events
        sf::Event event;
        while (window.pollEvent(event))
        {
            // Close window when pressing Escape or clicking close
            if (event.type == sf::Event::Closed || 
                (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape))
            {
                window.close();
            }  
            // mouse left click release detection  
            if (mouseLeftButtonIsPressed && event.type == sf::Event::MouseButtonReleased){
                if (event.mouseButton.button == sf::Mouse::Left){
                    sf::Vector2i mousePixel = sf::Mouse::getPosition(window);
                    sf::Vector2f virtualMouse = window.mapPixelToCoords(mousePixel, virtualView);
                    board.endDragging(virtualMouse);
                    mouseLeftButtonIsPressed = false;
                }
            }
            
        }
        // detect mouse left click and select a piece to darg 
        if (!mouseLeftButtonIsPressed && sf::Mouse::isButtonPressed(sf::Mouse::Left)) {
            sf::Vector2i mousePixel = sf::Mouse::getPosition(window);
            sf::Vector2f virtualMouse = window.mapPixelToCoords(mousePixel, virtualView);
            draggingPieceTexture = board.startDragging(virtualMouse);
            if (draggingPieceTexture->getSize() != sf::Vector2u(0, 0)){
                draggingPieceSprite.setTexture(*draggingPieceTexture);
                draggingPieceSprite.setScale(board.getSelectedPieceSpriteScale());
                mouseLeftButtonIsPressed = true;
            }
            else {
                mouseLeftButtonIsPressed = false;
            }        
        }
        // draw it in the exact place of mouse while dragging
        if (mouseLeftButtonIsPressed){
            sf::Vector2i mousePixel = sf::Mouse::getPosition(window);
            sf::Vector2f virtualMouse = window.mapPixelToCoords(mousePixel, virtualView);
            sf::FloatRect bounds = draggingPieceSprite.getLocalBounds();
            // if the sprite is scaled, bounds.width and bounds.height would not account for it 
            sf::Vector2f scale = draggingPieceSprite.getScale();
            float widthTT = bounds.width  * scale.x;
            float heightTT = bounds.height  * scale.y;
            draggingPieceSprite.setPosition(virtualMouse.x-(widthTT/2),virtualMouse.y-(heightTT/2));
        }
        // Clear the window with black color
        window.clear(sf::Color::Black);
        window.draw(board);
        if (mouseLeftButtonIsPressed){
            window.draw(draggingPieceSprite);
        }
        // Display the window
        window.display();
    }

    return 0;
}