// DualShock 4 via libScePad (polling, initial user).
#include <orbis/Pad.h>
#include <orbis/UserService.h>

#include "platform/ps4/ps4_platform.hpp"

namespace ml::ps4 {

bool Pad::init(std::string *err)
{
    sceUserServiceInitialize(nullptr);
    int user = 0;
    if (sceUserServiceGetInitialUser(&user) != 0) { *err = "usuario inicial indisponivel"; return false; }
    scePadInit();
    handle_ = scePadOpen(user, 0, 0, nullptr);
    if (handle_ < 0) { *err = "controle nao abriu (" + std::to_string(handle_) + ")"; return false; }
    return true;
}

PadState Pad::read()
{
    PadState s;
    OrbisPadData d;
    if (handle_ < 0 || scePadReadState(handle_, &d) != 0 || !d.connected) return s;
    s.connected = true;
    struct { uint32_t from, to; } map[] = {
        {ORBIS_PAD_BUTTON_UP, BtnUp}, {ORBIS_PAD_BUTTON_DOWN, BtnDown}, {ORBIS_PAD_BUTTON_LEFT, BtnLeft},
        {ORBIS_PAD_BUTTON_RIGHT, BtnRight}, {ORBIS_PAD_BUTTON_CROSS, BtnCross}, {ORBIS_PAD_BUTTON_CIRCLE, BtnCircle},
        {ORBIS_PAD_BUTTON_SQUARE, BtnSquare}, {ORBIS_PAD_BUTTON_TRIANGLE, BtnTriangle}, {ORBIS_PAD_BUTTON_L1, BtnL1},
        {ORBIS_PAD_BUTTON_R1, BtnR1}, {ORBIS_PAD_BUTTON_L2, BtnL2}, {ORBIS_PAD_BUTTON_R2, BtnR2},
        {ORBIS_PAD_BUTTON_OPTIONS, BtnOptions}, {ORBIS_PAD_BUTTON_TOUCH_PAD, BtnTouchpad},
        {ORBIS_PAD_BUTTON_L3, BtnL3}, {ORBIS_PAD_BUTTON_R3, BtnR3},
    };
    for (auto &m : map) if (d.buttons & m.from) s.buttons |= m.to;
    s.lx = d.leftStick.x;
    s.ly = d.leftStick.y;
    s.rx = d.rightStick.x;
    s.ry = d.rightStick.y;
    return s;
}

}  // namespace ml::ps4
