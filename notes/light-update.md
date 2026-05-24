# Project Render 0.5.0: LET THERE BE LIGHT

## Summary
- 0.5.0 becomes a dedicated lighting release: ReGIR, real IES, grouped light instancing, better light gizmos, and fast viewport light placement.
- ReGIR replaces the planned CPU/GPU Light Tree path for scalable local-light sampling.
- Runtime lighting stays GPU-friendly: editor data can be grouped, but the renderer receives compact flattened records and ReGIR structures.

## Phase 1: Light Data Model
- Split editor lights into `LightPrototype` and `LightInstance`.
  - Prototype owns shared data: type, enabled, color/intensity, radius, cone angles, area size, IES profile, sampling flags.
  - Instance owns transform data: position, direction/orientation, enabled, selected state.
- Keep the existing 64-byte `Light` GPU struct as the flattened runtime record for compatibility where possible.
- Add a CPU flatten step:
  - `prototype + instances -> vector<Light>`.
  - Store `prototypeId`, `instanceId`, and flattened light index mapping for UI selection/debug.
- Scene I/O:
  - Add new versioned light schema for prototypes and instances.
  - Migrate old `lgt` flat lights into one prototype with one instance each.
  - Preserve old scene loading with no visual change.

## Phase 2: ReGIR Rendering Foundation
- Add GPU ReGIR buffers:
  - Cell reservoir buffer.
  - Per-cell metadata/counts.
  - Flattened light bounds/power records.
  - Optional debug output buffer for selected light, PDF, cell occupancy, and fallback reason.
- Add ReGIR compute passes:
  - Clear/update grid when camera, light data, or scene bounds change.
  - Generate/update cell candidates from flattened local lights.
  - Read ReGIR candidates from wavefront shading stages.
- Replace flat local-light selection in wavefront:
  - `wavefront_restir_seed_cs.hlsl` should sample local-light candidates through ReGIR before feeding ReSTIR DI reservoirs.
  - `WavefrontSampleDirectLight` should use ReGIR for local lights in secondary explicit lighting.
  - Keep sun and environment as separate candidates with MIS, not baked into the local-light grid.
- Stage ownership stays clean:
  - ReGIR emits candidate light samples.
  - ReSTIR DI owns reservoir reuse.
  - Shadow passes own visibility.
  - Accumulation owns final integration.

## Phase 3: All-Lighting Integration
- Thread ReGIR-backed local light visibility into primary and secondary direct lighting.
- Extend GI surface-radiance evaluation so GI candidates can include local/IES/direct light, not only sun/environment.
- Add emissive mesh proxy participation:
  - Extract eligible emissive materials/mesh bounds into light-like records.
  - Include them in ReGIR as coarse candidates first.
  - Keep true mesh-emissive hit evaluation unchanged.
- Add fallback controls:
  - Flat sampler fallback for debugging.
  - ReGIR off/debug mode.
  - Safe behavior when ReGIR buffers are missing, empty, or overflowed.

## Phase 4: IES Pipeline
- Turn IES into a real authored light profile asset.
- CPU side:
  - Reuse and harden existing `.ies` parsing.
  - Bake profiles into a GPU atlas/array.
  - Track profile path, display name, atlas slice, resolution, and load status.
- GPU side:
  - Bind IES atlas resources in the raytracing/wavefront root signatures.
  - Replace placeholder `evaluate_ies_light` with profile lookup using light direction space.
  - Apply IES intensity as angular modulation on compatible point/spot lights.
- UI side:
  - Add Load IES, Clear IES, profile name, and status in the Lights panel.
  - Treat IES as a profile on a light, not only a separate confusing light type.

## Phase 5: Light Authoring UX
- Redesign the Lights panel around prototype groups:
  - One row per prototype group.
  - Show type, name, enabled state, instance count, total emitted light count, and IES/profile status.
  - Editing shared fields updates all instances immediately.
  - Instance sub-list exposes transform-only data.
- Add authoring commands:
  - Create point/spot/rect/disk/IES-profile light.
  - Duplicate as copy.
  - Duplicate as instance.
  - Convert selected copies to one prototype group where compatible.
  - Remove instance vs remove whole group.
- Add viewport placement:
  - Click-to-create light at mouse position using existing viewport ray/mesh pick.
  - If a mesh is hit, place on the surface and align directional lights to the normal.
  - If no mesh is hit, place at stable camera-depth fallback.
  - Add “Move Selected Light To Surface” using the same picking path.
- Transform behavior:
  - Instance transforms update only that instance.
  - Prototype edits update all instances.
  - Light transform changes reset accumulation but do not rebuild BLAS.

## Phase 6: Gizmos And Viewport Controls
- Add a top-toolbar light-gizmo visibility toggle using the existing dark flat icon style.
- Improve light overlays:
  - Point: radius sphere/circle.
  - Spot: cone outline, falloff ring, direction arrow.
  - Area rect/disk: exact area outline and normal arrow.
  - IES: photometric cone/lobe hint when a profile is assigned.
  - Instances: compact grouped label, selected instance highlight, disabled dim state.
- Add density-safe display modes:
  - Show all.
  - Selected only.
  - Hide labels.
  - Hide all.
- Keep interaction practical in busy scenes:
  - Labels should not dominate the viewport.
  - Selection must still work when gizmos are hidden.
  - Gizmo drawing must be cheap enough for hundreds/thousands of lights.

## Phase 7: Debug And Release
- Add debug views:
  - ReGIR cell occupancy.
  - Selected light/prototype index.
  - Candidate PDF/weight.
  - ReGIR fallback/overflow state.
  - IES atlas slice preview/status.
- Add acceptance scenes/scenarios:
  - 100, 500, and 1000-light interior.
  - Mixed point/spot/area/IES lights.
  - Repeated instanced fixtures.
  - Emissive fixture proxies.
  - Old flat-light scene load.
- Release work:
  - Bump version to `0.5.0` in CMake, app resources, README, and release notes.
  - Add `RELEASE_NOTES_0.5.0.md`.
  - Release title: **LET THERE BE LIGHT**.
  - Verify Release executable and installer package.

## Assumptions
- ReGIR covers local lights, IES lights, area lights, instanced lights, and emissive mesh proxies.
- Environment-map sampling stays on the existing environment CDF path and is combined through MIS.
- Prototype groups are the editor source of truth; flattened `Light` records are runtime upload data.
- LiveLink flat light updates map to single-instance prototype groups until LiveLink exposes true light instancing.
