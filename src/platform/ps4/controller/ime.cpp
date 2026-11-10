// System keyboard (libSceImeDialog). The dialog is drawn by the system over our frames, so the app keeps
// presenting while it is open. Note: Sony's wchar_t is 16-bit (UTF-16) while our toolchain's is 32-bit, so
// the buffers are uint16_t and passed through casts.
// Status: compiles; not yet validated on a console. The app falls back to its own keyboard on failure.
#include <orbis/CommonDialog.h>
#include <orbis/ImeDialog.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>

#include <cstring>

#include "platform/ps4/ps4_platform.hpp"

namespace ml::ps4 {

namespace {
size_t toUtf16(const std::string &s, uint16_t *out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size() && n + 1 < cap;) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        int len = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (len == 1) cp = c;
        else {
            cp = c & (0x3F >> (len - 1));
            for (int k = 1; k < len && i + size_t(k) < s.size(); k++) cp = (cp << 6) | (static_cast<unsigned char>(s[i + size_t(k)]) & 0x3F);
        }
        i += size_t(len);
        if (cp > 0xFFFF) cp = '?';   // BMP is enough for URLs, names and searches
        out[n++] = uint16_t(cp);
    }
    out[n] = 0;
    return n;
}

std::string fromUtf16(const uint16_t *in)
{
    std::string o;
    for (; *in; in++) {
        uint32_t cp = *in;
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
        else { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
    }
    return o;
}
}  // namespace

bool Ps4Platform::startTextInput(const std::string &title, const std::string &initial, bool url)
{
    if (!imeReady_) {
        if (sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG) < 0) return false;
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG);
        sceCommonDialogInitialize();   // "already initialised" is fine
        imeReady_ = true;
    }
    int32_t user = 0;
    if (sceUserServiceGetInitialUser(&user) != 0) return false;
    toUtf16(initial, imeText_, sizeof imeText_ / sizeof imeText_[0]);
    toUtf16(title, imeTitle_, sizeof imeTitle_ / sizeof imeTitle_[0]);
    OrbisImeDialogSetting s;
    std::memset(&s, 0, sizeof s);
    s.userId = uint32_t(user);
    s.type = url ? ORBIS_TYPE_TYPE_URL : ORBIS_TYPE_DEFAULT;
    s.enterLabel = ORBIS_BUTTON_LABEL_DEFAULT;
    s.maxTextLength = 2048;
    s.inputTextBuffer = reinterpret_cast<wchar_t *>(imeText_);
    s.posx = 1920 / 2;
    s.posy = 1080 / 2;
    s.horizontalAlignment = ORBIS_H_CENTER;
    s.verticalAlignment = ORBIS_V_CENTER;
    s.title = reinterpret_cast<const wchar_t *>(imeTitle_);
    return sceImeDialogInit(&s, nullptr) >= 0;
}

int Ps4Platform::pollTextInput(std::string *out)
{
    OrbisDialogStatus st = sceImeDialogGetStatus();
    if (st == ORBIS_DIALOG_STATUS_RUNNING) return 0;
    if (st != ORBIS_DIALOG_STATUS_STOPPED) return -1;
    OrbisDialogResult r;
    std::memset(&r, 0, sizeof r);
    sceImeDialogGetResult(&r);
    sceImeDialogTerm();
    if (r.endstatus != ORBIS_DIALOG_OK) return -1;
    if (out) *out = fromUtf16(imeText_);
    return 1;
}

}  // namespace ml::ps4
