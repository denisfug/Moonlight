#include "app/updater.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "core/json.hpp"
#include "mbedtls/sha256.h"

namespace ml {

int parseVersion(const std::string &s)
{
    size_t i = 0;
    while (i < s.size() && !std::isdigit(static_cast<unsigned char>(s[i]))) i++;
    if (i == s.size()) return -1;
    char *end = nullptr;
    long major = std::strtol(s.c_str() + i, &end, 10);
    long minor = 0;
    if (end && *end == '.') minor = std::strtol(end + 1, nullptr, 10);
    if (major < 0 || minor < 0 || minor > 999) return -1;
    return int(major * 1000 + minor);
}

Updater::~Updater()
{
    quit_ = true;
    for (int i = 0; i < 3000 && busy_.load(); i++) platform_.sleepMs(10);
}

Updater::State Updater::state()
{
    lock();
    State s = state_;
    unlock();
    return s;
}

void Updater::set(const State &s)
{
    lock();
    state_ = s;
    unlock();
}

void Updater::check()
{
    if (busy_.exchange(true)) return;
    State s = state();
    s.phase = Phase::Checking;
    s.error.clear();
    set(s);
    if (!platform_.startThread(&Updater::checkMain, this, "ml_update")) {
        s.phase = Phase::Failed;
        s.error = "nao iniciou a thread";
        set(s);
        busy_ = false;
    }
}

void Updater::download()
{
    State s = state();
    if (s.assetUrl.empty() || (s.phase != Phase::Available && s.phase != Phase::Failed)) return;
    if (busy_.exchange(true)) return;
    s.phase = Phase::Downloading;
    s.done = 0;
    s.error.clear();
    set(s);
    if (!platform_.startThread(&Updater::downloadMain, this, "ml_update_dl")) {
        s.phase = Phase::Failed;
        s.error = "nao iniciou a thread";
        set(s);
        busy_ = false;
    }
}

void Updater::checkMain(void *self) { static_cast<Updater *>(self)->runCheck(); }
void Updater::downloadMain(void *self) { static_cast<Updater *>(self)->runDownload(); }

std::string Updater::parseRelease(const std::string &json, std::string *tag, std::string *assetUrl, int64_t *size, std::string *title)
{
    JsonError err;
    Json j = parseJson(json, &err, JsonLimits{2 * 1024 * 1024, 32});
    if (!err.ok()) return "resposta invalida do GitHub";
    *tag = j["tag_name"].str("");
    if (tag->empty()) return "nenhuma versao publicada";
    if (title) *title = j["name"].str("");
    const Json &assets = j["assets"];
    for (size_t i = 0; i < assets.size(); i++) {
        std::string name = assets[i]["name"].str("");
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".pkg") == 0) {
            *assetUrl = assets[i]["browser_download_url"].str("");
            *size = int64_t(assets[i]["size"].asInt(0));
            if (!assetUrl->empty()) return "";
        }
    }
    return "a versao " + *tag + " nao tem arquivo .pkg";
}

std::string Updater::parseFeed(const std::string &json, const std::string &feedUrl, State *out)
{
    JsonError err;
    Json j = parseJson(json, &err, JsonLimits{256 * 1024, 16});
    if (!err.ok()) return "update.json invalido";
    std::string v = j["version"].str(""), pkg = j["pkg"].str("");
    if (parseVersion(v) < 0 || pkg.empty()) return "update.json sem versao/pkg";
    if (pkg.compare(0, 8, "https://") != 0) pkg = feedUrl.substr(0, feedUrl.rfind('/') + 1) + pkg;
    out->latest = v.substr(v.find_first_of("0123456789"));
    out->assetUrl = pkg;
    out->size = j["size"].asInt(0);
    out->sha256 = j["sha256"].str("");
    out->notes = j["notes"].str("");
    return "";
}

