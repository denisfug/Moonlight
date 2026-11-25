// Windows desktop platform — SDL2 Display + Input sobre HostPlatform.
#include "platform/win/win_platform.hpp"
#include "platform/win/win_video.hpp"

#include <cstdio>
#include <cstring>

namespace ml::win {

// ================================================================ WinDisplay

WinDisplay::WinDisplay(SDL_Window *win, int w, int h)
    : win_(win), w_(w), h_(h), pixels_(static_cast<size_t>(w) * h, 0xFF000000u)
{
    renderer_ = SDL_CreateRenderer(win_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_)
        renderer_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);

    // ARGB8888 = 0xAARRGGBB em memória little-endian — igual ao Canvas do Moonlight.
    texture_ = SDL_CreateTexture(renderer_,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        w_, h_);
}

WinDisplay::~WinDisplay()
{
    if (texture_)  SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    // A janela é destruída pelo WinPlatform.
}

void WinDisplay::present()
{
    if (!texture_ || !renderer_) return;
    SDL_UpdateTexture(texture_, nullptr, pixels_.data(), w_ * 4);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

// ================================================================ WinInput

WinInput::WinInput()
{
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    // Abre o primeiro controle disponível (DualShock 4, DualSense, Xbox…)
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            gc_ = SDL_GameControllerOpen(i);
            if (gc_) {
                std::fprintf(stderr, "[win] controle: %s\n",
                    SDL_GameControllerName(gc_));
                break;
            }
        }
    }
    state_.connected = true;
}

WinInput::~WinInput()
{
    if (gc_) SDL_GameControllerClose(gc_);
}

uint32_t WinInput::sdlButtonToMl(SDL_GameControllerButton b) const
{
    // DualShock 4 / DualSense: SDL mapeia Cross→A, Circle→B, Square→X, Triangle→Y
    switch (b) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:        return BtnUp;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:      return BtnDown;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:      return BtnLeft;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:     return BtnRight;
        case SDL_CONTROLLER_BUTTON_A:              return BtnCross;
        case SDL_CONTROLLER_BUTTON_B:              return BtnCircle;
        case SDL_CONTROLLER_BUTTON_X:              return BtnSquare;
        case SDL_CONTROLLER_BUTTON_Y:              return BtnTriangle;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:   return BtnL1;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:  return BtnR1;
        case SDL_CONTROLLER_BUTTON_BACK:           return BtnOptions;
        case SDL_CONTROLLER_BUTTON_START:          return BtnOptions;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:      return BtnL3;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:     return BtnR3;
        default: return 0;
    }
}

