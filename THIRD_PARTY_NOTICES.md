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
- When shipping Qt LGPL builds, keep the deployment model compatible with LGPL
  obligations, including dynamic linking and user-replaceable Qt libraries.
- When shipping optional proprietary integrations, verify redistribution rights
  for every SDK/runtime included in the installer.
