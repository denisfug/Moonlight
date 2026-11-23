// AudioOut port: 48 kHz stereo S16, 1024-frame blocks (the format proven by our GoldHEN music plugin).
// sceSystemServiceTickVideoPlayback() is called every ~5s of output to suppress screensaver.
// sceSystemServiceDisableMusicPlayer() is called on open() to prevent system BGM conflicts.
// Both APIs are confirmed available in OpenOrbis (orbis/SystemService.h).
// NOTA TÉCNICA — Níveis de reprodução em segundo plano:
//   Nível 1 (player interno): implementado — áudio toca enquanto Moonlight está ativo.
//   Nível 2 (continuar durante jogos): BLOQUEADO — o PS4 termina o processo do aplicativo
//     ao abrir um jogo (kernel SceShellCore SIGKILL, não contornável via homebrew).
//     GoldHEN plugins (.sprx) sobrevivem no shell, mas o SDK GoldHEN Plugins não expõe
//     APIs de áudio (sceAudioOut requer processo de app, não shell context).
//   Nível 3 (Menu Rápido nativo): BLOQUEADO — requer APIs Sony privadas
//     (SceNpCommerce + sistema interno de notificação de mídia) não disponíveis no OpenOrbis.
//     Nenhum homebrew alcançou isso. Registrar como limitação permanente até mudança de API.
#include <orbis/AudioOut.h>
#include <orbis/SystemService.h>

#include "platform/ps4/ps4_platform.hpp"

namespace ml::ps4 {

namespace {
class Ps4AudioOut : public AudioOut {
public:
    bool open() override
    {
        sceAudioOutInit();   // already initialised is fine
        // Prevent system BGM (PS4 background music player) from conflicting with our output.
        sceSystemServiceDisableMusicPlayer();
        handle_ = sceAudioOutOpen(0xFF, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, 1024, 48000, ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
        tickCounter_ = 0;
        return handle_ >= 0;
    }
    int framesPerBlock() const override { return 1024; }
    bool write(const int16_t *s) override
    {
        if (handle_ < 0) return false;
        bool ok = sceAudioOutOutput(handle_, s) >= 0;
        // Tick screensaver suppression every ~5 seconds (48000/1024 ≈ 47 blocks/s × 5 ≈ 235).
        if (ok && ++tickCounter_ >= 235) {
            sceSystemServiceTickVideoPlayback();
            tickCounter_ = 0;
        }
        return ok;
    }
    void close() override
    {
        if (handle_ < 0) return;
        sceAudioOutOutput(handle_, nullptr);
        sceAudioOutClose(handle_);
        handle_ = -1;
        // Re-enable system music player so the XMB BGM can resume after Moonlight exits.
        sceSystemServiceReenableMusicPlayer();
    }
    ~Ps4AudioOut() override { close(); }

private:
    int handle_      = -1;
    int tickCounter_ = 0;
};
}  // namespace

std::unique_ptr<AudioOut> makeAudioOut() { return std::unique_ptr<AudioOut>(new Ps4AudioOut); }

}  // namespace ml::ps4
