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
- Phase 3: external-ID mapping and main-thread scene apply bridge
- Phase 4: first renderer invalidation pass integrated across current scene mutation and scene-load/mode-switch paths
- Phase 5: batch-scoped invalidation and light-update coalescing for LiveLink apply passes
- Phase 7: Real-time 3ds Max parity, coordinate basis mapping, and DXR stabilization
- 3ds Max 2025 named-pipe provider bootstrap
- 3ds Max 2025 incremental node, camera, material, light, and selection sync at 60fps
- 3ds Max 2025 mesh payload export through optimized native binary (`.prmesh`) files
- 3ds Max 2025 slot-aware multi-submaterial extraction with multi-submesh `.prmesh` payload preservation
- Qt host-side live-link provider controls for connect, disconnect, and reconnect/resync

Not implemented yet:

- broader proprietary 3ds Max shader-graph evaluation beyond the current slot-aware OpenPBR-style extraction
- stronger geometry change detection and payload lifecycle management for exported Max meshes

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

- `main.cpp` calls `LiveLink::TickRuntime()` after input update

### Behavior Impact

Phase 1 is intentionally infrastructure-only.

Current behavior:

- no provider is registered by default
- validated batches are now consumed on the main thread and applied to the supported scene/runtime targets
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

## Phase 3 Status

### Goal

Support stable synchronization between external DCC object identity and engine-side runtime objects, and apply validated batches on the main thread.

### Implemented

Added a scene-sync runtime under `src/livelink/`:

- `livelink_scene_sync.h`
- `livelink_scene_sync.cpp`

Also updated runtime integration so queued batches are consumed each frame on the main thread.

### What Phase 3 Added

#### 1. External-ID mapping table

Implemented a persistent mapping from canonical `ObjectId` values to engine-side runtime handles.

Current handle kinds:

- scene node
- main camera
- environment

The mapping also stores:

- owning session ID
- last applied revision per object

#### 2. Main-thread batch application

Queued batches from the coordinator are now consumed on the engine main thread.

Current runtime flow:

- poll providers
- consume validation issues
- consume queued batches
- apply supported deltas into `Scene`, camera state, and environment state

#### 3. Validation issue draining

Coordinator validation issues are now drained and logged so they do not accumulate silently forever.

#### 4. Stable node binding maintenance

Implemented:

- binding creation for new external node IDs
- binding cleanup on session close
- binding cleanup on full scene reset
- node-index reindexing after node removal
- stale per-object revision rejection during apply

### Supported Delta Application In Phase 3

Currently applied:

- `SessionOpened`
- `SessionClosed`
- `FullSceneSync`
- `NodeAdded`
- `NodeRemoved`
- `NodeTransformChanged`
- `NodeVisibilityChanged`
- `SelectionChanged`
- `CameraChanged`
- `EnvironmentChanged`

Current behavior details:

- node deltas target the Phase 2 scene mutation API
- node transforms and visibility use the Phase 4 invalidation path
- camera deltas update `g_cameraData` and push through `UpdateCameraCB()`
- environment deltas can load a file IBL and reset DXR state as needed

### Current Unsupported Delta Application

Queued but not yet meaningfully applied:

- `MeshPayloadChanged`
- `MaterialChanged`
- `LightChanged`

These currently log an apply warning instead of mutating the runtime scene.

### Files Touched In Phase 3

- `src/livelink/livelink_scene_sync.h`
- `src/livelink/livelink_scene_sync.cpp`
- `src/livelink/livelink_runtime.h`
- `src/livelink/livelink_runtime.cpp`
- `src/main.cpp`
- `CMakeLists.txt`

### Phase 3 Result

Result:

- the repo now has a real external-ID mapping layer near the live-link runtime
- validated queued batches are now applied on the engine main thread
- live-link is no longer structurally inert even without a real DCC provider

### Phase 3 Remaining Gaps

Still missing:

- mesh payload replacement against real imported content
- material delta application
- light delta application
- broader engine-handle coverage beyond nodes, camera, and environment
- conflict resolution beyond the current revision and binding checks

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

## Phase 6 Status

### Goal

Exercise the host-side live-link architecture without requiring a real DCC plugin.

### Implemented

Added a procedural mock provider:

