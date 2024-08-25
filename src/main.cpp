#include<iostream>
#include<vector>
#include<string>
#include <cmath>
#include <thread>
#include <memory>
#include <SFML/Graphics.hpp>
#include "values.hpp"
#include "configuration.hpp"
#include "board.hpp"


int main()
{
    // Create the main window
    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "chess-gui");
    window.setFramerateLimit(60);
    Board chessBoard(window);

    // create the config window object 
    Config conf(WINDOW_WIDTH/2,WINDOW_HEIGHT/2);

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
                        // if it is outside of the board   event.mouseButton.x
                        if( !(event.mouseButton.x > ((WINDOW_WIDTH/WINDOW_WIDTH_RATIO)*4) && event.mouseButton.x < ((WINDOW_WIDTH/WINDOW_WIDTH_RATIO)*12)) ||
                         !(event.mouseButton.y > ((WINDOW_HEIGHT/WINDOW_HEIGHT_RATIO)/2) && event.mouseButton.y < (((WINDOW_HEIGHT/WINDOW_HEIGHT_RATIO)/2)*17)) ){
                            
                            sf::Vector2i mainWindowPosition = window.getPosition();
                            conf.open(mainWindowPosition);

                         }
                         else {
                            chessBoard.selectTargetPiece(window,event.mouseButton.x,event.mouseButton.y);
                            // record the position where the mouse was pressed
                            mousePressed = true;
                            mousePressPosition = sf::Vector2f(event.mouseButton.x, event.mouseButton.y);
                            isDragging = false;
                         }
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
                    // if (isDragging)
                    // {
                    //     // Handle dragging logic here
                    //     std::cout << "dragged to: " << event.mouseButton.x << ", " << event.mouseButton.y << std::endl;
                    // }
                    // else
                    // {
                    //         // Handle clicking logic here
                    //     std::cout << "clicked at: " << event.mouseButton.x << ", " << event.mouseButton.y << std::endl;
                    // }
                    sf::Vector2i mousePosition = sf::Mouse::getPosition(window);
                    chessBoard.placePiece(mousePosition.x,mousePosition.y);
                }
                break;
            default:
                break;
            }
        }
        if (isDragging && mousePressed){
            //std::cout<<"dragging is happening "<< number <<std::endl;
            sf::Vector2i mousePosition = sf::Mouse::getPosition(window);
            chessBoard.dragPiece(mousePosition.x,mousePosition.y);
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
