#pragma once
#include <SFML/Graphics.hpp>
#include <memory>
#include "board.hpp"
#include "ui_components.hpp"
#include "uci_engine.hpp"

enum class AppState { MainMenu, PvCMenu, CvCMenu, Playing };

class Application {
public:
    Application();
    void run();

private:
    void initUI();
    void processEvents();
    void update(const sf::Vector2f& mousePos, bool mouseClicked);
    void render();

    // Specific event/update handlers
    void handlePlayingEvents(const sf::Event& event, const sf::Vector2f& mousePos);
    void updateMainMenu(const sf::Vector2f& mousePos, bool mouseClicked);
    void updatePvCMenu(const sf::Vector2f& mousePos, bool mouseClicked);
    void updateCvCMenu(const sf::Vector2f& mousePos, bool mouseClicked);

    // Engine Integration
    std::unique_ptr<UciEngine> engine;
    bool isEngineSearching;
    void processEngineTurn();


    // Core SFML 
    sf::RenderWindow window;
    sf::View logicalView;
    sf::Font font;
    const float LOGICAL_WIDTH = 1920.f;
    const float LOGICAL_HEIGHT = 1080.f;

    // State & Board
    AppState state;
    std::unique_ptr<Board> board;
    bool isWhiteSide;

    // Board Dragging State
    bool mouseLeftButtonIsPressed;
    bool isDraggingPiece;
    sf::Sprite draggingPieceSprite;
    sf::Texture* draggingPieceTexture;
    sf::Vector2f dragStartPos;

    // UI Elements (Using unique_ptr so we can initialize them AFTER the font loads)
    std::unique_ptr<Button> btnBackTopLeft, btnPvC, btnCvC, btnSide, btnStartPvC, btnStartCvC;
    std::unique_ptr<TextInput> pathPvC, timePvC, incPvC, path1CvC, path2CvC, timeCvC, incCvC;
    sf::Text titleMain, titlePvC, titleCvC;

};