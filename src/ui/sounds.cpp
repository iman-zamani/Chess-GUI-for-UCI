/* Chess-GUI-for-UCI — move sounds (miniaudio). GPL-2.0 */
#include "ui/sounds.hpp"
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_FLAC
#define MA_NO_MP3
#include "miniaudio.h"
#include <cstdio>

struct Sounds::Impl {
    ma_engine engine;
    bool ok = false;
};

Sounds::Sounds() = default;

Sounds::~Sounds(){
    if (impl_ && impl_->ok) ma_engine_uninit(&impl_->engine);
}

void Sounds::play(Kind k){
    if (!tried_){
        tried_ = true;
        impl_ = std::make_unique<Impl>();
        impl_->ok = ma_engine_init(nullptr, &impl_->engine) == MA_SUCCESS;
        if (!impl_->ok) std::fprintf(stderr, "No audio device: move sounds are off\n");
    }
    if (!impl_ || !impl_->ok) return;
    const char* file = k == Kind::Capture ? "Resources/sounds/capture.wav"
                     : k == Kind::Check ? "Resources/sounds/check.wav"
                     : k == Kind::End ? "Resources/sounds/end.wav" : "Resources/sounds/move.wav";
    ma_engine_play_sound(&impl_->engine, file, nullptr);
}
