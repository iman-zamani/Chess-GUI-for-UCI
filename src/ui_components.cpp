#include "ui_components.hpp"

// --- BUTTON IMPLEMENTATION ---
Button::Button(float x, float y, float w, float h, const std::string& text, sf::Font& font) {
    isHovered = false;
    shape.setPosition(x, y);
    shape.setSize(sf::Vector2f(w, h));
    shape.setFillColor(sf::Color(15, 15, 26)); 
    shape.setOutlineColor(sf::Color(0, 240, 255)); 
    shape.setOutlineThickness(3.f); // Made slightly thicker for high-res scaling

    label.setFont(font);
    label.setString(text);
    label.setCharacterSize(24); // Increased base size for 1080p logical view
    label.setFillColor(sf::Color::White);
    
    sf::FloatRect bounds = label.getLocalBounds();
    label.setPosition(x + (w - bounds.width) / 2.f, y + (h - bounds.height) / 2.f - 8.f);
}

void Button::update(sf::Vector2f mousePos) {
    isHovered = shape.getGlobalBounds().contains(mousePos);
    shape.setFillColor(isHovered ? sf::Color(0, 240, 255, 50) : sf::Color(15, 15, 26));
}

void Button::draw(sf::RenderWindow& window) {
    window.draw(shape);
    window.draw(label);
}

// --- TEXT INPUT IMPLEMENTATION ---
TextInput::TextInput(float x, float y, float w, float h, const std::string& placeholder, sf::Font& font) {
    isFocused = false;
    shape.setPosition(x, y);
    shape.setSize(sf::Vector2f(w, h));
    shape.setFillColor(sf::Color(10, 10, 15));
    shape.setOutlineColor(sf::Color(100, 100, 100));
    shape.setOutlineThickness(3.f);

    label.setFont(font);
    label.setString(placeholder);
    label.setCharacterSize(20);
    label.setFillColor(sf::Color(150, 150, 150));
    label.setPosition(x, y - 35);

    inputText.setFont(font);
    inputText.setCharacterSize(24);
    inputText.setFillColor(sf::Color::White);
    inputText.setPosition(x + 15, y + 10);
}

void TextInput::update(sf::Vector2f mousePos, bool mouseClicked) {
    if (mouseClicked) {
        isFocused = shape.getGlobalBounds().contains(mousePos);
        shape.setOutlineColor(isFocused ? sf::Color(0, 240, 255) : sf::Color(100, 100, 100));
    }
}

void TextInput::handleText(sf::Uint32 unicode) {
    if (!isFocused) return;
    if (unicode == '\b') { // Backspace
        if (!value.empty()) value.pop_back();
    } else if (unicode < 128 && unicode > 31) {
        value += static_cast<char>(unicode);
    }
    inputText.setString(value);
}

void TextInput::draw(sf::RenderWindow& window) {
    window.draw(label);
    window.draw(shape);
    window.draw(inputText);
}