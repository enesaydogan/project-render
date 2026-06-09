#pragma once
#include "cooked_payload.h" // CookedVolume
#include <string>

// Reads a .vdb file's density grid and converts it into Project Render's sparse
// bricked runtime volume (CookedVolume). Backed by OpenVDB; when the build is
// configured without OpenVDB, IsAvailable() returns false and import fails
// gracefully. Bridge layer (links OpenVDB), but renderer-free.
namespace VdbImport {

bool IsAvailable();

// Imports the first float (density) grid found in `path`. Returns false and
// sets *error on failure.
bool ImportVdbToVolume(const std::string &path, assetlib::CookedVolume &out,
                       std::string *error);

} // namespace VdbImport
