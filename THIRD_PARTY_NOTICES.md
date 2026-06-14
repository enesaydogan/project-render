# Third-Party Notices

Project Render's own source code is intended to be licensed under the MIT
License. Third-party libraries, SDKs, tools, runtimes, plugins, headers, and
binary redistributables remain under their own licenses and vendor terms.

This file is a practical notice list for the dependencies used by Project
Render. It is not a replacement for the full license texts shipped by each
project or vendor.

## Open-Source / Redistributable Components

### Qt 6

- Used for the optional Qt editor UI.
- Licensed by The Qt Company under commercial and open-source terms, including
  LGPL/GPL options depending on the module and distribution model.
- Project Render uses Qt dynamically when Qt UI is enabled.
- Keep Qt license texts and deployment obligations with redistributed builds.

### Intel Open Image Denoise

- Used for final-frame and export denoising.
- Licensed under the Apache License 2.0.
- Local license text: `thirdparty/oidn/doc/LICENSE.txt`

### Assimp

- Used for model import.
- Licensed under the BSD 3-Clause License.
- Assimp is fetched/built through the CMake dependency flow.

### Dear ImGui

- Used for the legacy/editor UI layer.
- Licensed under the MIT License.

### ImGuizmo

- Used for viewport transform gizmos.
- Licensed under the MIT License.

### miniz

- Used for compression/runtime packaging support.
- Licensed under permissive open-source terms; retain the upstream notice when
  redistributing.

### OpenVDB And VDB Runtime Dependencies

- OpenVDB 12.0.1 is used to inspect and import `.vdb` sparse volume grids.
- OpenVDB is licensed under the Apache License 2.0.
- The Windows build resolves OpenVDB through the repository's vcpkg tree and
  ships the runtime libraries required by the selected vcpkg build.
- The installed OpenVDB dependency stack currently includes:
  - Blosc 1.21.6, BSD license.
  - Imath 3.2.2 and OpenEXR 3.4.11, BSD 3-Clause licenses.
  - oneTBB 2022.3.0, Apache License 2.0.
  - LZ4 1.10.0, BSD 2-Clause license.
  - Zstandard 1.5.7, distributed under its BSD or GPLv2 options; Project Render
    uses the redistributable library under the BSD terms.
  - Snappy 1.2.2, BSD 3-Clause license.
  - zlib 1.3.2, zlib license.
  - Boost.Iostreams 1.91.0 and its Boost dependencies, Boost Software License
    1.0.
- Exact license texts for the installed packages are stored under:
  - `repos/vcpkg/installed/x64-windows/share/openvdb/copyright`
  - `repos/vcpkg/installed/x64-windows/share/blosc/copyright`
  - `repos/vcpkg/installed/x64-windows/share/imath/copyright`
  - `repos/vcpkg/installed/x64-windows/share/openexr/copyright`
  - `repos/vcpkg/installed/x64-windows/share/tbb/copyright`
  - `repos/vcpkg/installed/x64-windows/share/lz4/copyright`
  - `repos/vcpkg/installed/x64-windows/share/zstd/copyright`
  - `repos/vcpkg/installed/x64-windows/share/snappy/copyright`
  - `repos/vcpkg/installed/x64-windows/share/zlib/copyright`
  - `repos/vcpkg/installed/x64-windows/share/boost-iostreams/copyright`
- Release installers include these texts under `licenses/`.

### nlohmann/json

- Used through NVIDIA Streamline's external dependency tree.
- Licensed under the MIT License.
- Local license text: `thirdparty/Streamline/external/json/LICENSE.MIT`

### ShaderMake

- Used for shader build tooling.
- Local license text: `thirdparty/ShaderMake/LICENSE.txt`

### MathLib

- Used as a third-party utility library.
- Local license text: `thirdparty/MathLib/LICENSE.txt`

## NVIDIA Components

### NVIDIA Streamline, DLSS, NGX, NIS, Reflex

- Used for NVIDIA Streamline integration and DLSS/DLSS Ray Reconstruction
  workflows.
- Streamline source and SDK files are governed by NVIDIA's Streamline license.
- DLSS/NGX/NIS/Reflex binaries and related files are governed by NVIDIA
  redistributable/runtime terms.
- Local runtime license files include:
  - `thirdparty/Streamline/bin/x64/nvngx_dlss.license.txt`
  - `thirdparty/Streamline/bin/x64/nis.license.txt`
  - `thirdparty/Streamline/bin/x64/reflex.license.txt`
  - matching development variants under `thirdparty/Streamline/bin/x64/development/`
- Only ship NVIDIA runtime binaries that are allowed for redistribution by the
  applicable NVIDIA terms.

### NVIDIA OptiX

- Used optionally for final-frame denoising when `USE_OPTIX_DENOISER=ON`.
- The OptiX SDK is not Project Render code and remains governed by NVIDIA's
  OptiX SDK license.
- Do not redistribute OptiX SDK files unless the applicable NVIDIA license
  permits it.

## Proprietary Vendor SDKs

These SDKs are optional integration dependencies and are not relicensed by
Project Render's MIT license.

### SketchUp SDK

- Used optionally for SketchUp import support.
- Governed by Trimble/SketchUp SDK terms.
- Do not redistribute SDK files or runtime binaries unless the SDK license
  permits it.

### Autodesk 3ds Max SDK

- Used for the optional 3ds Max LiveLink plugins.
- Governed by Autodesk's SDK terms.
- Do not redistribute SDK files unless the Autodesk license permits it.

### Graphisoft Archicad API DevKit

- Used for the optional Archicad 28 LiveLink add-on.
- Governed by Graphisoft's Archicad API DevKit terms.
- Do not redistribute SDK files unless the Graphisoft license permits it.

### Chaos V-Ray SDK

- Used only for optional V-Ray material conversion paths when available.
- Governed by Chaos/V-Ray SDK terms.
- Do not redistribute SDK files unless the Chaos license permits it.

## Notes for Distributors

- The MIT License for Project Render does not grant rights to third-party
  trademarks, SDKs, runtimes, model import libraries, denoisers, GPU SDKs, or
  plugins beyond the rights granted by their respective owners.
- When shipping binaries, include all required third-party license texts and
  notices.
- Keep the OpenVDB and compression/runtime license files included when
  redistributing builds with `USE_OPENVDB=ON`.
- When shipping Qt LGPL builds, keep the deployment model compatible with LGPL
  obligations, including dynamic linking and user-replaceable Qt libraries.
- When shipping optional proprietary integrations, verify redistribution rights
  for every SDK/runtime included in the installer.
