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
- Phase 4: first renderer invalidation pass integrated across current scene mutation and scene-load/mode-switch paths

Not implemented yet:

- external-ID mapping between DCC objects and engine objects
- fine-grained renderer dirty-state classification beyond the current coarse buckets
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

- request deferred TLAS refresh for transform and visibility edits
- request deferred full AS rebuild for import, reimport, add, remove, load, and DXR mode bootstrap paths
- reset accumulation

That is better than the original Phase 2 state, but still intentionally coarse.

This is refined further in Phase 4.

### Files Touched In Phase 2

- `src/scene.h`
- `src/scene.cpp`

### Phase 2 Result

Result:

- import, async import merge, and reimport now share the same mutation seam
- the repo no longer depends on `ImportModel` as the only way to create or replace imported scene content
- future live-link delta application has real scene-level targets to call into

---

## Phase 4 Status

### Goal

Introduce a renderer invalidation plan so scene mutation APIs stop behaving like immediate mini-reimports.

### Implemented

Renderer invalidation is now expressed through explicit deferred requests instead of direct scene-thread rebuild work in the main mutation paths.

Implemented buckets:

- accumulation reset only
- TLAS refresh request
- full acceleration-structure rebuild request

### What Phase 4 Changed

#### 1. Scene mutation now maps to invalidation intent

Current mapping:

- `UpdateNodeTransform` -> TLAS refresh + accumulation reset
- `SetNodeVisibility` -> TLAS refresh + accumulation reset
- `AddImportedNode` -> full AS rebuild + accumulation reset
- `ReplaceNodeImportedContent` -> full AS rebuild + accumulation reset
- `RemoveNode` -> full AS rebuild + accumulation reset
- scene load with embedded meshes -> full AS rebuild + accumulation reset

#### 2. Direct rebuild hotspots were moved onto the same plan

Updated paths now use deferred requests instead of immediate rebuild calls:

- scene mutation code in `scene.cpp`
- PRS load path in `scene_io.cpp`
- DXR mode bootstrap in `input_handler.cpp`
- Qt DXR mode switch controls

#### 3. DXR now honors deferred rebuilds before missing-TLAS early-out

This matters because a deferred rebuild request must be able to bootstrap DXR when TLAS is currently null.

Current behavior:

- pending AS rebuild/update requests are processed before the renderer decides to clear due to missing TLAS

#### 4. Scene mutation no longer recreates the RT pipeline by default

The current scene mutation and scene-load paths no longer recreate the DXR pipeline as part of normal topology edits.

### Files Touched In Phase 4

- `src/scene.h`
- `src/scene.cpp`
- `src/dxr_renderer.h`
- `src/dxr_renderer.cpp`
- `src/scene_io.cpp`
- `src/input_handler.cpp`
- `src/qt/RenderSettingsPanel.cpp`
- `src/qt/RenderModePanel.cpp`

### Phase 4 Result

Result:

- the repo now has a real renderer invalidation plan wired through current scene mutation and scene-load/mode-switch paths
- transform and visibility changes can use the existing TLAS update/refit path instead of forcing full scene-thread rebuild work
- full rebuild requests are deferred to the DXR frame path instead of being executed inline in the scene layer

### Phase 4 Remaining Gaps

Still missing:

- material-change classification under the same central invalidation API
- light-only invalidation buckets
- more granular topology/resource categories beyond TLAS refresh vs full AS rebuild
- a single universal invalidation entry point for every renderer-affecting system in the repo

So Phase 4 is implemented in a practical repo-wide first pass, but not yet at the final granularity needed for high-frequency DCC live edits.

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