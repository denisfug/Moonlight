// Declarations for the PS4 system media player library (libSceAvPlayer).
// The OpenOrbis headers only list the function names (no parameters or types), so these were written
// for Moonlight from the interface as documented by the shadPS4 emulator project
// (src/core/libraries/avplayer/avplayer.h, GPL-2.0-or-later) — only the ABI facts (field order, sizes,
// argument lists) are used here, no code. Sizes are checked with static_asserts below.
// Status: compiles; NOT yet validated on a console (see docs/MEDIA_ENGINE.md).
#pragma once
#include <cstddef>
#include <cstdint>

extern "C" {

typedef void *MlAvPlayerHandle;

typedef void *(*MlAvAllocate)(void *obj, uint32_t align, uint32_t size);
typedef void (*MlAvDeallocate)(void *obj, void *mem);
struct MlAvMemAllocator {
    void *object;
    MlAvAllocate allocate;
    MlAvDeallocate deallocate;
    MlAvAllocate allocateTexture;     // video frames: must be GPU-visible memory
    MlAvDeallocate deallocateTexture;
};

typedef int32_t (*MlAvOpenFile)(void *obj, const char *name);
typedef int32_t (*MlAvCloseFile)(void *obj);
typedef int32_t (*MlAvReadOffset)(void *obj, uint8_t *buf, uint64_t pos, uint32_t len);
typedef uint64_t (*MlAvSizeFile)(void *obj);
struct MlAvFileReplacement {   // all null = the library opens files itself
    void *object;
    MlAvOpenFile open;
    MlAvCloseFile close;
    MlAvReadOffset readOffset;
    MlAvSizeFile size;
};

enum MlAvEvent : int32_t {
    MlAvStateStop = 0x01,
    MlAvStateReady = 0x02,
    MlAvStatePlay = 0x03,
    MlAvStatePause = 0x04,
    MlAvStateBuffering = 0x05,
    MlAvTimedText = 0x10,
    MlAvWarning = 0x20,
    MlAvEncryption = 0x30,
    MlAvDrmError = 0x40,
};
typedef void (*MlAvEventCallback)(void *obj, int32_t event, int32_t srcId, void *data);
struct MlAvEventReplacement {
    void *object;
    MlAvEventCallback callback;
};

struct MlAvInitData {
    MlAvMemAllocator memory;
    MlAvFileReplacement file;
    MlAvEventReplacement event;
    int32_t debugLevel;            // 0 none .. 3 all
    uint32_t basePriority;
    int32_t numOutputVideoFramebuffers;
    bool autoStart;
    uint8_t reserved[3];
    const char *defaultLanguage;
};

struct MlAvAudioEx {
    uint16_t channelCount;
    uint8_t reserved[2];
    uint32_t sampleRate;
    uint32_t size;
    uint8_t languageCode[4];
    uint8_t reserved1[64];
};
struct MlAvVideoEx {
    uint32_t width, height;
    float aspectRatio;
    uint8_t languageCode[4];
    uint32_t framerate;
    uint32_t cropLeft, cropRight, cropTop, cropBottom;
    uint32_t pitch;
    uint8_t lumaBitDepth, chromaBitDepth;
    bool fullRange;
    uint8_t reserved1[37];
};
union MlAvStreamDetailsEx {
    MlAvAudioEx audio;
    MlAvVideoEx video;
    uint8_t reserved[80];
};
struct MlAvFrameInfoEx {
    void *data;                    // video: NV12 (Y plane, then interleaved UV); audio: S16 PCM
    uint8_t reserved[4];
    uint64_t timestamp;            // ms
    MlAvStreamDetailsEx details;
};

struct MlAvAudio {
    uint16_t channelCount;
    uint8_t reserved1[2];
    uint32_t sampleRate;
    uint32_t size;
    char languageCode[4];
};
union MlAvStreamDetails {
    uint8_t reserved[16];
    MlAvAudio audio;
};
struct MlAvFrameInfo {
    uint8_t *data;
    uint8_t reserved[4];
    uint64_t timestamp;
    MlAvStreamDetails details;
};

struct MlAvStreamInfo {
    uint32_t type;                 // 0 video, 1 audio, 2 timed text
    uint8_t reserved[4];
    MlAvStreamDetails details;
    uint64_t duration;             // ms
    uint64_t startTime;
};

MlAvPlayerHandle sceAvPlayerInit(MlAvInitData *data);
int32_t sceAvPlayerAddSource(MlAvPlayerHandle h, const char *path);
int32_t sceAvPlayerStart(MlAvPlayerHandle h);
int32_t sceAvPlayerStop(MlAvPlayerHandle h);
int32_t sceAvPlayerPause(MlAvPlayerHandle h);
int32_t sceAvPlayerResume(MlAvPlayerHandle h);
int32_t sceAvPlayerClose(MlAvPlayerHandle h);
bool sceAvPlayerIsActive(MlAvPlayerHandle h);
uint64_t sceAvPlayerCurrentTime(MlAvPlayerHandle h);
int32_t sceAvPlayerJumpToTime(MlAvPlayerHandle h, uint64_t ms);
int32_t sceAvPlayerStreamCount(MlAvPlayerHandle h);
int32_t sceAvPlayerGetStreamInfo(MlAvPlayerHandle h, uint32_t streamId, MlAvStreamInfo *info);
bool sceAvPlayerGetVideoDataEx(MlAvPlayerHandle h, MlAvFrameInfoEx *info);
bool sceAvPlayerGetAudioData(MlAvPlayerHandle h, MlAvFrameInfo *info);

}  // extern "C"

static_assert(sizeof(MlAvMemAllocator) == 40, "AvPlayer ABI");
static_assert(sizeof(MlAvFileReplacement) == 40, "AvPlayer ABI");
static_assert(sizeof(MlAvEventReplacement) == 16, "AvPlayer ABI");
static_assert(sizeof(MlAvInitData) == 120, "AvPlayer ABI");
static_assert(sizeof(MlAvStreamDetailsEx) == 80, "AvPlayer ABI");
static_assert(sizeof(MlAvFrameInfoEx) == 104, "AvPlayer ABI");
static_assert(sizeof(MlAvFrameInfo) == 40, "AvPlayer ABI");
static_assert(sizeof(MlAvStreamInfo) == 40, "AvPlayer ABI");
