#include "cook_jobs.h"

#include "asset_registry.h"
#include "cooked_payload.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace assetlib {

struct CookService::Impl {
  struct Job {
    AssetId id;
    std::vector<CookService::Output> outputs;
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
  std::atomic<size_t> workTotal{0};
  std::atomic<size_t> workCompleted{0};
  std::set<AssetId> pendingIds;
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
      std::map<AssetId, Completion> completions;
      for (const CookService::Output &output : job.outputs) {
        const AssetId outputId = output.assetId.valid() ? output.assetId : job.id;
        auto [it, inserted] = completions.try_emplace(outputId);
        Completion &c = it->second;
        if (inserted) {
          c.id = outputId;
          c.ok = true;
        }
        std::vector<uint8_t> blob =
            output.produce ? output.produce() : std::vector<uint8_t>();
        const bool outputOk =
            !blob.empty() && WriteCookedFile(output.path, blob);
        c.ok = c.ok && outputOk;
        if (outputOk) {
          if (!output.stagedPath.empty()) {
            std::error_code ec;
            std::filesystem::remove(output.stagedPath, ec);
          }
          const uint64_t outputHash = HashBytes(blob.data(), blob.size());
          c.hash ^= outputHash + 0x9e3779b97f4a7c15ull + (c.hash << 6) +
                    (c.hash >> 2);
        }
        workCompleted.fetch_add(1, std::memory_order_acq_rel);
      }
      {
        std::lock_guard<std::mutex> lk(mtx);
        for (auto &entry : completions)
          done.push_back(std::move(entry.second));
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
  std::vector<Output> outputs;
  outputs.push_back({std::move(path), std::move(produce)});
  EnqueueBatch(id, std::move(outputs));
}

void CookService::EnqueueBatch(const AssetId &id,
                               std::vector<Output> outputs) {
  if (outputs.empty())
    return;
  {
    std::lock_guard<std::mutex> lk(m_impl->mtx);
    if (m_impl->pendingIds.find(id) != m_impl->pendingIds.end())
      return;
    for (const Output &output : outputs) {
      const AssetId outputId = output.assetId.valid() ? output.assetId : id;
      if (m_impl->pendingIds.find(outputId) != m_impl->pendingIds.end())
        return;
    }
    const size_t previous =
        m_impl->inFlight.fetch_add(1, std::memory_order_acq_rel);
    if (previous == 0) {
      m_impl->workTotal.store(outputs.size(), std::memory_order_release);
      m_impl->workCompleted.store(0, std::memory_order_release);
    } else {
      m_impl->workTotal.fetch_add(outputs.size(), std::memory_order_acq_rel);
    }
    m_impl->pendingIds.insert(id);
    for (const Output &output : outputs) {
      if (output.assetId.valid())
        m_impl->pendingIds.insert(output.assetId);
    }
    m_impl->jobs.push_back({id, std::move(outputs)});
  }
  m_impl->cv.notify_one();
}

bool CookService::IsPending(const AssetId &id) const {
  std::lock_guard<std::mutex> lk(m_impl->mtx);
  return m_impl->pendingIds.find(id) != m_impl->pendingIds.end();
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
    if (!meta) {
      std::lock_guard<std::mutex> lk(m_impl->mtx);
      m_impl->pendingIds.erase(c.id);
      continue; // asset removed before cook finished
    }
    AssetMetadata updated = *meta;
    updated.cookState = c.ok ? CookState::Current : CookState::Failed;
    if (c.ok)
      updated.cookedPayloadHash = c.hash;
    registry.Update(updated);
    {
      std::lock_guard<std::mutex> lk(m_impl->mtx);
      m_impl->pendingIds.erase(c.id);
    }
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

std::string CookService::progressText() const {
  const size_t assets = pending();
  if (assets == 0)
    return {};
  const size_t total = m_impl->workTotal.load(std::memory_order_acquire);
  const size_t completed =
      (std::min)(m_impl->workCompleted.load(std::memory_order_acquire), total);
  return "Cooking " + std::to_string(completed) + "/" +
         std::to_string(total) + " output(s) for " +
         std::to_string(assets) + " asset(s)...";
}

} // namespace assetlib
