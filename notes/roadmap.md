# project-render Roadmap

This roadmap is ordered by practical impact for an ArchViz-focused real-time path
tracer. The engine already has a strong rendering core: DX12/DXR, ReSTIR DI/GI,
MIS, DLSS-RR, NRD/SVGF/OIDN, Prague sky, IES lights, OpenPBR-oriented materials,
LiveLink, animation export, batch render, and texture streaming. The next steps
should therefore focus on production scalability, output quality, DCC fidelity,
and workflow polish rather than adding isolated demo features.

## Priority 1: Scalable Light Sampling

### Feature
Add a Light Tree or ReGIR-style spatial light sampler for ReSTIR and next-event
estimation.

### Why this comes first
The current path tracer still samples local lights from a flat array in
`shaders/path_tracer_core.hlsl`. That works for a few lights, but it becomes
noisy and slow in real interiors with hundreds or thousands of spot, area, IES,
and emissive fixtures. Better light sampling improves convergence immediately
and makes every denoiser downstream behave better.

### Implementation slices
- Add a CPU-side light bounds/build step for local lights.
- Start with a simple light tree over bounding spheres or AABBs.
- Add GPU buffer upload for tree nodes.
- Replace uniform light selection with tree-guided candidate selection.
- Feed the guided candidate into existing ReSTIR reservoirs.
- Add debug views for chosen light index, candidate pdf, and contribution.
- Later, evaluate ReGIR if dynamic light counts or very large scenes need it.

### Acceptance checks
- A 1000-light interior should converge visibly faster than flat sampling.
- Near-field lights should be selected quickly without waiting many frames.
- ReSTIR temporal/spatial reuse should remain stable during camera movement.
- The old flat sampler should remain available as a debug fallback.

## Priority 2: Production EXR and AOV Export

### Feature
Add production render output beyond tonemapped PNG: multilayer or sidecar EXR
for beauty and render passes.

### Why this comes second
ArchViz users need post-production control. PNG export is useful for previews,
but final images need linear HDR data, masks, depth, and utility passes. This is
also a good internal debugging tool because it exposes what the renderer is
actually producing before tonemapping and denoising.

### Implementation slices
- Add EXR export for linear HDR beauty.
- Add optional sidecar AOVs first: albedo, normal, depth, motion vectors.
- Add lighting AOVs: direct diffuse, indirect diffuse, direct specular,
  indirect specular, emission.
- Add material/object ID output.
- Add a render export preset in Qt and ImGui.
- Later, support multilayer EXR and Cryptomatte-style masks.

### Acceptance checks
- Final beauty EXR should match the viewport before display transform.
- AOV dimensions should match output resolution, including DLSS-disabled final
  export paths.
- Depth and motion vectors should be documented and consistent.
- Batch render and animation export should support the new output modes.

## Priority 3: Decals, Material Layers, and Trim Sheets

### Feature
Support non-destructive surface detailing: decals, dirt masks, tile/grout
overlays, labels, edge wear, trim sheets, and layered material blends.

### Why this comes third
This has huge ArchViz value. Real interiors are not made only of clean base
materials. Decals and layers let users add detail without exploding mesh counts
or creating many near-duplicate materials.

### Implementation slices
- Add a decal component type with transform, projection bounds, texture slots,
  opacity, blend mode, and material target mask.
- Raster preview: project decals in the material/lighting pass.
- DXR path: gather decals at hit points using a compact decal list/grid.
- Add material-layer support for dirt/moss/wear masks.
- Add trim-sheet controls in the material editor.
- Add save/load and LiveLink-safe identifiers.

### Acceptance checks
- Decals should render consistently in raster and DXR modes.
- Decals should not require modifying source mesh UVs.
- Layered materials should preserve the 64-byte hot material payload by using
  side buffers or indirection.
- Editing decals should reset accumulation without forcing unnecessary BLAS
  rebuilds.

## Priority 4: LiveLink Fidelity

### Feature
Improve DCC sync fidelity: exact hierarchy reconstruction, slot-reorder-safe
material identity, and broader 3ds Max material graph translation.

### Why this comes fourth
The LiveLink foundation is already strong. The remaining gaps are the things
users notice when they compare against their DCC scene: wrong hierarchy, changed
material bindings after slot edits, and simplified proprietary materials.

### Implementation slices
- Preserve DCC parent-child hierarchy instead of only using a synthetic top-level
  Live Sync group.
- Store parent external IDs and rebuild scene node parenting on resume.
- Add material slot stable IDs that survive slot reordering.
- Translate more 3ds Max material graphs into the engine material IR.
- Report unsupported nodes/properties explicitly in diagnostics.
- Add LiveLink regression scenes for rename, reparent, reorder, delete, and
  reconnect/resume behavior.

### Acceptance checks
- Reopening a Max scene and PRS scene should reconnect without material swaps.
- Reparented objects should preserve transforms and hierarchy in the engine.
- Unsupported material graph features should show clear warnings, not silent
  visual changes.
- Frequent transform/material edits should avoid full renderer rebuilds unless
  topology actually changes.

## Priority 5: Color Management and Display Pipeline

### Feature
Add real color management: OCIO/ACES-style view transforms, display selection,
LUT support, and consistent linear output.

### Why this comes fifth
The renderer has exposure and filmic tonemapping, but production users need
predictable matching between viewport, export, and post-production tools.

### Implementation slices
- Define internal working color space policy.
- Add display/view transform selection.
- Add LUT loading for creative looks.
- Make EXR export explicitly linear and display-independent.
- Keep PNG/JPEG-style outputs display transformed.
- Save color-management settings in scenes and camera views.

