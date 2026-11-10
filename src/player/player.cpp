#include "player/player.hpp"

#include <cstring>
#include <vector>

#include "player/mp3_core.h"

namespace ml {

AudioPlayer::~AudioPlayer() { stop(); }

void AudioPlayer::joinThread()
{
    quit_ = true;
    // A network stream can be blocked in a read (timeout 4 s), so wait a little longer for it.
    int tries = remote_ ? 1000 : 400;
    for (int i = 0; i < tries && threadRunning_.load(); i++) platform_.sleepMs(5);
    quit_ = false;
}

bool AudioPlayer::openStream(const std::string &url, HttpClient *http, std::string *error, bool live, int64_t durationMs)
{
    stop();
    data_.clear();
    Url u = parseUrl(url);
    // Radio streams are audio only (decoded as MP3 frames, nothing else is interpreted), and most
    // stations still serve plain http, so http is accepted here; API calls still require https.
    if (!u.valid) { if (error) *error = "endereco da radio invalido"; state_ = PlayState::Error; return false; }
    if (!http) { if (error) *error = "rede indisponivel nesta plataforma"; state_ = PlayState::Error; return false; }
    if (u.scheme == "https" && !http->supportsHttps()) { if (error) *error = "HTTPS indisponivel"; state_ = PlayState::Error; return false; }
    live_ = live;
    remote_ = true;
    streamUrl_ = u.str();
    http_ = http;
    durationMs_ = live ? 0 : durationMs;
    positionMs_ = 0;
    seekTo_ = -1;
    framesOut_ = 0;
    lastError_.clear();
    state_ = PlayState::Ready;
    return true;
}

bool AudioPlayer::open(const std::string &path, std::string *error)
{
    stop();
    data_.clear();
    live_ = false;
    remote_ = false;
    if (!platform_.fs().readFile(path, data_, kMaxFileBytes) || data_.empty()) {
        if (error) *error = "nao foi possivel ler o arquivo";
        state_ = PlayState::Error;
        return false;
    }
    durationMs_ = pc_scan_duration_ms(reinterpret_cast<const uint8_t *>(data_.data()), data_.size());
    if (durationMs_ <= 0) {
        if (error) *error = "arquivo nao contem audio MP3 valido";
        state_ = PlayState::Error;
        data_.clear();
        return false;
    }
    positionMs_ = 0;
    seekTo_ = -1;
    framesOut_ = 0;
    state_ = PlayState::Ready;
    return true;
}

void AudioPlayer::play()
{
    PlayState s = state_.load();
    if (s == PlayState::Idle || s == PlayState::Error) return;
    if (s == PlayState::Ended) { seekTo_ = 0; }
    state_ = PlayState::Playing;
    if (!threadRunning_.load()) {
        threadRunning_ = true;
        if (!platform_.startThread(&AudioPlayer::threadMain, this, "ml_audio")) {
            threadRunning_ = false;
            state_ = PlayState::Error;
        }
    }
}

void AudioPlayer::pause()
{
    if (state_.load() == PlayState::Playing) state_ = PlayState::Paused;
}

void AudioPlayer::stop()
{
    joinThread();
    if (state_.load() != PlayState::Idle && state_.load() != PlayState::Error) state_ = PlayState::Ready;
    positionMs_ = 0;
    seekTo_ = -1;
}

void AudioPlayer::seek(int64_t ms)
{
    if (remote_) return;   // streams play from the start (no seeking yet)
    if (ms < 0) ms = 0;
    if (durationMs_ > 0 && ms > durationMs_) ms = durationMs_;
    seekTo_ = ms;
    positionMs_ = ms;
}

void AudioPlayer::setVolume(int p) { volume_ = p < 0 ? 0 : (p > 100 ? 100 : p); }

void AudioPlayer::threadMain(void *self) { static_cast<AudioPlayer *>(self)->run(); }

namespace {
struct MemReader {
    const std::string *data;
    size_t pos;
};
long streamRead(void *ctx, void *buf, size_t len)
{
    long n = static_cast<HttpStream *>(ctx)->read(buf, len);
    return n < 0 ? -1 : n;
}
long memRead(void *ctx, void *buf, size_t len)
{
    auto *r = static_cast<MemReader *>(ctx);
    size_t n = r->data->size() - r->pos;
    if (n > len) n = len;
    std::memcpy(buf, r->data->data() + r->pos, n);
    r->pos += n;
    return static_cast<long>(n);
}
}  // namespace

void AudioPlayer::run()
{
    std::unique_ptr<AudioOut> out = platform_.makeAudioOut();
    pc_player *dec = pc_create();
    if (!out || !out->open() || !dec) {
        state_ = PlayState::Error;
        if (dec) pc_destroy(dec);
        threadRunning_ = false;
        return;
    }
    std::unique_ptr<HttpStream> stream;
    if (remote_) {
        HttpRequest req;
        req.url = streamUrl_;
        req.timeoutMs = 4000;   // per read: a station that stalls this long counts as dropped
        std::string err;
        stream = http_->openStream(req, &err);
        if (!stream) {
            lastError_ = (live_ ? "nao conectou na radio: " : "nao conectou no servidor: ") + err;
            state_ = PlayState::Error;
            out->close();
            pc_destroy(dec);
            threadRunning_ = false;
            return;
        }
        pc_start(dec, streamRead, stream.get());
    }
    const int block = out->framesPerBlock();
    std::vector<int16_t> buf(static_cast<size_t>(block) * 2);
    MemReader rd{&data_, 0};
    uint64_t framesAtBase = 0;   // output frames since the last (re)start
    int64_t baseMs = 0;
    bool started = remote_;   // streams start where the server begins sending, never seek

    while (!quit_.load()) {
        int64_t want = seekTo_.exchange(-1);
        if (want >= 0 || !started) {
            if (want < 0) want = positionMs_.load();
            // Constant-bitrate approximation: byte offset proportional to time; minimp3 resyncs on
            // the next frame header. Good for CBR files, approximate for VBR.
            size_t audioStart = 0;
            const auto *b = reinterpret_cast<const unsigned char *>(data_.data());
            if (data_.size() > 10 && b[0] == 'I' && b[1] == 'D' && b[2] == '3')
                audioStart = 10 + ((b[6] & 0x7F) << 21 | (b[7] & 0x7F) << 14 | (b[8] & 0x7F) << 7 | (b[9] & 0x7F));
            if (audioStart > data_.size()) audioStart = 0;
            double ratio = durationMs_ > 0 ? double(want) / double(durationMs_) : 0;
            rd.pos = want <= 0 ? 0 : audioStart + static_cast<size_t>(ratio * double(data_.size() - audioStart));
            if (rd.pos > data_.size()) rd.pos = data_.size();
            pc_start(dec, memRead, &rd);
            baseMs = want;
            framesAtBase = 0;
            started = true;
        }
        if (state_.load() != PlayState::Playing) {
            if (state_.load() == PlayState::Paused) {
                std::memset(buf.data(), 0, buf.size() * sizeof(int16_t));
                out->write(buf.data());   // keep the port fed with silence while paused
                continue;
            }
            break;
        }
        pc_set_gain(dec, volume_.load() * 256 / 100);
        size_t got = pc_render(dec, buf.data(), static_cast<size_t>(block));
        out->write(buf.data());
        framesOut_ += static_cast<uint64_t>(block);
        framesAtBase += got;
        positionMs_ = baseMs + static_cast<int64_t>(framesAtBase * 1000 / 48000);
        if (got < static_cast<size_t>(block)) {   // end of stream
            if (live_) {
                lastError_ = pc_had_error(dec) ? "a radio enviou dados que nao sao MP3" : "a transmissao da radio caiu";
                state_ = PlayState::Error;
                break;
            }
            positionMs_ = durationMs_;
            state_ = PlayState::Ended;
            break;
        }
    }
    out->close();
    pc_destroy(dec);
    threadRunning_ = false;
}

const char *VideoPlayer::unavailableReason()
{
    return "Este aparelho nao tem player de video (no PC de testes nao existe; no PS4 o player do sistema "
           "nao carregou). Veja docs/MEDIA_ENGINE.md.";
}

bool VideoPlayer::open(const std::string &, std::string *error)
{
    if (error) *error = unavailableReason();
    return false;
}

}  // namespace ml
