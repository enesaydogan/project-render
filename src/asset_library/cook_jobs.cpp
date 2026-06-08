#include "cook_jobs.h"

#include "asset_registry.h"
#include "cooked_payload.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace assetlib {

struct CookService::Impl {
  struct Job {
    AssetId id;
    std::filesystem::path path;
    std::function<std::vector<uint8_t>()> produce;
  };
  struct Completion {
    AssetId id;
    bool ok = false;
    uint64_t hash = 0;
  };

  std::thread worker;
  std::mutex mtx;
  std::condition_variable cv;
  std::deque<Job> jobs;
  std::vector<Completion> done;
  std::atomic<size_t> inFlight{0}; // queued + currently-processing
  bool stop = false;

  Impl() {
    worker = std::thread([this]() { run(); });
  }
  ~Impl() {
    {
      std::lock_guard<std::mutex> lk(mtx);
      stop = true;
    }
    cv.notify_all();
    if (worker.joinable())
      worker.join();
  }

  void run() {
    for (;;) {
      Job job;
      {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [this]() { return stop || !jobs.empty(); });
        if (stop && jobs.empty())
          return;
        job = std::move(jobs.front());
        jobs.pop_front();
      }
      Completion c;
      c.id = job.id;
      std::vector<uint8_t> blob = job.produce ? job.produce() : std::vector<uint8_t>();
      c.ok = !blob.empty() && WriteCookedFile(job.path, blob);
      c.hash = blob.empty() ? 0 : HashBytes(blob.data(), blob.size());
      {
        std::lock_guard<std::mutex> lk(mtx);
        done.push_back(c);
      }
      inFlight.fetch_sub(1, std::memory_order_acq_rel);
    }
  }
};

CookService::CookService() : m_impl(new Impl()) {}
CookService::~CookService() { delete m_impl; }

CookService &CookService::Get() {
  static CookService instance;
  return instance;
}

void CookService::Enqueue(const AssetId &id, std::filesystem::path path,
                          std::function<std::vector<uint8_t>()> produce) {
  {
    std::lock_guard<std::mutex> lk(m_impl->mtx);
    m_impl->inFlight.fetch_add(1, std::memory_order_acq_rel);
    m_impl->jobs.push_back({id, std::move(path), std::move(produce)});
  }
  m_impl->cv.notify_one();
}

void CookService::Pump(AssetRegistry &registry) {
  std::vector<Impl::Completion> ready;
  {
    std::lock_guard<std::mutex> lk(m_impl->mtx);
    if (m_impl->done.empty())
      return;
    ready.swap(m_impl->done);
  }
  bool changed = false;
  for (const auto &c : ready) {
    const AssetMetadata *meta = registry.Get(c.id);
    if (!meta)
      continue; // asset removed before cook finished
    AssetMetadata updated = *meta;
    updated.cookState = c.ok ? CookState::Current : CookState::Failed;
    if (c.ok)
      updated.cookedPayloadHash = c.hash;
    registry.Update(updated);
    changed = true;
  }
  // Persist once the backlog is fully drained.
  if (changed && m_impl->inFlight.load(std::memory_order_acquire) == 0)
    registry.Save();
}

size_t CookService::pending() const {
  return m_impl->inFlight.load(std::memory_order_acquire);
}

std::string CookService::statusText() const {
  size_t n = pending();
  if (n == 0)
    return "Cook: idle";
  return "Cooking " + std::to_string(n) + " asset(s)…";
}

} // namespace assetlib
