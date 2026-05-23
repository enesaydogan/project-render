# Tile-Based Panorama Export Plan

## Goal

Render 4096x2048 spherical panorama exports on 6 GB VRAM GPUs without lowering final image quality. The current export path treats the panorama as one full-size DXR frame, so all render targets, accumulation buffers, ReSTIR reservoirs, and wavefront queues are allocated at 4096x2048 at the same time. That is too expensive for low-VRAM cards.

The fix is to render the panorama in smaller tiles or horizontal stripes, compose the full image in CPU memory, then denoise and save the completed panorama.

## Current Problem

4096x2048 is 8.39 million pixels. At that size, the renderer currently allocates several full-resolution GPU resources at once:

- Wavefront queues: roughly 1.5 GB in the current compact profile.
- ReSTIR DI/GI reservoirs: roughly 1.0 GB for 2 DI and 6 GI full-resolution RGBA32F textures.
- Beauty and transmission accumulation: roughly 320 MB.
- RT output, depth, motion vectors, AOVs, denoiser guides, and tonemap targets: hundreds of MB more.
- Export preview target: small by comparison, but still another full-size target.

Texture compression and smaller queue profiles help, but they do not change the structural issue: the export path is allocating the whole panorama as if it were an interactive viewport render.

There is also a correctness risk with the current queue caps. A 4096x2048 panorama has more pixels than the current max path queue capacity. Raising the cap would increase VRAM pressure, so the right solution is not simply larger queues.

## Design

Use a tiled panorama export path only for spherical 360 exports at large resolutions. Perspective exports can stay on the existing single-frame path for the first implementation.

The key separation:

- Full panorama size: the final image size, for example 4096x2048.
- Tile render size: the active GPU render size, for example 4096x256 or 4096x512.
- Tile offset: the location of the active tile inside the full panorama.

The shader must compute spherical UVs from full-image pixel coordinates, not local tile coordinates:

```text
fullPixel = tileOffset + localPixel
uv = (fullPixel + jitter) / fullPanoramaSize
```

That keeps projection math consistent across tile boundaries.

## Denoiser Strategy

Do not denoise each tile independently in the first implementation. Per-tile denoising can create visible seams because the denoiser lacks neighboring context at tile edges.

The first implementation should use:

1. Render all tiles.
2. Copy each tile's linear HDR beauty output to a CPU full-frame buffer.
3. Copy albedo and normal guide tiles to CPU full-frame guide buffers if the guides are valid.
4. Run CPU OIDN once on the completed full panorama.
5. Tonemap the denoised full panorama and write PNG.

This is slower than GPU denoising but far safer for seams and much better for 6 GB GPUs.

Later, a fast GPU denoiser path can be added with guard bands. That path should render each tile with overlap padding, denoise the padded tile, then copy only the inner tile into the final image.

## Implementation Stages

### Stage 1: Tile State

Extend the render export job state with panorama tiling data:

- Full panorama width and height.
- Tile width and height.
- Current tile index.
- Tile x and y offset.
- Tile count.
- CPU full-frame beauty buffer.
- Optional CPU full-frame albedo guide buffer.
- Optional CPU full-frame normal guide buffer.

Only enable this path when the export projection is `Spherical360` and the target resolution is large enough to benefit from tiling.

### Stage 2: Shader Coordinates

Add export tile constants to the camera or a small DXR constant block:

- `exportFullWidth`
- `exportFullHeight`
- `exportTileOffsetX`
- `exportTileOffsetY`

Update `wavefront_bootstrap_cs.hlsl` so primary ray generation uses full panorama coordinates when tiled export is active.

The local dispatch still writes local tile pixels into tile-sized GPU resources. Only projection UV generation uses full-frame coordinates.

### Stage 3: Tile Render Loop

For each tile:

1. Recreate the DXR pipeline at tile size.
2. Reset accumulation.
3. Render until max SPP or noise stop.
4. Copy tile outputs from GPU to CPU staging buffers.
5. Composite the tile into the full CPU panorama buffer.
6. Advance to the next tile.

The first pass can use horizontal stripes. Good starting values:

- 4096x256 for safest VRAM.
- 4096x512 if 256 is too slow and memory is available.

### Stage 4: CPU Full-Frame Denoise

After all tiles are complete:

1. If denoiser is off, tonemap the CPU beauty buffer directly.
2. If OIDN is selected, run CPU OIDN on the full panorama buffer.
3. Use albedo and normal guides when they are correct and stable.
4. Write the final PNG from the CPU result.

If guide buffers are not trustworthy in the first pass, ship beauty-only OIDN first and add guided denoise afterward.

### Stage 5: UI and Progress

Keep the first UI simple:

- Automatically use tiled panorama export for large spherical exports.
- Show progress as `Tile X/Y, SPP A/B`.
- Keep existing resolution, projection, SPP, and denoiser controls.

An advanced tile size setting can be added later if needed.

## Verification

Required checks:

- 1024x512 spherical tiled export matches the single-frame spherical export closely.
- 4096x2048 spherical export completes on a 6 GB GPU.
- Denoiser off export has no missing tile rows or projection discontinuities.
- CPU OIDN export has no visible seams at tile boundaries.
- Release build succeeds.

Useful stress tests:

- Bright sun and high-contrast edges across tile boundaries.
- Interior scene with glossy/specular materials.
- Glass-heavy scene.
- High texture memory scene.

## Notes

This plan preserves the renderer architecture. Traversal, resolve, shadow visibility, ReSTIR, and accumulation still own their existing stages. The change is export orchestration and projection coordinate mapping, not a shortcut inside the wavefront stages.

The goal is to make high-resolution panorama export memory-streamed, similar to how production renderers handle large offline outputs, instead of forcing the interactive renderer to hold the whole panorama in VRAM at once.
