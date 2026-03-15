# LiveLink Implementation Status

## Purpose

Track the implementation state of the engine-side live-link work in this repo.

This file is intended to answer:

- what is already implemented
- what changed in each phase
- what remains before a real 3ds Max live-link provider can be connected

---

## Current Summary

Completed so far:

- Phase 1: engine-side live-link core
- Phase 2: reusable scene mutation API extracted from import-heavy flows

Not implemented yet:

- external-ID mapping between DCC objects and engine objects
- renderer dirty-state classification
- main-thread delta apply path from queued live-link batches into `Scene`
- mock provider
- 3ds Max provider
- live-link UI panel/status controls

---

## Phase 1 Status

### Goal

Introduce a real engine-owned live-link core without changing runtime scene behavior.

### Implemented

The following live-link module was added under `src/livelink/`:

- `livelink_types.h`
- `livelink_types.cpp`
- `livelink_provider.h`
- `livelink_coordinator.h`
- `livelink_coordinator.cpp`
- `livelink_runtime.h`
- `livelink_runtime.cpp`

### What Phase 1 Added

#### 1. Canonical live-link types

Implemented:

- provider capabilities
- connection state
- object type classification
- scene delta kinds
- stable object identity container
- session info
- delta payload variants
- delta batch structure
- validation issue structure

#### 2. Provider interface

Implemented a provider-neutral engine-side interface:

- `ILiveLinkProvider`

Responsibilities supported now:

- expose provider name
- expose capability flags
- expose connection state
- expose last error
- connect
- disconnect
- poll new delta batches

#### 3. Coordinator

Implemented:

- provider registration and unregistration
- provider connect and disconnect control
- session tracking
- batch validation
- queued delta batch storage
- validation issue reporting
- runtime stats snapshot
- provider snapshot inspection

#### 4. Validation rules

Implemented checks for:

- provider/session identity mismatch
- empty session IDs
- unknown delta kind
- missing object IDs where required
- zero revision on non-lifecycle deltas
- out-of-order batch sequence
- stale revisions
- session-not-opened cases

#### 5. Engine runtime ownership

Implemented:

- a global runtime accessor for the coordinator
- per-frame polling through the main loop

Current integration point:

- `main.cpp` calls `LiveLink::TickCoordinator()` after input update

### Behavior Impact

Phase 1 is intentionally infrastructure-only.

Current behavior:

- no provider is registered by default
- no scene deltas are applied yet
- rendering and scene behavior remain unchanged

### Files Touched In Phase 1

- `src/livelink/livelink_types.h`
- `src/livelink/livelink_types.cpp`
- `src/livelink/livelink_provider.h`
- `src/livelink/livelink_coordinator.h`
- `src/livelink/livelink_coordinator.cpp`
- `src/livelink/livelink_runtime.h`
- `src/livelink/livelink_runtime.cpp`
- `src/main.cpp`
- `CMakeLists.txt`

### Phase 1 Result

Result:

- the repo now has a real engine-side live-link core that future providers can plug into
- no DCC-specific code was introduced
- no renderer code was made DCC-aware

---

## Phase 2 Status

### Goal

Extract reusable scene mutation entry points from current import and reimport logic so future live-link delta application can target scene mutation directly instead of file-import code.

### Implemented

New scene mutation API was added to `Scene`:

- `AddNode`
- `AddImportedNode`
- `ReplaceNodeImportedContent`
- `RenameNode`
- `UpdateNodeTransform`
- `SetNodeVisibility`
- `RemoveNode`

Also added:

- `ImportedNodePayload`

### What Phase 2 Changed

#### 1. Imported content is now wrapped in a reusable payload

`ImportedNodePayload` now carries:

- source path
- optional display name
- meshes
- materials
- textures

This gives live-link and import code a shared data shape for node creation or replacement.

#### 2. Import finalization was extracted behind scene mutation API

Previously:

- import paths merged meshes, materials, textures, node creation, and DXR rebuild logic inline

Now:

- `FinalizeImportedNode` delegates to `AddImportedNode`

#### 3. Reimport now uses the same mutation layer

Previously:

- `ReimportNode` had its own custom merge and replacement logic inline

Now:

- `ReimportNode` delegates to `ReplaceNodeImportedContent`

#### 4. Async import UI merge now uses the same mutation layer

Previously:

- the background-import completion path inside the Scene panel duplicated merge logic again

Now:

- async import completion calls `AddImportedNode`

#### 5. Node removal was extracted

Previously:

- `DeleteNode` contained the full remove-and-rebuild logic

Now:

- `DeleteNode` is a compatibility wrapper over `RemoveNode`

### Remaining Behavior In Phase 2

Phase 2 improves structure, but does not yet minimize rebuild cost.

Current mutation behavior still broadly does this:

- rebuild acceleration structures
- sometimes recreate the ray tracing pipeline
- reset accumulation

That is expected for now.

This will be refined in the future renderer invalidation phase.

### Files Touched In Phase 2

- `src/scene.h`
- `src/scene.cpp`

### Phase 2 Result

Result:

- import, async import merge, and reimport now share the same mutation seam
- the repo no longer depends on `ImportModel` as the only way to create or replace imported scene content
- future live-link delta application has real scene-level targets to call into

---

## What Is Still Missing Before A Real LiveLink Path Exists

### High-priority missing pieces

- external object ID to engine object mapping
- delta-to-scene apply layer using the live-link queue
- renderer invalidation classification
- mock provider for testing host behavior
- diagnostics UI for live-link state

### Important technical gap

The repo still has some direct scene construction paths outside the new mutation API, especially scene loading and other runtime-specific flows.

That means:

- the mutation seam is real now
- but it is not yet the single universal path for all scene edits

---

## Practical Readiness Assessment

### Ready now

- define and receive provider-neutral delta batches
- validate and queue live-link batches safely
- build future providers against a stable host-side API
- call scene mutation through explicit engine functions instead of import-only code

### Not ready yet

- apply live-link deltas into the runtime scene
- preserve stable identity across repeated live edits
- update only the minimum renderer state for each kind of scene change
- run a real 3ds Max session against the engine

---

## Recommended Next Step

Best next implementation phase:

- renderer invalidation classification

Reason:

- the scene mutation API now exists
- but transform, visibility, mesh, and material changes still pay too much rebuild cost
- before real live-link deltas are applied, the renderer needs finer invalidation rules

---

## Short Status Line

The repo now has a real live-link core and a real scene mutation seam, but it does not yet have a delta apply path or efficient renderer invalidation.