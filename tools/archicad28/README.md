# Archicad 28 LiveLink

This folder contains a standalone Archicad 28 add-on project for project-render.

Current scope:

- builds an Archicad 28 add-on (`.apx`)
- connects to the renderer over a Windows named pipe
- registers a `project-render LiveLink` menu with:
  - `Start Session`
  - `Sync Scene Now`
  - `Stop Session`
- sends protocol-compatible `SessionOpened`, `FullSceneSync`, `NodeAdded`, `NodeTransformChanged`, `NodeVisibilityChanged`, `MeshPayloadChanged`, `MaterialChanged`, `CameraChanged`, and `SessionClosed` batches
- exports visible Archicad 3D elements into `.prmesh` payloads before sending them to the renderer
- converts Archicad mesh coordinates into the renderer's Y-up space and exports per-vertex UVs
- reuses stable Archicad surface identity so repeated surfaces bind back to one logical scene material in the renderer
- emits merged material references instead of duplicating material entries per element
- includes Archicad project metadata in the opened session payload

Material behavior:

- Archicad surface GUIDs are propagated as stable material IDs in `.prmesh` payload version 4
- material deltas merge references for all elements using the same surface
- engine-side edits apply to the shared scene material instead of fighting duplicated material rows

What it does not do yet:

- light export
- UI panel or pipe-name preferences inside Archicad

## Engine side

The Archicad add-on defaults to the pipe name `project-render-archicad-livelink`.

Run the renderer with the named-pipe provider enabled and pass the same pipe name:

```powershell
./build/Release/bin/project-render.exe --archicad-livelink-pipe project-render-archicad-livelink
```

## Build the add-on

The project uses the Archicad 28 API Development Kit already present under `thirdparty/archicad-sdk` by default.

If you want to point at a different SDK location:

```powershell
$env:ARCHICAD_28_SDK = "C:/Path/To/Archicad28APIDevKit"
```

Configure and build:

```powershell
cmake -S tools/archicad28 -B build-archicad28 -G "Visual Studio 17 2022" -A x64 -T v142
cmake --build build-archicad28 --config Release
```

To copy the add-on automatically after each build, set `AC_ADDON_DEPLOY_DIR` at configure time:

```powershell
cmake -S tools/archicad28 -B build-archicad28 -G "Visual Studio 17 2022" -A x64 -T v142 `
  -DAC_ADDON_DEPLOY_DIR="C:/Program Files/Graphisoft/Archicad 28/Eklentiler/Import-Export"
cmake --build build-archicad28 --config Release
```

This uses a CMake post-build copy via `cmake -E copy_if_different`.

Archicad 28's SDK requires the MSVC `v142` toolset. If CMake reports that `v142` is unavailable, install the Visual Studio 2019 C++ toolset from Visual Studio Installer and configure again.

If `AC_ADDON_DEPLOY_DIR` points under `Program Files`, the build must run with permission to write there. A normal non-elevated shell will still fail at the copy step.

The output add-on is:

- `build-archicad28/Release/ProjectRenderArchicad28LiveLink.apx`

## Load in Archicad

1. start `project-render` with `--archicad-livelink-pipe project-render-archicad-livelink`
2. load `ProjectRenderArchicad28LiveLink.apx` in Archicad's Add-On Manager
3. open the `project-render LiveLink` menu
4. use `Start Session` to open a session and export the current visible 3D scene
5. use `Sync Scene Now` whenever you want to resend the current scene state
6. use `Stop Session` before unloading the add-on if you want a graceful session close

## Notes

- the wire format matches the existing line-delimited JSON `SceneDeltaBatch` protocol used by the 3ds Max LiveLink project
- the current exporter uses Archicad ModelAccess with a temporary sight to build per-element tessellated mesh payloads
- the current implementation supports scene export plus material-aware dirty resync work, but it is not yet a full automatic change-notification pipeline for every Archicad edit
- a symlink is usually not better here on Windows: it still requires write access in the Archicad add-on folder once, and file symlinks typically require admin rights or Developer Mode; hard links are not an option across `D:` and `C:`
