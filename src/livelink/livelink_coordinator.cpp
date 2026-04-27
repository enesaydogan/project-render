#include "livelink_coordinator.h"

#include <algorithm>
#include <optional>
#include <stdexcept>

namespace LiveLink {

namespace {

template <typename T>
const T *FindPayload(const SceneDelta &delta) {
  return std::get_if<T>(&delta.payload);
}

bool DeltaRequiresTarget(SceneDeltaKind kind) {
  switch (kind) {
  case SceneDeltaKind::SessionOpened:
  case SceneDeltaKind::SessionClosed:
  case SceneDeltaKind::FullSceneSync:
    return false;
  default:
    return true;
  }
}

bool IsSessionLifecycleDelta(SceneDeltaKind kind) {
  return kind == SceneDeltaKind::SessionOpened ||
         kind == SceneDeltaKind::SessionClosed;
}

} // namespace

LiveLinkCoordinator::LiveLinkCoordinator() = default;

LiveLinkCoordinator::~LiveLinkCoordinator() { DisconnectAllProviders(); }

LiveLinkCoordinator::ProviderId
LiveLinkCoordinator::RegisterProvider(LiveLinkProviderPtr provider) {
  if (!provider) {
    throw std::invalid_argument("LiveLink provider must not be null");
  }

  ProviderRecord record;
  record.id = m_nextProviderId++;
  record.providerName = provider->GetProviderName();
  record.capabilities = provider->GetCapabilities();
  record.provider = std::move(provider);

  std::lock_guard<std::mutex> lock(m_mutex);
  m_providers.emplace(record.id, std::move(record));
  m_stats.providerCount = m_providers.size();
  return m_nextProviderId - 1;
}

bool LiveLinkCoordinator::UnregisterProvider(ProviderId providerId) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_providers.find(providerId);
  if (it == m_providers.end()) {
    return false;
  }

  it->second.provider->Disconnect();
  const std::string providerName = it->second.providerName;
  for (auto sessionIt = m_sessions.begin(); sessionIt != m_sessions.end();) {
    if (sessionIt->second.providerName == providerName) {
      sessionIt = m_sessions.erase(sessionIt);
    } else {
      ++sessionIt;
    }
  }

  m_providers.erase(it);
  m_stats.providerCount = m_providers.size();
  return true;
}

bool LiveLinkCoordinator::ConnectProvider(ProviderId providerId) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_providers.find(providerId);
  if (it == m_providers.end()) {
    return false;
  }
  return it->second.provider->Connect();
}

bool LiveLinkCoordinator::DisconnectProvider(ProviderId providerId) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_providers.find(providerId);
  if (it == m_providers.end()) {
    return false;
  }
  it->second.provider->Disconnect();
  return true;
}

void LiveLinkCoordinator::DisconnectAllProviders() {
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto &[_, record] : m_providers) {
    if (record.provider) {
      record.provider->Disconnect();
    }
  }
}

void LiveLinkCoordinator::PollProviders() {
  std::lock_guard<std::mutex> lock(m_mutex);

  size_t connectedProviderCount = 0;
  for (auto &[providerId, record] : m_providers) {
    if (!record.provider) {
      continue;
    }

    ConnectionState providerState = record.provider->GetConnectionState();
    if (providerState == ConnectionState::Disconnected ||
        providerState == ConnectionState::Error) {
      continue;
    }

    std::vector<SceneDeltaBatch> batches;
    const bool ok = record.provider->Poll(batches);
    if (!ok) {
      AppendIssue(ValidationIssue::Severity::Warning, record.providerName, "",
                  record.provider->GetLastError().empty()
                      ? "Provider poll failed"
                      : record.provider->GetLastError());
      continue;
    }

    providerState = record.provider->GetConnectionState();
    if (providerState == ConnectionState::Connected) {
      ++connectedProviderCount;
    }

    for (const SceneDeltaBatch &batch : batches) {
      ValidateAndQueueBatch(record, batch);
    }
  }

  m_stats.providerCount = m_providers.size();
  m_stats.connectedProviderCount = connectedProviderCount;
  m_stats.queuedBatchCount = m_queuedBatches.size();
  m_stats.queuedDeltaCount = m_queuedDeltaCount;
}

bool LiveLinkCoordinator::HasQueuedBatches() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return !m_queuedBatches.empty();
}

size_t LiveLinkCoordinator::GetQueuedBatchCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_queuedBatches.size();
}

