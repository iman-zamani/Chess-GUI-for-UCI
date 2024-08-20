#include<iostream>
#include<vector>
#include<string>
#include <SFML/Graphics.hpp>
#include "board.hpp"
int main()
{
    // Create the main window
    sf::RenderWindow window(sf::VideoMode(3840, 2160), "chess-gui");
    window.setFramerateLimit(60);
    Board chessBoard(window,"r2qk2r/2pnp1bp/p2p4/1p2Ppp1/2P5/P1N1Q3/1P1P1PPP/R1B1KB1R b - f6 120 132");
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
                chessBoard.setIsPieceDragging(true);
            }
            if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left ) {
                chessBoard.setIsPieceDragging(false);
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
