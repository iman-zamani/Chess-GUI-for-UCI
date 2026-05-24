#include "application.hpp"
#include "utils.hpp"
#include <SFML/Window/Clipboard.hpp>
#include <iostream>
#include <cmath>

Application::Application() 
    : state(AppState::MainMenu), isWhiteSide(true), 
      mouseLeftButtonIsPressed(false), isDraggingPiece(false), draggingPieceTexture(nullptr),
      currentPopup(PopupType::None), gameOverPopupShown(false), currentDialogTarget(DialogTarget::None)
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
    float cy = LOGICAL_HEIGHT / 2.f;

    btnBackTopLeft = std::make_unique<Button>(50, 50, 140, 50, "< BACK", font);
    btnBackPlaying = std::make_unique<Button>(50, 50, 140, 50, "< BACK", font);

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
    pathPvC = std::make_unique<TextInput>(cx - 400, 350, 650, 60, "UCI Engine Path", font);
    btnBrowsePvC = std::make_unique<Button>(cx + 260, 350, 140, 60, "BROWSE", font);
    
    btnSide = std::make_unique<Button>(cx - 400, 480, 250, 60, "Side: WHITE", font);
    timePvC = std::make_unique<TextInput>(cx - 100, 480, 150, 60, "Time (m)", font, true);
    incPvC = std::make_unique<TextInput>(cx + 100, 480, 150, 60, "Inc (s)", font, true);
    btnStartPvC = std::make_unique<Button>(cx - 200, 650, 400, 80, "START GAME", font);

    // CvC Menu
    titleCvC = sf::Text("COMPUTER VS COMPUTER", font, 60);
    titleCvC.setFillColor(sf::Color(0, 240, 255));
    titleCvC.setPosition(cx - titleCvC.getLocalBounds().width / 2.f, 150);
    path1CvC = std::make_unique<TextInput>(cx - 400, 320, 650, 60, "Engine 1 Path (White)", font);
    btnBrowse1CvC = std::make_unique<Button>(cx + 260, 320, 140, 60, "BROWSE", font);
    
    path2CvC = std::make_unique<TextInput>(cx - 400, 450, 650, 60, "Engine 2 Path (Black)", font);
    btnBrowse2CvC = std::make_unique<Button>(cx + 260, 450, 140, 60, "BROWSE", font);
    
    timeCvC = std::make_unique<TextInput>(cx - 400, 580, 150, 60, "Time (m)", font, true);
    incCvC = std::make_unique<TextInput>(cx - 200, 580, 150, 60, "Inc (s)", font, true);
    btnStartCvC = std::make_unique<Button>(cx - 200, 750, 400, 80, "START GAME", font);
    
    // Popup System (Widened background to 800px so text won't overflow)
    popupBg.setSize(sf::Vector2f(800, 250));
    popupBg.setFillColor(sf::Color(20, 20, 30, 240));
    popupBg.setOutlineColor(sf::Color(0, 240, 255));
    popupBg.setOutlineThickness(4.f);
    popupBg.setPosition(cx - 400, cy - 125);

    popupText.setFont(font);
    popupText.setCharacterSize(30);
    popupText.setFillColor(sf::Color::White);

    btnPopupYes = std::make_unique<Button>(cx - 160, cy + 30, 120, 50, "YES", font);
    btnPopupNo  = std::make_unique<Button>(cx + 40, cy + 30, 120, 50, "NO", font);
    btnPopupOk  = std::make_unique<Button>(cx - 60, cy + 30, 120, 50, "OK", font);

    timePvC->setValue("5");
    timeCvC->setValue("5");
    incPvC->setValue("0");
    incCvC->setValue("0");
}

void Application::showPopup(PopupType type, const std::string& message) {
    currentPopup = type;
    popupText.setString(message);
    sf::FloatRect bounds = popupText.getLocalBounds();
    popupText.setPosition(LOGICAL_WIDTH / 2.f - bounds.width / 2.f, LOGICAL_HEIGHT / 2.f - 60.f);
}

void Application::run() {
    while (window.isOpen()) {
        window.setView(logicalView);
        processEvents();
        render();
    }
}

