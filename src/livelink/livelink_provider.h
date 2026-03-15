#pragma once

#include "livelink_types.h"

#include <memory>
#include <string>
#include <vector>

namespace LiveLink {

class ILiveLinkProvider {
public:
  virtual ~ILiveLinkProvider() = default;

  virtual std::string GetProviderName() const = 0;
  virtual Capability GetCapabilities() const = 0;
  virtual ConnectionState GetConnectionState() const = 0;
  virtual std::string GetLastError() const = 0;

  virtual bool Connect() = 0;
  virtual void Disconnect() = 0;

  // Poll should append any newly available batches to |outBatches| and return
  // true when the provider call itself succeeded, even if there were no new
  // batches available.
  virtual bool Poll(std::vector<SceneDeltaBatch> &outBatches) = 0;
};

using LiveLinkProviderPtr = std::unique_ptr<ILiveLinkProvider>;

} // namespace LiveLink