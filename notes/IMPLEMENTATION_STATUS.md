# Implementation Status - ArchViz Renderer

This document tracks the current development state of the `project-render` engine, mapping completed features against the original ArchViz and high-performance path tracing goals.

## ✅ Completed Features

### 🏗️ Core Infrastructure & Asset Pipeline
- **Universal Mesh Import**: Robust Assimp-based loader supporting **FBX, OBJ, STL, DAE, and PLY**.
- **glTF 2.0 Engine**: Full-featured glTF 2.0 implementation including PBR material parsing (Metallic-Roughness & Specular-Glossiness).
- **GPU Resource Management**: High-count descriptor heaps (16k+ handles) and persistent SRV management for large-scale scenes.
- **Async Compute Dispatch**: Dedicated async compute queue for **ReSTIR DI/GI** spatial and temporal resampling, decoupling light transport from primary ray generation.

### 🌌 Unified DXR Path Tracer
- **DXR 1.1 Architecture**: Leverages Raytracing Tier 1.1 for high-efficiency hardware traversal.
- **ReSTIR DI (Direct Illumination)**: Reservoir-based resampling for direct lights with temporal and spatial reuse.
- **ReSTIR GI (Global Illumination)**: Indirect path resampling using Reconnection Shift Mapping for stable 1-SPP indirect lighting.
- **Multiple Importance Sampling (MIS)**: Balanced power heuristic MIS combining BRDF importance sampling and Next Event Estimation (NEE).
- **Physical BSDFs**: 
  - GGX Microfacet distribution.
  - Height-correlated Smith masking-shadowing function.
  - Fresnel Schlick with energy compensation and transmission support.
- **Russian Roulette**: Adaptive ray termination after the second bounce based on path throughput.

### 🧠 Intelligent Denoising Stack
- **NVIDIA DLSS Ray Reconstruction (DLSS-RR)**: Integrated via NVIDIA Streamline for state-of-the-art AI denoising of complex reflections.
- **NVIDIA NRD (ReLAX)**: Real-time denoiser for separate diffuse and specular radiance signals.
- **SVGF**: Variance-guided spatio-temporal filtering for immediate feedback during interaction.
- **Intel OIDN 2.4.1**: Integrated for high-quality "Final Frame" cleanup using **GPU Zero-Copy** D3D12 shared handles.

### 📸 ArchViz & Photographic Systems
- **Prague Sky Model**: Physically based dynamic atmospheric scattering model for accurate daylighting.
- **Physical Camera System**: Photography-based exposure control using **ISO, Shutter Speed, and F-Number**.
- **Photometric Lighting**: Full support for **IES Light Profiles** (IES textures baked into Texture2DArrays).
- **Auto-Exposure**: Real-time luminance histogram analysis for stable EV100 calculation and auto-exposure compensation.
- **Material Specializations**: Clearcoat, Transmission (glass/foliage), and Emissive lobe support.

### 🖥️ Editor & User Experience
- **Advanced Workspace**: ImGui docking implementation with **Persistent Layouts** (saved/loaded automatically).
- **Lossless Screenshots**: High-bitrate tonemapped frame export to PNG via WIC.
- **Performance Profiling**: Real-time GPU timestamp-based profiling for individual passes (Direct, GI, Denoise, Post).

---

## 🏎️ Engineering Extras (Optimizations Applied)
- **64-Byte Material Struct**: Packed material data to fit precisely within a single GPU cache line, reducing memory pressure.
- **Hardware Opaque Fast-Path**: Dynamic BLAS rebuilding that flags non-alpha geometry as `OPAQUE` to maximize RT Core performance.
- **Specular Virtual Points**: Physically correct motion vector generation for specular reflections to ensure DLSS-RR stability on curved surfaces.
- **Jitter-Aligned Motion Vectors**: Synchronized Streamline configuration with jittered camera matrices for razor-sharp upscaling.

---

## 🛠️ Roadmap: Next Evolution

### 1. Advanced Light Transport & Performance
- [ ] **Shader Execution Reordering (SER)**: Implement NVAPI-based SER to reduce thread divergence in multi-bounce paths.
- [ ] **Light Trees / ReGIR**: Spatial data structures for scalable ReSTIR in scenes with thousands of area lights.
- [ ] **Ray Cones for Texture LOD**: Replace fixed-mip sampling with ray-footprint-based LOD to eliminate texture cache thrashing.

### 2. ArchViz Rendering Extensions
- [ ] **Volumetric Fog & VDB**: Integrated volumetric scattering for aerial perspective and localized fog.
- [ ] **Decals & Trim Sheets**: Support for non-destructive surface detailing on complex meshes.
- [ ] **Advanced Translucency**: Subsurface scattering (SSS) for stone, skin, and plastics.

### 3. Editor & Pipeline
- [ ] **Animated Camera Paths**: Keyframe-based camera animation for cinematic walkthrough exports.
- [ ] **Material Layering**: Support for blended materials (e.g., dirt/moss overlays) via vertex colors or weight maps.
- [ ] **Plugin System**: Modular architecture for extending import/export capabilities.

---

## 📋 Build & Technical Status
- **Language/SDK**: C++17, DirectX 12 (Agility SDK), Windows SDK 10.0.22621.
- **Dependencies**: Assimp, ImGui, NVIDIA Streamline, Intel OIDN, TinyGLTF.
- **Current Status**: ✅ Production-ready for static scene ArchViz; Real-time dynamic lighting stable.

## 🔗 Key References
- **ReSTIR GI**: [Bitterli et al. 2020](https://research.nvidia.com/publication/2020-07_spatiotemporal-reservoir-resampling-real-time-ray-tracing-dynamic-direct)
- **DLSS-RR**: [NVIDIA Streamline Documentation](https://github.com/NVIDIAGameWorks/Streamline)
- **PBR Principles**: [Physically Based Rendering (PBRT)](https://www.pbr-book.org/)
