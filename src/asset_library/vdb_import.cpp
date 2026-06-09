#include "vdb_import.h"

#ifdef PR_HAVE_OPENVDB

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <openvdb/openvdb.h>
#include <openvdb/tools/Interpolation.h>
#include <openvdb/tools/LevelSetUtil.h>
#include <string>
#include <type_traits>
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

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

int DensityGridRank(const std::string &name, openvdb::GridClass gridClass) {
  const std::string lower = Lower(name);
  if (lower == "density")
    return 100;
  if (lower.find("density") != std::string::npos)
    return 90;
  if (lower.find("smoke") != std::string::npos ||
      lower.find("fog") != std::string::npos)
    return 80;
  if (gridClass == openvdb::GRID_LEVEL_SET)
    return 70;
  if (lower.find("temperature") != std::string::npos ||
      lower.find("heat") != std::string::npos ||
      lower.find("flame") != std::string::npos)
    return 20;
  return 50;
}

bool IsTemperatureGridName(const std::string &name) {
  const std::string lower = Lower(name);
  return lower.find("temperature") != std::string::npos ||
         lower.find("flame") != std::string::npos ||
         lower.find("heat") != std::string::npos ||
         lower.find("fire") != std::string::npos;
}

template <typename ValueT>
float ScalarValueToFloat(const ValueT &value) {
  if constexpr (std::is_same_v<ValueT, bool>) {
    return value ? 1.0f : 0.0f;
  } else {
    return static_cast<float>(value);
  }
}

inline float ScalarValueToFloat(const openvdb::Vec3s &value) {
  return value.length();
}

inline float ScalarValueToFloat(const openvdb::Vec3d &value) {
  return static_cast<float>(value.length());
}

template <typename GridT>
openvdb::FloatGrid::Ptr ConvertToFloatGrid(
    const openvdb::GridBase::Ptr &base) {
  auto source = openvdb::gridPtrCast<GridT>(base);
  if (!source)
    return {};
  auto result =
      openvdb::FloatGrid::create(ScalarValueToFloat(source->background()));
  result->setTransform(source->transform().copy());
  result->setName(source->getName());
  result->setGridClass(source->getGridClass());
  for (auto it = source->cbeginValueOn(); it; ++it) {
    const float value = ScalarValueToFloat(it.getValue());
    if (!std::isfinite(value))
      continue;
    if (it.isVoxelValue()) {
      result->tree().setValueOn(it.getCoord(), value);
    } else {
      openvdb::CoordBBox tileBounds;
      if (it.getBoundingBox(tileBounds))
        result->tree().fill(tileBounds, value, true);
    }
  }
  return result;
}

