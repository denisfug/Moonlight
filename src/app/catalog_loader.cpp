#include "app/catalog_loader.hpp"

namespace ml {

CatalogLoader::~CatalogLoader()
{
    quit_ = true;
    // a request in progress can take up to its timeout (15 s)
    for (int i = 0; i < 1700 && running_.load(); i++) platform_.sleepMs(10);
}

int CatalogLoader::request(const std::string &url, const UrlPolicy &policy)
{
    if (!http_ || url.empty()) return 0;
    if (!started_.exchange(true)) {
        running_ = true;
        if (!platform_.startThread(&CatalogLoader::threadMain, this, "ml_catalogs")) {
            running_ = false;
            started_ = false;
            return 0;
        }
    }
    lock();
    int t = ++next_;
    queue_.push_back({t, url, policy});
    unlock();
    return t;
}

void CatalogLoader::cancelAll()
{
    lock();
    queue_.clear();
    done_.clear();
    unlock();
}

std::vector<CatalogLoader::Result> CatalogLoader::take()
{
    std::vector<Result> out;
    lock();
    out.swap(done_);
    unlock();
    return out;
}

bool CatalogLoader::busy()
{
    lock();
    bool b = !queue_.empty() || active_.load() > 0;
    unlock();
    return b;
}

void CatalogLoader::threadMain(void *self) { static_cast<CatalogLoader *>(self)->run(); }

void CatalogLoader::run()
{
    while (!quit_.load()) {
        Job job;
        bool have = false;
        lock();
        if (!queue_.empty()) {
            job = queue_.front();
            queue_.pop_front();
            have = true;
            active_++;
        }
        unlock();
        if (!have) { platform_.sleepMs(20); continue; }
        Result r;
        r.ticket = job.ticket;
        r.error = SourceManager::fetchCatalogAt(http_, job.url, job.policy, r.metas);
        lock();
        done_.push_back(std::move(r));
        active_--;
        unlock();
    }
    running_ = false;
}

}  // namespace ml
