#pragma once
#include <string>

namespace SceneIO {
using ProgressCallback = void (*)(float progress01, const char *stage);

// Optional callback invoked from save/load code (may be called from worker threads).
void SetProgressCallback(ProgressCallback cb);

// Save scene in compressed binary .prs format. Returns true on success.
bool SaveScene(const std::string &path);

// Load scene — auto-detects binary .prs or legacy .json. Returns true on success.
bool LoadScene(const std::string &path);

// Explicit loaders (called by LoadScene based on format detection)
bool LoadScenePRS(const std::string &path);
bool LoadSceneJSON(const std::string &path);
} // namespace SceneIO