size_t LiveLinkCoordinator::GetQueuedDeltaCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_queuedDeltaCount;
}

std::vector<SceneDeltaBatch> LiveLinkCoordinator::ConsumeQueuedBatches() {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<SceneDeltaBatch> batches;
  batches.reserve(m_queuedBatches.size());
  while (!m_queuedBatches.empty()) {
    batches.push_back(std::move(m_queuedBatches.front()));
    m_queuedBatches.pop_front();
  }
  m_queuedDeltaCount = 0;
  m_stats.queuedBatchCount = 0;
  m_stats.queuedDeltaCount = 0;
  return batches;
}

std::vector<ValidationIssue> LiveLinkCoordinator::ConsumeValidationIssues() {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<ValidationIssue> issues = std::move(m_validationIssues);
  m_validationIssues.clear();
  return issues;
}

CoordinatorStats LiveLinkCoordinator::GetStatsSnapshot() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_stats;
}

std::vector<ProviderSnapshot> LiveLinkCoordinator::GetProviderSnapshots() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<ProviderSnapshot> snapshots;
  snapshots.reserve(m_providers.size());

  for (const auto &[providerId, record] : m_providers) {
    ProviderSnapshot snapshot;
    snapshot.providerId = providerId;
    snapshot.providerName = record.providerName;
    snapshot.capabilities = record.capabilities;
    snapshot.connectionState = record.provider->GetConnectionState();
    snapshot.lastError = record.provider->GetLastError();

    for (const auto &[_, session] : m_sessions) {
      if (session.providerName == record.providerName) {
        snapshot.sessions.push_back(session);
      }
    }

    snapshots.push_back(std::move(snapshot));
  }

  return snapshots;
}

