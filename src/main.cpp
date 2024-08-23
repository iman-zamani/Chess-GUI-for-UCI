#include<iostream>
#include<vector>
#include<string>
#include <cmath>
#include <SFML/Graphics.hpp>
#include "board.hpp"
int main()
{
    // Create the main window
    sf::RenderWindow window(sf::VideoMode(2400, 1350), "chess-gui");
    window.setFramerateLimit(60);
    Board chessBoard(window);

    const float DRAG_THRESHOLD = 10.0f;
    bool isDragging = false;
    bool mousePressed = false;
    sf::Vector2f mousePressPosition;

    int number  = 0;
    // Start the game loop
    while (window.isOpen())
    {
        // Process events
        sf::Event event;
        while (window.pollEvent(event))
        {
            
            switch (event.type)
            {
            case sf::Event::Closed:
                window.close();
                break;
            case sf::Event::MouseButtonPressed:
                if (event.mouseButton.button == sf::Mouse::Left)
                    {
                        chessBoard.selectTargetPiece(window,event.mouseButton.x,event.mouseButton.y);
                        // record the position where the mouse was pressed
                        mousePressed = true;
                        mousePressPosition = sf::Vector2f(event.mouseButton.x, event.mouseButton.y);
                        isDragging = false;
                    }
                break;

            case sf::Event::MouseMoved:
                    if (mousePressed)
                    {
                        // calculate the distance moved
                        sf::Vector2f currentPosition(event.mouseMove.x, event.mouseMove.y);
                        if (sqrt(pow(currentPosition.x - mousePressPosition.x, 2) + pow(currentPosition.y - mousePressPosition.y, 2)) > DRAG_THRESHOLD){
                            isDragging = true;
                        }
                    }
                    break;

            case sf::Event::MouseButtonReleased:
                if (event.mouseButton.button == sf::Mouse::Left){
                    
                    mousePressed = false;
                    if (isDragging)
                    {
                        // Handle dragging logic here
                        std::cout << "dragged to: " << event.mouseButton.x << ", " << event.mouseButton.y << std::endl;
                    }
                    else
                    {
                            // Handle clicking logic here
                        std::cout << "clicked at: " << event.mouseButton.x << ", " << event.mouseButton.y << std::endl;
                    }
                }
                break;
            default:
                break;
            }
        }
        if (isDragging && mousePressed){
            std::cout<<"dragging is happening "<< number <<std::endl;
            number++;
        }
        else if (isDragging){
            isDragging = false ;
        }
        // Clear screen
        window.clear();

        chessBoard.draw(window);

        // Update the window
        window.display();
    }

    return 0;
}
