Status: approved for implementation on 2026-04-27


## Plan: Wavefront Path Tracing Architecture

Build a vendor-neutral, queue-driven DXR path tracing backend that starts alongside the current monolithic RayGen path, reaches visual parity, then becomes the default and eventually replaces the old backend. The future-proof design is a hybrid DXR architecture: keep hardware traversal in DXR, move scheduling, material/lobe classification, shadow visibility, and bounce progression into explicit GPU queues and compute phases, and layer optional NVIDIA SER on top of the traversal/material stage instead of making NVAPI a core dependency.

**Steps**
1. Phase 1: Freeze the target architecture and ABI. Define the wavefront frame graph, queue types, per-ray state, sort keys, and shader-library boundaries before code changes. This must preserve today’s outputs: ReSTIR DI/GI, DLSS inputs, motion vectors, denoiser inputs, adaptive sampling, glass handling, and batch/export behavior.
2. Phase 1: Define a strict queue ABI with versioned structs for PathState, HitRecord, ShadowTask, ReservoirTask, and QueueCounters. Keep hot per-path state compact and stable: pixel index, ray origin/direction, throughput, depth, ray type, medium/transmission state, previous pdf/delta flags, RNG state, and material/lobe sort key. Reserve extension fields for volumetrics, light trees, and material layering so later features do not force another queue format break.
3. Phase 1: Split today’s monolithic shader logic into reusable libraries without changing behavior. Extract material decode, BRDF eval, BSDF sampling, environment eval, NEE, and path termination from the current RayGen flow into shared HLSL helpers usable by both the legacy path and the new wavefront passes. This step blocks all later implementation because it removes duplicated transport logic.
4. Phase 2: Introduce a GPU scheduler in the renderer. Add persistent queue buffers, counter buffers, indirect-dispatch argument buffers, and pass orchestration in the DXR renderer. Reuse the existing async compute infrastructure as the scheduling backbone so queue-driven shading, ReSTIR spatial passes, and future denoiser prep can share one synchronization model. This depends on 1-3.
5. Phase 2: Implement a hybrid traversal stage. Keep traversal in DXR and drive it from queue-backed ray batches rather than a full-screen monolithic loop. The recommended design is a queue-indexed DispatchRays trace stage that reads PathState entries, traces one ray per queue element, and writes compact HitRecord or MissRecord outputs. Keep closest-hit minimal: fetch geometry/material data, pack hit attributes, classify material/lobe, and avoid expensive lighting logic there. This depends on 4.
6. Phase 2: Implement the first executable wavefront slice: primary visibility and first-hit decode only. Generate primary rays, trace them through the queue-backed traversal stage, write G-buffer/AOV outputs, and hand off to the existing accumulation/denoiser path. Match current depth, albedo, normal/roughness, specular albedo, motion vector, and transmission behavior exactly before touching multi-bounce transport. This is the first parity gate and depends on 5.
7. Phase 3: Add material-class shading bins and explicit bounce queues. After traversal, sort or bin hits by a compact material/lobe key: diffuse, glossy reflection, delta reflection, refraction, emissive, alpha-tested foliage/grass, and miss. Run shading kernels per bin, compute direct lighting tasks, spawn shadow tasks, and enqueue next-bounce PathState entries. Keep per-bounce compaction explicit and perform Russian roulette in the queue scheduler rather than inline in traversal. This depends on 6.
8. Phase 3: Move shadow visibility to a dedicated queue and kernel. Batch shadow rays separately from shading, because this is the cheapest high-impact coherence win and maps directly to your existing many-light ArchViz scenes. Keep the interface generic enough to swap flat-light sampling for Light Tree or ReGIR later without changing the rest of the scheduler. This depends on 7 and can be developed in parallel with step 9 once the queue ABI is stable.
9. Phase 3: Rehost ReSTIR DI/GI on top of the wavefront scheduler instead of treating it as a side system. Keep the current temporal/spatial reservoir math, but make candidate generation and visibility checks consume queue outputs. Preserve the current async compute path and make the reservoir passes operate on queue-produced primary/secondary hit records rather than assumptions baked into the monolithic RayGen shader. This depends on 7 and can run in parallel with 8.
10. Phase 3: Introduce a pluggable light-sampler interface now, even if the first implementation still uses the current flat light array. Define the API so the same shading kernel can consume FlatSampler, LightTreeSampler, or ReGIRSampler. This is required for a future-proof ArchViz renderer because wavefront alone will not solve convergence in many-light interiors. This depends on 7, and replacement of the legacy path should be blocked on at least one scalable light-sampling implementation.
11. Phase 4: Add optional SER as an optimization layer, not an architectural pillar. Use NVAPI only around the traversal/material-classification hot path and possibly shadow/material-heavy bins where divergence remains. Unsupported GPUs must keep the exact same wavefront backend and only lose the SER optimization. This depends on 7-10.
12. Phase 4: Expand the scheduler for ArchViz-specific transport cases that will matter later: refractive stacks, thin-walled glass, vegetation/alpha-tested materials, emissive decals, and future participating media. Add queue flags and medium-state hooks now so volumetrics and layered materials can slot into the same scheduler without reworking the path state model. This depends on 7-10.
13. Phase 5: Reach parity, flip default, then retire the monolithic path. Keep the old backend as a debug and bisect tool until parity is proven for representative interior, exterior, glass-heavy, foliage-heavy, and many-light scenes. Once the wavefront backend is stable, make it the default, keep the legacy path temporarily behind a debug toggle, and remove it only after validation coverage is strong. This depends on 6-12.

