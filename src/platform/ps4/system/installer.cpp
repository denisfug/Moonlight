// Package install through the system background-transfer service (libSceBgft) — the same service the
// PS4 uses for its own downloads. Flow and the 0x80990088 "already installed" code follow the public
// PS4-4PT homebrew (facts only, no code copied); types from OpenOrbis orbis/Bgft.h (flatz's bgft.h).
// Status: compiles; NOT yet tested on a console. Experimental.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <orbis/AppInstUtil.h>
#include <orbis/Bgft.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>

#include "platform/ps4/ps4_platform.hpp"

namespace ml::ps4 {

namespace {
constexpr int32_t kAlreadyInstalled = int32_t(0x80990088u);
constexpr const char *kTitleId = "MLHB00001";

std::string hex(int v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", unsigned(v));
    return b;
}
}  // namespace

bool Ps4Platform::initInstaller(std::string *err)
{
    if (installerReady_) return true;
    int r = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL);
    if (r < 0) { *err = "modulo AppInstUtil " + hex(r); return false; }
    r = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_BGFT);
    if (r < 0) { *err = "modulo BGFT " + hex(r); return false; }
    r = sceAppInstUtilInitialize();
    if (r != 0) { *err = "sceAppInstUtilInitialize " + hex(r); return false; }
    // The service keeps using this heap for as long as the app runs.
    const size_t heapSize = 1024 * 1024;
    void *heap = std::calloc(1, heapSize);
    if (!heap) { *err = "sem memoria para o instalador"; return false; }
    OrbisBgftInitParams ip;
    std::memset(&ip, 0, sizeof ip);
    ip.heap = heap;
    ip.heapSize = heapSize;
    r = sceBgftServiceIntInit(&ip);
    if (r != 0) { std::free(heap); *err = "sceBgftServiceIntInit " + hex(r); return false; }
    installerReady_ = true;
    return true;
}

int Ps4Platform::installPackage(const std::string &path, bool replaceInstalled, std::string *err)
{
    std::string e;
    if (!initInstaller(&e)) { if (err) *err = e; return -1; }
    int32_t user = -1;
    if (sceUserServiceGetForegroundUser(&user) != 0 && sceUserServiceGetInitialUser(&user) != 0) {
        if (err) *err = "usuario do PS4 nao encontrado";
        return -1;
    }
    if (replaceInstalled) {
        int r = sceAppInstUtilAppUnInstall(kTitleId);
        if (r != 0) { if (err) *err = "remover a versao atual falhou " + hex(r); return -1; }
    }
    OrbisBgftDownloadParamEx p;
    std::memset(&p, 0, sizeof p);
    p.params.entitlementType = 5;
    p.params.id = kTitleId;
    p.params.userId = user;
    p.params.contentUrl = path.c_str();
    p.params.contentName = "Moonlight HB";
    p.params.playgoScenarioId = "0";
    p.params.option = ORBIS_BGFT_TASK_OPT_NONE;   // keep the .pkg (GoldHEN installer as fallback)
    p.slot = 0;
    OrbisBgftTaskId task = -1;
    int r = sceBgftServiceIntDownloadRegisterTaskByStorageEx(&p, &task);
    if (r == kAlreadyInstalled) return 1;
    if (r != 0) { if (err) *err = "registrar instalacao " + hex(r); return -1; }
    r = sceBgftServiceDownloadStartTask(task);
    if (r != 0) { if (err) *err = "iniciar instalacao " + hex(r); return -1; }
    return 0;
}

}  // namespace ml::ps4
