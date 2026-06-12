#pragma once
#include <string>

namespace SceneIO {
using ProgressCallback = void (*)(float progress01, const char *stage);

// Optional callback invoked from save/load code (may be called from worker threads).
void SetProgressCallback(ProgressCallback cb);

// True once a running save has fully serialized the scene into its own
// buffers — from then on the save no longer reads scene state, so the editor
// can release its scene/render lock while compression and the file write
// finish in the background. Always false during loads. Reset this before
// starting a new job.
bool IsSceneStateReleased();
void ResetSceneStateReleased();

// Save scene in compressed binary .prs format. Returns true on success.
bool SaveScene(const std::string &path);

// Load scene — .prs only. Returns true on success.
bool LoadScene(const std::string &path);

// Explicit PRS loader.
bool LoadScenePRS(const std::string &path);
} // namespace SceneIO
