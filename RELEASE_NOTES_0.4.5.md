# Project Render 0.4.5 Release Notes

Changes since `78f4bb51b4474ee2eda2f19cb3df4e829ffb14a3`.

## Highlights

- Focused heavily on memory and VRAM efficiency for large arch-viz scenes, including GPU-side texture compression, detailed VRAM reporting, and reduced high-resolution export pressure.
- Added tile-based rendering for large final exports, including 360 panorama and perspective stills, so high-resolution images can be rendered in smaller GPU-memory slices.
- Added guide-aware tiled denoising: tiled exports stitch HDR beauty, OIDN albedo guide, and OIDN normal guide buffers, then run a full-frame CPU OIDN pass for final quality.
- Added spherical 360 panorama export, vertical tilt correction, custom export resolution, aspect ratio locking, and corrected perspective tile projection.
- Improved final render stop behavior so export can stop at the configured noise target or Max SPP, whichever comes first.
- Expanded export progress feedback so long tile stitching and denoising stages are visible instead of looking like a hang.

## Memory and VRAM

- Added texture usage detection from material slots and scene materials.
- Added GPU-side BC texture compression for imported and loaded textures:
  - BC1 for opaque color maps.
  - BC3 for alpha, color, normal, and packed maps.
  - BC4 for scalar maps.
  - Authored DDS and HDR textures are preserved.
- Added Texture Compression controls in Render Settings.
- Added a detailed Tools > VRAM Breakdown window with texture format, usage, compression mode, GPU memory, CPU source memory, hidden/runtime state, and scene memory details.
- Reduced additional renderer memory pressure for large scenes and high-resolution exports.
- Preserved CPU source texture copies for save/load, thumbnail previews, and recompression when changing compression modes.

## Tile-Based Export and Denoising

- Added tile-based export for large still renders.
- Added tile-based 360 panorama export for 4096x2048-style output on lower-VRAM GPUs.
- Added tile-based perspective export and fixed the projection aspect issue that caused distorted perspective tiles.
- Added final full-frame CPU OIDN denoising for tiled exports using stitched beauty, albedo guide, and normal guide buffers.
- Avoided guide-less tiled denoising so final images do not get smeared by beauty-only OIDN.
- Added visible denoising progress text after tiles finish so the app does not look frozen during the final CPU denoise pass.
- Added tile render warnings and automatic higher SPP defaults when tile rendering is selected.

## Camera and Render Export

- Added spherical 360 panorama projection for render export.
- Added vertical tilt correction for keeping architectural verticals upright with an off-axis corrected projection path.
- Added custom render resolution controls.
- Added manual aspect ratio entry and aspect-ratio locking.
- Added automatic 2:1 output sizing for spherical panorama export.
- Improved final export completion logic so noise stop uses the displayed target threshold directly while Max SPP remains a valid stop condition.
- Improved progress popup details for tile number, output size, noise status, denoising, and batch/animation states.

## Materials, Textures, and Editor Workflow

- Added UV rotation controls.
- Added live preview while changing material and light colors.
- Added Clay Render options in Render Settings.
- Added mirror tools for object transforms.
- Fixed material editor thumbnails after texture compression changes.
- Improved Qt styling and editor polish.
- Added or improved save reminder behavior when closing the app.

## Import, Scene I/O, and Reliability

- Improved scene save/load performance with diagnostic timing work.
- Fixed first-interaction issues after loading scenes by syncing renderer-owned present size, camera yaw/pitch/forward state, and transient input state.
- Improved drag-and-drop import placement so files import at the mouse position.
- Fixed SketchUp BGRA texture channel handling.
- Fixed texture state and light deletion issues.
- Added safeguards so users are less likely to lose work when closing the app.

## Denoising

- Improved OIDN NaN/Inf and emissive handling.
- Improved OIDN behavior for exported images.
- Kept OIDN CPU/GPU and OptiX final-frame modes available for non-tiled exports.
- For tiled exports, the final denoise path prioritizes correctness and lower VRAM by using CPU OIDN after CPU-side stitching.

## Notes

- Tile-based export is designed for memory relief, not speed. It can take longer than a full-frame render, especially with guide-aware CPU OIDN at the end.
- Higher SPP is recommended for tiled rendering so quality does not drift between tiles before the final stitch and denoise pass.
- Texture compression reduces GPU memory usage, but CPU source images are intentionally retained for save/load, thumbnails, and recompression.
- If a target noise value is extremely low, export may still run until Max SPP is reached first.
