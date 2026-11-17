#include "library/library.hpp"

#include <algorithm>

namespace ml {

namespace {

Json toJson(const LibraryItem &i)
{
    Json o = Json::object();
    o.set("id", Json::string(i.id));
    o.set("type", Json::string(i.type));
    o.set("name", Json::string(i.name));
    o.set("poster", Json::string(i.poster));
    o.set("videoId", Json::string(i.videoId));
    o.set("season", Json::number(i.season));
    o.set("episode", Json::number(i.episode));
    o.set("positionMs", Json::number(static_cast<double>(i.positionMs)));
    o.set("durationMs", Json::number(static_cast<double>(i.durationMs)));
    o.set("updatedAt", Json::number(static_cast<double>(i.updatedAt)));
    return o;
}

LibraryItem fromJson(const Json &o)
{
    LibraryItem i;
    i.id = o["id"].asString();
    i.type = o["type"].asString();
    i.name = o["name"].asString();
    i.poster = o["poster"].asString();
    i.videoId = o["videoId"].asString();
    i.season = static_cast<int>(o["season"].asInt());
    i.episode = static_cast<int>(o["episode"].asInt());
    i.positionMs = o["positionMs"].asInt();
    i.durationMs = o["durationMs"].asInt();
    i.updatedAt = o["updatedAt"].asInt();
    return i;
}

void readList(const Json &a, std::vector<LibraryItem> &out)
{
    out.clear();
    for (size_t k = 0; k < a.size(); k++) {
        LibraryItem i = fromJson(a[k]);
        if (!i.id.empty()) out.push_back(std::move(i));
    }
}

Json writeList(const std::vector<LibraryItem> &v)
{
    Json a = Json::array();
    for (const auto &i : v) a.push(toJson(i));
    return a;
}

constexpr double kFinished = 0.92;

}  // namespace

void Library::load()
{
    Json j = store_.load("library.json");
    readList(j["favorites"], favorites_);
    readList(j["history"], history_);
}

bool Library::save()
{
    Json j = Json::object();
    j.set("version", Json::number(1));
    j.set("favorites", writeList(favorites_));
    j.set("history", writeList(history_));
    return store_.save("library.json", j);
}

bool Library::isFavorite(const std::string &id) const
{
    return std::any_of(favorites_.begin(), favorites_.end(), [&](const LibraryItem &i) { return i.id == id; });
}

void Library::toggleFavorite(const LibraryItem &item, int64_t now)
{
    auto it = std::find_if(favorites_.begin(), favorites_.end(), [&](const LibraryItem &i) { return i.id == item.id; });
    if (it != favorites_.end()) favorites_.erase(it);
    else {
        LibraryItem c = item;
        c.updatedAt = now;
        favorites_.insert(favorites_.begin(), c);
    }
    save();
}

void Library::updateProgress(const LibraryItem &item, int64_t now)
{
    // one entry per title: a series keeps the last watched episode
    history_.erase(std::remove_if(history_.begin(), history_.end(), [&](const LibraryItem &i) { return i.id == item.id; }),
                   history_.end());
    LibraryItem c = item;
    c.updatedAt = now;
    history_.insert(history_.begin(), c);
    if (history_.size() > kMaxHistory) history_.resize(kMaxHistory);
    save();
}

std::vector<LibraryItem> Library::continueWatching(size_t max) const
{
    std::vector<LibraryItem> v;
    for (const auto &i : history_) {
        if (i.positionMs <= 0 || i.progress() >= kFinished) continue;
        v.push_back(i);
        if (v.size() >= max) break;
    }
    return v;
}

const LibraryItem *Library::progressFor(const std::string &id, const std::string &videoId) const
{
    for (const auto &i : history_)
        if (i.id == id && (videoId.empty() || i.videoId == videoId)) return &i;
    return nullptr;
}

}  // namespace ml
