#include "ui_components.hpp"
#include <algorithm>

// --- BUTTON IMPLEMENTATION ---
Button::Button(float x, float y, float w, float h, const std::string& text, sf::Font& font) {
    isHovered = false;
    shape.setPosition(x, y);
    shape.setSize(sf::Vector2f(w, h));
    shape.setFillColor(sf::Color(15, 15, 26)); 
    shape.setOutlineColor(sf::Color(0, 240, 255)); 
    shape.setOutlineThickness(3.f); 

    label.setFont(font);
    label.setString(text);
    label.setCharacterSize(24); 
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
TextInput::TextInput(float x, float y, float w, float h, const std::string& placeholder, sf::Font& font, bool numericOnly) 
    : isFocused(false), isNumericOnly(numericOnly) {
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

void TextInput::updateDisplayString() {
    std::string display = value;
    inputText.setString(display);
    
    // Smooth trailing horizontal clip if the string gets too long
    while (inputText.getLocalBounds().width > shape.getSize().x - 30 && !display.empty()) {
        display.erase(0, 1);
        inputText.setString(display);
    }
}

void TextInput::handleText(sf::Uint32 unicode) {
    if (!isFocused) return;
    if (unicode == '\b') { 
        if (!value.empty()) value.pop_back();
    } else if (unicode < 128 && unicode > 31) {
        if (isNumericOnly && (unicode < '0' || unicode > '9')) return;
        value += static_cast<char>(unicode);
    }
    updateDisplayString();
}

void TextInput::append(const std::string& str) {
    if (!isFocused) return;
    value += str;
    updateDisplayString();
}

void TextInput::setValue(const std::string& newVal) {
    value = newVal;
    updateDisplayString();
}

void TextInput::draw(sf::RenderWindow& window) {
    window.draw(label);
    window.draw(shape);
    
    // Standard SFML view clipping doesn't strictly clip sf::Text without complex setups,
    // but our updateDisplayString() manages horizontal length overflow cleanly.
    window.draw(inputText);
}