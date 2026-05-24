#pragma once
#include <string>
#include <vector>

// IES photometric profile — CPU-side asset.
// Baked into a GPU Texture2DArray slice at atlas build time.
struct IESProfile {
  std::string filePath;             // absolute path to .ies file
  std::string displayName;          // filename stem
  std::string sourceText;           // embedded original .ies payload
  std::vector<float> verticalAngles;
  std::vector<float> horizontalAngles;
  std::vector<float> candela;       // raw candela values (numVA * numHA)
  float multiplier = 1.0f;
  int numVerticalAngles = 0;
  int numHorizontalAngles = 0;
  int atlasSlice = -1;             // assigned GPU atlas slice index
  bool loaded = false;
  bool gpuReady = false;
};

constexpr int kMaxIESSlices = 32;
constexpr int kIESAtlasResolution = 256;
