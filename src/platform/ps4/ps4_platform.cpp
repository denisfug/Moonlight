#include "platform/ps4/ps4_platform.hpp"

#include <orbis/libkernel.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(ML_HAVE_GOLDHEN)
#include "GoldHEN.h"
#endif

namespace ml::ps4 {

bool Ps4Platform::init(std::string *err)
{
    if (!video_.init(err)) return false;
    if (!pad_.init(err)) notify("Moonlight HB: " + *err + " (continuando)");
    std::string netErr;
    httpOk_ = http_.init(&netErr);
    if (!httpOk_) notify("Moonlight HB: internet indisponivel (" + netErr + ")");
    return true;
}

void Ps4Platform::afterAssetsLoaded()
{
#if defined(ML_HAVE_GOLDHEN)
    static jailbreak_backup jb;   // kept for the app's lifetime: /data and USB stay reachable
    jailbroken_ = sys_sdk_jailbreak(&jb) == 0;
    if (!jailbroken_) notify("Moonlight HB: acesso total aos arquivos indisponivel (GoldHEN SDK?)");
#endif
}

std::vector<std::string> Ps4Platform::musicDirs() const
{
    std::vector<std::string> d{"/data/moonlight/music"};
    for (int i = 0; i < 8; i++) d.push_back("/mnt/usb" + std::to_string(i) + "/Music");
    return d;
}

uint64_t Ps4Platform::nowMs() { return sceKernelGetProcessTime() / 1000; }
void Ps4Platform::sleepMs(unsigned ms) { sceKernelUsleep(ms * 1000); }

void Ps4Platform::notify(const std::string &text)
{
    auto *r = static_cast<OrbisNotificationRequest *>(std::calloc(1, sizeof(OrbisNotificationRequest)));
    if (!r) return;
    r->type = NotificationRequest;
    r->targetId = -1;
    std::snprintf(r->message, sizeof r->message, "%s", text.c_str());
    sceKernelSendNotificationRequest(0, r, sizeof *r, 0);
    std::free(r);
}

namespace {
struct ThreadStart {
    void (*fn)(void *);
    void *arg;
};
void *trampoline(void *p)
{
    ThreadStart s = *static_cast<ThreadStart *>(p);
    delete static_cast<ThreadStart *>(p);
    s.fn(s.arg);
    return nullptr;
}
}  // namespace

bool Ps4Platform::startThread(void (*fn)(void *), void *arg, const char *name)
{
    OrbisPthread t;
    auto *s = new ThreadStart{fn, arg};
    if (scePthreadCreate(&t, nullptr, trampoline, s, name) != 0) { delete s; return false; }
    scePthreadDetach(t);
    return true;
}

}  // namespace ml::ps4
