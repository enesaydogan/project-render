# project-render 0.1.6 — High-End ArchViz Real-Time Engine

`project-render` is a state-of-the-art real-time rendering engine designed for high-fidelity Architectural Visualization (ArchViz). It leverages DirectX 12, DXR Ray Tracing, and NVIDIA Streamline to deliver physically accurate lighting and cinematic results.

---

## ✨ Key Features

### 🌌 Advanced Path Tracing
- **Unified DXR Path Tracer**: Progressive path tracing with high-precision accumulation (R32G32B32A32_FLOAT).
- **Multiple Importance Sampling (MIS)**: Power Heuristic-based MIS combining BRDF and Light sampling to eliminate fireflies on glossy surfaces.
- **ReSTIR DI & GI**: Reservoir-based Spatio-Temporal Importance Resampling.
- **Efficiency Optimizations**: Russian Roulette, 64-Byte Material Struct, Opaque Hardware Fast-Path.

### 🧠 Intelligent Denoising Stack
- **NVIDIA DLSS Ray Reconstruction (DLSS-RR)** & **NVIDIA NRD (ReLAX)**.
- **SVGF** & **Intel Open Image Denoise (OIDN) 2.x**.

### 🏗 Asset & ArchViz System
- **Universal Model Import**: Robust support for **glTF 2.0**, FBX, OBJ, and STL.
- **OpenPBR-Oriented Material Runtime**: Clearcoat, transmission, and tri-planar mapping.
- **ArchViz Material Editor**: Single scene material editor combining specular/metallic workflows. 
- **Physical Camera & Lighting**: IES Light Profiles, Prague Sky Model, Physical Exposure.

### 🖥️ Editor & Workflow
- **Qt UI Integration**: A fully event-driven, hardware-accelerated Qt6 UI replacing polling loops and greatly improving large scene material edits.
- **Saved Views & Camera Sequencing**: Keyframed animation paths with per-segment easing & MP4 out.
- **3ds Max 2024/2025 LiveLink**: Named-pipe live sync for nodes, `.prmesh` payloads, shared materials, and fast geometry streaming.
- **Archicad 28 LiveLink**: Pipe-based scene sync with stable material identity across re-syncs.

---

## 🚀 Quick Start (Windows)

### Prerequisites
- Windows 10/11
- NVIDIA RTX GPU (DXR 1.1 + Ray Reconstruction support recommended)
- Visual Studio 2022
- CMake 3.20+
- Qt 6 (Optional but recommended for full editor feature set, `Widgets` component required)
- vcpkg (for Assimp, fmt, and other third-party dependencies)

### Required Proprietary SDKs (Not tracked on GitHub)
Due to licensing agreements, the following SDKs are not included in the public repository. You must acquire them directly from the vendors and place them in the `thirdparty/` directory:

- `thirdparty/archicad-sdk`: Archicad API Development Kit (Required to build the Archicad 28 LiveLink plugin).
- `thirdparty/sketchup_sdk`: SketchUp C API SDK (Required to build any SketchUp import integration).
- `thirdparty/max2024-sdk` / `thirdparty/max2025-sdk`: 3ds Max SDKs (Required for 3ds Max 2024 and 2025 plugins).
- `thirdparty/vray-sdk`: V-Ray AppSDK or headers (Required if building specific V-Ray material conversion paths within the plugins).

*(Smaller open-source dependencies like `NRD`, `NRI`, `oidn`, and `Streamline` are included directly in the repo or fetched during build).*

---

## 🛠 Build Process

### 1. Main Application
```powershell
# Configure with vcpkg toolchain and Qt6 enabled
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake" \
      -DUSE_QT_UI=ON

# Build the Release target
cmake --build build --config Release -j 8
```

### 2. Plugins (Archicad 28 / 3ds Max 2024 / 3ds Max 2025)
The LiveLink plugins are built from their respective sub-projects in the `tools/` directory.

**Archicad 28 LiveLink:**
1. Ensure the `archicad-sdk` is dumped into `thirdparty/archicad-sdk`.
2. Configure and build:
```powershell
cmake -S tools/archicad28 -B build-archicad28-v142 -A x64
cmake --build build-archicad28-v142 --config Release
```

**3ds Max 2024 / 2025 LiveLink:**
1. Ensure your Max SDKs (and V-Ray headers if required) are available in `thirdparty/max2024-sdk` etc.
2. Build via their respective tool folders:
```powershell
cmake -S tools/3dsmax2024 -B build-max2024 -A x64
cmake --build build-max2024 --config Release

cmake -S tools/3dsmax2025 -B build-max2025 -A x64
cmake --build build-max2025 --config Release
```
*(Check `tools/3dsmax2025/README.md` or `tools/archicad28/README.md` for specific plugin install details if needed.)*

---

## 🛠 Advanced Configuration

### 3ds Max LiveLink
- Engine startup: `./build/Release/bin/project-render.exe --max-livelink-pipe`
- Live sync incremental updates for nodes, visibility, meshes, materials, and selection.

### DLSS / DLSS Ray Reconstruction
- **AppID**: DLSS requires an NVIDIA AppID (`SL_APPLICATION_ID` environment variable or `sl_appid.txt` in the root).

### Troubleshooting
- **Grey Viewport**: Ensure a sky model is selected or an HDRI is loaded in the Environment panel.
- **Deleting Objects with DXR Enabled**: The renderer forces an acceleration-structure rebuild. If you see crashes, verify build geometry flags.

---

