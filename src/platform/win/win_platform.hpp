// Windows desktop platform — janela SDL2 + renderizador software (Canvas -> SDL_Texture -> tela).
// Herda FileSystem, Network, Audio e threads do HostPlatform (POSIX / std::).
// Substitui Display (janela real) e Input (teclado + mouse + gamepad).
// Compilado apenas quando ML_WIN está definido.
#pragma once
#include "platform/host/host_platform.hpp"
#include <SDL2/SDL.h>
#include <atomic>
#include <mutex>

#ifndef ML_WIN_VERSION
#define ML_WIN_VERSION "dev"
#endif

namespace ml::win {

// ---------------------------------------------------------------- display
// Mantém um framebuffer em software (igual ao PS4) e faz blit para a janela SDL a cada present().
class WinDisplay : public Display {
public:
    WinDisplay(SDL_Window *win, int w, int h);
    ~WinDisplay() override;

    Framebuffer backBuffer() override { return {pixels_.data(), w_, h_, w_}; }
    void present() override;   // SDL_UpdateTexture + SDL_RenderCopy + SDL_RenderPresent

    int width()  const { return w_; }
    int height() const { return h_; }

private:
    SDL_Window   *win_      = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture  *texture_  = nullptr;
    int w_, h_;
    std::vector<uint32_t> pixels_;
};

// ---------------------------------------------------------------- input
// Lê eventos SDL e mapeia para PadState (igual ao controle PS4 na interface).
// Mouse: setas virtuais via cursor nas bordas/cards (hover simples por enquanto).
// Teclado: teclas de seta → D-pad, Enter → Cross, Esc → Circle, etc.
// Gamepad SDL: DualShock 4 / DualSense reconhecidos como joystick.
class WinInput : public Input {
public:
    WinInput();
    ~WinInput() override;

    PadState read() override;

    // Chamado pelo loop principal — processa fila de eventos SDL antes do read().
    // Retorna false quando a janela foi fechada.
    bool pollEvents();

    bool quit = false;

private:
    PadState state_;
    SDL_GameController *gc_ = nullptr;

    // Mapeamento: botão SDL → bit Button do Moonlight
    uint32_t sdlButtonToMl(SDL_GameControllerButton b) const;
};

// ---------------------------------------------------------------- platform
class WinPlatform : public host::HostPlatform {
public:
    // sandbox: diretório de dados do usuário (%APPDATA%\Moonlight ou argumento).
    // assetsDir: onde estão os assets (fonts, etc.) — normalmente ao lado do .exe.
    WinPlatform(const std::string &sandbox, const std::string &assetsDir,
                int w = 1920, int h = 1080);
    ~WinPlatform() override;

    bool initWindow(std::string *err);   // cria janela SDL; chame antes de App::init()

    std::string name() const override { return "win"; }
    Display &display() override { return *winDisplay_; }
    Input   &input()   override { return winInput_; }

    void notify(const std::string &text) override;
    bool startTextInput(const std::string &title, const std::string &initial, bool url) override;
    int  pollTextInput(std::string *out) override;

    // Retorna false quando a janela foi fechada (sinal para encerrar o loop principal).
    bool pollEvents() { return !winInput_.quit; }

    std::string dataDir()  const override { return dataDir_; }
    std::string assetDir() const override { return "/app0/assets"; }   // mapeado pelo HostFileSystem

    // Sem músicas locais no Desktop por enquanto (pode ser adicionado depois).
    std::vector<std::string> musicDirs() const override { return {}; }

    std::unique_ptr<VideoEngine> makeVideoEngine() override;

private:
    std::string dataDir_;
    int w_, h_;
    std::unique_ptr<WinDisplay> winDisplay_;
    WinInput winInput_;

    // IME simples via SDL_StartTextInput
    bool    imeActive_  = false;
    std::string imeText_;
    bool    imeDone_    = false;
    bool    imeCancelled_ = false;
};

}  // namespace ml::win
