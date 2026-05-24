#include "application.hpp"
#include <iostream>
#include <cmath>

Application::Application() 
    : state(AppState::MainMenu), isWhiteSide(true), 
      mouseLeftButtonIsPressed(false), isDraggingPiece(false), draggingPieceTexture(nullptr) 
{
    sf::VideoMode desktop = sf::VideoMode::getDesktopMode();
    unsigned int winW = static_cast<unsigned int>(desktop.width * 0.6f);
    unsigned int winH = static_cast<unsigned int>(desktop.height * 0.6f);
    
    window.create(sf::VideoMode(winW, winH), "DOMIN8 CHESS GUI", sf::Style::Default);
    window.setPosition(sf::Vector2i((desktop.width - winW) / 2, (desktop.height - winH) / 2));
    window.setFramerateLimit(60);
    window.setVerticalSyncEnabled(true);

    logicalView = sf::View(sf::FloatRect(0, 0, LOGICAL_WIDTH, LOGICAL_HEIGHT));

    if (!font.loadFromFile("./Resources/font.ttf")) {
        std::cerr << "WARNING: Could not load ./Resources/font.ttf!" << std::endl;
    }

    initUI();
}

void Application::initUI() {
    float cx = LOGICAL_WIDTH / 2.f;

    btnBackTopLeft = std::make_unique<Button>(50, 50, 140, 50, "< BACK", font);

    // Main Menu
    titleMain = sf::Text("DOMIN8 CHESS", font, 80);
    titleMain.setFillColor(sf::Color(0, 240, 255));
    titleMain.setPosition(cx - titleMain.getLocalBounds().width / 2.f, 200);
    btnPvC = std::make_unique<Button>(cx - 200, 450, 400, 80, "Play vs Computer", font);
    btnCvC = std::make_unique<Button>(cx - 200, 560, 400, 80, "Computer vs Computer", font);

    // PvC Menu
    titlePvC = sf::Text("PLAYER VS COMPUTER", font, 60);
    titlePvC.setFillColor(sf::Color(0, 240, 255));
    titlePvC.setPosition(cx - titlePvC.getLocalBounds().width / 2.f, 150);
    pathPvC = std::make_unique<TextInput>(cx - 400, 350, 800, 60, "UCI Engine Path", font);
    btnSide = std::make_unique<Button>(cx - 400, 480, 250, 60, "Side: WHITE", font);
    timePvC = std::make_unique<TextInput>(cx - 100, 480, 150, 60, "Time (m)", font);
    incPvC = std::make_unique<TextInput>(cx + 100, 480, 150, 60, "Inc (s)", font);
    btnStartPvC = std::make_unique<Button>(cx - 200, 650, 400, 80, "START GAME", font);

    // CvC Menu
    titleCvC = sf::Text("COMPUTER VS COMPUTER", font, 60);
    titleCvC.setFillColor(sf::Color(0, 240, 255));
    titleCvC.setPosition(cx - titleCvC.getLocalBounds().width / 2.f, 150);
    path1CvC = std::make_unique<TextInput>(cx - 400, 320, 800, 60, "Engine 1 Path (White)", font);
    path2CvC = std::make_unique<TextInput>(cx - 400, 450, 800, 60, "Engine 2 Path (Black)", font);
    timeCvC = std::make_unique<TextInput>(cx - 400, 580, 150, 60, "Time (m)", font);
    incCvC = std::make_unique<TextInput>(cx - 200, 580, 150, 60, "Inc (s)", font);
    btnStartCvC = std::make_unique<Button>(cx - 200, 750, 400, 80, "START GAME", font);
    
    // Set Default Times
    timePvC->value = "5"; timePvC->inputText.setString("5");
    timeCvC->value = "5"; timeCvC->inputText.setString("5");
    incPvC->value = "0"; incPvC->inputText.setString("0");
    incCvC->value = "0"; incCvC->inputText.setString("0");
}

void Application::run() {
    while (window.isOpen()) {
        window.setView(logicalView);
        processEvents();
        render();
    }
}

void Application::processEvents() {
    sf::Event event;
    bool mouseClicked = false;
    sf::Vector2f mousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window), logicalView);

    while (window.pollEvent(event)) {
        if (event.type == sf::Event::Closed || (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape)) {
            window.close();
        }

        if (event.type == sf::Event::Resized) {
            float windowRatio = event.size.width / (float)event.size.height;
            float viewRatio = LOGICAL_WIDTH / LOGICAL_HEIGHT;
            float sizeX = 1.f, sizeY = 1.f, posX = 0.f, posY = 0.f;

            if (windowRatio < viewRatio) {
                sizeY = windowRatio / viewRatio;
                posY = (1.f - sizeY) / 2.f;
            } else {
                sizeX = viewRatio / windowRatio;
                posX = (1.f - sizeX) / 2.f;
            }
            logicalView.setViewport(sf::FloatRect(posX, posY, sizeX, sizeY));
        }

        if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
            mouseClicked = true;
        }

        // Text Input Routing
        if (event.type == sf::Event::TextEntered) {
            if (state == AppState::PvCMenu) {
                pathPvC->handleText(event.text.unicode);
                timePvC->handleText(event.text.unicode);
                incPvC->handleText(event.text.unicode);
            } else if (state == AppState::CvCMenu) {
                path1CvC->handleText(event.text.unicode);
                path2CvC->handleText(event.text.unicode);
                timeCvC->handleText(event.text.unicode);
                incCvC->handleText(event.text.unicode);
            }
        }

        if (state == AppState::Playing) {
            handlePlayingEvents(event, mousePos);
        }
    }

    update(mousePos, mouseClicked);
}

