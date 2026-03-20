# project-render — High-End ArchViz Real-Time Engine

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
- **ArchViz Material Editor**: 
  - Metallic-Roughness & Specular-Glossiness workflows.
  - **Clearcoat & Transmission**: Specialized lobes for glass, automotive finishes, and thin-walled foliage.
- **Physical Camera & Lighting**:
  - **IES Light Profiles**: Support for industry-standard photometric light data.
  - **Advanced Sky Models**: Integrated **Prague Sky Model** for physically accurate daylighting.
  - **Physical Exposure**: Control via ISO, Shutter Speed, and Aperture.
  - **Auto-Exposure**: Real-time luminance histogram evaluation for stable EV100 calculation.

### 🖥️ Editor & Workflow
- **Docking Workspace**: Full ImGui docking support with persistent layouts.
- **High-Quality Export**: One-click lossless PNG export of tonemapped frames.
- **Performance Profiling**: Real-time GPU/CPU timing, SPP counters, and noise level estimation.
- **3ds Max 2025 LiveLink**: Named-pipe live sync for nodes, native `.prmesh` mesh payloads, materials, lights, camera, selection, Qt-side diagnostics, and saved-scene resume via persistent DCC identity.

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
  - slot-aware Multi/Sub material handling with preserved `.prmesh` material slots
  - non-destructive disconnect plus persisted PRS bindings on the engine side
  - saved-scene resume by combining persistent Max scene/node GUIDs with a persisted last-synced DCC snapshot
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

---

*This project is a high-performance rendering scaffold evolved into a feature-rich ArchViz tool in collaboration with an AI coding assistant.*