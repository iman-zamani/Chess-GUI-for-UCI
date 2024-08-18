#include <SFML/Graphics.hpp>
#include "board.hpp"
int main()
{
    // Create the main window
    sf::RenderWindow window(sf::VideoMode(3840, 2160), "chess-gui");

    board chessBoard(window,"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR ");
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
        }

        // Clear screen
        window.clear();

        chessBoard.draw(window);

        // Update the window
        window.display();
    }

    return 0;
}
