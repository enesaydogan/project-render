#include "vdb_import.h"

#ifdef PR_HAVE_OPENVDB

#include <algorithm>
#include <cmath>
#include <mutex>
#include <openvdb/openvdb.h>
#include <unordered_map>

using namespace assetlib;

namespace VdbImport {
namespace {

std::once_flag g_vdbInit;
void EnsureInit() {
  std::call_once(g_vdbInit, []() { openvdb::initialize(); });
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

  const openvdb::Coord origin = bbox.min();
  const openvdb::Coord ext = bbox.dim(); // voxels along each axis
  out = CookedVolume{};
  out.dim[0] = static_cast<uint32_t>(ext.x());
  out.dim[1] = static_cast<uint32_t>(ext.y());
  out.dim[2] = static_cast<uint32_t>(ext.z());
  out.brickSize = 8;
  out.activeVoxels = static_cast<uint64_t>(grid->activeVoxelCount());

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

  // Accumulate active voxels into dense per-brick float buffers.
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

  for (auto it = grid->cbeginValueOn(); it; ++it) {
    const float v = it.getValue();
    if (v <= 0.0f)
      continue;
    const openvdb::Coord c = it.getCoord() - origin; // local index
    const int lx = c.x(), ly = c.y(), lz = c.z();
    if (lx < 0 || ly < 0 || lz < 0)
      continue;
    BrickKey key{static_cast<uint32_t>(lx / B), static_cast<uint32_t>(ly / B),
                 static_cast<uint32_t>(lz / B)};
    auto &buf = buffers[key];
    if (buf.empty())
      buf.assign(static_cast<size_t>(voxPerBrick), 0.0f);
    const int ox = lx % B, oy = ly % B, oz = lz % B;
    buf[static_cast<size_t>((oz * B + oy) * B + ox)] = v;
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
