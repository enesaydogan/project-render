#pragma once

namespace MaterialEditor {
    // Draw the Inspector panel UI for selected object properties.
    void Draw(bool &visible);
    
    // Select a specific material index (e.g. from picking)
    void SelectMaterial(int materialIndex);

    // Picking Tool State
    bool IsPickingEnabled();
    void SetPickingEnabled(bool enabled);
}
