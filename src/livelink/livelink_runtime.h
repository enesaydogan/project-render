#pragma once

#include "livelink_coordinator.h"

namespace LiveLink {

LiveLinkCoordinator &GetCoordinator();
void TickCoordinator();
void ApplyQueuedBatches();
void TickRuntime();

} // namespace LiveLink