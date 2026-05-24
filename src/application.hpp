#pragma once
#include <SFML/Graphics.hpp>
#include <memory>
#include <string>
#include <future>
#include "board.hpp"
#include "ui_components.hpp"
#include "uci_engine.hpp"

enum class AppState { MainMenu, PvCMenu, CvCMenu, Playing };
enum class PopupType { None, ConfirmQuit, GameOver };
enum class DialogTarget { None, PvC, CvC1, CvC2 };

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
    void showPopup(PopupType type, const std::string& message);

    // Engine Integration
    std::unique_ptr<UciEngine> engine;
    bool isEngineSearching;
    void processEngineTurn();

    // Async File Dialog
    std::future<std::string> fileDialogFuture;
    DialogTarget currentDialogTarget;
    void pollFileDialog();

    // Core SFML 
    sf::RenderWindow window;
    sf::View logicalView;
    sf::Font font;
    const float LOGICAL_WIDTH = 1920.f;
    const float LOGICAL_HEIGHT = 1080.f;

    // Board Offset (Centers the board and avoids top-left back button)
    const float BOARD_SHIFT_X = 420.f; 
    const float BOARD_SHIFT_Y = 0.f;

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

    // UI Elements
    std::unique_ptr<Button> btnBackTopLeft, btnPvC, btnCvC, btnSide, btnStartPvC, btnStartCvC;
    std::unique_ptr<Button> btnBackPlaying;
    std::unique_ptr<Button> btnBrowsePvC, btnBrowse1CvC, btnBrowse2CvC;
    std::unique_ptr<TextInput> pathPvC, timePvC, incPvC, path1CvC, path2CvC, timeCvC, incCvC;
    sf::Text titleMain, titlePvC, titleCvC;

    // Popup UI
    PopupType currentPopup;
    bool gameOverPopupShown;
    sf::RectangleShape popupBg;
    sf::Text popupText;
    std::unique_ptr<Button> btnPopupYes, btnPopupNo, btnPopupOk;
};