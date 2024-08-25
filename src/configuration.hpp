
class Config {
public:
    Config(int width, int height);

    // the main loop
    void run();

    void open(sf::Vector2i mainWindowPosition);
    bool isOpen() const;
private:
    int windowWidth , windowHeight;
    std::unique_ptr<sf::RenderWindow> window; // we use a unique_ptr to manage the window lifecycle
    sf::Color backgroundColor;
    std::thread windowThread; 

    // handle all events, such as closing the window
    void processEvents();

    void render() ;
};