# Material Architecture

This project now treats materials as two layers:

1. Authoring and interchange state.
2. Compiled runtime state for raster and DXR.

The goal is to support richer archviz-facing authoring without increasing ray payload size or reintroducing duplicated material rules across the renderer, editors, scene IO, and live-link.

## Current layout

- `src/assets/asset_loader.h`
  - Owns the canonical CPU-side material struct.
  - Contains authoring-facing fields such as `workflow`, `metalnessTexture`, and `roughnessGlossTexture`.

- `src/material/material_system.*`
  - Single source of truth for shared material semantics.
  - Owns workflow labels, presets, texture-slot helpers, runtime flag generation, derived packed surface texture generation, and raster/DXR packing.

- `src/material/material_io.*`
  - Owns PRS-facing material serialization and restore helpers.
  - Keeps material save and load behavior aligned with the same authoring model.

- `src/material/material_livelink.*`
  - Owns translation from live-link payloads into engine materials.
  - Keeps live-link material application from duplicating field-mapping logic.

## Runtime model

Authoring can use either:

- packed metal-rough textures, or
- separate metalness and roughness or glossiness textures.

Runtime remains compact:

- if separate authoring textures are present, the engine derives a hidden packed surface texture;
- raster upload uses shared `RuntimeRasterMaterialConstants`;
- DXR upload uses shared `RuntimeDxrMaterialData` plus `RuntimeDxrMaterialExtraData`;
- ray payload size does not change.

## Live-link compatibility

The engine now accepts both:

- legacy packed `metalRoughTexture*` fields;
- extended payload fields for `workflow`, `metalnessTexture*`, and `roughnessGlossTexture*`.

Older providers continue to work through the packed path. Providers can opt into the split workflow incrementally.

## Completed refactor steps

1. Separate authoring slots from packed runtime usage.
2. Add workflow-aware editor behavior for metal-rough and reflection-glossiness.
3. Restore material texture thumbnails in the editor.
4. Centralize workflow labels and preset logic.
5. Centralize derived packed surface texture generation.
6. Centralize raster material packing.
7. Centralize DXR material packing.
8. Centralize PRS material serialization and restore.
9. Centralize live-link material payload application.
10. Keep runtime shading compact while expanding authoring semantics.

## Remaining external follow-up

The only major remaining work is upstream provider adoption:

- Archicad and Max exporters still primarily emit packed metal-rough payloads.
- They can be upgraded independently to send split texture semantics without further renderer changes.