**Relevant files**
- `d:/project-render/src/dxr_renderer.cpp` — owns `Initialize`, `EnsureAsyncComputeContext`, `DispatchRestirSpatialPasses`, `CreateRayTracingPipeline`, and the current DispatchRays orchestration; this is the control point for queue resources, pass scheduling, synchronization, and runtime toggles.
- `d:/project-render/shaders/path_tracer_core.hlsl` — source of truth for the current `RayGen` transport loop, adaptive sampling, ReSTIR DI setup, GI handoff, sky/environment handling, DLSS inputs, and accumulation behavior that must be preserved during extraction.
- `d:/project-render/shaders/raytracing/common.hlsli` — defines `RayPayload`, ray type constants, material/light bindings, camera constants, and shared shader ABI; this should become the canonical place for wavefront queue payload enums and shared transport helpers.
- `d:/project-render/shaders/raytracing/hit.hlsl` — current closest-hit material decode/evaluation path; refactor this into a minimal hit-record producer plus shared decode helpers.
- `d:/project-render/src/material/material_system.h` — runtime DXR material packing and flags; extend it with stable material-classification helpers or sort-key generation so shading bins remain coherent as materials evolve.
- `d:/project-render/src/material/material_system.cpp` — matching CPU-side material packing logic; keep CPU and GPU classification rules aligned for debugging and future validation.
- `d:/project-render/notes/roadmap.md` — current feature ordering; use it to keep the wavefront plan aligned with Light Tree/ReGIR, volumetrics, and material-system roadmap items that affect the queue ABI.

**Verification**
1. Add an internal backend toggle with three modes: legacy monolithic, wavefront parity mode, and wavefront optimized mode. Every phase must be comparable against the same camera/frame seed.
2. First parity gate: primary-hit only wavefront path reproduces today’s depth, motion vectors, albedo, normal/roughness, specular albedo, and transmissive-primary behavior with matching denoiser inputs.
3. Second parity gate: single-bounce direct lighting matches the current renderer within tight image-diff tolerance on interior, exterior, glass, and foliage scenes.
4. Third parity gate: multi-bounce transport matches legacy output statistically across fixed seeds, including refractive paths, emissives, environment MIS, adaptive sampling, and accumulation reset behavior.
5. Measure GPU times separately for traversal, shading bins, shadow queue, ReSTIR passes, denoising, and total frame time. Require improvements on at least one balanced viewport scene and one final-frame scene before enabling the new backend by default.
6. Validate queue stability under stress: many lights, many instances, large material counts, heavy alpha-tested vegetation, and accumulation over long sample counts. Check for queue overflow, dead queues, and sync stalls.
7. Add shader-debug views for queue length, material-bin occupancy, shadow-ray count, average live paths per bounce, and overflow flags so performance regressions remain diagnosable after the old path is removed.

**Decisions**
- Chosen migration model: eventually replace the current path tracer, but only after an additive parity phase keeps the old backend available for debugging and regression triage.
- Chosen platform posture: vendor-neutral wavefront core with optional NVIDIA SER fast path; NVAPI is optimization-only.
- Chosen product target: balanced viewport and final-render performance, not a viewport-only or offline-only backend.
- Scope included: renderer architecture, queue model, migration order, validation strategy, and compatibility with current ReSTIR/denoiser/AOV systems.
- Scope excluded for the first implementation: full volumetric transport, a complete Light Tree/ReGIR implementation, and a forced move to inline ray tracing or pure-compute traversal.

**Further Considerations**
1. Prefer a queue-indexed DXR traversal stage first, not an immediate full move to TraceRayInline/RayQuery compute traversal. It preserves your current DXR hit shader investment and is the safest path to parity.
2. Treat the light-sampler API as a first-class interface from day one. Otherwise the wavefront backend will be tightly coupled to the current flat light array and will have to be reopened when Light Tree or ReGIR lands.
3. Reserve path-state bits for medium tracking and layered-material context now. Your roadmap already includes volumetrics and richer material layering, and those are the features most likely to force an ABI rewrite if not planned early.