void Updater::runCheck()
{
    State s = state();
    HttpClient *http = platform_.http();
    if (!http) {
        s.phase = Phase::Failed;
        s.error = "sem rede";
        set(s);
        busy_ = false;
        return;
    }
    // 1. update.json on the updates branch
    if (!feedUrl.empty()) {
        HttpRequest f;
        f.url = feedUrl;
        f.timeoutMs = 15000;
        f.maxBytes = 256 * 1024;
        HttpResponse fr = http->get(f);
        State n = s;
        if (fr.ok() && parseFeed(fr.body, feedUrl, &n).empty()) {
            n.error.clear();
            n.phase = parseVersion(n.latest) > parseVersion(current_) ? Phase::Available : Phase::UpToDate;
            set(n);
            busy_ = false;
            return;
        }
    }
    // 2. latest GitHub Release
    HttpRequest q;
    q.url = apiBase + "/repos/" + repo_ + "/releases/latest";
    q.timeoutMs = 15000;
    q.maxBytes = 2 * 1024 * 1024;
    q.headers.push_back({"Accept", "application/vnd.github+json"});
    HttpResponse r = http->get(q);
    std::string tag, url, title;
    int64_t size = 0;
    if (!r.error.empty()) {
        s.phase = Phase::Failed;
        s.error = r.error;
    } else if (r.status == 404) {
        s.phase = Phase::UpToDate;   // no release published yet
        s.latest.clear();
    } else if (r.status != 200) {
        s.phase = Phase::Failed;
        s.error = "GitHub respondeu HTTP " + std::to_string(r.status);
    } else {
        std::string e = parseRelease(r.body, &tag, &url, &size, &title);
        if (!e.empty()) {
            s.phase = Phase::Failed;
            s.error = e;
        } else {
            size_t v = tag.find_first_of("0123456789");
            s.latest = v == std::string::npos ? tag : tag.substr(v);
            s.assetUrl = url;
            s.size = size;
            s.notes = title;
            s.sha256.clear();
            s.phase = parseVersion(tag) > parseVersion(current_) ? Phase::Available : Phase::UpToDate;
        }
    }
    set(s);
    busy_ = false;
}

void Updater::runDownload()
{
    State s = state();
    HttpClient *http = platform_.http();
    std::string err;
    HttpRequest q;
    q.url = s.assetUrl;
    q.timeoutMs = 30000;
    std::unique_ptr<HttpStream> st = http ? http->openStream(q, &err) : nullptr;
    std::string data;
    if (st) {
        data.reserve(size_t(s.size > 0 ? s.size : 8 << 20));
        char buf[32768];
        for (;;) {
            if (quit_) { err = "cancelado"; break; }
            long n = st->read(buf, sizeof buf);
            if (n < 0) { err = "download interrompido"; break; }
            if (n == 0) break;
            data.append(buf, size_t(n));
            if (data.size() > 256u * 1024 * 1024) { err = "arquivo grande demais"; break; }
            lock();
            state_.done = int64_t(data.size());
            unlock();
        }
        if (err.empty() && s.size > 0 && int64_t(data.size()) != s.size) err = "download incompleto";
        if (err.empty() && (data.size() < 4 || data.compare(0, 4, "\x7F" "CNT") != 0)) err = "o arquivo baixado nao e um PKG";
        if (err.empty() && !s.sha256.empty()) {
            unsigned char d[32];
            mbedtls_sha256(reinterpret_cast<const unsigned char *>(data.data()), data.size(), d, 0);
            char hex[65];
            for (int i = 0; i < 32; i++) std::snprintf(hex + 2 * i, 3, "%02x", d[i]);
            std::string want = s.sha256;
            for (auto &ch : want) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            if (want != hex) err = "arquivo baixado diferente do publicado (sha256)";
        }
    } else if (err.empty()) {
        err = "sem rede";
    }
    if (err.empty()) {
        // 01.14 on the console: "nao gravou em /data/pkg". Try the usual places in order and report
        // every failure with its system code.
        FileSystem &fs = platform_.fs();
        const char *dirs[] = {"/data/pkg", "/data/moonlight/pkg", "/mnt/usb0", "/mnt/usb1"};
        std::string tried;
        bool saved = false;
        for (const char *d : dirs) {
            if (!fs.makeDirs(d) && !fs.exists(d)) { tried += std::string(d) + ": " + fs.lastError() + "; "; continue; }
            std::string path = std::string(d) + "/Moonlight-HB_" + s.latest + ".pkg";
            if (fs.writeFile(path, data)) { s.path = path; saved = true; break; }
            tried += std::string(d) + ": " + (fs.lastError().empty() ? std::string("falhou") : fs.lastError()) + "; ";
        }
        if (!saved) err = "nao gravou o arquivo (" + tried + "espaco livre?)";
    }
    if (err.empty()) {
        s.phase = Phase::Downloaded;
        s.done = int64_t(data.size());
    } else {
        s.phase = Phase::Failed;
        s.error = err;
    }
    set(s);
    busy_ = false;
}

}  // namespace ml
