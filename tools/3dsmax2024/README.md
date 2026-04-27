# 3ds Max 2025 LiveLink

This folder contains the 3ds Max 2025 LiveLink project for project-render.

Current scope:

- builds a 3ds Max 2025 utility plugin (`.dlu`)
- connects to the renderer over a Windows named pipe
- sends a `SessionOpened` delta
- sends a `FullSceneSync` marker
- sends an initial snapshot of scene nodes as:
  - `NodeAdded`
  - `NodeTransformChanged`
  - `NodeVisibilityChanged`
- exports geometry nodes to native `.prmesh` payloads and sends `MeshPayloadChanged`
- exports material state and sends `MaterialChanged`
- exports camera and supported light state
- polls the scene while the utility is active and sends incremental:
  - `NodeAdded`
  - `NodeRemoved`
  - `NodeTransformChanged`
  - `NodeVisibilityChanged`
  - `SelectionChanged`
  - `MeshPayloadChanged` when detected geometry changes require a fresh payload
  - `MaterialChanged` when supported material state changes
- sends `SessionClosed` when the utility is closed

Material behavior:

- `.prmesh` payloads now carry stable material IDs
- shared scene materials are reused by stable ID instead of spawning duplicates per node slot
- Multi/Sub slots stay bound to the correct faces while still resolving to one logical scene material when the DCC identity is shared
- editing one shared material in the engine updates every surface bound to that Max material

What it does not do yet:

- UI controls inside the utility panel
- robust topology/material dependency tracking beyond the current mesh fingerprint pass
- advanced dependency-driven partial export beyond the current polling and fingerprint pipeline

## Engine side

Run the renderer with the named-pipe provider enabled:

```powershell
./build/Release/bin/project-render.exe --max-livelink-pipe
```

Optional custom pipe name:

```powershell
./build/Release/bin/project-render.exe --max-livelink-pipe my-custom-pipe
```

The default pipe name is `project-render-max-livelink`.

## Build the plugin

Set your 3ds Max 2024 SDK root and configure the standalone project:

```powershell
$env:ADSK_3DSMAX_2024_SDK = "C:/Path/To/3dsMaxSDK"
cmake -S tools/3dsmax2024 -B build-max2024 -G "Visual Studio 17 2022" -A x64
cmake --build build-max2024 --config Release
```

If your SDK uses a different lib layout, pass these explicitly:

```powershell
cmake -S tools/3dsmax2024 -B build-max2024 -G "Visual Studio 17 2022" -A x64 \
  -DMAX_SDK_INCLUDE_DIR="C:/Path/To/3dsMaxSDK/include" \
  -DMAX_SDK_LIB_DIR="C:/Path/To/3dsMaxSDK/lib/x64/Release"
```

The output plugin is:

- `build-max2024/Release/ProjectRenderLiveLink.dlu`

## Load in 3ds Max

Copy the `.dlu` into a plugin search path or load it manually from 3ds Max.

Then:

1. start `project-render` with `--max-livelink-pipe`
2. open the utility in 3ds Max
3. the plugin will send an initial scene snapshot into the running renderer
4. keep the utility open while editing if you want incremental sync to continue

Geometry notes:

- renderable Max geometry is currently exported as temporary `.prmesh` payloads under the system temp directory
- node-only deltas create scene nodes in the engine, but visible meshes depend on `MeshPayloadChanged`
- material identity is preserved across resyncs so the engine can collapse repeated Max materials into shared scene materials

## Wire format

Transport is line-delimited JSON over a Windows named pipe.

Each line is one `SceneDeltaBatch` object with fields:

- `providerName`
- `sessionId`
- `sequence`
- `fullSync`
- `deltas`

This is intentionally simple so the Max-side exporter can evolve before a more rigid protocol is frozen.