**Execution Detail**
1. First implementation slice should be primary visibility parity only. Keep the current full-screen DispatchRays path alive, but add a second backend that still uses the same DXR library/state object creation path in `CreateRayTracingPipeline` and initially changes only what `RayGen` and `ClosestHit` are responsible for.
2. For that first slice, move only these responsibilities out of monolithic `RayGen`: primary ray generation, primary trace submission, and primary-hit surface output packing. Do not move ReSTIR, GI, adaptive sampling, accumulation, Russian roulette, or multi-bounce path continuation yet.
3. Make `ClosestHit` in `shaders/raytracing/hit.hlsl` a compact hit-record producer for the wavefront path. It should still decode geometry/material state, but it should stop doing local GI-eval shading work for the wavefront path variant. The current shader performs texture sampling, grass-specific tinting, emissive handling, normal mapping, AO, and GI-eval shadow tracing; that is too much work for a queue-friendly closest-hit stage.
4. Keep the current `RayPayload` ABI intact for the legacy backend, but do not use it as the long-term queue ABI. The new queue ABI should be separate and stable so future features do not inherit the current payload packing compromises.
5. First queue schema recommendation:
   PathState: pixel index, ray origin, ray direction, throughput, bounce index, ray type, RNG seed/state, previous pdf, previous-delta flag, path flags, reserved medium slot.
   HitRecord: pixel index, instance index, primitive index, barycentrics, hit distance, material index, world position or reconstructable hit info, world normal, packed surface params, packed albedo/specular/transmission, classification key.
   ShadowTask: pixel index, origin, direction, max distance, radiance, BSDF weight, sampler metadata, queue flags.
   QueueCounters: active path count, hit count, miss count, shadow count, overflow flag, per-bin offsets.
6. First sort/bin key recommendation: ray type in the high bits, material class in the middle bits, and flags in the low bits. Minimum material classes: opaque diffuse, glossy dielectric, metallic/glossy conductor, delta reflection, refraction, emissive, alpha-tested, grass/foliage, miss. This is enough to make later SER and light-tree work coherent without overfitting the first version.
7. First pass graph recommendation:
   Pass A: generate primary PathState queue from camera constants.
   Pass B: queue-backed DXR trace pass producing HitRecord and MissRecord outputs.
   Pass C: primary-surface resolve pass writing existing G-buffer and denoiser inputs (`g_depth`, `g_motionVectors`, `g_albedoOut`, `g_normalRoughnessOut`, `g_specularAlbedo`, `g_specHitDistance`) in exactly the current format.
   Pass D: hand off back to the legacy accumulation/output path so the user-visible frame remains debuggable during migration.
8. Keep the state object/SBT architecture simple in the first milestone. Today the renderer exports one `RayGen`, one `Miss`, one `ClosestHit`, and one `AnyHit`, with a single hit group and one-entry miss/hit tables. Do not explode this into many hit groups immediately. Prefer one additional wavefront-oriented raygen export or a parallel DXR library entry point before introducing many specialized hit groups.
9. Reuse the existing async compute scheduling model already used for ReSTIR spatial/GI passes, but do not force the first primary-visibility slice onto async compute if that complicates debugging. Correctness and parity are higher priority than early overlap.
10. Keep pipeline recursion shallow and explicit. The current RT pipeline is configured with recursion depth 4 even though the RayGen loop handles most bounce progression explicitly. Preserve that principle in the wavefront backend: traversal stages should trace one segment at a time, and the scheduler should own path continuation.

**First files to touch**
- `d:/project-render/shaders/raytracing.hlsl` — add or route a wavefront-oriented DXR export set without breaking legacy entry points.
- `d:/project-render/shaders/raytracing/common.hlsli` — introduce shared queue structs, classification enums, and helper packing/unpacking functions.
- `d:/project-render/shaders/raytracing/hit.hlsl` — split material decode from expensive local shading and add a path that writes compact hit records for wavefront.
- `d:/project-render/shaders/path_tracer_core.hlsl` — extract reusable surface/light/path helpers out of `RayGen` so legacy and wavefront backends can share them.
- `d:/project-render/src/dxr_renderer.cpp` — allocate queue/counter buffers, add backend toggles, extend pipeline creation for the new export path, and orchestrate the first pass graph.

**Blocking constraints discovered in code**
- `ClosestHit` currently mixes hit decoding with GI-eval shadow tracing and material-side special cases; this is the main structural blocker to wavefront coherence.
- `RayGen` currently owns primary pretrace-through-glass handling, DLSS input writes, ReSTIR DI initialization, GI candidate tracing, adaptive sampling, and full path continuation in one shader. Extraction order matters: primary-surface outputs must be separated before multi-bounce transport.
- The current renderer assumes texture-backed reservoirs and screen-space output UAVs are written directly from `RayGen`. A wavefront backend must preserve those resource formats to avoid destabilizing denoisers and post passes during migration.
