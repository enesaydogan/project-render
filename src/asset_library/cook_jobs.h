#pragma once
#include "asset_id.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Background cook worker. Heavy serialize/compress/file-write work runs on a
// single worker thread; the registry is only ever touched on the main thread
// via Pump(). This is the doc's "background import and recook jobs" with
// progress surfaced to the UI (Phase 2). Bridge layer.
namespace assetlib {

class AssetRegistry;

class CookService {
public:
  struct Output {
    std::filesystem::path path;
    std::function<std::vector<uint8_t>()> produce;
  };

  static CookService &Get();

  // Enqueue a unit of cook work. `produce` runs on the worker thread and
  // returns the final on-disk bytes (e.g. serialize + LZMS compress); the
  // worker writes them atomically to `path` and records a completion keyed by
  // `id`. Capture inputs by value/move — `produce` must not touch shared state.
  void Enqueue(const AssetId &id, std::filesystem::path path,
               std::function<std::vector<uint8_t>()> produce);
  void EnqueueBatch(const AssetId &id, std::vector<Output> outputs);
  bool IsPending(const AssetId &id) const;

  // Apply finished cook results to the registry (sets cookState + payload hash)
  // on the calling (main) thread. Saves the registry once when the queue fully
  // drains. Safe to call every frame.
  void Pump(AssetRegistry &registry);

  // For UI progress.
  size_t pending() const;
  std::string statusText() const;
  std::string progressText() const;

  ~CookService();

private:
  CookService();
  CookService(const CookService &) = delete;
  CookService &operator=(const CookService &) = delete;
  struct Impl;
  Impl *m_impl;
};

} // namespace assetlib