bool LiveLinkCoordinator::ValidateAndQueueBatch(ProviderRecord &record,
                                                const SceneDeltaBatch &batch) {
  const std::string providerName =
      batch.providerName.empty() ? record.providerName : batch.providerName;
  const bool compatibleMaxProvider =
      record.providerName == "3dsMax2025Pipe" &&
      (providerName == "3dsMax2025Pipe" || providerName == "3dsMax2024Pipe");
  if (providerName != record.providerName && !compatibleMaxProvider) {
    AppendIssue(ValidationIssue::Severity::Error, record.providerName,
                batch.sessionId,
                "Rejected batch because providerName does not match the "
                "registered provider");
    ++m_stats.batchesRejected;
    return false;
  }

  if (batch.sessionId.empty()) {
    AppendIssue(ValidationIssue::Severity::Error, record.providerName, "",
                "Rejected batch with empty sessionId");
    ++m_stats.batchesRejected;
    return false;
  }

  const std::string sessionKey = MakeSessionKey(record.id, batch.sessionId);
  SessionInfo *session = nullptr;
  auto sessionIt = m_sessions.find(sessionKey);
  if (sessionIt != m_sessions.end()) {
    session = &sessionIt->second;
  }

  const bool containsSessionOpen =
      std::any_of(batch.deltas.begin(), batch.deltas.end(), [](const SceneDelta &delta) {
        return delta.kind == SceneDeltaKind::SessionOpened;
      });

  if (!session && !containsSessionOpen) {
    AppendIssue(ValidationIssue::Severity::Error, record.providerName,
                batch.sessionId,
                "Rejected batch because the session was never opened");
    ++m_stats.batchesRejected;
    return false;
  }

  if (!session) {
    SessionInfo newSession;
    newSession.sessionId = batch.sessionId;
    newSession.providerName = record.providerName;
    newSession.capabilities = record.capabilities;
    newSession.connectionState = ConnectionState::Connected;
    session = &m_sessions.emplace(sessionKey, std::move(newSession)).first->second;
  }

  if (batch.sequence > 0 && batch.sequence <= session->lastReceivedBatchSequence) {
    AppendIssue(ValidationIssue::Severity::Warning, record.providerName,
                batch.sessionId,
                "Rejected out-of-order or duplicate batch sequence");
    ++m_stats.batchesRejected;
    return false;
  }

  SceneDeltaBatch sanitized = batch;
  sanitized.providerName = record.providerName;
  sanitized.deltas.clear();
  sanitized.deltas.reserve(batch.deltas.size());

  uint64_t maxAcceptedRevision = session->lastAcceptedRevision;
  for (const SceneDelta &delta : batch.deltas) {
    if (delta.kind == SceneDeltaKind::Unknown) {
      AppendIssue(ValidationIssue::Severity::Error, record.providerName,
                  batch.sessionId, "Rejected delta with unknown kind");
      ++m_stats.deltasRejected;
      continue;
    }

    if (DeltaRequiresTarget(delta.kind) && delta.target.Empty()) {
      AppendIssue(ValidationIssue::Severity::Error, record.providerName,
                  batch.sessionId,
                  std::string("Rejected ") + ToString(delta.kind) +
                      " because its target ObjectId is incomplete");
      ++m_stats.deltasRejected;
      continue;
    }

    if (!IsSessionLifecycleDelta(delta.kind) && delta.kind != SceneDeltaKind::FullSceneSync &&
        delta.revision == 0) {
      AppendIssue(ValidationIssue::Severity::Error, record.providerName,
                  batch.sessionId,
                  std::string("Rejected ") + ToString(delta.kind) +
                      " because revision is 0");
      ++m_stats.deltasRejected;
      continue;
    }

    if (DeltaRequiresTarget(delta.kind)) {
      if (!session->documentId.empty() && delta.target.documentId != session->documentId) {
        AppendIssue(ValidationIssue::Severity::Error, record.providerName,
                    batch.sessionId,
                    std::string("Rejected ") + ToString(delta.kind) +
                        " because documentId does not match the session");
        ++m_stats.deltasRejected;
        continue;
      }
      if (!session->sourceApp.empty() && delta.target.sourceApp != session->sourceApp) {
        AppendIssue(ValidationIssue::Severity::Error, record.providerName,
                    batch.sessionId,
                    std::string("Rejected ") + ToString(delta.kind) +
                        " because sourceApp does not match the session");
        ++m_stats.deltasRejected;
        continue;
      }
    }

    if (delta.revision > 0 && delta.revision <= session->lastAcceptedRevision &&
        !IsSessionLifecycleDelta(delta.kind)) {
      AppendIssue(ValidationIssue::Severity::Warning, record.providerName,
                  batch.sessionId,
                  std::string("Rejected stale ") + ToString(delta.kind) +
                      " revision");
      ++m_stats.deltasRejected;
      continue;
    }

    if (delta.kind == SceneDeltaKind::SessionOpened) {
      const SessionOpenedPayload *payload = FindPayload<SessionOpenedPayload>(delta);
      if (payload) {
        session->documentPath = payload->documentPath;
        session->displayName = payload->displayName;
      }
      if (!delta.target.sourceApp.empty()) {
        session->sourceApp = delta.target.sourceApp;
      }
      if (!delta.target.documentId.empty()) {
        session->documentId = delta.target.documentId;
      }
      session->connectionState = ConnectionState::Connected;
      session->capabilities = record.capabilities;
    } else if (delta.kind == SceneDeltaKind::SessionClosed) {
      session->connectionState = ConnectionState::Disconnected;
    }

    sanitized.deltas.push_back(delta);
    if (delta.revision > maxAcceptedRevision) {
      maxAcceptedRevision = delta.revision;
    }
    ++m_stats.deltasAccepted;
  }

  if (sanitized.deltas.empty()) {
    AppendIssue(ValidationIssue::Severity::Warning, record.providerName,
                batch.sessionId,
                "Rejected batch because no deltas passed validation");
    ++m_stats.batchesRejected;
    return false;
  }

  session->providerName = record.providerName;
  session->capabilities = record.capabilities;
  session->lastReceivedBatchSequence = batch.sequence;
  session->lastAcceptedRevision = maxAcceptedRevision;
  if (session->connectionState != ConnectionState::Disconnected) {
    session->connectionState = ConnectionState::Connected;
  }

  m_queuedDeltaCount += sanitized.deltas.size();
  m_queuedBatches.push_back(std::move(sanitized));
  ++m_stats.batchesAccepted;
  m_stats.queuedBatchCount = m_queuedBatches.size();
  m_stats.queuedDeltaCount = m_queuedDeltaCount;
  return true;
}

void LiveLinkCoordinator::AppendIssue(ValidationIssue::Severity severity,
                                      const std::string &providerName,
                                      const std::string &sessionId,
                                      const std::string &message) {
  m_validationIssues.push_back(
      ValidationIssue{severity, providerName, sessionId, message});
}

std::string LiveLinkCoordinator::MakeSessionKey(ProviderId providerId,
                                                const std::string &sessionId) {
  return std::to_string(providerId) + ":" + sessionId;
}

} // namespace LiveLink
