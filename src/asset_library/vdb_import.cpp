#include "vdb_import.h"

#ifdef PR_HAVE_OPENVDB

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <openvdb/openvdb.h>
#include <openvdb/tools/Interpolation.h>
#include <openvdb/tools/LevelSetUtil.h>
#include <unordered_map>

using namespace assetlib;

namespace VdbImport {
namespace {

std::once_flag g_vdbInit;
void EnsureInit() {
  std::call_once(g_vdbInit, []() { openvdb::initialize(); });
}

constexpr uint64_t kMaxImportedVoxels = 32ull * 1024ull * 1024ull;
constexpr uint32_t kMaxImportedDimension = 2048;

std::array<uint32_t, 3>
ChooseImportedDimensions(const openvdb::Coord &sourceDim) {
  const uint32_t dim[3] = {
      static_cast<uint32_t>(sourceDim.x()),
      static_cast<uint32_t>(sourceDim.y()),
      static_cast<uint32_t>(sourceDim.z())};
  const long double sourceVoxels =
      static_cast<long double>(dim[0]) * dim[1] * dim[2];
  long double scale = 1.0L;
  if (sourceVoxels > static_cast<long double>(kMaxImportedVoxels)) {
    scale = std::cbrt(static_cast<long double>(kMaxImportedVoxels) /
                      sourceVoxels);
  }
  const uint32_t sourceMax = (std::max)({dim[0], dim[1], dim[2]});
  if (sourceMax > kMaxImportedDimension) {
    scale = (std::min)(
        scale, static_cast<long double>(kMaxImportedDimension) / sourceMax);
  }
  return {
      (std::max)(1u, static_cast<uint32_t>(std::floor(dim[0] * scale))),
      (std::max)(1u, static_cast<uint32_t>(std::floor(dim[1] * scale))),
      (std::max)(1u, static_cast<uint32_t>(std::floor(dim[2] * scale)))};
}

} // namespace

bool IsAvailable() { return true; }

bool ImportVdbToVolume(const std::string &path, CookedVolume &out,
                       std::string *error) {
  auto fail = [&](const std::string &m) {
    if (error)
      *error = m;
    return false;
  };
  EnsureInit();

  openvdb::FloatGrid::Ptr grid;
  try {
    openvdb::io::File file(path);
    file.open();
    // Prefer a grid literally named "density", else the first FloatGrid.
    for (auto it = file.beginName(); it != file.endName(); ++it) {
      openvdb::GridBase::Ptr base = file.readGrid(it.gridName());
      if (auto fg = openvdb::gridPtrCast<openvdb::FloatGrid>(base)) {
        grid = fg;
        if (it.gridName() == "density")
          break;
      }
    }
    file.close();
  } catch (const std::exception &e) {
    return fail(std::string("VDB read failed: ") + e.what());
  }
  if (!grid)
    return fail("no float (density) grid found in VDB");

  const openvdb::CoordBBox bbox = grid->evalActiveVoxelBoundingBox();
  if (bbox.empty())
    return fail("VDB grid has no active voxels");

  const bool wasLevelSet = grid->getGridClass() == openvdb::GRID_LEVEL_SET;
  if (wasLevelSet) {
    try {
      openvdb::tools::sdfToFogVolume(*grid);
    } catch (const std::exception &e) {
      return fail(std::string("VDB level-set conversion failed: ") + e.what());
    }
  }

  const openvdb::Coord origin = bbox.min();
  const openvdb::Coord sourceDim = bbox.dim();
  const std::array<uint32_t, 3> importedDim =
      ChooseImportedDimensions(sourceDim);
  out = CookedVolume{};
  out.dim[0] = importedDim[0];
  out.dim[1] = importedDim[1];
  out.dim[2] = importedDim[2];
  out.brickSize = 8;

  // World-space AABB from the grid transform.
  const openvdb::Vec3d wmin =
      grid->indexToWorld(openvdb::Vec3d(bbox.min().x(), bbox.min().y(),
                                        bbox.min().z()));
  const openvdb::Vec3d wmax = grid->indexToWorld(openvdb::Vec3d(
      bbox.max().x() + 1, bbox.max().y() + 1, bbox.max().z() + 1));
  for (int i = 0; i < 3; ++i) {
    out.boundsMin[i] = static_cast<float>(std::min(wmin[i], wmax[i]));
    out.boundsMax[i] = static_cast<float>(std::max(wmin[i], wmax[i]));
  }

  // Sample the source tree into bounded runtime dimensions. Sampling instead
  // of walking only leaf voxels also handles constant active tiles produced by
  // level-set-to-fog conversion.
  const int B = static_cast<int>(out.brickSize);
  const int voxPerBrick = B * B * B;
  struct BrickKey {
    uint32_t bx, by, bz;
    bool operator==(const BrickKey &o) const {
      return bx == o.bx && by == o.by && bz == o.bz;
    }
  };
  struct BrickKeyHash {
    size_t operator()(const BrickKey &k) const {
      return (static_cast<size_t>(k.bx) * 73856093u) ^
             (static_cast<size_t>(k.by) * 19349663u) ^
             (static_cast<size_t>(k.bz) * 83492791u);
    }
  };
  std::unordered_map<BrickKey, std::vector<float>, BrickKeyHash> buffers;

  auto accessor = grid->getConstAccessor();
  using Accessor = openvdb::FloatGrid::ConstAccessor;
  openvdb::tools::GridSampler<Accessor, openvdb::tools::BoxSampler> sampler(
      accessor, grid->transform());
  const bool reduced =
      importedDim[0] != static_cast<uint32_t>(sourceDim.x()) ||
      importedDim[1] != static_cast<uint32_t>(sourceDim.y()) ||
      importedDim[2] != static_cast<uint32_t>(sourceDim.z());
  uint64_t activeVoxels = 0;
  for (uint32_t z = 0; z < importedDim[2]; ++z) {
    const double sourceZ =
        origin.z() + ((static_cast<double>(z) + 0.5) * sourceDim.z() /
                          importedDim[2] -
                      0.5);
    for (uint32_t y = 0; y < importedDim[1]; ++y) {
      const double sourceY =
          origin.y() + ((static_cast<double>(y) + 0.5) * sourceDim.y() /
                            importedDim[1] -
                        0.5);
      for (uint32_t x = 0; x < importedDim[0]; ++x) {
        float value = 0.0f;
        if (reduced) {
          const double sourceX =
              origin.x() + ((static_cast<double>(x) + 0.5) * sourceDim.x() /
                                importedDim[0] -
                            0.5);
          value = sampler.isSample(
              openvdb::Vec3d(sourceX, sourceY, sourceZ));
        } else {
          value = accessor.getValue(
              origin + openvdb::Coord(static_cast<int>(x),
                                      static_cast<int>(y),
                                      static_cast<int>(z)));
        }
        if (!(value > 0.0f) || !std::isfinite(value))
          continue;

        ++activeVoxels;
        BrickKey key{x / out.brickSize, y / out.brickSize,
                     z / out.brickSize};
        auto &buf = buffers[key];
        if (buf.empty())
          buf.assign(static_cast<size_t>(voxPerBrick), 0.0f);
        const uint32_t ox = x % out.brickSize;
        const uint32_t oy = y % out.brickSize;
        const uint32_t oz = z % out.brickSize;
        buf[static_cast<size_t>((oz * B + oy) * B + ox)] = value;
      }
    }
  }
  out.activeVoxels = activeVoxels;

  if (wasLevelSet || reduced) {
    fprintf(stderr,
            "VDB import: %s%s%dx%dx%d -> %ux%ux%u, %llu active voxels\n",
            wasLevelSet ? "level set converted to fog, " : "",
            reduced ? "resampled " : "", sourceDim.x(), sourceDim.y(),
            sourceDim.z(), out.dim[0], out.dim[1], out.dim[2],
            static_cast<unsigned long long>(out.activeVoxels));
  }

  // Quantize each touched brick to 8-bit over its own [min,max] range.
  out.bricks.reserve(buffers.size());
  for (auto &kv : buffers) {
    float mn = kv.second[0], mx = kv.second[0];
    for (float f : kv.second) {
      mn = std::min(mn, f);
      mx = std::max(mx, f);
    }
    CookedVolumeBrick brick;
    brick.bx = kv.first.bx;
    brick.by = kv.first.by;
    brick.bz = kv.first.bz;
    brick.minVal = mn;
    brick.maxVal = mx;
    brick.data.resize(static_cast<size_t>(voxPerBrick));
    const float range = mx - mn;
    for (int i = 0; i < voxPerBrick; ++i) {
      float t = range > 0.0f ? (kv.second[static_cast<size_t>(i)] - mn) / range
                             : 0.0f;
      t = std::clamp(t, 0.0f, 1.0f);
      brick.data[static_cast<size_t>(i)] =
          static_cast<uint8_t>(std::lround(t * 255.0f));
    }
    out.bricks.push_back(std::move(brick));
  }
  return true;
}

} // namespace VdbImport

#else // PR_HAVE_OPENVDB

namespace VdbImport {
bool IsAvailable() { return false; }
bool ImportVdbToVolume(const std::string &, assetlib::CookedVolume &,
                       std::string *error) {
  if (error)
    *error = "this build was configured without OpenVDB (.vdb unsupported)";
  return false;
}
} // namespace VdbImport

#endif // PR_HAVE_OPENVDB
