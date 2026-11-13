// Automatic .pkg install for the self-updater (ps4). The modules libSceAppInstUtil / libSceBgft are
// loaded DYNAMICALLY here (sceKernelLoadStartModule) and the entry points resolved with sceKernelDlsym,
// so nothing is linked at load time: that is what caused the start crash in 01.10 (a linked system lib
// pulling an unresolved import / init touched before main). Here the module is only brought in when the
// user actually asks to install, and every failure is returned by value with its system code.
#include <orbis/libkernel.h>

#include <cstdio>
#include <cstring>

#include "platform/ps4/ps4_platform.hpp"

namespace ml::ps4 {

namespace {
std::string hex(long v)
{
    char b[24];
    std::snprintf(b, sizeof b, "0x%08lX", (unsigned long)(uint32_t)v);
    return b;
}

// Signatures copied verbatim from orbis/AppInstUtil.h (resolved by name, never linked).
using FnInit = int32_t (*)(void);
using FnInstall = int32_t (*)(const char *pkgPath, void *reserved);
using FnTitle = int32_t (*)(const char *pkgPath, char *titleId, int32_t *isApp);
using FnExists = int32_t (*)(const char *titleId, int32_t *exists);

void *sym(int32_t mod, const char *name)
{
    void *p = nullptr;
    sceKernelDlsym(mod, name, &p);
    return p;
}
}  // namespace

int Ps4Platform::installPackage(const std::string &path, bool replaceInstalled, std::string *err)
{
    auto fail = [&](const std::string &m) {
        if (err) *err = m;
        return -1;
    };

    if (installMod_ <= 0) {
        int32_t h = (int32_t)sceKernelLoadStartModule("/system/common/lib/libSceAppInstUtil.sprx", 0, nullptr, 0,
                                                       nullptr, nullptr);
        if (h < 0) return fail("carregar libSceAppInstUtil " + hex(h));
        installMod_ = h;
    }

    auto init = (FnInit)sym(installMod_, "sceAppInstUtilInitialize");
    auto install = (FnInstall)sym(installMod_, "sceAppInstUtilAppInstallPkg");
    auto getTitle = (FnTitle)sym(installMod_, "sceAppInstUtilGetTitleIdFromPkg");
    auto appExists = (FnExists)sym(installMod_, "sceAppInstUtilAppExists");
    if (!init || !install) return fail("simbolos do instalador nao encontrados");

    init();   // "already initialised" is harmless

    // SAFETY (01.20): we NEVER uninstall first. 01.19 called sceAppInstUtilAppPrepareOverwritePkg,
    // which removes the app before installing; when the install did not complete the app was gone
    // (owner reported "apagou o app"). Here, if the title is already installed we only ask the caller
    // to confirm (return 1), then install directly over it. sceAppInstUtilAppInstallPkg overwrites a
    // fake PKG of the same title in place; if it refuses, it returns an error and we fall back to the
    // manual path with the app still installed. A failed install can no longer delete the app.
    if (!replaceInstalled && getTitle && appExists) {
        char titleId[64];
        std::memset(titleId, 0, sizeof titleId);
        int32_t isApp = 0;
        if (getTitle(path.c_str(), titleId, &isApp) >= 0 && titleId[0]) {
            int32_t exists = 0;
            if (appExists(titleId, &exists) >= 0 && exists) return 1;
        }
    }

    int32_t r = install(path.c_str(), nullptr);
    if (r < 0) return fail("sceAppInstUtilAppInstallPkg " + hex(r));
    return 0;
}

}  // namespace ml::ps4
