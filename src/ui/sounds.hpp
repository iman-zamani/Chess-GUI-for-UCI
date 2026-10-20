/* Chess-GUI-for-UCI — move sounds (miniaudio). GPL-2.0 */
#pragma once
#include <memory>
#include <string>

// Plays the short sounds in Resources/sounds. The audio device is opened on
// the first play; without a device (or sound files) play() does nothing.
class Sounds {
public:
    enum class Kind { Move, Capture, Check, End };
    Sounds();
    ~Sounds();
    void play(Kind k);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool tried_ = false;
};
