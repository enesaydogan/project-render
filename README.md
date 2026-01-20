project-render — ArchViz real-time engine (starter)

Quick start (Windows):

1) Create a `build` directory and configure with CMake (Visual Studio generator):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

2) Or use the existing `build.ps1` if present.

Optional: enable tinygltf via vcpkg
- Install `vcpkg` and the `tinygltf` port (x64):

```powershell
.
# From the vcpkg repo root (example):
./vcpkg.exe install tinygltf:x64-windows
```

- Configure CMake to use the vcpkg toolchain and enable `USE_TINYGLTF`:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DUSE_TINYGLTF=ON
cmake --build build --config Release
```

Next steps:
- Implement full DirectX 12 resource management and frame rendering.
- Add render graph, DXR, and DLSS integration.

This repo was initialized by the assistant as a starting scaffold.