*This project is a high-performance rendering scaffold evolved into a feature-rich ArchViz tool in collaboration with an AI coding assistant.*# project-render 0.1.6  High-End ArchViz Real-Time Engine

project-render is a state-of-the-art real-time rendering engine designed for high-fidelity Architectural Visualization (ArchViz). It leverages DirectX 12, DXR Ray Tracing, and NVIDIA Streamline to deliver physically accurate lighting and cinematic results.

---

##  Key Features

###  Advanced Path Tracing
- **Unified DXR Path Tracer**: Progressive path tracing with high-precision accumulation (R32G32B32A32_FLOAT).
- **Multiple Importance Sampling (MIS)**: Power Heuristic-based MIS combining BRDF and Light sampling to eliminate fireflies on glossy surfaces.
- **ReSTIR DI & GI**: Reservoir-based Spatio-Temporal Importance Resampling.
- **Efficiency Optimizations**: Russian Roulette, 64-Byte Material Struct, Opaque Hardware Fast-Path.

###  Intelligent Denoising Stack
- **NVIDIA DLSS Ray Reconstruction (DLSS-RR)** & **NVIDIA NRD (ReLAX)**.
- **SVGF** & **Intel Open Image Denoise (OIDN) 2.x**.

###  Asset & ArchViz System
- **Universal Model Import**: Robust support for **glTF 2.0**, FBX, OBJ, and STL.
- **OpenPBR-Oriented Material Runtime**: Clearcoat, transmission, and tri-planar mapping.
- **ArchViz Material Editor**: Single scene material editor combining specular/metallic workflows. 
- **Physical Camera & Lighting**: IES Light Profiles, Prague Sky Model, Physical Exposure.

###  Editor & Workflow
- **Qt UI Integration**: A fully event-driven, hardware-accelerated Qt6 UI replacing polling loops and greatly improving large scene material edits.
- **Saved Views & Camera Sequencing**: Keyframed animation paths with per-segment easing & MP4 out.
- **3ds Max 2024/2025 LiveLink**: Named-pipe live sync for nodes, .prmesh payloads, shared materials, and fast geometry streaming.
- **Archicad 28 LiveLink**: Pipe-based scene sync with stable material identity across re-syncs.

---

##  Quick Start (Windows)

### Prerequisites
- Windows 10/11
- NVIDIA RTX GPU (DXR 1.1 + Ray Reconstruction support recommended)
- Visual Studio 2022
- CMake 3.20+
- Qt 6 (Optional but recommended for full editor feature set, Widgets component required)
- vcpkg (for Assimp, fmt, and other third-party dependencies)

### Required Proprietary SDKs (Not tracked on GitHub)
Due to licensing agreements, the following SDKs are not included in the public repository. You must acquire them directly from the vendors and place them in the 	hirdparty/ directory:

- 	hirdparty/archicad-sdk: Archicad API Development Kit (Required to build the Archicad 28 LiveLink plugin).
- 	hirdparty/sketchup_sdk: SketchUp C API SDK (Required to build any SketchUp import integration).
- 	hirdparty/max2024-sdk / max2025-sdk: 3ds Max SDKs (Required for 3ds Max 2024 and 2025 plugins).
- 	hirdparty/vray-sdk: V-Ray AppSDK or headers (Required if building specific V-Ray material conversion paths within the plugins).

*(Smaller open-source dependencies like NRD, NRI, oidn, and Streamline are included directly in the repo or fetched during build).*

---

##  Build Process

### 1. Main Application
`powershell
# Configure with vcpkg toolchain and Qt6 enabled
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake" \
      -DUSE_QT_UI=ON

# Build the Release target
cmake --build build --config Release -j 8
`

### 2. Plugins (Archicad 28 / 3ds Max 2024 / 3ds Max 2025)
The LiveLink plugins are built from their respective sub-projects in the 	ools/ directory.

**Archicad 28 LiveLink:**
1. Ensure the rchicad-sdk is dumped into 	hirdparty/archicad-sdk.
2. Configure and build:
`powershell
cmake -S tools/archicad28 -B build-archicad28-v142 -A x64
cmake --build build-archicad28-v142 --config Release
`

**3ds Max 2024 / 2025 LiveLink:**
1. Ensure your Max SDKs (and V-Ray headers if required) are available.
2. Build via their respective tool folders:
`powershell
cmake -S tools/3dsmax2024 -B build-max2024 -A x64
cmake --build build-max2024 --config Release

cmake -S tools/3dsmax2025 -B build-max2025 -A x64
cmake --build build-max2025 --config Release
`
*(Check 	ools/3dsmax2025/README.md or 	ools/archicad28/README.md for specific plugin install details.)*

---

##  Advanced Configuration

### 3ds Max LiveLink
- Engine startup: \./build/Release/bin/project-render.exe --max-livelink-pipe\
- Live sync incremental updates for nodes, visibility, meshes, materials, and selection.

### DLSS / DLSS Ray Reconstruction
- **AppID**: DLSS requires an NVIDIA AppID (\SL_APPLICATION_ID\ environment variable or \sl_appid.txt\ in the root).

### Troubleshooting
- **Grey Viewport**: Ensure a sky model is selected or an HDRI is loaded in the Environment panel.
- **Deleting Objects with DXR Enabled**: The renderer forces an acceleration-structure rebuild. If you see crashes, verify build geometry flags.

---

*This project is a high-performance rendering scaffold evolved into a feature-rich ArchViz tool in collaboration with an AI coding assistant.*
