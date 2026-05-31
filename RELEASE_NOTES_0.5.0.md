# Project Render 0.5.0 Release Notes

**Release name:** LET THERE BE LIGHT

Changes since `eef7cef493f2d4f84b249593a56bed7953261670`.

## Highlights

- Turned 0.5.0 into a dedicated lighting release focused on ReGIR, real IES profiles, instanced fixture workflows, and faster viewport light authoring.
- Added GPU ReGIR local-light sampling for scalable scenes with many point, spot, area, IES, and emissive-proxy lights.
- Added real IES profile parsing, GPU lookup, scene save/load support, and Lights panel controls.
- Added grouped light prototype/instance behavior, multi-light selection, Shift-drag duplication, and better light gizmos for dense interior scenes.
- Added click-to-create and click-to-move light placement from viewport surface picks.
- Added emissive mesh proxy participation in ReGIR using triangle-backed emissive sampling instead of rough proxy-only sampling.
- Improved DLSS Ray Reconstruction stability by disabling unsupported RR dynamic input resolution and keeping RR motion/AOV guidance dense and finite.

## ReGIR Lighting

- Added ReGIR GPU buffers, update passes, candidate selection, fallback handling, and wavefront integration.
- Routed local-light sampling through ReGIR before ReSTIR DI reservoir reuse.
- Kept sun and environment sampling separate from the local-light grid.
- Added runtime fallback paths for missing, empty, overflowed, or disabled ReGIR data.
- Added the lowercase `regir debug` panel with live settings and renderer stats, including cell state, fallback reasons, selected-light data, and emissive mesh triangle counts.
- Removed the older `REGIR_ENABLED` compile-time gate so the runtime path and debug UI stay aligned.

## IES And Emissive Lights

- Turned IES into an authored light profile rather than a placeholder light type.
- Added IES parsing, deduplication, GPU atlas/slice tracking, profile names, status reporting, and scene persistence.
- Added GPU IES angular modulation for compatible lights.
- Added IES save/load fixes so scenes preserve assigned profiles.
- Added triangle-backed emissive mesh data for ReGIR proxy sampling.
- Updated DXR, wavefront resolve, wavefront ReSTIR seed, and ReSTIR spatial bindings for emissive triangle data.
- Improved ReGIR emissive reach by using area-weighted emissive power instead of a center/radius approximation.

## Light Authoring Workflow

- Reworked the Lights panel around faster fixture editing and denser scenes.
- Added point, spot, area, and IES-profile light creation flows.
- Added multi-light selection, box selection, multi-type selection, and grouped selection behavior.
- Added Shift-drag duplication and instance-aware duplication workflows.
- Added delete support for multi-light selections and light instances.
- Added a single-gizmo workflow for mixed light selections.
- Added click-to-create and click-to-move light placement that uses viewport mesh picks and surface normals.
- Improved light overlays and gizmos so they remain usable in busier interior scenes.

## Scene I/O And LiveLink

- Added versioned light scene data for prototype/instance-style lights while preserving old flat-light scene loading.
- Preserved IES assignments and light instance data through scene save/load.
- Added LiveLink support for light instance creation and updates.
- Improved 3ds Max LiveLink ordering and timeout behavior so parent/child connections and large syncs are more reliable.
- Filtered renderable splines and closed mesh cases more carefully in the Max LiveLink path.

## Rendering And Reconstruction Stability

- Improved DLSS-RR guide and motion-vector handling after the lighting work exposed more temporal edge cases.
- Disabled DLSS-RR dynamic input resolution because the bundled Streamline RR path reinitializes when input resolution changes.
- Kept DLSS/RR frames dense when adaptive sampling is enabled so Streamline does not receive stale color, depth, motion, or AOV data.
- Kept specular motion-vector fallbacks finite so RR does not receive invalid half-float motion values on non-reflective pixels.
- Added several guardrails around RR history resets and guide data to reduce shimmer, jitter, and checker-style flashes.

## Notes

- ReGIR is now the scalable local-light path for large fixture-heavy interiors, but the flat sampler fallback remains useful for debugging.
- IES profiles are stored as scene assets; keep the original profile files available when sharing scenes if you want predictable reload behavior.
- DLSS-RR Dynamic Resolution is intentionally disabled until the Streamline RR SDK path supports dynamic input resolution without denoiser reinitialization.
- Dense lighting scenes should use the `regir debug` panel when diagnosing missing candidates, overflow, fallback reasons, or emissive proxy participation.
