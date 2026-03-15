#include "livelink_runtime.h"

namespace LiveLink {

LiveLinkCoordinator &GetCoordinator() {
  static LiveLinkCoordinator s_coordinator;
  return s_coordinator;
}

void TickCoordinator() { GetCoordinator().PollProviders(); }

} // namespace LiveLink