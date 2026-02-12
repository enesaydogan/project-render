# project-render — High-End ArchViz Real-Time Engine

`project-render` is a state-of-the-art real-time rendering engine designed for high-fidelity Architectural Visualization (ArchViz). It leverages DirectX 12, DXR Ray Tracing, and NVIDIA Streamline to deliver physically accurate lighting and cinematic results.

---

## ✨ Key Features

### 🌌 Advanced Rendering
- **Unified DXR Path Tracer**: Progressive path tracing with high-precision accumulation (R32G32B32A32_FLOAT).
- **ReSTIR DI & GI**: Reservoir-based Spatio-Temporal Importance Resampling for both direct lighting and many-bounce indirect global illumination.
- **Image-Based Lighting (IBL)**: Physics-based Rayleigh/Mie atmospheric scattering and HDR environment map support with pre-filtered importance sampling.
- **Microfacet PBR BRDF**: Physically-based rendering using GGX distribution, Smith geometry, and Schlick Fresnel, shared between raster and raytracing paths for visual parity.

### 🧠 Intelligent Denoising & Upscaling
- **NVIDIA DLSS Ray Reconstruction (DLSS-D)**: Integrated via Streamline for high-quality real-time denoising of complex lighting and reflections.
- **Intel Open Image Denoise (OIDN) 2.x**: Integrated for high-quality final-frame cleanup via GPU zero-copy shared handles.
- **DLSS Super Resolution (DLSS-SR)**: High-performance upscaling for fluid interaction even at 4K.

### 🏗 Asset & Material System
- **Universal Model Import**: Robust support for **glTF 2.0 (GLB/GLTF)**, FBX, OBJ, STL, and more via Assimp.
- **ArchViz Material Editor**: Real-time material tweaking with support for:
  - Metallic-Roughness & Specular-Glossiness workflows.
  - Clearcoat and secondary specular lobes.
  - Thin-walled transmission (glass/leaves).
  - Tri-planar mapping and local UV transforms.
- **Procedural Grass System**: Optimized grass generation and rendering for large-scale environments.

---

## 🚀 Quick Start (Windows)

### Prerequisites
- Windows 10/11
- NVIDIA RTX GPU (for DXR and DLSS features)
- Visual Studio 2022
- CMake 3.20+

### Build Instructions
1. **Standard Build**:
   ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```

2. **Full Feature Build (glTF + vcpkg)**:
   Ensure `vcpkg` is installed and run:
   ```powershell
   # Install tinygltf dependency
   vcpkg install tinygltf:x64-windows

   # Configure with vcpkg toolchain
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
         -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake" `
         -DUSE_TINYGLTF=ON
   cmake --build build --config Release
   ```

---

## 🛠 Advanced Features & Troubleshooting

### DLSS / DLSS Ray Reconstruction
- **AppID requirement**: DLSS requires an NVIDIA AppID. Set the environment variable `SL_APPLICATION_ID` or create `sl_appid.txt` next to the executable.
- **Development DLLs**: Production builds require whitelisted AppIDs. Debug builds automatically use "Development" DLLs which bypass whitelist checks but display an onscreen watermark.

### NVIDIA Streamline (SL)
- **Logging**: Streamline logs are written to the `sl_logs` directory. Logging behavior can be customized by placing `sl.interposer.json` next to the executable.
- **Ray Reconstruction**: Throttled by default to optimize for quality vs frequency. See `src/dxr_renderer.cpp` for evaluation logic.

### Troubleshooting
- **Grey Viewport**: Usually indicates a missing HDRI or an empty scene. Import a model or load an environment map via the UI.
- **DLSS Watermark**: Indicates you are using development NGX binaries. This is normal for non-production environments.

---

*This project was initialized and developed in collaboration with an AI coding assistant as a starting scaffold and evolved into a feature-rich renderer.*