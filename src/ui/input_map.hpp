// Converts raw DualShock state into UI "pressed" events: edge detection, auto-repeat for
// directions/L1/R1, and the left stick mapped to the D-pad.
#pragma once
#include <cstdint>

#include "platform/platform.hpp"

namespace ml::ui {

class InputMap {
public:
    // Returns the buttons that should act this frame (bitmask of ml::Button).
    uint32_t update(const PadState &s, uint64_t nowMs);
    uint32_t held() const { return held_; }

    int repeatDelayMs = 380;
    int repeatRateMs = 85;
    int stickThreshold = 60;   // distance from centre (0..127)

private:
    uint32_t held_ = 0;
    uint64_t holdStart_ = 0, lastRepeat_ = 0;
    uint32_t repeatBtn_ = 0;
};

}  // namespace ml::ui
