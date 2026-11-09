// AudioOut port: 48 kHz stereo S16, 1024-frame blocks (the format proven by our GoldHEN music plugin).
#include <orbis/AudioOut.h>

#include "platform/ps4/ps4_platform.hpp"

namespace ml::ps4 {

namespace {
class Ps4AudioOut : public AudioOut {
public:
    bool open() override
    {
        sceAudioOutInit();   // already initialised is fine
        handle_ = sceAudioOutOpen(0xFF, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, 1024, 48000, ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
        return handle_ >= 0;
    }
    int framesPerBlock() const override { return 1024; }
    bool write(const int16_t *s) override { return handle_ >= 0 && sceAudioOutOutput(handle_, s) >= 0; }
    void close() override
    {
        if (handle_ < 0) return;
        sceAudioOutOutput(handle_, nullptr);
        sceAudioOutClose(handle_);
        handle_ = -1;
    }
    ~Ps4AudioOut() override { close(); }

private:
    int handle_ = -1;
};
}  // namespace

std::unique_ptr<AudioOut> makeAudioOut() { return std::unique_ptr<AudioOut>(new Ps4AudioOut); }

}  // namespace ml::ps4