- `src/livelink/livelink_mock_provider.h`
- `src/livelink/livelink_mock_provider.cpp`

### What Phase 6 Adds

The mock provider can be enabled at startup with:

- `--mock-livelink`

Current mock behavior:

- opens a mock session
- emits a non-destructive full-scene sync marker
- adds two mock nodes
- animates one node transform over time
- toggles visibility on the second node before removing it
- emits selection updates
- emits camera FOV updates

### Current Purpose

This provider is for host-runtime validation, not production transport.

It proves:

- provider registration and connection
- queued-batch validation
- main-thread batch application
- object-ID mapping stability across repeated updates
- node removal reindex handling

### Files Touched In Phase 6

- `src/livelink/livelink_mock_provider.h`
- `src/livelink/livelink_mock_provider.cpp`
- `src/main.cpp`
- `CMakeLists.txt`

### Phase 6 Result

Result:

- the repo can now exercise the live-link runtime end to end without a real DCC bridge
- Phase 3 and Phase 4 behavior can be tested with deterministic synthetic deltas

---

## Phase 7 Status

### Goal

Achieve seamless, high-frequency, mathematically correct visual synchronization between 3ds Max and the engine's DXR backend without engine-side hacks.

### Implemented

- 60FPS (16ms) polling loop implemented in the 3ds Max C++ plugin.
- Native `.prmesh` mesh export and incremental mesh replacement.
- Incremental material deltas emitted from 3ds Max node materials and applied onto per-node engine material bindings.
- Incremental light deltas emitted from 3ds Max light nodes with type, color, intensity, position, direction, cone, and area-shape metadata.
- Automatic reconnect-driven full resync when the named-pipe connection drops and returns.
- Qt host-side provider controls for connect, disconnect, and reconnect/resync.
- Safe DXR pipeline bounding and TLAS initialization allowing DXR geometry to correctly bootstrap on the primary frame after scene clearance (preventing the "red screen" pipeline recreation loops).

### Phase 7 Result

Result:

- The engine now has a practical real-time 3ds Max live-link path for nodes, meshes, camera, materials, lights, and selection.
- High-frequency tracking allows interactive edits without the old 250ms delay.
- The host can now explicitly disconnect and reconnect providers and recover with a fresh full sync after transport loss.

---

## What Is Still Missing Before A Real LiveLink Path Exists

### High-priority missing pieces

- finer renderer invalidation classification
- broader proprietary material-graph evaluation from 3ds Max beyond the current slot-aware multi-submaterial support

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
- map external object identity to engine runtime objects
- apply a supported subset of live-link deltas on the main thread
- apply mesh payload replacement from URI-backed assets on bound scene nodes
- apply material parameter deltas onto bound runtime materials
- apply typed light deltas with color, intensity, transform, cone, and area-shape data onto bound runtime lights
- inspect provider state and recent validation/apply issues in the Qt UI
- connect, disconnect, and reconnect live-link providers from the Qt UI
- accept external live-link batches over a Windows named pipe
- build a first 3ds Max 2025 utility plugin that sends full scene node snapshots, camera, material, light, and selection updates plus native `.prmesh` binary exports
- build future providers against a stable host-side API
- call scene mutation through explicit engine functions instead of import-only code
- run a sustained incremental 3ds Max editing session against the engine with reconnect-driven full resync support

### Not ready yet

- preserve stable identity across repeated live edits for every supported runtime object type
- update only the minimum renderer state for each kind of scene change
- fully implement broader proprietary 3ds Max shader-graph evaluation beyond the current slot-aware multi-submaterial path

---

## Recommended Next Step

Best next implementation phase:

- proprietary shader-graph translation and payload lifecycle management

Reason:

- The adapter now streams slot-aware node, mesh, camera, light, and material deltas in real time. The biggest remaining quality gap is deeper proprietary shader-graph fidelity and stronger mesh payload lifecycle tracking.

---

## Short Status Line

The repo now has a real live-link core, external-ID mapping, a main-thread delta apply path, reconnect-capable named-pipe transport, Qt provider controls, a 60FPS 3ds Max plugin that streams nodes, meshes, camera, materials, lights, and selection, plus first-pass renderer invalidation. The biggest remaining gaps are finer invalidation and deeper 3ds Max material fidelity.