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

DLSS / DLSS Ray Reconstruction (Streamline)

- This project integrates NVIDIA Streamline (DLSS-SR + DLSS-RR) for the DXR render path.
- DLSS features use NGX and require an NVIDIA-provided application id.
	- Set environment variable `SL_APPLICATION_ID` (or `PROJECT_RENDER_SL_APPLICATION_ID`) before launching, or
	- Create `sl_appid.txt` next to the executable containing the integer application id.

Dev NGX DLLs (no AppID whitelist)

- In Debug builds, CMake copies Streamline binaries from `thirdparty/Streamline/bin/x64/development`.
- These "development" NGX DLLs typically bypass the AppID whitelist check but may show a DLSS watermark overlay.

Streamline logging

- By default we do NOT copy `sl.interposer.json` next to the executable, so Streamline logging is controlled by the app (see Render Mode panel) and written under `sl_logs`.
- If you manually place `sl.interposer.json` next to the executable, it will override logging settings (and can re-enable the Streamline console).

This repo was initialized by the assistant as a starting scaffold.