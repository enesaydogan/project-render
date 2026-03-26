#pragma once

#include <cstddef>
#include <functional>
#include <windows.h>

namespace MaterialEditor {
    // Draw the Inspector panel UI for selected object properties.
    void Draw(HWND hwnd, bool &visible);
    
    // Select a specific material index (e.g. from picking)
    void SelectMaterial(int materialIndex);

    // Returns pending material selection from picking and clears it.
    // Returns -1 when no selection is pending.
    int ConsumePendingMaterialSelect();

    // Picking Tool State
    bool IsPickingEnabled();
    void SetPickingEnabled(bool enabled);

    // Register for picked-material or picking-state changes.
    size_t RegisterStateListener(std::function<void()> callback);
    void UnregisterStateListener(size_t listenerId);
}
