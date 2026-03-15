#pragma once

#include "livelink_coordinator.h"

namespace LiveLink {

LiveLinkCoordinator &GetCoordinator();
void TickCoordinator();

} // namespace LiveLink