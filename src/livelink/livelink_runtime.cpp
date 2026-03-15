#include "livelink_runtime.h"

#include "livelink_scene_sync.h"

namespace LiveLink {

LiveLinkCoordinator &GetCoordinator() {
  static LiveLinkCoordinator s_coordinator;
  return s_coordinator;
}

void TickCoordinator() { GetCoordinator().PollProviders(); }

void ApplyQueuedBatches() { GetSceneSync().ApplyQueuedBatches(GetCoordinator()); }

void TickRuntime() {
  TickCoordinator();
  ApplyQueuedBatches();
}

} // namespace LiveLink