# Wavefront Phase 3 Scheduler

Status: implemented for the current optimized wavefront backend, excluding
ReGIR and SER.

Phase 3 builds on the Phase 1 ABI and Phase 2 primary-surface gate. `Legacy`
stays as the reference path, `Wavefront Parity` stays the primary-only AOV
gate, and `Wavefront Optimized` owns queue-driven transport.

## Material Bins

Primary and secondary traversal stages pack a material/lobe sort key into each
hit record and compact hit indices into the shared material-bin index buffer.
Resolve runs one indirect compute dispatch per material bin, then a separate
miss pass for sky/environment records. This keeps traversal as record
production and makes resolve own shading, continuation, and shadow task
emission.

Current bins are:

- diffuse
- glossy dielectric
- conductor
- delta reflection
- refraction
- emissive
- translucent

## Bounce Queues

Primary resolve writes continuations into queue B. Secondary resolve ping-pongs
between queue A and queue B, resetting the destination counter before each
bounce. Russian-roulette style continuation decisions and bounce budget checks
belong to resolve, not traversal.

## Shadow Queue

Primary and secondary resolve emit `WavefrontShadowTask` records into the shadow
queue. Shadow DXR visibility consumes only that queue and writes visible direct
lighting into the shadow-contribution scratch buffer. Shadow integration folds
the scratch buffer into `g_output` and `g_accumulation` after the scheduler has
finished tracing shadows.

Local point, spot, IES, rect, and disk lights are expected to contribute in
`Wavefront Optimized`. Primary resolve always emits an explicit sampled local
light task, so local emitters are not hidden behind sun-only reservoir choices
or low-weight DI candidates. Direct-light task weights use a two-sided normal
orientation for robustness against flipped hit normals. Direct local-light
tasks carry their full weighted radiance in `throughput`, so shadow visibility
only gates visibility and integrates the queued contribution. Rect and disk
lights carry their sampled direction and distance through the shadow task rather
than collapsing the light to a center-point approximation.

## ReSTIR

Wavefront optimized has an explicit `restir-seed` scheduler stage for ReSTIR DI.
It consumes queue-produced primary hit records through the same material-bin
task lists used by primary resolve, writes the DI reservoir textures, and clears
miss pixels. Primary resolve then reads the scheduler-seeded DI reservoir when
emitting direct-light shadow tasks.

The existing spatial ReSTIR DI pass remains the temporal/spatial reuse stage and
continues to consume the same reservoir resource format as legacy.

ReSTIR GI candidate generation still happens inside primary resolve in this
phase because it currently depends on the GI candidate ray query and secondary
surface radiance evaluation helpers. The GI spatial reuse pass remains
resource-format compatible with legacy. Moving GI candidate generation into its
own scheduler stage should be treated as the next ReSTIR-specific refactor, not
as part of Light Tree/ReGIR.

Parity mode clears reservoir outputs and does not run ReSTIR, so primary-AOV
validation remains isolated from transport reuse.

## Light Sampler Boundary

The current implementation still uses the flat light array plus the directional
sun, but wavefront shaders now sample through `WavefrontLightSamplerContext`.
That context is intentionally small today: mode and available-light count. Light
Tree or another scalable sampler can replace the implementation behind that
boundary without reopening traversal, queue records, or shadow task ABI.

## Excluded From This Phase

- ReGIR
- SER
- volumetric queues
- new light-tree data structures
- retiring the legacy backend
