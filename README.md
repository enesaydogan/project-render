# - IMPORTANT NOTE -

# I created this renderer for my use. I dont know any coding. 99% of the code is from Agentic coding. It does the job for me. And features i put in based on projects i made. If projects need spherical panoram i add spherical panorama. Its free and always be free. I hope you like it. Good Luck! 

# project-render 0.5.0 - LET THERE BE LIGHT

`project-render` is a real-time rendering engine for high-fidelity Architectural Visualization (ArchViz). It uses DirectX 12, DXR ray tracing, NVIDIA Streamline, and optional final-frame denoisers for physically based lighting and export workflows.

---

## What's New in 0.5.0

- **LET THERE BE LİGHT**: A dedicated lighting release centered on scalable local-light sampling, IES profiles, instanced fixtures, and faster light authoring.
- **ReGIR local-light sampling**: Added a GPU ReGIR path for point, spot, area, IES, and emissive-proxy lights, with runtime fallback handling and a `regir debug` panel for live renderer state.
- **Real IES workflow**: IES profiles are parsed, saved with scenes, bound on the GPU, and exposed through the Lights panel as authored light-profile data instead of a placeholder light type.
- **Grouped and instanced lights**: Lights now support prototype/instance style editing, multi-selection, Shift-drag duplication, grouped fixture workflows, and LiveLink instance updates.
- **Viewport light authoring**: Added click-to-create and click-to-move light placement using viewport surface picks, plus better light gizmos, box selection, and multi-type selection behavior.
- **Emissive mesh participation**: ReGIR can sample emissive mesh proxies through real triangle-backed emissive data instead of rough sphere/point approximations.
- **DLSS-RR stability work**: Disabled unsupported DLSS-RR dynamic input resolution and tightened dense motion/AOV inputs to reduce shimmer, checker flashes, and stale reconstruction guidance.

---

## Key Features

### Advanced Path Tracing
- **Unified DXR Path Tracer**: Progressive path tracing with high-precision accumulation (`R32G32B32A32_FLOAT`).
- **Wavefront Architecture**: Wavefront-first DXR runtime with separated traversal, resolve, visibility, ReSTIR, and accumulation stages.
- **Multiple Importance Sampling (MIS)**: Power-heuristic MIS combining BRDF and light sampling to reduce fireflies on glossy surfaces.
- **ReSTIR DI & GI**: Reservoir-based spatio-temporal importance resampling, now fully integrated with the wavefront path.
- **Efficiency Optimizations**: Russian roulette, compact material data, opaque hardware fast paths, Opacity Micromaps, and secondary GI/shadow thinning.

### Reconstruction & Final Denoising
- **NVIDIA DLSS Ray Reconstruction (DLSS-RR)** for supported real-time reconstruction through NVIDIA Streamline.
- **Intel Open Image Denoise (OIDN) 2.x** for final-frame/export denoising workflows.
- **NVIDIA OptiX Denoiser 9.x** as an optional NVIDIA/CUDA final-frame denoiser.

### Asset & ArchViz System
- **Universal Model Import**: Robust support for glTF 2.0, FBX, OBJ, STL, and fast drag-and-drop file import.
- **OpenPBR-Oriented Material Runtime**: Material classes, clearcoat, transmission, tri-planar mapping, stochastic tiling, parallax mapping, and window-box interiors.
- **Foliage & Environment**: Scatter tooling, grass emitting, foliage performance improvements, and real-time volumetric clouds.
- **ArchViz Material Editor**: Qt and ImGui material editors with class presets, selected-material focus, texture preview/weights, mapping controls, and a Kelvin-capable color picker.
- **Physical Camera & Lighting**: IES light profiles, Prague Sky Model with intensity controls, white balance, physical exposure, and Clay Render mode for rapid lighting tweaks.

### Editor & Workflow
- **Qt UI Integration**: Event-driven, hardware-accelerated Qt6 UI with highly optimized material/environment editing panels.
- **Advanced Selection Tools**: Transform undo/redo, delete/cleanup tools, fast node locking, selection bounding box outlines, and Shift-drag quick copy/instancing support.
- **Saved Views & Camera Sequencing**: Keyframed animation paths with per-segment easing and MP4 export.
- **Safer Scene I/O**: Save/load runs asynchronously with progress feedback and normal close/exit paths are blocked while scene I/O is active.
- **3ds Max 2024/2025 LiveLink**: Named-pipe live sync for nodes, `.prmesh` payloads, shared materials, and geometry streaming.
- **Archicad 28 LiveLink**: Pipe-based scene sync with stable material identity across re-syncs.

---

## Quick Start (Windows)

### Prerequisites
- Windows 10/11
- NVIDIA RTX GPU recommended for DXR, DLSS-RR, and OptiX
- Visual Studio 2022
- CMake 3.20+
- Qt 6, optional but recommended for the full editor UI (`Widgets` component required)
- vcpkg for Assimp, fmt, and other third-party dependencies

### Required Proprietary SDKs
Due to licensing agreements, these SDKs are not included in the public repository. Acquire them directly from the vendors and place them under `thirdparty/` when needed:

- `thirdparty/archicad-sdk`: Archicad API Development Kit, required for the Archicad 28 LiveLink plugin.
- `thirdparty/sketchup_sdk`: SketchUp C API SDK, required for SketchUp import integration.
- `thirdparty/max2024-sdk` / `thirdparty/max2025-sdk`: 3ds Max SDKs, required for 3ds Max plugins.
- `thirdparty/vray-sdk`: V-Ray AppSDK or headers, required only for specific V-Ray material conversion paths.

Smaller dependencies such as OIDN and Streamline are included directly in the repo or fetched during build.

---

## License

Project Render's own source code is intended to be released under the MIT License: free of charge, permissive, and suitable for personal, commercial, and open-source use.

Third-party libraries, SDKs, runtimes, plugins, headers, and binary redistributables remain under their own licenses and vendor terms. In particular, Qt, OIDN, Assimp, Streamline/DLSS/NGX, OptiX, SketchUp SDK, Autodesk 3ds Max SDK, Graphisoft Archicad API DevKit, and Chaos V-Ray SDK are not relicensed by Project Render.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency notices and redistribution notes.

---

## Build Process

### Main Application
Configure with Qt enabled:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake" `
      -DUSE_QT_UI=ON
```

Build the Release target:

```powershell
cmake --build build --config Release --target project-render
```

### Optional Build Flags
- `USE_QT_UI=ON`: Enables the Qt editor UI.
- `USE_OPENIMAGEDENOISE=ON`: Enables OIDN support using `thirdparty/oidn`.
- `USE_OPTIX_DENOISER=ON`: Enables NVIDIA OptiX final-frame denoising.
- `OPTIX_ROOT=...`: Path to the NVIDIA OptiX SDK root when `USE_OPTIX_DENOISER=ON`.

### OptiX Denoiser Build
OptiX is optional and is disabled by default. Use NVIDIA OptiX SDK 9.0 or newer; OptiX SDK 9.1.0 has been tested with CUDA Toolkit 12.2.

Example:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DUSE_QT_UI=ON `
      -DUSE_OPENIMAGEDENOISE=ON `
      -DUSE_OPTIX_DENOISER=ON `
      -DOPTIX_ROOT="C:/ProgramData/NVIDIA Corporation/OptiX SDK 9.1.0"

cmake --build build --config Release --target project-render
```

If you see this runtime log:

```text
OptixDenoiser: USE_OPTIX_DENOISER is OFF; OptiX denoise skipped.
```

the executable was built from a CMake cache where `USE_OPTIX_DENOISER` was disabled. Reconfigure that build directory with `-DUSE_OPTIX_DENOISER=ON`, then rebuild.

---

## Plugins

The LiveLink plugins are built from their respective sub-projects in `tools/`.

### Archicad 28 LiveLink
Ensure `thirdparty/archicad-sdk` is present, then build:

```powershell
cmake -S tools/archicad28 -B build-archicad28-v142 -A x64
cmake --build build-archicad28-v142 --config Release
```

### 3ds Max 2024 / 2025 LiveLink
Ensure the matching Max SDKs are available under `thirdparty/`, then build:

```powershell
cmake -S tools/3dsmax2024 -B build-max2024 -A x64
cmake --build build-max2024 --config Release

cmake -S tools/3dsmax2025 -B build-max2025 -A x64
cmake --build build-max2025 --config Release
```

Check `tools/3dsmax2025/README.md` or `tools/archicad28/README.md` for plugin installation details.

---

## Advanced Configuration

### 3ds Max LiveLink
- Engine startup: `./build/Release/bin/project-render.exe --max-livelink-pipe`
- Live sync supports incremental updates for nodes, visibility, meshes, materials, and selection.

### DLSS / DLSS Ray Reconstruction
- DLSS requires an NVIDIA AppID through the `SL_APPLICATION_ID` environment variable or `sl_appid.txt` in the repo root.
- DLSS-RR is a temporal real-time reconstruction path and is separate from final-frame OIDN/OptiX denoising.

### Final Denoiser Modes
- **Off**: Presents the converged path-traced accumulation directly.
- **OIDN CPU/GPU**: Cross-vendor final-frame denoising path, depending on the available OIDN backend.
- **OptiX**: NVIDIA/CUDA final-frame denoiser. Requires `USE_OPTIX_DENOISER=ON`, a CUDA-capable NVIDIA GPU matching the D3D12 adapter, and a compatible OptiX SDK.

### Troubleshooting
- **Grey viewport**: Ensure a sky model is selected or an HDRI is loaded in the Environment panel.
- **Non-English file paths**: 0.5.0 keeps the UTF-8 path handling improvements for glTF/GLB and scene I/O. If an importer still fails, try moving the asset to a short ASCII-only path and report the original path.
- **OptiX stub log**: Reconfigure and rebuild the exact executable you launch with `-DUSE_OPTIX_DENOISER=ON`.
- **OptiX runtime failure**: Verify CUDA Toolkit installation, NVIDIA driver support, and that the D3D12 adapter matches the CUDA device.
- **Deleting objects with DXR enabled**: The renderer forces an acceleration-structure rebuild. If you see crashes, verify build geometry flags.

---

This project is a high-performance rendering scaffold evolved into a feature-rich ArchViz tool in collaboration with an AI coding assistant.