openvdb::FloatGrid::Ptr ConvertSupportedGrid(
    const openvdb::GridBase::Ptr &base, bool allowVectorFallback) {
  if (auto grid = openvdb::gridPtrCast<openvdb::FloatGrid>(base))
    return grid;
  if (base->isType<openvdb::DoubleGrid>())
    return ConvertToFloatGrid<openvdb::DoubleGrid>(base);
  if (base->isType<openvdb::Int32Grid>())
    return ConvertToFloatGrid<openvdb::Int32Grid>(base);
  if (base->isType<openvdb::Int64Grid>())
    return ConvertToFloatGrid<openvdb::Int64Grid>(base);
  if (base->isType<openvdb::BoolGrid>())
    return ConvertToFloatGrid<openvdb::BoolGrid>(base);
  if (allowVectorFallback && base->isType<openvdb::Vec3SGrid>())
    return ConvertToFloatGrid<openvdb::Vec3SGrid>(base);
  if (allowVectorFallback && base->isType<openvdb::Vec3DGrid>())
    return ConvertToFloatGrid<openvdb::Vec3DGrid>(base);
  return {};
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
  openvdb::FloatGrid::Ptr temperatureGrid;
  try {
    openvdb::io::File file(path);
    file.open();
    std::vector<std::pair<int, openvdb::GridBase::Ptr>> scalarCandidates;
    std::vector<std::pair<int, openvdb::GridBase::Ptr>> vectorCandidates;
    for (auto it = file.beginName(); it != file.endName(); ++it) {
      openvdb::GridBase::Ptr base = file.readGrid(it.gridName());
      const int rank = DensityGridRank(it.gridName(), base->getGridClass());
      if (base->isType<openvdb::FloatGrid>() ||
          base->isType<openvdb::DoubleGrid>() ||
          base->isType<openvdb::Int32Grid>() ||
          base->isType<openvdb::Int64Grid>() ||
          base->isType<openvdb::BoolGrid>()) {
        scalarCandidates.emplace_back(rank, std::move(base));
      } else if (base->isType<openvdb::Vec3SGrid>() ||
                 base->isType<openvdb::Vec3DGrid>()) {
        vectorCandidates.emplace_back(rank, std::move(base));
      } else {
        fprintf(stderr, "VDB import: auxiliary grid '%s' type '%s' ignored\n",
                it.gridName().c_str(), base->type().c_str());
      }
    }
    file.close();
    for (const auto &candidate : scalarCandidates) {
      if (candidate.second &&
          IsTemperatureGridName(candidate.second->getName())) {
        temperatureGrid = ConvertSupportedGrid(candidate.second, false);
        if (temperatureGrid)
          break;
      }
    }
    auto pickBest = [](auto &candidates, bool allowVector) {
      if (candidates.empty())
        return openvdb::FloatGrid::Ptr{};
      std::sort(candidates.begin(), candidates.end(),
                [](const auto &a, const auto &b) { return a.first > b.first; });
      return ConvertSupportedGrid(candidates.front().second, allowVector);
    };
    grid = pickBest(scalarCandidates, false);
    if (!grid)
      grid = pickBest(vectorCandidates, true);
  } catch (const std::exception &e) {
    return fail(std::string("VDB read failed: ") + e.what());
  }
  if (!grid)
    return fail("no renderable scalar or vector grid found in VDB");

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
  std::unordered_map<BrickKey, std::vector<float>, BrickKeyHash>
      temperatureBuffers;

  auto accessor = grid->getConstAccessor();
  using Accessor = openvdb::FloatGrid::ConstAccessor;
  openvdb::tools::GridSampler<Accessor, openvdb::tools::BoxSampler> sampler(
      accessor, grid->transform());
  std::unique_ptr<openvdb::FloatGrid::ConstAccessor> temperatureAccessor;
  std::unique_ptr<openvdb::tools::GridSampler<
      openvdb::FloatGrid::ConstAccessor, openvdb::tools::BoxSampler>>
      temperatureSampler;
  if (temperatureGrid) {
    temperatureAccessor =
        std::make_unique<openvdb::FloatGrid::ConstAccessor>(
            temperatureGrid->getConstAccessor());
    temperatureSampler = std::make_unique<openvdb::tools::GridSampler<
        openvdb::FloatGrid::ConstAccessor, openvdb::tools::BoxSampler>>(
        *temperatureAccessor, temperatureGrid->transform());
    out.temperatureMin = (std::numeric_limits<float>::max)();
    out.temperatureMax = (std::numeric_limits<float>::lowest)();
  }
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
        const double sourceX =
            origin.x() + ((static_cast<double>(x) + 0.5) * sourceDim.x() /
                              importedDim[0] -
                          0.5);
        float value = 0.0f;
        if (reduced) {
          value = sampler.isSample(
              openvdb::Vec3d(sourceX, sourceY, sourceZ));
        } else {
          value = accessor.getValue(
              origin + openvdb::Coord(static_cast<int>(x),
                                      static_cast<int>(y),
                                      static_cast<int>(z)));
        }
        BrickKey key{x / out.brickSize, y / out.brickSize,
                     z / out.brickSize};
        const uint32_t ox = x % out.brickSize;
        const uint32_t oy = y % out.brickSize;
        const uint32_t oz = z % out.brickSize;

        if (temperatureSampler) {
          const openvdb::Vec3d world =
              grid->indexToWorld(openvdb::Vec3d(sourceX, sourceY, sourceZ));
          const float temperature = temperatureSampler->wsSample(world);
          if (temperature > 0.0f && std::isfinite(temperature)) {
            auto &temperatureBuffer = temperatureBuffers[key];
            if (temperatureBuffer.empty())
              temperatureBuffer.assign(static_cast<size_t>(voxPerBrick),
                                       0.0f);
            temperatureBuffer[static_cast<size_t>((oz * B + oy) * B + ox)] =
                temperature;
            out.temperatureMin =
                (std::min)(out.temperatureMin, temperature);
            out.temperatureMax =
                (std::max)(out.temperatureMax, temperature);
          }
        }

        if (!(value > 0.0f) || !std::isfinite(value))
          continue;
        ++activeVoxels;
        auto &buf = buffers[key];
        if (buf.empty())
          buf.assign(static_cast<size_t>(voxPerBrick), 0.0f);
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

  out.temperatureBricks.reserve(temperatureBuffers.size());
  for (auto &kv : temperatureBuffers) {
    float mn = kv.second[0], mx = kv.second[0];
    for (float f : kv.second) {
      mn = (std::min)(mn, f);
      mx = (std::max)(mx, f);
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
      float t = range > 0.0f
                    ? (kv.second[static_cast<size_t>(i)] - mn) / range
                    : 0.0f;
      t = (std::clamp)(t, 0.0f, 1.0f);
      brick.data[static_cast<size_t>(i)] =
          static_cast<uint8_t>(std::lround(t * 255.0f));
    }
    out.temperatureBricks.push_back(std::move(brick));
  }
  if (out.temperatureBricks.empty()) {
    out.temperatureMin = 0.0f;
    out.temperatureMax = 0.0f;
  } else {
    fprintf(stderr,
            "VDB import: temperature channel '%s', range %.3f..%.3f, "
            "%zu bricks\n",
            temperatureGrid->getName().c_str(), out.temperatureMin,
            out.temperatureMax, out.temperatureBricks.size());
  }
  return true;
}