void Application::handlePlayingEvents(const sf::Event& event, const sf::Vector2f& mousePos) {
    if (!mouseLeftButtonIsPressed && event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
        dragStartPos = mousePos;
        mouseLeftButtonIsPressed = true; 
        draggingPieceTexture = board->startDragging(mousePos);
        if (draggingPieceTexture && draggingPieceTexture->getSize() != sf::Vector2u(0, 0)) {
            draggingPieceSprite.setTexture(*draggingPieceTexture);
            draggingPieceSprite.setScale(board->getSelectedPieceSpriteScale());
            isDraggingPiece = true;
            board->findLegalMoves();
        } else {
            isDraggingPiece = false;
        }
    }

    if (mouseLeftButtonIsPressed && event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left) {
        mouseLeftButtonIsPressed = false;
        float deltaX = std::abs(mousePos.x - dragStartPos.x);
        float deltaY = std::abs(mousePos.y - dragStartPos.y);

        if (deltaX < 5.f && deltaY < 5.f) {
            if (isDraggingPiece) { board->endDragging(mousePos); }
            board->handleSquareClick(mousePos);
        } else {
            if (isDraggingPiece) { board->endDragging(mousePos); }
        }
        isDraggingPiece = false;
    }
}

void Application::update(const sf::Vector2f& mousePos, bool mouseClicked) {
    if (state == AppState::MainMenu) updateMainMenu(mousePos, mouseClicked);
    else if (state == AppState::PvCMenu) updatePvCMenu(mousePos, mouseClicked);
    else if (state == AppState::CvCMenu) updateCvCMenu(mousePos, mouseClicked);
    else if (state == AppState::Playing && mouseLeftButtonIsPressed && isDraggingPiece && draggingPieceTexture) {
        sf::FloatRect bounds = draggingPieceSprite.getLocalBounds();
        sf::Vector2f scale = draggingPieceSprite.getScale();
        draggingPieceSprite.setPosition(mousePos.x - (bounds.width * scale.x / 2), mousePos.y - (bounds.height * scale.y / 2));
    }
}

void Application::updateMainMenu(const sf::Vector2f& mousePos, bool mouseClicked) {
    btnPvC->update(mousePos);
    btnCvC->update(mousePos);
    if (mouseClicked) {
        if (btnPvC->isHovered) state = AppState::PvCMenu;
        if (btnCvC->isHovered) state = AppState::CvCMenu;
    }
}

void Application::updatePvCMenu(const sf::Vector2f& mousePos, bool mouseClicked) {
    btnBackTopLeft->update(mousePos);
    pathPvC->update(mousePos, mouseClicked);
    timePvC->update(mousePos, mouseClicked);
    incPvC->update(mousePos, mouseClicked);
    btnSide->update(mousePos);
    btnStartPvC->update(mousePos);

    if (mouseClicked) {
        if (btnBackTopLeft->isHovered) state = AppState::MainMenu;
        if (btnSide->isHovered) {
            isWhiteSide = !isWhiteSide;
            btnSide->label.setString(isWhiteSide ? "Side: WHITE" : "Side: BLACK");
        }
        if (btnStartPvC->isHovered) {
            board = std::make_unique<Board>(window);
            state = AppState::Playing;
        }
    }
}

void Application::updateCvCMenu(const sf::Vector2f& mousePos, bool mouseClicked) {
    btnBackTopLeft->update(mousePos);
    path1CvC->update(mousePos, mouseClicked);
    path2CvC->update(mousePos, mouseClicked);
    timeCvC->update(mousePos, mouseClicked);
    incCvC->update(mousePos, mouseClicked);
    btnStartCvC->update(mousePos);

    if (mouseClicked) {
        if (btnBackTopLeft->isHovered) state = AppState::MainMenu;
        if (btnStartCvC->isHovered) {
            board = std::make_unique<Board>(window);
            state = AppState::Playing;
        }
    }
}

void Application::render() {
    window.clear(state == AppState::Playing ? sf::Color::Black : sf::Color(10, 10, 15));

    if (state == AppState::MainMenu) {
        window.draw(titleMain);
        btnPvC->draw(window);
        btnCvC->draw(window);
    } else if (state == AppState::PvCMenu) {
        btnBackTopLeft->draw(window);
        window.draw(titlePvC);
        pathPvC->draw(window);
        btnSide->draw(window);
        timePvC->draw(window);
        incPvC->draw(window);
        btnStartPvC->draw(window);
    } else if (state == AppState::CvCMenu) {
        btnBackTopLeft->draw(window);
        window.draw(titleCvC);
        path1CvC->draw(window);
        path2CvC->draw(window);
        timeCvC->draw(window);
        incCvC->draw(window);
        btnStartCvC->draw(window);
    } else if (state == AppState::Playing && board) {
        window.draw(*board);
        if (mouseLeftButtonIsPressed && isDraggingPiece) {
            window.draw(draggingPieceSprite);
        }
    }
    window.display();
}