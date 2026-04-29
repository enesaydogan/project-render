# Wavefront Phase 2 Scheduler

Status: implemented for the current queue-backed primary and transport slice.

This note records the Phase 2 runtime contract built on the ABI frozen in
`notes/wavefront-phase1-abi.md`.

## Backend Modes

`Legacy` remains the monolithic RayGen path and is still the reference backend.

`Wavefront Parity` is the first executable wavefront parity gate. It runs:

1. counter reset
2. bootstrap compute into queue A
3. queue-backed primary DXR visibility
4. primary-surface resolve

The primary-surface resolve writes the existing output and AOV resources, but
uses `WAVEFRONT_RESOLVE_FLAG_PRIMARY_SURFACE_ONLY` so it does not enqueue
continuation paths, emit shadow tasks, seed ReSTIR reservoirs, or run the
legacy monolithic fallback. This keeps the Phase 2 comparison focused on primary
visibility, first-hit decode, DLSS-RR metadata, and resource-format parity.

`Wavefront Optimized` runs the same bootstrap and primary visibility stages,
then enables the full queue scheduler:

1. primary resolve with DI/GI seed generation
2. primary shadow visibility
3. secondary visibility and resolve ping-pong between queue A and queue B
4. per-bounce shadow visibility
5. shadow integration into output and accumulation
6. ReSTIR spatial reuse on reservoirs produced by the wavefront path

## Stage Ownership

Traversal stages only consume path queues and produce hit records. Resolve
stages write surface outputs and enqueue future work. Shadow visibility owns
occlusion. Shadow integration owns folding visible direct lighting into
accumulation. This preserves the renderer architecture and keeps the parity mode
from hiding scheduler problems behind legacy RayGen output.

## Verification

For Phase 2, compare `Wavefront Parity` against `Legacy` with fixed camera and
seed for:

- depth and linear depth
- motion vectors
- albedo
- normal/roughness
- specular albedo and specular motion
- DLSS-RR primary guide behavior through transmissive surfaces

Use `Wavefront Optimized` for transport, shadow queue, and reservoir validation.
Do not treat optimized transport differences as Phase 2 primary-surface
failures unless the parity mode shows the same AOV mismatch.
