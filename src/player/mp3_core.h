/*
 * player_core — platform-independent MP3 streaming decoder + resampler.
 * Output: interleaved stereo int16 at 48000 Hz (the only rate PS4 AudioOut takes).
 * The caller provides a read callback, so the same code runs on PS4 and on the PC tests.
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stddef.h>

#define PC_OUT_RATE 48000

typedef long (*pc_read_fn)(void *ctx, void *buf, size_t len);   /* returns bytes, 0 = EOF, <0 = error */

typedef struct pc_player pc_player;

/* Allocates ~40 KiB. Returns NULL on allocation failure. */
pc_player *pc_create(void);
void pc_destroy(pc_player *p);

/* Starts a new stream (skips an ID3v2 tag if present). */
void pc_start(pc_player *p, pc_read_fn read, void *ctx);

/* Fills `frames` stereo frames (2*frames int16) into out. Returns frames written;
 * fewer than requested means end of stream (rest of out is zero-filled). */
size_t pc_render(pc_player *p, int16_t *out, size_t frames);

/* Gain in 1/256 units (256 = unity). */
void pc_set_gain(pc_player *p, int gain_q8);

/* Info about the stream (valid after the first frame was decoded). */
int pc_src_rate(const pc_player *p);
int pc_src_channels(const pc_player *p);
int pc_had_error(const pc_player *p);

/* Total duration of an in-memory MP3 (skips an ID3v2 tag). 0 if no frames. */
long pc_scan_duration_ms(const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
