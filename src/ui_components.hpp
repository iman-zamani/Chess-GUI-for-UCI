#pragma once
#include <SFML/Graphics.hpp>
#include <string>

class Button {
public:
    sf::RectangleShape shape;
    sf::Text label;
    bool isHovered;

    Button(float x, float y, float w, float h, const std::string& text, sf::Font& font);
    void update(sf::Vector2f mousePos);
    void draw(sf::RenderWindow& window);
};

class TextInput {
public:
    sf::RectangleShape shape;
    sf::Text label;
    sf::Text inputText;
    std::string value;
    bool isFocused;
    bool isNumericOnly;

    TextInput(float x, float y, float w, float h, const std::string& placeholder, sf::Font& font, bool numericOnly = false);
    void update(sf::Vector2f mousePos, bool mouseClicked);
    void handleText(sf::Uint32 unicode);
    void append(const std::string& text);
    void setValue(const std::string& newVal);
    void draw(sf::RenderWindow& window);

private:
    void updateDisplayString();
};