/* Chess-GUI-for-UCI — SFML 2/3 compatibility layer. GPL-2.0
 *
 * Why this exists: SFML 2's macOS backend renders windows at 1x — its GL view
 * never requests a Retina backing surface, so on HiDPI Macs the OS upscales
 * the framebuffer and EVERYTHING is blurry, regardless of Info.plist settings.
 * SFML 3 fixed this. This header lets the same code build against either
 * version; use SFML 3 on macOS for sharp output.
 */
#pragma once
#include <SFML/Graphics.hpp>
#include <SFML/Config.hpp>
#include <cstdint>
#include <optional>

// Normalized event so the app code is identical across SFML 2/3.
struct UiEvent {
    enum Kind { None, Closed, Resized, KeyPressed, TextEntered,
                MousePressed, MouseReleased, MouseMoved } kind = None;
    unsigned width = 0, height = 0;                       // Resized
    sf::Keyboard::Key key = sf::Keyboard::Key::Unknown;   // KeyPressed
    bool ctrl = false, sys = false;
    char32_t unicode = 0;                                 // TextEntered
    sf::Mouse::Button button = sf::Mouse::Button::Left;   // Mouse*
};

#if SFML_VERSION_MAJOR >= 3

inline bool pollUiEvent(sf::RenderWindow& w, UiEvent& out){
    const std::optional ev = w.pollEvent();
    if (!ev) return false;
    out = UiEvent{};
    if (ev->is<sf::Event::Closed>()) out.kind = UiEvent::Closed;
    else if (const auto* r = ev->getIf<sf::Event::Resized>()){
        out.kind = UiEvent::Resized; out.width = r->size.x; out.height = r->size.y;
    } else if (const auto* k = ev->getIf<sf::Event::KeyPressed>()){
        out.kind = UiEvent::KeyPressed; out.key = k->code;
        out.ctrl = k->control; out.sys = k->system;
    } else if (const auto* t = ev->getIf<sf::Event::TextEntered>()){
        out.kind = UiEvent::TextEntered; out.unicode = t->unicode;
    } else if (const auto* m = ev->getIf<sf::Event::MouseButtonPressed>()){
        out.kind = UiEvent::MousePressed; out.button = m->button;
    } else if (const auto* m = ev->getIf<sf::Event::MouseButtonReleased>()){
        out.kind = UiEvent::MouseReleased; out.button = m->button;
    } else if (ev->is<sf::Event::MouseMoved>()) out.kind = UiEvent::MouseMoved;
    return true;                       // drain the queue even for unmapped events
}
inline sf::IntRect   IR(int x,int y,int w,int h){ return sf::IntRect({x,y},{w,h}); }
inline sf::FloatRect FR(float x,float y,float w,float h){ return sf::FloatRect({x,y},{w,h}); }
inline sf::VideoMode VM(unsigned w,unsigned h){ return sf::VideoMode({w,h}); }
inline sf::Vector2u  desktopSize(){ return sf::VideoMode::getDesktopMode().size; }
inline float rectL(const sf::FloatRect& r){ return r.position.x; }
inline float rectT(const sf::FloatRect& r){ return r.position.y; }
inline float rectW(const sf::FloatRect& r){ return r.size.x; }
inline float rectH(const sf::FloatRect& r){ return r.size.y; }
template<class S> inline void setRotDeg(S& s, float deg){ s.setRotation(sf::degrees(deg)); }
inline sf::Text makeText(const sf::Font& f, const sf::String& s, unsigned size){
    return sf::Text(f, s, size);
}
inline bool loadFont(sf::Font& f, const std::string& path){ return f.openFromFile(path); }

#else // ------------------------------------------------------------- SFML 2

inline bool pollUiEvent(sf::RenderWindow& w, UiEvent& out){
    sf::Event e;
    if (!w.pollEvent(e)) return false;
    out = UiEvent{};
    switch (e.type){
    case sf::Event::Closed:  out.kind = UiEvent::Closed; break;
    case sf::Event::Resized: out.kind = UiEvent::Resized;
        out.width = e.size.width; out.height = e.size.height; break;
    case sf::Event::KeyPressed: out.kind = UiEvent::KeyPressed;
        out.key = e.key.code; out.ctrl = e.key.control; out.sys = e.key.system; break;
    case sf::Event::TextEntered: out.kind = UiEvent::TextEntered;
        out.unicode = e.text.unicode; break;
    case sf::Event::MouseButtonPressed: out.kind = UiEvent::MousePressed;
        out.button = e.mouseButton.button; break;
    case sf::Event::MouseButtonReleased: out.kind = UiEvent::MouseReleased;
        out.button = e.mouseButton.button; break;
    case sf::Event::MouseMoved: out.kind = UiEvent::MouseMoved; break;
    default: break;
    }
    return true;
}
inline sf::IntRect   IR(int x,int y,int w,int h){ return sf::IntRect(x,y,w,h); }
inline sf::FloatRect FR(float x,float y,float w,float h){ return sf::FloatRect(x,y,w,h); }
inline sf::VideoMode VM(unsigned w,unsigned h){ return sf::VideoMode(w,h); }
inline sf::Vector2u  desktopSize(){
    auto m = sf::VideoMode::getDesktopMode(); return {m.width, m.height};
}
inline float rectL(const sf::FloatRect& r){ return r.left; }
inline float rectT(const sf::FloatRect& r){ return r.top; }
inline float rectW(const sf::FloatRect& r){ return r.width; }
inline float rectH(const sf::FloatRect& r){ return r.height; }
template<class S> inline void setRotDeg(S& s, float deg){ s.setRotation(deg); }
inline sf::Text makeText(const sf::Font& f, const sf::String& s, unsigned size){
    return sf::Text(s, f, size);
}
inline bool loadFont(sf::Font& f, const std::string& path){ return f.loadFromFile(path); }

#endif