std::string DescribeGrids(const std::string &path) {
  EnsureInit();
  std::string out;
  char line[512];
  try {
    openvdb::io::File file(path);
    file.open();
    for (auto it = file.beginName(); it != file.endName(); ++it) {
      openvdb::GridBase::Ptr base = file.readGrid(it.gridName());
      const char *cls = "?";
      switch (base->getGridClass()) {
      case openvdb::GRID_FOG_VOLUME: cls = "fog"; break;
      case openvdb::GRID_LEVEL_SET: cls = "levelset"; break;
      case openvdb::GRID_STAGGERED: cls = "staggered"; break;
      default: cls = "unknown"; break;
      }
      double mn = 0, mx = 0;
      bool haveRange = false;
      if (auto fg = openvdb::gridPtrCast<openvdb::FloatGrid>(base)) {
        float lo, hi;
        fg->evalMinMax(lo, hi);
        mn = lo; mx = hi; haveRange = true;
      } else if (auto dg = openvdb::gridPtrCast<openvdb::DoubleGrid>(base)) {
        double lo, hi;
        dg->evalMinMax(lo, hi);
        mn = lo; mx = hi; haveRange = true;
      }
      snprintf(line, sizeof(line),
               "  grid '%s'  type=%s  class=%s  activeVox=%llu%s",
               it.gridName().c_str(), base->type().c_str(), cls,
               static_cast<unsigned long long>(base->activeVoxelCount()),
               haveRange ? "" : "\n");
      out += line;
      if (haveRange) {
        snprintf(line, sizeof(line), "  range=%.4g..%.4g\n", mn, mx);
        out += line;
      }
    }
    file.close();
  } catch (const std::exception &e) {
    out += std::string("DescribeGrids failed: ") + e.what() + "\n";
  }
  return out;
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
std::string DescribeGrids(const std::string &) { return "OpenVDB disabled\n"; }
} // namespace VdbImport

#endif // PR_HAVE_OPENVDB
