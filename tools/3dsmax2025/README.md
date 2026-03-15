# 3ds Max 2025 LiveLink Bootstrap

This folder contains the first real 3ds Max-side LiveLink project for project-render.

Current scope:

- builds a 3ds Max 2025 utility plugin (`.dlu`)
- connects to the renderer over a Windows named pipe
- sends a `SessionOpened` delta
- sends a `FullSceneSync` marker
- sends an initial snapshot of scene nodes as:
  - `NodeAdded`
  - `NodeTransformChanged`
  - `NodeVisibilityChanged`
- sends `SessionClosed` when the utility is closed

What it does not do yet:

- live incremental change tracking from Max notifications
- camera, light, material, or mesh export from Max
- UI controls inside the utility panel
- reconnect or resend logic beyond the first snapshot path

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

Set your 3ds Max 2025 SDK root and configure the standalone project:

```powershell
$env:ADSK_3DSMAX_2025_SDK = "C:/Path/To/3dsMaxSDK"
cmake -S tools/3dsmax2025 -B build-max2025 -G "Visual Studio 17 2022" -A x64
cmake --build build-max2025 --config Release
```

If your SDK uses a different lib layout, pass these explicitly:

```powershell
cmake -S tools/3dsmax2025 -B build-max2025 -G "Visual Studio 17 2022" -A x64 \
  -DMAX_SDK_INCLUDE_DIR="C:/Path/To/3dsMaxSDK/include" \
  -DMAX_SDK_LIB_DIR="C:/Path/To/3dsMaxSDK/lib/x64/Release"
```

The output plugin is:

- `build-max2025/Release/ProjectRenderLiveLink.dlu`

## Load in 3ds Max

Copy the `.dlu` into a plugin search path or load it manually from 3ds Max.

Then:

1. start `project-render` with `--max-livelink-pipe`
2. open the utility in 3ds Max
3. the plugin will send one initial scene snapshot into the running renderer

## Wire format

Transport is line-delimited JSON over a Windows named pipe.

Each line is one `SceneDeltaBatch` object with fields:

- `providerName`
- `sessionId`
- `sequence`
- `fullSync`
- `deltas`

This is intentionally simple so the Max-side exporter can evolve before a more rigid protocol is frozen.