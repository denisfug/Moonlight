#include "app/problem_log.hpp"

#include "core/net.hpp"

namespace ml {

void ProblemLog::load()
{
    entries_.clear();
    Json j = store_.load("problems.json");
    const Json &list = j["problems"];
    for (size_t i = 0; i < list.size() && entries_.size() < kMax; i++) {
        const Json &e = list[i];
        Problem p;
        p.when = e["when"].asInt(0);
        p.title = e["title"].asString();
        p.poster = e["poster"].asString();
        p.source = e["source"].asString();
        p.where = e["where"].asString();
        p.reason = e["reason"].asString();
        p.details = e["details"].asString();
        if (!p.reason.empty()) entries_.push_back(std::move(p));
    }
}

void ProblemLog::save()
{
    Json list = Json::array();
    for (const auto &p : entries_) {
        Json e = Json::object();
        e.set("when", Json::number(double(p.when)));
        e.set("title", Json::string(p.title));
        e.set("poster", Json::string(p.poster));
        e.set("source", Json::string(p.source));
        e.set("where", Json::string(p.where));
        e.set("reason", Json::string(p.reason));
        e.set("details", Json::string(p.details));
        list.push(e);
    }
    Json j = Json::object();
    j.set("problems", list);
    store_.save("problems.json", j);
}

void ProblemLog::add(Problem p)
{
    if (p.details.size() > 4000) p.details.resize(4000);
    if (!entries_.empty() && entries_[0].title == p.title && entries_[0].reason == p.reason) {
        entries_[0] = std::move(p);
    } else {
        entries_.insert(entries_.begin(), std::move(p));
        if (entries_.size() > kMax) entries_.resize(kMax);
    }
    save();
}

void ProblemLog::clear()
{
    entries_.clear();
    save();
}

std::string hostForLog(const std::string &location)
{
    Url u = parseUrl(location);
    if (u.valid) return u.host;
    return location.empty() || location[0] == '/' ? "arquivo local" : "";
}

}  // namespace ml
