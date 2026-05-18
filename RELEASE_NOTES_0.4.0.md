# Project Render 0.4.0 Release Notes

Changes since `bb826056cff1d419f9e78bea8e4026aa1fc48569`.

## Highlights

- Moved the renderer to a wavefront-first DXR architecture and removed the legacy raygen/path-tracer shader path from the shipping pipeline.
- Expanded the arch-viz material system with material classes, OpenPBR-style defaults, mapping controls, stochastic tiling, parallax, and window-box workflows.
- Added major Qt editor polish: toolbar icons, material auto-focus, transform undo/redo, cleanup tools, render progress UI, safer scene I/O, and a custom arch-viz color picker.
- Improved import, save/load, and reimport robustness, including UTF-8/non-English path handling and blocking normal app exit while scene save/load is active.
- Improved final-frame export and denoiser stability, including OIDN input sanitization and DXR AO fixes after the wavefront migration.

## Rendering

- Wavefront DXR is now the primary architecture for primary/secondary/shadow rays, material resolve, ReSTIR, and accumulation.
- Removed the old legacy raygen and hitgroup exports from the DXR state object.
- Improved wavefront IBL boost behavior, adaptive sampling, GI/shadow behavior, and DXR AO compatibility.
- Disabled older fast-GI/fast-shadow shortcuts that conflicted with the wavefront parity goals.
- Increased cloud rendering resources and fixed cloud-related behavior.
- Added camera white balance and improved saved camera state handling.

## Materials and Mapping

- Added material class authoring defaults and class controls in both material editors.
- Expanded the material runtime with clearer texture weights, packed material data, and OpenPBR-oriented controls.
- Added height parallax mapping.
- Added window-box parallax mapping for architectural interiors.
- Added back-face rendering support for reversed window-box planes.
- Fixed stochastic tiling "Per Surface" mode so separate faces no longer share the same offset/rotation/color variation.
- Added a custom Qt color picker with RGB, HSV, Kelvin temperature, previous/current swatches, and hue/saturation/value variation strips.

## Editor and Workflow

- Added scene-owned transform history for gizmo transforms.
- Added Ctrl+Z/Ctrl+Y, Edit > Undo/Redo, and toolbar undo/redo icons.
- Added a delete shortcut and safer delete flow for selected nodes.
- Added an orphaned-data cleanup tool and improved cleanup after asset deletion/reimport.
- Improved material panel behavior so selected mesh materials are surfaced automatically.
- Improved render preview and render/export progress feedback.
- Added safeguards that block normal window close, File > Exit, and ImGui Exit while scene save/load is active.

## Import, Save, and Paths

- Improved scene save/load reliability.
- Improved UTF-8 and non-English path handling for imported assets, including glTF/GLB paths with Turkish characters.
- Fixed reimport and orphan-cleanup paths that could disturb material references.
- Improved drag-and-drop/import path normalization.

## Denoising and Export

- Sanitized OIDN input buffers to prevent NaN/Inf failures in exported files.
- Improved final-frame export safety and progress reporting.
- Kept OptiX and OIDN final-frame denoising paths available as optional build/runtime paths.

## LiveLink and Plugins

- Fixed Archicad LiveLink behavior.
- Improved 3ds Max and Archicad material/geometry sync paths.
- Updated installer packaging components for the core app plus optional 3ds Max 2024, 3ds Max 2025, and Archicad 28 plugins when their builds are present.

## Notes

- Normal close/exit paths are blocked during active scene I/O to prevent save corruption or data loss. Force-kill, power loss, or OS termination can still interrupt writes.
- If a non-English asset path still fails in a third-party importer path, try a short ASCII-only path as a temporary workaround and report the failing path.
