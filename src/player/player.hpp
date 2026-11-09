// Playback abstraction. Audio and video have independent implementations.
//   AudioPlayer: local MP3 via minimp3 -> platform AudioOut (48 kHz stereo). Works on PC tests.
//   VideoPlayer: no engine integrated yet; open() fails with an explicit reason (docs/MEDIA_ENGINE.md).
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "platform/platform.hpp"

namespace ml {

enum class PlayState { Idle, Ready, Playing, Paused, Ended, Error };

class MediaPlayer {
public:
    virtual ~MediaPlayer() = default;
    virtual bool open(const std::string &location, std::string *error) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(int64_t ms) = 0;
    virtual void setVolume(int percent) = 0;   // 0..100
    virtual int volume() const = 0;
    virtual int64_t getPosition() const = 0;   // ms
    virtual int64_t getDuration() const = 0;   // ms, 0 = unknown
    virtual PlayState state() const = 0;
};

class AudioPlayer : public MediaPlayer {
public:
    explicit AudioPlayer(Platform &p) : platform_(p) {}
    ~AudioPlayer() override;
    bool open(const std::string &path, std::string *error) override;   // local file path
    void play() override;
    void pause() override;
    void stop() override;
    void seek(int64_t ms) override;
    void setVolume(int percent) override;
    int volume() const override { return volume_.load(); }
    int64_t getPosition() const override { return positionMs_.load(); }
    int64_t getDuration() const override { return durationMs_; }
    PlayState state() const override { return state_.load(); }
    // Frames written to the audio device since open() (diagnostics/tests).
    uint64_t framesOut() const { return framesOut_.load(); }

    static constexpr size_t kMaxFileBytes = 64 * 1024 * 1024;

private:
    Platform &platform_;
    std::string data_;
    int64_t durationMs_ = 0;
    std::atomic<PlayState> state_{PlayState::Idle};
    std::atomic<int64_t> positionMs_{0};
    std::atomic<int64_t> seekTo_{-1};
    std::atomic<int> volume_{80};
    std::atomic<bool> quit_{false};
    std::atomic<bool> threadRunning_{false};
    std::atomic<uint64_t> framesOut_{0};

    static void threadMain(void *self);
    void run();
    void joinThread();
};

class VideoPlayer : public MediaPlayer {
public:
    bool open(const std::string &location, std::string *error) override;
    void play() override {}
    void pause() override {}
    void stop() override {}
    void seek(int64_t) override {}
    void setVolume(int p) override { volume_ = p; }
    int volume() const override { return volume_; }
    int64_t getPosition() const override { return 0; }
    int64_t getDuration() const override { return 0; }
    PlayState state() const override { return PlayState::Error; }
    static const char *unavailableReason();

private:
    int volume_ = 80;
};

}  // namespace ml
