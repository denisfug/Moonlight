#include "app/multi_fetch.hpp"

#include "addons/source_manager.hpp"

namespace ml {

namespace {
std::atomic<int> gActive{0};   // constant-initialised: no static constructor in the PS4 binary
}

int MultiFetch::activeThreads() { return gActive.load(); }

std::shared_ptr<MultiFetch> MultiFetch::start(Platform &p, std::vector<Item> items)
{
    std::shared_ptr<MultiFetch> mf(new MultiFetch(p));
    mf->slots_.resize(items.size());
    for (size_t i = 0; i < items.size(); i++) {
        Slot &s = mf->slots_[i];
        s.label = std::move(items[i].label);
        s.url = std::move(items[i].url);
        s.policy = items[i].policy;
        s.timeoutMs = items[i].timeoutMs;
    }
    for (size_t i = 0; i < mf->slots_.size(); i++) {
        Arg *arg = new Arg{mf, i};
        gActive++;
        if (!p.startThread(&MultiFetch::threadMain, arg, "ml_fetch")) {
            gActive--;
            delete arg;
            mf->lock();
            mf->slots_[i].state = State::Failed;
            mf->slots_[i].error = "nao iniciou a busca";
            mf->unlock();
        }
    }
    return mf;
}

void MultiFetch::threadMain(void *p)
{
    Arg *arg = static_cast<Arg *>(p);
    arg->self->run(arg->index);
    delete arg;   // may release the last reference to the MultiFetch
    gActive--;
}

void MultiFetch::run(size_t index)
{
    Slot &s = slots_[index];   // url/policy/timeout are read-only after start
    uint64_t t0 = platform_.nowMs();
    Json j;
    std::string e = SourceManager::fetchJsonAt(platform_.http(), s.url, s.policy, j, s.timeoutMs);
    int secs = int((platform_.nowMs() - t0 + 500) / 1000);
    lock();
    s.seconds = secs;
    if (e.empty()) {
        s.json = std::move(j);
        s.state = State::Done;
    } else {
        s.error = std::move(e);
        s.state = State::Failed;
    }
    unlock();
}

bool MultiFetch::runDetached(Platform &p, std::function<void()> fn)
{
    auto *heap = new std::function<void()>(std::move(fn));
    gActive++;
    bool ok = p.startThread(
        [](void *arg) {
            auto *f = static_cast<std::function<void()> *>(arg);
            (*f)();
            delete f;
            gActive--;
        },
        heap, "ml_task");
    if (!ok) {
        gActive--;
        delete heap;
    }
    return ok;
}

int MultiFetch::pending()
{
    int n = 0;
    lock();
    for (const auto &s : slots_) if (s.state == State::Pending) n++;
    unlock();
    return n;
}

MultiFetch::State MultiFetch::poll(size_t i, Json *json, std::string *error, int *seconds)
{
    lock();
    Slot &s = slots_[i];
    State st = s.state;
    if (seconds) *seconds = s.seconds;
    if (st == State::Failed && error) *error = s.error;
    if (st == State::Done && !s.taken && json) {
        *json = std::move(s.json);
        s.json = Json();
        s.taken = true;
    }
    unlock();
    return st;
}

}  // namespace ml
