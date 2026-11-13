#include "mp3_core.h"

#include <stdlib.h>
#include <string.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3/minimp3.h"

#define IN_BUF  (16 * 1024)

struct pc_player {
    mp3dec_t dec;
    pc_read_fn read;
    void *ctx;
    uint8_t in[IN_BUF];
    size_t in_len;
    int eof;
    int error;

    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];  /* decoded frame, interleaved */
    int pcm_frames;                               /* frames in pcm */
    int pcm_pos;                                  /* integer position of next src frame */
    int channels;
    int rate;

    /* resampler: output frame k maps to source position k * rate / 48000 */
    uint32_t frac;          /* fractional part, scaled by PC_OUT_RATE */
    int16_t prev_l, prev_r; /* last source frame of the previous chunk, for interpolation */
    int have_prev;
    int gain_q8;
};

pc_player *pc_create(void)
{
    pc_player *p = calloc(1, sizeof *p);
    if (p) p->gain_q8 = 256;
    return p;
}

void pc_destroy(pc_player *p) { free(p); }
void pc_set_gain(pc_player *p, int g) { p->gain_q8 = g < 0 ? 0 : g; }
int pc_src_rate(const pc_player *p) { return p->rate; }
int pc_src_channels(const pc_player *p) { return p->channels; }
int pc_had_error(const pc_player *p) { return p->error; }

static void fill(pc_player *p)
{
    while (!p->eof && p->in_len < IN_BUF) {
        long n = p->read(p->ctx, p->in + p->in_len, IN_BUF - p->in_len);
        if (n < 0) { p->error = 1; p->eof = 1; break; }
        if (n == 0) { p->eof = 1; break; }
        p->in_len += (size_t)n;
    }
}

static void consume(pc_player *p, size_t n)
{
    if (n > p->in_len) n = p->in_len;
    memmove(p->in, p->in + n, p->in_len - n);
    p->in_len -= n;
}

/* Skip "ID3" v2 header: 10 bytes + syncsafe size (+10 if footer flag). */
static void skip_id3(pc_player *p)
{
    fill(p);
    if (p->in_len < 10 || memcmp(p->in, "ID3", 3) != 0) return;
    size_t sz = ((size_t)(p->in[6] & 0x7f) << 21) | ((size_t)(p->in[7] & 0x7f) << 14) |
                ((size_t)(p->in[8] & 0x7f) << 7) | (size_t)(p->in[9] & 0x7f);
    sz += 10 + ((p->in[5] & 0x10) ? 10 : 0);
    while (sz > 0 && (p->in_len > 0 || !p->eof)) {
        size_t c = sz < p->in_len ? sz : p->in_len;
        consume(p, c);
        sz -= c;
        if (sz) fill(p);
    }
}

void pc_start(pc_player *p, pc_read_fn read, void *ctx)
{
    int g = p->gain_q8;
    memset(p, 0, sizeof *p);
    p->gain_q8 = g;
    mp3dec_init(&p->dec);
    p->read = read;
    p->ctx = ctx;
    skip_id3(p);
}

/* Decode the next frame into p->pcm. Returns 0 at end of stream. */
static int next_frame(pc_player *p)
{
    for (;;) {
        fill(p);
        if (p->in_len == 0) return 0;
        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&p->dec, p->in, (int)p->in_len, p->pcm, &info);
        if (info.frame_bytes == 0) {
            /* no frame found in the whole buffer: drop it (garbage) or finish */
            if (p->eof) return 0;
            consume(p, p->in_len > 1 ? p->in_len - 1 : 1);
            continue;
        }
        consume(p, (size_t)info.frame_bytes);
        if (samples == 0) continue;               /* skipped data (e.g. Xing frame) */
        if (info.channels < 1 || info.channels > 2 || info.hz < 8000 || info.hz > 48000) {
            p->error = 1;
            return 0;
        }
        p->channels = info.channels;
        p->rate = info.hz;
        p->pcm_frames = samples;
        return 1;
    }
}

static inline void src_frame(const pc_player *p, int i, int *l, int *r)
{
    if (p->channels == 2) { *l = p->pcm[2 * i]; *r = p->pcm[2 * i + 1]; }
    else { *l = *r = p->pcm[i]; }
}

static inline int16_t clip(long v)
{
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

size_t pc_render(pc_player *p, int16_t *out, size_t frames)
{
    size_t done = 0;
    while (done < frames) {
        if (p->pcm_pos >= p->pcm_frames) {
            if (p->pcm_frames > 0) {
                int l, r;
                src_frame(p, p->pcm_frames - 1, &l, &r);
                p->prev_l = (int16_t)l; p->prev_r = (int16_t)r; p->have_prev = 1;
            }
            p->pcm_pos -= p->pcm_frames;
            p->pcm_frames = 0;
            if (!next_frame(p)) break;
            continue;
        }
        /* linear interpolation between src[pos-1] and src[pos] at fraction frac/OUT_RATE */
        int l1, r1, l0, r0;
        src_frame(p, p->pcm_pos, &l1, &r1);
        if (p->pcm_pos > 0) src_frame(p, p->pcm_pos - 1, &l0, &r0);
        else if (p->have_prev) { l0 = p->prev_l; r0 = p->prev_r; }
        else { l0 = l1; r0 = r1; }
        long t = (long)p->frac;   /* 0..OUT_RATE-1 */
        long l = l0 + ((long)(l1 - l0) * t) / PC_OUT_RATE;
        long r = r0 + ((long)(r1 - r0) * t) / PC_OUT_RATE;
        out[2 * done]     = clip((l * p->gain_q8) >> 8);
        out[2 * done + 1] = clip((r * p->gain_q8) >> 8);
        done++;
        /* advance source position by rate/OUT_RATE */
        p->frac += (uint32_t)p->rate;
        while (p->frac >= PC_OUT_RATE) { p->frac -= PC_OUT_RATE; p->pcm_pos++; }
    }
    if (done < frames) memset(out + 2 * done, 0, (frames - done) * 2 * sizeof(int16_t));
    return done;
}

/* Duration by walking frame headers without decoding PCM (minimp3 skips synthesis when pcm == NULL). */
long pc_scan_duration_ms(const uint8_t *buf, size_t len)
{
    mp3dec_t dec;
    mp3dec_frame_info_t info;
    mp3dec_init(&dec);
    size_t off = 0;
    if (len > 10 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') {
        size_t tag = ((buf[6] & 0x7F) << 21 | (buf[7] & 0x7F) << 14 | (buf[8] & 0x7F) << 7 | (buf[9] & 0x7F)) + 10;
        if (buf[5] & 0x10) tag += 10;
        if (tag < len) off = tag;
    }
    double ms = 0;
    while (off < len) {
        int remain = len - off > 0x7FFFFFFF ? 0x7FFFFFFF : (int)(len - off);
        int samples = mp3dec_decode_frame(&dec, buf + off, remain, NULL, &info);
        if (info.frame_bytes <= 0) break;
        off += (size_t)info.frame_bytes;
        if (samples > 0 && info.hz > 0) ms += samples * 1000.0 / info.hz;
    }
    return (long)ms;
}
