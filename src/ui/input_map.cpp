#include "ui/input_map.hpp"

namespace ml::ui {

static constexpr uint32_t kRepeatable = BtnUp | BtnDown | BtnLeft | BtnRight | BtnL1 | BtnR1 | BtnL2 | BtnR2;

uint32_t InputMap::update(const PadState &s, uint64_t now)
{
    uint32_t b = s.connected ? s.buttons : 0;
    if (s.connected) {
        int dx = int(s.lx) - 128, dy = int(s.ly) - 128;
        int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        if (adx > stickThreshold || ady > stickThreshold) {
            if (adx > ady) b |= dx < 0 ? BtnLeft : BtnRight;
            else b |= dy < 0 ? BtnUp : BtnDown;
        }
    }
    uint32_t pressed = b & ~held_;
    held_ = b;
    uint32_t rep = b & kRepeatable;
    if (pressed & kRepeatable) {
        repeatBtn_ = pressed & kRepeatable;
        holdStart_ = lastRepeat_ = now;
    } else if (rep && (rep & repeatBtn_)) {
        if (now - holdStart_ >= uint64_t(repeatDelayMs) && now - lastRepeat_ >= uint64_t(repeatRateMs)) {
            pressed |= rep & repeatBtn_;
            lastRepeat_ = now;
        }
    } else {
        repeatBtn_ = 0;
    }
    return pressed;
}

}  // namespace ml::ui
