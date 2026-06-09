#pragma once
#include "cooked_payload.h" // CookedVolume
#include <string>
#include <vector>

// Reads a .vdb file's density grid and converts it into Project Render's sparse
// bricked runtime volume (CookedVolume). Backed by OpenVDB; when the build is
// configured without OpenVDB, IsAvailable() returns false and import fails
// gracefully. Bridge layer (links OpenVDB), but renderer-free.
namespace VdbImport {

struct GridInfo {
  std::string name;
  std::string valueType;
  bool scalar = false;
  bool vector = false;
};

struct ImportOptions {
  std::string densityGrid;
  std::string temperatureGrid;
};

bool IsAvailable();
bool ListGrids(const std::string &path, std::vector<GridInfo> &out,
               std::string *error);

// Imports the first float (density) grid found in `path`. Returns false and
// sets *error on failure.
bool ImportVdbToVolume(const std::string &path, assetlib::CookedVolume &out,
                       std::string *error);
bool ImportVdbToVolume(const std::string &path, const ImportOptions &options,
                       assetlib::CookedVolume &out, std::string *error);

// Diagnostic: returns a human-readable listing of every grid in the file
// (name, type, class, active voxel count, value range). For tooling/debugging.
std::string DescribeGrids(const std::string &path);

} // namespace VdbImport
