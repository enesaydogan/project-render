# Archicad 28 LiveLink

This folder contains a standalone Archicad 28 add-on project for project-render.

Current scope:

- builds an Archicad 28 add-on (`.apx`)
- connects to the renderer over a Windows named pipe
- registers a `project-render LiveLink` menu with:
  - `Start`
  - `Start Full Sync`
  - `Stop`
- sends protocol-compatible `SessionOpened`, `FullSceneSync`, and `SessionClosed` batches
- includes Archicad project metadata in the opened session payload

What it does not do yet:

- element, mesh, material, camera, or light export from Archicad
- incremental scene monitoring
- UI panel or pipe-name preferences inside Archicad

## Engine side

The Archicad add-on defaults to the pipe name `project-render-archicad-livelink`.

Run the renderer with the named-pipe provider enabled and pass the same pipe name:

```powershell
./build/Release/bin/project-render.exe --max-livelink-pipe project-render-archicad-livelink
```

The renderer flag is still named `--max-livelink-pipe` because the engine-side named-pipe provider is currently shared.

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

Archicad 28's SDK requires the MSVC `v142` toolset. If CMake reports that `v142` is unavailable, install the Visual Studio 2019 C++ toolset from Visual Studio Installer and configure again.

The output add-on is:

- `build-archicad28/Release/ProjectRenderArchicad28LiveLink.apx`

## Load in Archicad

1. start `project-render` with `--max-livelink-pipe project-render-archicad-livelink`
2. load `ProjectRenderArchicad28LiveLink.apx` in Archicad's Add-On Manager
3. open the `project-render LiveLink` menu
4. use `Start Full Sync` to open a session and send the initial full-sync marker
5. use `Stop` before unloading the add-on if you want a graceful session close

## Notes

- the wire format matches the existing line-delimited JSON `SceneDeltaBatch` protocol used by the 3ds Max LiveLink project
- this scaffold is intended to be the Archicad-side foundation before element export and incremental notifications are added