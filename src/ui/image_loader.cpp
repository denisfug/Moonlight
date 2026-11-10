#include "ui/image_loader.hpp"

#include <cstdlib>

extern "C" {
unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *comp, int req_comp);
void stbi_image_free(void *retval_from_stbi_load);
}

namespace ml::ui {

bool decodeImage(const std::string &bytes, int w, int h, Image &out)
{
    if (bytes.empty() || w <= 0 || h <= 0 || bytes.size() > 0x7FFFFFFF) return false;
    int sw = 0, sh = 0, comp = 0;
    unsigned char *px = stbi_load_from_memory(reinterpret_cast<const unsigned char *>(bytes.data()), int(bytes.size()), &sw, &sh, &comp, 3);
    if (!px) return false;
    if (sw <= 0 || sh <= 0 || int64_t(sw) * sh > 40LL * 1000 * 1000) { stbi_image_free(px); return false; }
    // cover crop: scale so the image fills w x h, centred
    double scale = std::max(double(w) / sw, double(h) / sh);
    double cropW = w / scale, cropH = h / scale;
    double ox = (sw - cropW) / 2, oy = (sh - cropH) / 2;
    out.w = w;
    out.h = h;
    out.px.assign(size_t(w) * h, 0xFF000000u);
    // box filter over the source area of each destination pixel (good for downscaling posters)
    for (int y = 0; y < h; y++) {
        int y0 = int(oy + y / scale), y1 = std::max(y0 + 1, int(oy + (y + 1) / scale));
        y1 = std::min(y1, sh);
        for (int x = 0; x < w; x++) {
            int x0 = int(ox + x / scale), x1 = std::max(x0 + 1, int(ox + (x + 1) / scale));
            x1 = std::min(x1, sw);
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1; sy += 1 + (y1 - y0) / 4)
                for (int sx = x0; sx < x1; sx += 1 + (x1 - x0) / 4) {
                    const unsigned char *p = px + (size_t(sy) * sw + sx) * 3;
                    r += p[0]; g += p[1]; b += p[2]; n++;
                }
            if (n) out.px[size_t(y) * w + x] = 0xFF000000u | (r / n) << 16 | (g / n) << 8 | (b / n);
        }
    }
    stbi_image_free(px);
    return true;
}

void ImageLoader::start()
{
    if (started_.exchange(true) || !http_) return;
    if (!platform_.startThread(&ImageLoader::threadMain, this, "ml_images")) started_ = false;
}

const Image *ImageLoader::get(const std::string &url, int w, int h)
{
    if (url.empty() || !http_) return nullptr;
    std::string key = url + "#" + std::to_string(w) + "x" + std::to_string(h);
    lock();
    auto it = done_.find(key);
    const Image *res = it != done_.end() ? &it->second : nullptr;
    if (!res && !failed_.count(key) && !queued_.count(key)) {
        Url u = parseUrl(url);
        bool ok = u.valid && (u.scheme == "http" || http_->supportsHttps());
        if (ok) {
            queued_[key] = true;
            queue_.push_back({key, url, w, h});
            if (queue_.size() > 48) { queued_.erase(queue_.front().key); queue_.pop_front(); }   // keep recent requests
        } else {
            failed_[key] = true;
        }
    }
    unlock();
    return res;   // valid until the next trim() (called by the UI thread between frames)
}

void ImageLoader::trim()
{
    lock();
    if (done_.size() > kMaxImages) done_.clear();
    unlock();
}

void ImageLoader::threadMain(void *self) { static_cast<ImageLoader *>(self)->run(); }

void ImageLoader::run()
{
    for (;;) {
        Job job;
        bool have = false;
        lock();
        if (!queue_.empty()) { job = queue_.back(); queue_.pop_back(); have = true; }   // newest first (on screen)
        unlock();
        if (!have) { platform_.sleepMs(30); continue; }
        HttpRequest req;
        req.url = job.url;
        req.timeoutMs = 10000;
        req.maxBytes = kMaxBytes;
        HttpResponse r = http_->get(req);
        Image img;
        bool ok = r.ok() && decodeImage(r.body, job.w, job.h, img);
        lock();
        queued_.erase(job.key);
        if (ok) {
            // Only inserts here (unordered_map keeps element addresses stable); trimming happens on the
            // UI thread in trim(), between frames, so a pointer from get() is valid for the whole frame.
            done_[job.key] = std::move(img);
        } else {
            failed_[job.key] = true;
        }
        unlock();
        if (ok) updated_ = true;
    }
}

}  // namespace ml::ui
