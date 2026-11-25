// Ponto de entrada — Moonlight Desktop (Windows/Linux com SDL2).
// Abre diretamente na tela inicial, sem intro.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

#include "app/app.hpp"
#include "platform/win/win_platform.hpp"

#ifndef ML_VERSION
#define ML_VERSION "dev"
#endif

static std::string defaultDataDir()
{
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, buf)))
        return std::string(buf) + "\\Moonlight";
    return "data";
#else
    const char *home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.moonlight";
#endif
}

static std::string exeDir()
{
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, sizeof buf);
    std::string s = buf;
    auto pos = s.rfind('\\');
    return pos != std::string::npos ? s.substr(0, pos) : ".";
#else
    return ".";
#endif
}

int main(int argc, char **argv)
{
    std::string dataDir  = defaultDataDir();
    std::string assetsDir = exeDir() + "/assets";

    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--data=", 7) == 0)   dataDir   = argv[i] + 7;
        if (std::strncmp(argv[i], "--assets=", 9) == 0) assetsDir = argv[i] + 9;
    }

    std::fprintf(stderr, "Moonlight HB %s (Desktop)\n", ML_VERSION);
    std::fprintf(stderr, "  data:   %s\n", dataDir.c_str());
    std::fprintf(stderr, "  assets: %s\n", assetsDir.c_str());

    ml::win::WinPlatform platform(dataDir, assetsDir);

    std::string err;
    if (!platform.initWindow(&err)) {
        std::fprintf(stderr, "ERRO: %s\n", err.c_str());
        return 1;
    }

    ml::App app(platform);
    if (!app.init(&err)) {
        std::fprintf(stderr, "ERRO: app.init: %s\n", err.c_str());
        return 1;
    }

    // Loop principal — idêntico ao PS4 mas com poll de eventos SDL.
    for (unsigned n = 0;; n++) {
        if (!platform.pollEvents()) break;   // janela fechada

        if (n < 120 || n % 60 == 0) app.dirty();
        app.frame();
        if (!app.renderedLastFrame()) platform.sleepMs(16);
    }

    return 0;
}
