# project-render 0.1.0 — High-End ArchViz Real-Time Engine

`project-render` is a state-of-the-art real-time rendering engine designed for high-fidelity Architectural Visualization (ArchViz). It leverages DirectX 12, DXR Ray Tracing, and NVIDIA Streamline to deliver physically accurate lighting and cinematic results.

---

## ✨ Key Features

### 🌌 Advanced Path Tracing
- **Unified DXR Path Tracer**: Progressive path tracing with high-precision accumulation (R32G32B32A32_FLOAT).
- **Multiple Importance Sampling (MIS)**: Power Heuristic-based MIS combining BRDF and Light sampling to eliminate fireflies on glossy surfaces.
- **ReSTIR DI & GI**: Reservoir-based Spatio-Temporal Importance Resampling.
  - **Indirect GI**: Indirect path resampling with Reconnection Shift Mapping for stable multi-bounce global illumination.
  - **Direct DI**: Many-light sampling with temporal and spatial reservoir reuse.
- **Efficiency Optimizations**:
  - **Russian Roulette**: Adaptive path termination based on throughput to maximize MegaRays/second.
  - **64-Byte Material Struct**: GPU-cache optimized material data layout for reduced memory bandwidth.
  - **Opaque Hardware Fast-Path**: Dynamic BLAS flagging for non-alpha-tested geometry.

### 🧠 Intelligent Denoising Stack
- **NVIDIA DLSS Ray Reconstruction (DLSS-RR)**: State-of-the-art denoising via Streamline. Includes custom jitter-aligned motion vectors and specular virtual point reprojection for perfect reflections.
- **NVIDIA NRD (ReLAX)**: Real-time denoising for diffuse and specular signals, ideal for dynamic scenes.
- **SVGF (Spatio-Temporal Variance-Guided Filtering)**: Integrated temporal filtering with A-Trous spatial passes for immediate feedback.
- **Intel Open Image Denoise (OIDN) 2.x**: AI-accelerated "Final Frame" denoising via GPU zero-copy shared handles for ultra-high quality exports.

### 🏗 Asset & ArchViz System
- **Universal Model Import**: Robust support for **glTF 2.0**, FBX, OBJ, and STL via Assimp.
- **OpenPBR-Oriented Material Runtime**:
  - Canonical runtime subset built around base color, metalness, roughness, specular weight, IOR, transmission, coat, emissive, alpha mode, and double-sided state.
  - Thin-walled transmission and translucency controls for windows, leaves, fabrics, and other ArchViz-specific shading cases.
  - Built-in UV scale/offset and world-space tri-planar mapping controls for fast look-dev without round-tripping back to DCC tools.
  - Texture slots for base color, normal, emissive, occlusion, and metallic-roughness, with glTF materials mapped into the same runtime model.
- **ArchViz Material Editor**:
  - Metallic-Roughness and specular-driven workflows exposed in one scene material editor.
  - **Clearcoat & Transmission**: Specialized lobes for glass, automotive finishes, polished surfaces, and thin-walled foliage.
  - Grass/material overrides for procedural lawn rendering without needing a separate material system.
- **Shared Scene Materials for LiveLink**:
  - Stable-ID material reuse across imported payloads avoids thousands of duplicate scene materials.
  - Face and slot bindings can point at one logical scene material, so editing one updates every bound surface.
  - Material lists now prioritize scene-used materials instead of dumping every historical entry.
- **Physical Camera & Lighting**:
  - **IES Light Profiles**: Support for industry-standard photometric light data.
  - **Advanced Sky Models**: Integrated **Prague Sky Model** for physically accurate daylighting.
  - **Physical Exposure**: Control via ISO, Shutter Speed, and Aperture.
  - **Auto-Exposure**: Real-time luminance histogram evaluation for stable EV100 calculation.

### 🖥️ Editor & Workflow
- **Docking Workspace**: Full ImGui docking support with persistent layouts.
- **Saved Views & Camera Sequencing**: Saved camera states feed keyframed animation paths with per-segment easing.
- **Render Output Pipeline**: Single-frame PNG export, batch saved-view rendering, and animation export to image sequences or MP4.
- **Qt Materials Panel**: Event-driven refresh path replaces the old polling timer, which removes most of the scroll/input fighting during material edits.
- **High-Quality Export**: One-click lossless PNG export of tonemapped frames.
- **Performance Profiling**: Real-time GPU/CPU timing, SPP counters, and noise level estimation.
- **3ds Max 2025 LiveLink**: Named-pipe live sync for nodes, native `.prmesh` mesh payloads, shared materials, lights, camera, selection, Qt-side diagnostics, and saved-scene resume via persistent DCC identity.
- **Archicad 28 LiveLink**: Pipe-based scene sync with `.prmesh` payloads, merged material references, material/camera deltas, and stable material identity across re-syncs.

