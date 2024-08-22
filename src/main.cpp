#include<iostream>
#include<vector>
#include<string>
#include <SFML/Graphics.hpp>
#include "board.hpp"
int main()
{
    // Create the main window
    sf::RenderWindow window(sf::VideoMode(2400, 1350), "chess-gui");
    window.setFramerateLimit(60);
    Board chessBoard(window);
    // Start the game loop
    while (window.isOpen())
    {
        // Process events
        sf::Event event;
        while (window.pollEvent(event))
        {
            // Close window: exit
            if (event.type == sf::Event::Closed)
                window.close();

            if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
                // Get the position of the click
                sf::Vector2i position = sf::Mouse::getPosition(window);
                chessBoard.selectTargetPiece(window,position.x,position.y);
                chessBoard.setIsPieceDragging(true,position.x,position.y);
            }
            if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left ) {
                sf::Vector2i position = sf::Mouse::getPosition(window);
                chessBoard.setIsPieceDragging(false,position.x,position.y);
            }
        }

        // Clear screen
        window.clear();

        chessBoard.draw(window);

        // Update the window
        window.display();
    }

    return 0;
}
