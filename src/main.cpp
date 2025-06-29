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
    float scale = 0.5f;
    unsigned int windowWidth = static_cast<unsigned int>(screenWidth * scale);
    unsigned int windowHeight = static_cast<unsigned int>(screenHeight * scale);
    // center position of the screen 
    int posX = static_cast<int>((screenWidth - windowWidth) / 2);
    int posY = static_cast<int>((screenHeight - windowHeight) / 2);
    
    sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "GUI FOR UCI", sf::Style::Default);
    window.setPosition(sf::Vector2i(posX, posY));
    

    // set frame limit so we overload the hardware for no reason 
    window.setFramerateLimit(60);
    // Set vertical sync to prevent screen tearing
    window.setVerticalSyncEnabled(true);

    Board board(window);   

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
        }

        // Clear the window with black color
        window.clear(sf::Color::Black);
        window.draw(board);
        // Display the window
        window.display();
    }

    return 0;
}