bool WinInput::pollEvents()
{
    // Zera botões a cada poll — o App::frame() chama read() logo depois.
    // Mantemos apenas o estado do eixo analógico (lx/ly).
    state_.buttons = 0;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            quit = true;
            return false;

        // ---- teclado ----
        case SDL_KEYDOWN: {
            switch (ev.key.keysym.sym) {
            case SDLK_UP:        state_.buttons |= BtnUp;      break;
            case SDLK_DOWN:      state_.buttons |= BtnDown;    break;
            case SDLK_LEFT:      state_.buttons |= BtnLeft;    break;
            case SDLK_RIGHT:     state_.buttons |= BtnRight;   break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:  state_.buttons |= BtnCross;   break;
            case SDLK_BACKSPACE:
            case SDLK_ESCAPE:    state_.buttons |= BtnCircle;  break;
            case SDLK_PAGEUP:    state_.buttons |= BtnL1;      break;
            case SDLK_PAGEDOWN:  state_.buttons |= BtnR1;      break;
            case SDLK_TAB:       state_.buttons |= BtnR1;      break;
            case SDLK_SPACE:     state_.buttons |= BtnCross;   break;
            case SDLK_F1:        state_.buttons |= BtnTriangle; break;
            case SDLK_F4:        state_.buttons |= BtnOptions; break;
            case SDLK_F11:
                // Alterna tela cheia (a janela é acessada via evento window ID)
                {
                    SDL_Window *w = SDL_GetWindowFromID(ev.key.windowID);
                    if (w) {
                        Uint32 flags = SDL_GetWindowFlags(w);
                        SDL_SetWindowFullscreen(w,
                            (flags & SDL_WINDOW_FULLSCREEN_DESKTOP)
                                ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                }
                break;
            default: break;
            }
            break;
        }

        // ---- mouse (scroll = navegar) ----
        case SDL_MOUSEWHEEL:
            if (ev.wheel.y > 0) state_.buttons |= BtnUp;
            if (ev.wheel.y < 0) state_.buttons |= BtnDown;
            if (ev.wheel.x < 0) state_.buttons |= BtnLeft;
            if (ev.wheel.x > 0) state_.buttons |= BtnRight;
            break;

        // ---- clique esquerdo = Cross (confirmar) ----
        case SDL_MOUSEBUTTONDOWN:
            if (ev.button.button == SDL_BUTTON_LEFT)  state_.buttons |= BtnCross;
            if (ev.button.button == SDL_BUTTON_RIGHT) state_.buttons |= BtnCircle;
            break;

        // ---- gamepad ----
        case SDL_CONTROLLERBUTTONDOWN:
            state_.buttons |= sdlButtonToMl(
                static_cast<SDL_GameControllerButton>(ev.cbutton.button));
            break;

        case SDL_CONTROLLERAXISMOTION: {
            // Eixo analógico esquerdo → D-pad digital (threshold 50%)
            if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                state_.lx = uint8_t((ev.caxis.value + 32768) >> 8);
                if (ev.caxis.value < -16384) state_.buttons |= BtnLeft;
                if (ev.caxis.value >  16384) state_.buttons |= BtnRight;
            }
            if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                state_.ly = uint8_t((ev.caxis.value + 32768) >> 8);
                if (ev.caxis.value < -16384) state_.buttons |= BtnUp;
                if (ev.caxis.value >  16384) state_.buttons |= BtnDown;
            }
            // Gatilhos L2/R2
            if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT  && ev.caxis.value > 16384)
                state_.buttons |= BtnL2;
            if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT && ev.caxis.value > 16384)
                state_.buttons |= BtnR2;
            break;
        }

        case SDL_CONTROLLERDEVICEADDED:
            if (!gc_ && SDL_IsGameController(ev.cdevice.which)) {
                gc_ = SDL_GameControllerOpen(ev.cdevice.which);
                if (gc_)
                    std::fprintf(stderr, "[win] controle conectado: %s\n",
                        SDL_GameControllerName(gc_));
            }
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            if (gc_ && SDL_GameControllerGetJoystick(gc_) ==
                       SDL_JoystickFromInstanceID(ev.cdevice.which)) {
                SDL_GameControllerClose(gc_);
                gc_ = nullptr;
                std::fprintf(stderr, "[win] controle desconectado\n");
            }
            break;

        // IME / entrada de texto (usado por startTextInput)
        case SDL_TEXTINPUT:
            // Acumulado pelo WinPlatform via evento interno — não tratado aqui.
            break;

        default: break;
        }
    }
    return true;
}

PadState WinInput::read()
{
    return state_;
}

// ================================================================ WinPlatform

WinPlatform::WinPlatform(const std::string &sandbox, const std::string &assetsDir,
                         int w, int h)
    : host::HostPlatform(sandbox, assetsDir, w, h), dataDir_(sandbox), w_(w), h_(h)
{
}

WinPlatform::~WinPlatform()
{
    winDisplay_.reset();
    SDL_Quit();
}

bool WinPlatform::initWindow(std::string *err)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        if (err) *err = std::string("SDL_Init falhou: ") + SDL_GetError();
        return false;
    }

    SDL_Window *win = SDL_CreateWindow(
        "Moonlight HB",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w_, h_,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!win) {
        if (err) *err = std::string("SDL_CreateWindow falhou: ") + SDL_GetError();
        return false;
    }

    winDisplay_ = std::unique_ptr<WinDisplay>(new WinDisplay(win, w_, h_));
    return true;
}

void WinPlatform::notify(const std::string &text)
{
    std::fprintf(stderr, "[moonlight] %s\n", text.c_str());
}

bool WinPlatform::startTextInput(const std::string &/*title*/,
                                  const std::string &initial, bool /*url*/)
{
    imeText_      = initial;
    imeDone_      = false;
    imeCancelled_ = false;
    imeActive_    = true;
    SDL_StartTextInput();
    return false;   // usa teclado próprio do Moonlight (IME SDL é bônus futuro)
}

int WinPlatform::pollTextInput(std::string *out)
{
    if (!imeActive_) return -1;
    if (imeCancelled_) { imeActive_ = false; return -1; }
    if (imeDone_)      { imeActive_ = false; if (out) *out = imeText_; return 1; }
    return 0;
}

std::unique_ptr<VideoEngine> WinPlatform::makeVideoEngine()
{
    return std::make_unique<WinVideoEngine>();
}

}  // namespace ml::win
