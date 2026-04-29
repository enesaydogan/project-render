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

## ReSTIR

Wavefront optimized primary resolve seeds DI/GI reservoirs from queue-produced
primary hit records. The existing spatial ReSTIR passes then consume those
reservoir textures through the same resource formats used by the legacy path.
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
