# Wavefront Phase 1 ABI

Status: frozen for the current wavefront backend ABI v3.

This note records the Phase 1 contract for the queue-driven DXR backend. Later
phases may add new queues or reserved fields, but changes to the fields below
must bump `WAVEFRONT_ABI_VERSION` in `shaders/raytracing/common.hlsli` and the
matching `kWavefrontAbiVersion` in `src/dxr_renderer.cpp`.

## Pass Graph

The intended ownership remains:

1. Bootstrap compute generates primary `PathState` entries in queue A and
   initializes indirect dispatch metadata.
2. Primary DXR visibility traces one primary segment per queued path and writes
   `HitRecord` entries. It may also trace the non-jittered primary guide used
   only for stable DLSS-RR AOV metadata.
3. Primary resolve shades first hits, writes the current frame AOVs, seeds
   reservoirs, enqueues continuation paths, and emits `ShadowTask` records.
4. Shadow DXR visibility consumes `ShadowTask` records and atomically writes
   visible direct-light contribution into the shadow contribution scratch
   buffer.
5. Secondary DXR visibility and secondary resolve ping-pong queue A and queue B
   for explicit bounce progression. Resolve owns continuation and shadow task
   emission.
6. Shadow integration folds the scratch shadow contribution buffer into
   `g_output` and `g_accumulation` once per pixel.
7. Post-wavefront passes consume the same output and AOV resource formats as
   the legacy backend.

Traversal stages emit records only. Resolve stages emit work and surface/AOV
outputs. Shadow visibility owns visibility. Accumulation/output integration
stays outside traversal.

## Queue ABI

The shader ABI is declared in `shaders/raytracing/common.hlsli`; CPU mirrors and
size checks live in `src/dxr_renderer.cpp`.

`WavefrontPathState` is 12 dwords:
- `origin`
- `pixelIndex`
- `direction`
- `rngState`
- `throughput`
- `packedState`

`packedState` stores the current ray type plus specular, refractive, and diffuse
bounce counts. Medium tracking is not encoded yet; future medium support must
use a versioned extension rather than repurposing existing bits silently.

`WavefrontHitRecord` is 28 dwords:
- hit distance, pixel index, packed color/material payload, packed path state,
  and material sort key
- non-jittered primary guide origin/direction/hit payload for stable DLSS-RR
  AOVs
- reserved guide fields, currently zeroed

The guide payload is metadata, not shading work. It must not emit light,
reservoir, continuation, or accumulation work.

`WavefrontShadowTask` is 12 dwords:
- origin, direction, max distance
- packed light sampler metadata
- throughput
- packed pixel state

`WavefrontDispatchArgs` is 4 dwords and mirrors the indirect compute dispatch
shape plus active count.

## Queue Counter Slots

The first slots are fixed:

- `0`: queue A path count
- `1`: primary active count
- `2`: primary hit count
- `3`: primary miss count
- `4`: queue B path count
- `5`: shadow task count
- `6+`: material-bin counters

Counters beyond the current material-bin range are reserved for future
versioned extensions.

## Frozen Resource Formats

Wavefront must continue writing the same public resources as legacy:

- `g_output`
- `g_accumulation`
- `g_depth`
- `g_linearDepth`
- `g_motionVectors`
- `g_albedoOut`
- `g_normalRoughnessOut`
- `g_specularAlbedo`
- `g_specHitDistance`
- `g_specularMotionVectors`
- transmission accumulation and variance resources

DLSS-RR metadata should be sourced from the stable primary guide where legacy
does so. Jittered path state remains the source of transport/color sampling.

## Provisional Areas

These areas are intentionally not frozen as final architecture yet:

- scalable light-sampler API
- medium/volume state
- layered material context
- reservoir task queues
- final material class taxonomy beyond the current seven bins

They should be added as explicit ABI extensions, not ad hoc field reuse.
