#pragma once

#include "livelink_provider.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace LiveLink {

struct CoordinatorStats {
  size_t providerCount = 0;
  size_t connectedProviderCount = 0;
  uint64_t batchesAccepted = 0;
  uint64_t batchesRejected = 0;
  uint64_t deltasAccepted = 0;
  uint64_t deltasRejected = 0;
  size_t queuedBatchCount = 0;
  size_t queuedDeltaCount = 0;
};

struct ProviderSnapshot {
  uint64_t providerId = 0;
  std::string providerName;
  Capability capabilities = Capability::None;
  ConnectionState connectionState = ConnectionState::Disconnected;
  std::string lastError;
  std::vector<SessionInfo> sessions;
};

class LiveLinkCoordinator {
public:
  using ProviderId = uint64_t;

  LiveLinkCoordinator();
  ~LiveLinkCoordinator();

  ProviderId RegisterProvider(LiveLinkProviderPtr provider);
  bool UnregisterProvider(ProviderId providerId);

  bool ConnectProvider(ProviderId providerId);
  bool DisconnectProvider(ProviderId providerId);
  void DisconnectAllProviders();

  void PollProviders();

  bool HasQueuedBatches() const;
  size_t GetQueuedBatchCount() const;
  size_t GetQueuedDeltaCount() const;
  std::vector<SceneDeltaBatch> ConsumeQueuedBatches();
  std::vector<ValidationIssue> ConsumeValidationIssues();

  CoordinatorStats GetStatsSnapshot() const;
  std::vector<ProviderSnapshot> GetProviderSnapshots() const;

private:
  struct ProviderRecord {
    ProviderId id = 0;
    LiveLinkProviderPtr provider;
    std::string providerName;
    Capability capabilities = Capability::None;
  };

  bool ValidateAndQueueBatch(ProviderRecord &record,
                             const SceneDeltaBatch &batch);
  void AppendIssue(ValidationIssue::Severity severity,
                   const std::string &providerName,
                   const std::string &sessionId,
                   const std::string &message);

  static std::string MakeSessionKey(ProviderId providerId,
                                    const std::string &sessionId);

  mutable std::mutex m_mutex;
  ProviderId m_nextProviderId = 1;
  std::unordered_map<ProviderId, ProviderRecord> m_providers;
  std::unordered_map<std::string, SessionInfo> m_sessions;
  std::deque<SceneDeltaBatch> m_queuedBatches;
  std::vector<ValidationIssue> m_validationIssues;
  size_t m_queuedDeltaCount = 0;
  CoordinatorStats m_stats;
};

} // namespace LiveLink