---

## 📦 Version 0.1.0 Highlights

- OpenPBR-oriented scene material runtime with coat, transmission, thin-walled, translucency, UV transform, and tri-planar controls
- Shared-material LiveLink path for both 3ds Max and Archicad
- Stable material IDs serialized in `.prmesh` payload version 4
- Archicad material delta export with merged references instead of per-node duplication
- Material editors tuned for scene-used materials and cleaner large-scene workflows
- Qt material panel refresh moved from polling to event-driven updates
- DXR scene deletion path now forces acceleration-structure rebuilds after node removal
- Animation workflow improvements: clearer ease-in/ease-out sequencing, MP4 export support, time estimation, and safer cancellation during long renders
- Recent renderer maintenance: HDR analytic sun intensity fixes and ongoing material conversion cleanup

### Recent Git Milestones

- `25e23e7`: deletions trigger TLAS rebuilds now
- `36d6a4d`: Qt material panel polling removed in favor of event-driven refreshes
- `307b074`: shared material identity now survives `.prmesh` payload round-trips
- `7cb436c`: HDR analytic sun intensity multipliers corrected
- `6f6c0a7`: material conversion path fixes
- `677b74a`, `8f14e5d`, `cb04853`, `3823f76`: animation export, easing, time estimation, and usability work

---

## 🚀 Quick Start (Windows)

### Prerequisites
- Windows 10/11
- NVIDIA RTX GPU (DXR 1.1 + Ray Reconstruction support recommended)
- Visual Studio 2022
- CMake 3.20+

### Build Instructions
1. **Standard Build**:
   ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```

2. **Full Feature Build (glTF + vcpkg)**:
   ```powershell
   # Configure with vcpkg toolchain
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
         -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake" `
         -DUSE_TINYGLTF=ON
   cmake --build build --config Release
   ```

---

## 🛠 Advanced Configuration

### 3ds Max LiveLink
- Engine startup: run the renderer with the named-pipe provider enabled.

```powershell
./build/Release/bin/project-render.exe --max-livelink-pipe
```

- Optional custom pipe name:

```powershell
./build/Release/bin/project-render.exe --max-livelink-pipe my-custom-pipe
```

- Default pipe name: `project-render-max-livelink`
- Current scope:
  - incremental sync for nodes, transforms, visibility, meshes, materials, lights, camera, and selection
  - slot-aware Multi/Sub material handling with preserved `.prmesh` material slots and shared scene-material reuse by stable ID
  - non-destructive disconnect plus persisted PRS bindings on the engine side
  - saved-scene resume by combining persistent Max scene/node GUIDs with a persisted last-synced DCC snapshot
- material edits now collapse onto shared scene materials when the DCC material identity is the same, which keeps the material editor usable on large scenes
- Plugin build and install details live in `tools/3dsmax2025/README.md`

### DLSS / DLSS Ray Reconstruction
- **AppID**: DLSS requires an NVIDIA AppID. Set the environment variable `SL_APPLICATION_ID` or create `sl_appid.txt` in the root.
- **Development Mode**: Debug builds automatically use "Development" DLLs (watermarked) to bypass whitelist checks.

### NVIDIA Streamline (SL)
- **Reflections**: Specular motion vectors are calculated using virtual reflection points to ensure DLSS-RR stability on curved surfaces.
- **Jitter**: Jittered motion vectors are enabled by default for maximum clarity during accumulation.

### Troubleshooting
- **Grey Viewport**: Ensure a sky model is selected or an HDRI is loaded in the Environment panel.
- **Low Performance**: Check that `D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE` is being correctly applied to non-transparent meshes.
- **Material Expectations**: The engine uses an OpenPBR-oriented runtime subset rather than full MaterialX or a complete offline renderer material graph. Keep that in mind when matching every DCC parameter one-to-one.
- **Deleting Objects with DXR Enabled**: The renderer now forces a full acceleration-structure rebuild after node deletion. If you still hit a delete-path crash, capture whether it happens from engine-side delete, LiveLink delete, or both.

---

*This project is a high-performance rendering scaffold evolved into a feature-rich ArchViz tool in collaboration with an AI coding assistant.*