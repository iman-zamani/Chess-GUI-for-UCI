#include <SFML/Graphics.hpp>
#include <thread>
#include <memory>
#include "configuration.hpp"


void Config::run() {
        while (window->isOpen()) {
            processEvents();
            render();
        }
}

void Config::processEvents() {
    sf::Event event;
    while (window->pollEvent(event)) {
        if (event.type == sf::Event::Closed)
            window->close();
    }
}
void Config::render() {
    window->clear(backgroundColor);
    

    window->display();
}


Config::Config(int width, int height)
    : backgroundColor(80, 80, 80) 
{
    this->windowHeight = height;
    this->windowWidth = width;
}



void Config::open(sf::Vector2i mainWindowPosition) {
    // if user opens the config window after closing the window that thread is running 
    // so we need to join it to avoid errors before creating a new thread that does the same job
    if (windowThread.joinable()) {
        windowThread.join(); 
    }
    // the window is created hear to avoid creating a new object each time 
    // when `open()` is called the config window will be created 
    window = std::make_unique<sf::RenderWindow>(sf::VideoMode(windowWidth, windowHeight), "Configuration");

    this->window->setFramerateLimit(60);
    
    //centering the configuration window relative to the main window 
    sf::Vector2i miniWindowPosition = mainWindowPosition + sf::Vector2i(windowWidth/2, windowHeight/2);
    window->setPosition(miniWindowPosition);
    
    // launching the run method in a new thread
    windowThread = std::thread(&Config::run, this);
}

bool Config::isOpen() const {
    return window && window->isOpen();
}