### Acceptance checks
- Linear EXR should not contain baked display tonemapping.
- PNG output should match the viewport.
- Saved views and animation keyframes should restore color settings.
- Sky, HDRI, texture import, and emissive values should remain predictable.

## Priority 6: General Volumetrics

### Feature
Add scene volumetrics: height fog, local fog volumes, participating media, and
eventually VDB/NanoVDB import.

### Why this comes sixth
Volumetrics are visually important, especially for sun shafts, atmosphere,
interior haze, and cinematic walkthroughs. It comes after output and material
workflow because it is more expensive and can destabilize denoising if rushed.

### Implementation slices
- Start with analytic height fog in raster and DXR.
- Add local box/sphere fog volumes with density, anisotropy, color, and falloff.
- Add single-scattering direct light integration.
- Add shadowed volumetric contribution from sun and important lights.
- Add temporal reprojection/denoising for volumetric buffers.
- Later, add VDB/NanoVDB import for authored volumes.

### Acceptance checks
- Height fog should be stable during camera movement.
- Local volumes should interact with sun/sky and IES/area lights.
- Volumetric contribution should be controllable in saved views and animation.
- Performance cost should be visible in profiler timing.

## Priority 7: General Instancing, Proxies, and Scatter

### Feature
Add a production scene-scale system for instancing, proxies, scatter, LOD, and
linked assets.

### Why this comes seventh
Large ArchViz scenes often contain repeated furniture, vegetation, facade
modules, cars, fixtures, and entourage. The engine needs a memory-conscious way
to handle those without duplicating geometry or making scene editing painful.

### Implementation slices
- Add true mesh instancing with per-instance transforms/material overrides.
- Add proxy asset references that load on demand.
- Add scatter authoring for surfaces and volumes.
- Add LOD and distance-based material simplification.
- Add scene statistics for unique meshes, instances, triangles, textures, and
  VRAM estimate.

### Acceptance checks
- Repeated assets should share BLAS where possible.
- Scatter should support deterministic seeds and save/load.
- LiveLink should preserve instancing where the DCC exposes it.
- Huge scenes should remain navigable in raster preview.

## Priority 8: SER or Wavefront Path Tracing

### Feature
Add Shader Execution Reordering through NVAPI where available, or restructure
the path tracer toward wavefront queues for better divergence control.

### Why this comes eighth
This can be a meaningful performance feature, but it should follow better light
sampling and scene data layout. SER helps the GPU execute divergent rays more
efficiently; it does not fix poor sampling by itself.

### Implementation slices
- Add hardware/driver capability detection.
- Create a small SER experiment around closest-hit material evaluation.
- Add compile-time and runtime fallbacks.
- Measure against representative scenes: glass, metals, vegetation, many lights.
- If SER coverage is limited, consider wavefront path tracing for selected
  bounces or material classes.

### Acceptance checks
- SER path must produce visually equivalent output.
- Unsupported GPUs must cleanly fall back.
- Performance uplift should be measured in GPU timestamps and rays/second.
- Shader complexity should remain debuggable.

## Priority 9: OpenPBR and Material Interchange Completion

### Feature
Finish the material system as a clean OpenPBR-oriented schema with explicit
interchange boundaries for glTF, future MaterialX, and DCC LiveLink payloads.

### Why this comes ninth
The engine already has many OpenPBR-style fields. The next step is less about
adding sliders and more about making the model strict, documented, testable, and
interchange-safe.

### Implementation slices
- Add material schema versioning and one-way migration for old scenes.
- Make unsupported imported material features explicit.
- Expand glTF KHR material extension coverage where it maps cleanly.
- Define MaterialX import/export service boundaries without forcing full graph
  shader generation at first.
- Keep hot DXR material data compact with optional side buffers.
- Add material validation warnings for impossible albedo, roughness, IOR, and
  transmission combinations.

### Acceptance checks
- Old scenes should migrate deterministically.
- Raster and DXR should agree for the same material inputs.
- Unsupported DCC/glTF properties should be visible in UI/logs.
- Material edits should use the smallest correct renderer invalidation path.

## Priority 10: Regression Scenes and Automated Validation

### Feature
Add a real validation harness: golden scenes, screenshot comparisons, shader
compile checks, save/load round trips, and LiveLink mutation tests.

### Why this is listed last but should start early
This is not glamorous, but it protects every other feature. It can be introduced
incrementally while the higher-priority rendering work is happening.

### Implementation slices
- Add small canonical scenes for glass, metals, IES, sun/sky, emissives, grass,
  decals, volumetrics, and denoisers.
- Add command-line render/export mode for deterministic test frames.
- Add image comparison with tolerance.
- Add scene save/load round-trip tests.
- Add LiveLink mock-provider tests for add, remove, reparent, material update,
  light update, and reconnect.
- Add shader compile validation in CI or a local script.

### Acceptance checks
- A clean build can compile all packaged shaders.
- Golden test scenes should catch obvious visual regressions.
- Save/load should preserve materials, views, animation, lights, and LiveLink
  bindings.
- Test output should be easy to inspect when something changes.

## Recommended Execution Order

1. Light Tree or ReGIR
2. EXR and AOV export
3. Decals, material layers, and trim sheets
4. LiveLink hierarchy/material identity/material graph fidelity
5. OCIO/ACES-style color management
6. Height fog and local volumetrics
7. Instancing, proxies, scatter, and LOD
8. SER or wavefront path tracing
9. OpenPBR/interchange completion
10. Regression scenes and automated validation

The first four items should have the biggest immediate payoff. They improve
noise, final delivery, visual richness, and DCC trust. After that, color,
volumes, and scene scale make the engine feel like a mature production tool.
SER and deeper material interchange are valuable, but they are most useful after
the core sampling and pipeline surfaces are solid.