void Application::processEvents() {
    pollFileDialog(); // Check async thread responses non-blockingly

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

        // Heavy Sanitation for Pasted Engine Paths
        if (event.type == sf::Event::KeyPressed) {
            bool isCtrl = sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) || 
                          sf::Keyboard::isKeyPressed(sf::Keyboard::RControl) || 
                          sf::Keyboard::isKeyPressed(sf::Keyboard::LSystem) || 
                          sf::Keyboard::isKeyPressed(sf::Keyboard::RSystem);
            
            if (event.key.code == sf::Keyboard::V && isCtrl) {
                std::string clip = sf::Clipboard::getString().toAnsiString();
                
                // Erase weird characters 
                clip.erase(std::remove(clip.begin(), clip.end(), '\n'), clip.end());
                clip.erase(std::remove(clip.begin(), clip.end(), '\r'), clip.end());
                clip.erase(std::remove(clip.begin(), clip.end(), '\t'), clip.end());
                
                // Trim trailing/leading spaces safely
                size_t first = clip.find_first_not_of(" ");
                if (first != std::string::npos) {
                    size_t last = clip.find_last_not_of(" ");
                    clip = clip.substr(first, (last - first + 1));
                } else {
                    clip = "";
                }

                if (clip.find("file://") == 0) clip = clip.substr(7);

                if (state == AppState::PvCMenu) pathPvC->append(clip);
                else if (state == AppState::CvCMenu) {
                    path1CvC->append(clip);
                    path2CvC->append(clip);
                }
            }
        }

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
    if (currentPopup != PopupType::None || board->getGameState() != Board::GameResult::Ongoing) return;

    bool isPlayerTurn = (board->getIsWhiteTurn() && isWhiteSide) || (!board->getIsWhiteTurn() && !isWhiteSide);
    if (!isPlayerTurn) return;

    // Offset the mouse so the board's internal collision math stays valid
    sf::Vector2f boardMousePos = mousePos;
    boardMousePos.x -= BOARD_SHIFT_X;
    boardMousePos.y -= BOARD_SHIFT_Y;

    if (!mouseLeftButtonIsPressed && event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
        dragStartPos = mousePos; // Drag start needs true screen position
        mouseLeftButtonIsPressed = true; 
        draggingPieceTexture = board->startDragging(boardMousePos);
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
            if (isDraggingPiece) { board->endDragging(boardMousePos); }
            board->handleSquareClick(boardMousePos);
        } else {
            if (isDraggingPiece) { board->endDragging(boardMousePos); }
        }
        isDraggingPiece = false;
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
        btnBrowsePvC->draw(window);
        btnSide->draw(window);
        timePvC->draw(window);
        incPvC->draw(window);
        btnStartPvC->draw(window);
    } else if (state == AppState::CvCMenu) {
        btnBackTopLeft->draw(window);
        window.draw(titleCvC);
        path1CvC->draw(window);
        btnBrowse1CvC->draw(window);
        path2CvC->draw(window);
        btnBrowse2CvC->draw(window);
        timeCvC->draw(window);
        incCvC->draw(window);
        btnStartCvC->draw(window);
    } else if (state == AppState::Playing && board) {
        
        // Translate the board rendering cleanly to the center
        sf::RenderStates states;
        states.transform.translate(BOARD_SHIFT_X, BOARD_SHIFT_Y);
        window.draw(*board, states);
        
        if (mouseLeftButtonIsPressed && isDraggingPiece) {
            window.draw(draggingPieceSprite); // Kept absolute so it follows mouse properly
        }
        
        btnBackPlaying->draw(window);

        if (currentPopup != PopupType::None) {
            window.draw(popupBg);
            window.draw(popupText);
            if (currentPopup == PopupType::ConfirmQuit) {
                btnPopupYes->draw(window);
                btnPopupNo->draw(window);
            } else if (currentPopup == PopupType::GameOver) {
                btnPopupOk->draw(window);
            }
        }
    }
    window.display();
}

