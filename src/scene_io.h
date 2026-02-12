#pragma once
#include <string>

namespace SceneIO {
// Save the current scene state to a JSON file. Returns true on success.
bool SaveScene(const std::string &path);

// Load a scene state from a JSON file. Returns true on success.
bool LoadScene(const std::string &path);
} // namespace SceneIO