void Application::update(const sf::Vector2f& mousePos, bool mouseClicked) {
    if (state == AppState::MainMenu) updateMainMenu(mousePos, mouseClicked);
    else if (state == AppState::PvCMenu) updatePvCMenu(mousePos, mouseClicked);
    else if (state == AppState::CvCMenu) updateCvCMenu(mousePos, mouseClicked);
    else if (state == AppState::Playing) {
        
        if (currentPopup != PopupType::None) {
            if (currentPopup == PopupType::ConfirmQuit) {
                btnPopupYes->update(mousePos);
                btnPopupNo->update(mousePos);
                if (mouseClicked) {
                    if (btnPopupYes->isHovered) {
                        state = AppState::MainMenu;
                        currentPopup = PopupType::None;
                        if (engine) engine->stopEngine();
                        board.reset();
                    } else if (btnPopupNo->isHovered) {
                        currentPopup = PopupType::None;
                    }
                }
            } else if (currentPopup == PopupType::GameOver) {
                btnPopupOk->update(mousePos);
                if (mouseClicked && btnPopupOk->isHovered) {
                    currentPopup = PopupType::None;
                }
            }
            return;
        }

        btnBackPlaying->update(mousePos);
        if (mouseClicked && btnBackPlaying->isHovered) {
            if (board->getGameState() != Board::GameResult::Ongoing) {
                state = AppState::MainMenu;
                if (engine) engine->stopEngine();
                board.reset();
            } else {
                showPopup(PopupType::ConfirmQuit, "Are you sure you want to leave the game?");
            }
            return;
        }

        if (!gameOverPopupShown && board->getGameState() != Board::GameResult::Ongoing) {
            std::string msg;
            Board::GameResult res = board->getGameState();
            if (res == Board::GameResult::WhiteWins) msg = "Game Over: White Wins!";
            else if (res == Board::GameResult::BlackWins) msg = "Game Over: Black Wins!";
            else msg = "Game Over: Draw!";
            showPopup(PopupType::GameOver, msg);
            gameOverPopupShown = true;
        }

        processEngineTurn();
        
        if (mouseLeftButtonIsPressed && isDraggingPiece && draggingPieceTexture) {
            sf::FloatRect bounds = draggingPieceSprite.getLocalBounds();
            sf::Vector2f scale = draggingPieceSprite.getScale();
            draggingPieceSprite.setPosition(mousePos.x - (bounds.width * scale.x / 2), mousePos.y - (bounds.height * scale.y / 2));
        }
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
    btnBrowsePvC->update(mousePos);
    timePvC->update(mousePos, mouseClicked);
    incPvC->update(mousePos, mouseClicked);
    btnSide->update(mousePos);
    btnStartPvC->update(mousePos);

    if (mouseClicked) {
        if (btnBackTopLeft->isHovered) state = AppState::MainMenu;
        
        // Launch file dialog non-blockingly
        if (btnBrowsePvC->isHovered && currentDialogTarget == DialogTarget::None) {
            currentDialogTarget = DialogTarget::PvC;
            fileDialogFuture = std::async(std::launch::async, openFileDialog);
        }
        
        if (btnSide->isHovered) {
            isWhiteSide = !isWhiteSide;
            btnSide->label.setString(isWhiteSide ? "Side: WHITE" : "Side: BLACK");
        }
        if (btnStartPvC->isHovered) {
            board = std::make_unique<Board>(window);
            engine = std::make_unique<UciEngine>();
            if (engine->startEngine(pathPvC->value)) {
                engine->sendCommand("uci");
                engine->sendCommand("isready");
                engine->sendCommand("ucinewgame");
            } else {
                std::cerr << "Failed to start engine at: " << pathPvC->value << "\n";
            }
            
            isEngineSearching = false;
            gameOverPopupShown = false;
            state = AppState::Playing;
        }
    }
}

void Application::updateCvCMenu(const sf::Vector2f& mousePos, bool mouseClicked) {
    btnBackTopLeft->update(mousePos);
    path1CvC->update(mousePos, mouseClicked);
    btnBrowse1CvC->update(mousePos);
    path2CvC->update(mousePos, mouseClicked);
    btnBrowse2CvC->update(mousePos);
    timeCvC->update(mousePos, mouseClicked);
    incCvC->update(mousePos, mouseClicked);
    btnStartCvC->update(mousePos);

    if (mouseClicked) {
        if (btnBackTopLeft->isHovered) state = AppState::MainMenu;
        
        // Launch file dialogs non-blockingly
        if (btnBrowse1CvC->isHovered && currentDialogTarget == DialogTarget::None) {
            currentDialogTarget = DialogTarget::CvC1;
            fileDialogFuture = std::async(std::launch::async, openFileDialog);
        }
        if (btnBrowse2CvC->isHovered && currentDialogTarget == DialogTarget::None) {
            currentDialogTarget = DialogTarget::CvC2;
            fileDialogFuture = std::async(std::launch::async, openFileDialog);
        }

        if (btnStartCvC->isHovered) {
            board = std::make_unique<Board>(window);
            gameOverPopupShown = false;
            state = AppState::Playing;
        }
    }
}



void Application::processEngineTurn() {
    if (board->getGameState() != Board::GameResult::Ongoing) return;
    
    bool isPlayerTurn = (board->getIsWhiteTurn() && isWhiteSide) || (!board->getIsWhiteTurn() && !isWhiteSide);
    
    if (!isPlayerTurn && engine && engine->isEngineRunning()) {
        if (!isEngineSearching) {
            std::string history = board->getUciMoveHistoryString();
            engine->sendCommand("position startpos moves " + history);
            
            int moveTimeMs = 1000; 
            engine->sendCommand("go movetime " + std::to_string(moveTimeMs));
            
            isEngineSearching = true;
        }

        std::string move = engine->getBestMove();
        if (!move.empty()) {
            board->applyUciMove(move);
            isEngineSearching = false;
        }
    }
}

void Application::pollFileDialog() {
    if (currentDialogTarget != DialogTarget::None) {
        if (fileDialogFuture.valid() && fileDialogFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            std::string p = fileDialogFuture.get();
            if (!p.empty()) {
                if (currentDialogTarget == DialogTarget::PvC) pathPvC->setValue(p);
                else if (currentDialogTarget == DialogTarget::CvC1) path1CvC->setValue(p);
                else if (currentDialogTarget == DialogTarget::CvC2) path2CvC->setValue(p);
            }
            currentDialogTarget = DialogTarget::None;
        }
    }
}