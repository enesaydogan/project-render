# LiveLink Architecture Ideas

## Goal

Build a live-link system that supports 3ds Max first, but stays engine-agnostic and DCC-agnostic so future adapters for Blender, Maya, SketchUp, or others can plug into the same host API.

## Core Ideas

1. Keep DCC-specific code outside the renderer and outside core scene mutation code.
2. Define one engine-side live-link API that all DCC adapters must target.
3. Treat live link as incremental scene synchronization, not repeated full import.
4. Separate transport, delta translation, scene mutation, and renderer invalidation.
5. Use stable external object IDs, not names, as the basis for synchronization.

## Recommended Layers

### 1. DCC Adapter Layer

Each DCC gets its own adapter.

Examples:

- 3ds Max adapter
- Blender adapter
- Maya adapter

Responsibilities:

- Watch the DCC scene for changes
- Convert native DCC data into canonical live-link messages
- Send full sync or incremental deltas to the engine

### 2. LiveLink Host Layer

This lives in the engine and is DCC-agnostic.

Responsibilities:

- Manage live-link sessions
- Receive and validate deltas
- Track source application, document, revision, and object mapping
- Schedule scene updates on the main thread

### 3. Scene Mutation Layer

This is the only layer that should mutate runtime scene state.

Responsibilities:

- Add/remove nodes
- Update transforms
- Update materials
- Replace geometry payloads
- Update cameras and lights
- Track dirty state for downstream systems

### 4. Renderer Invalidation Layer

This decides what actually needs to be rebuilt.

Examples:

- Transform-only update: update instances and TLAS, reset accumulation
- Material parameter update: update buffers, reset accumulation
- Mesh topology update: rebuild GPU buffers, BLAS, TLAS
- Light update: refresh light buffers, reset accumulation if needed

## Canonical Data Model

Every live-link object should carry stable identity.

Recommended fields:

- sourceApp
- documentId
- objectId
- objectType
- revision

Optional fields:

- parentObjectId
- displayName
- payloadHash
- lastModifiedTime

Do not use object names as identity keys.

## Good Message Types

Start with a small delta model.

- SessionOpened
- SessionClosed
- FullSceneSync
- NodeAdded
- NodeRemoved
- NodeTransformChanged
- NodeVisibilityChanged
- MeshPayloadChanged
- MaterialChanged
- LightChanged
- CameraChanged
- EnvironmentChanged
- SelectionChanged

## Good MVP Scope For 3ds Max

First version should support:

- Full scene sync on connect
- Transform sync
- Camera sync
- Light sync
- Material scalar/color changes
- Explicit mesh replacement when topology changes

Later versions can add:

- Material graph translation
- Selection sync
- Timeline sync
- Animation streaming
- Instancing optimization
- Shared-memory geometry payloads

## Dos

- Do keep the engine-side API independent from 3ds Max SDK types.
- Do define a provider-neutral interface such as `ILiveLinkProvider`.
- Do apply live-link changes on the engine main thread.
- Do classify changes by cost before touching GPU resources.
- Do support both full sync and incremental sync.
- Do store an external-ID to engine-object mapping table.
- Do make resync explicit when the adapter or engine loses trust in incremental state.
- Do keep live-link transport separate from asset import code.
- Do log deltas and rejected updates for debugging.
- Do design for one provider per open DCC document or session.

## Don'ts

- Don't wire 3ds Max logic directly into `Scene::ImportModel`.
- Don't treat live link as repeated file import.
- Don't key synchronization by node name.
- Don't rebuild all DXR and raster data for every change.
- Don't let background threads mutate live scene globals directly.
- Don't make renderer code aware of DCC-specific concepts.
- Don't mix transport protocol code with scene mutation logic.
- Don't assume all DCCs share the same coordinate system, units, or material model.
- Don't block the render loop on high-latency DCC operations.
- Don't assume every update should reset everything.

## Suggested Engine Interfaces

Potential engine-side interfaces:

- `ILiveLinkProvider`
- `LiveLinkSession`
- `LiveLinkCoordinator`
- `SceneDelta`
- `ISceneMutationSink`
- `RendererInvalidationPlan`

## Suggested Apply Strategy

1. Receive delta batch from provider.
2. Validate IDs, revision numbers, and payload shape.
3. Convert to engine mutation commands.
4. Apply on main thread.
5. Accumulate renderer dirty flags.
6. Perform the minimum required rebuild.

## Practical Refactor Path For This Repo

1. Extract scene mutation operations from import-heavy flows.
2. Introduce a canonical scene delta model.
3. Add a live-link coordinator module.
4. Add dirty-state classification for renderer rebuild decisions.
5. Implement 3ds Max adapter against that API.
6. Add UI for connection state, full resync, pause, and diagnostics.

## Phased Implementation Plan For This Repo

### Phase 0: Preparation And Constraints

Objective:

- Establish the exact boundaries that live link will integrate with.

Work:

- Document the current mutation-heavy paths in `Scene::ImportModel`, `FinalizeImportedNode`, `ResetScene`, `RebuildAccelerationStructures`, and `UpdateLights`.
- Mark which code paths currently trigger full DXR rebuilds and unconditional accumulation resets.
- Identify current main-thread-only assumptions around scene mutation and GPU resource creation.

Repo focus:

- `src/scene.cpp`
- `src/scene.h`
- `src/dxr_renderer.cpp`
- `src/main.cpp`

Exit criteria:

- Clear map of which scene operations are safe to expose as granular mutations.
- Clear list of what must remain on the main thread.

### Phase 1: Introduce Live-Link Core Types

Objective:

- Add the DCC-agnostic host-side API without changing runtime behavior yet.

Work:

- Create a new module such as `src/livelink/`.
- Add neutral types:
	- `LiveLinkSession`
	- `LiveLinkObjectId`
	- `SceneDelta`
	- `SceneDeltaBatch`
	- `ILiveLinkProvider`
	- `LiveLinkCoordinator`
- Add capability flags for providers such as transform sync, camera sync, material sync, mesh replacement, and full-scene sync.

Recommended file set:

- `src/livelink/livelink_types.h`
- `src/livelink/livelink_provider.h`
- `src/livelink/livelink_coordinator.h`
- `src/livelink/livelink_coordinator.cpp`

Exit criteria:

- The engine builds with empty live-link scaffolding.
- No existing import or render behavior changes.

### Phase 2: Extract Scene Mutation API From Import Paths

Objective:

- Stop using import routines as the only way to create or update scene content.

Work:

- Split `FinalizeImportedNode` into reusable scene mutation operations.
- Add explicit scene mutation functions for:
	- add node
	- remove node
	- update transform
	- update visibility
	- add or replace mesh payload
	- update material bindings
	- update light data
	- update camera data
- Preserve current import flows by making them call the new mutation layer.

Repo focus:

- `src/scene.cpp`
- `src/scene.h`

Important note:

- This is the most important refactor. Live link should target mutation primitives, not `ImportModel`.

Exit criteria:

- File import still works.
- Scene mutation primitives exist and are callable independently of file import.

### Phase 3: Add External-ID Mapping

Objective:

- Support stable synchronization between DCC objects and engine objects.

Work:

- Add a mapping table from external object IDs to engine-side node, mesh, light, camera, or material handles.
- Store source metadata per synced object:
	- source app
	- document ID
	- object ID
	- revision
- Add lookup and conflict handling for stale or duplicate IDs.

Implementation note:

- This mapping should live near the live-link coordinator, not inside renderer code.

Exit criteria:

- The host can resolve incoming deltas to engine-side objects without relying on names.

### Phase 4: Add Renderer Dirty-State Classification

Objective:

- Replace broad rebuild behavior with minimal invalidation behavior.

Work:

- Introduce a `RendererInvalidationPlan` or equivalent dirty flag set.
- Route scene mutations through that classifier.
- Define categories such as:
	- accumulation reset only
	- light buffer update
	- material buffer update
	- TLAS rebuild
	- BLAS rebuild
	- full ray tracing pipeline recreation only when truly required
- Remove unconditional rebuild calls from mutation paths where possible.

Repo focus:

- `src/scene.cpp`
- `src/dxr_renderer.cpp`
- `src/main.cpp`

Why this matters here:

- Today the repo often couples scene updates to `RebuildAccelerationStructures`, `CreateRayTracingPipeline`, and `ResetAccumulation` together. That is too coarse for live link.

Exit criteria:

- Transform-only changes no longer behave like full reimports.
- The engine can apply cheap live deltas without heavy rebuild cost.

### Phase 5: Add Main-Thread Apply Queue

Objective:

- Allow providers to receive or build deltas asynchronously while applying safely on the main thread.

Work:

- Add a thread-safe incoming queue for `SceneDeltaBatch`.
- Validate and enqueue deltas off-thread if needed.
- Apply them during the main update loop on the main thread.
- Keep GPU resource creation and global scene mutation on the main thread.

Repo focus:

- `src/main.cpp`
- `src/livelink/livelink_coordinator.cpp`

Exit criteria:

- No provider thread directly mutates `Scene` globals.
- Delta apply timing is deterministic and safe.

### Phase 6: Build A Mock Provider First

Objective:

- Prove the host-side architecture before writing a real 3ds Max plugin.

Work:

- Add a mock provider that replays canned deltas from JSON or generates test updates.
- Use it to test:
	- add node
	- transform update
	- light update
	- material scalar update
	- mesh replacement
- Verify renderer invalidation behavior under each delta type.

Why this is worth it:

- It lets you debug host architecture without fighting 3ds Max SDK complexity at the same time.

Exit criteria:

- The engine can accept and apply synthetic live-link deltas correctly.

### Phase 7: Implement 3ds Max Provider MVP

Objective:

- Ship the first real DCC bridge with minimal but solid scope.

Work:

- Create a separate 3ds Max plugin project outside the core renderer.
- Use localhost TCP or WebSocket for transport.
- Support:
	- connect/disconnect
	- full sync on connect
	- transform updates
	- camera updates
	- light updates
	- material scalar and color updates
	- explicit mesh replacement on topology changes

Do not include in the first Max MVP:

- full material graph translation
- animation streaming
- skinning and rig sync
- arbitrary custom modifiers

Exit criteria:

- A 3ds Max scene can connect and drive incremental updates in this app.

### Phase 8: Add Host UI And Diagnostics

Objective:

- Make live link operable and debuggable in-editor.

Work:

- Add a Live Link panel or status section.
- Show:
	- provider name
	- connection state
	- source document path
	- last sync time
	- pending delta count
	- last error
- Add controls:
	- connect
	- disconnect
	- pause sync
	- full resync
	- log details

Repo candidates:

- Qt panel under `src/qt/`
- optional ImGui fallback in `src/editor_ui.cpp`

Exit criteria:

- Live-link state is visible and recoverable without restarting the app.

### Phase 9: Performance And Robustness Pass

Objective:

- Make the system stable under sustained iteration.

Work:

- Add batching and coalescing for repeated transform updates.
- Add revision checks and out-of-order rejection.
- Add payload hashing for mesh/material updates where useful.
- Add reconnect and resync logic.
- Add instrumentation for apply cost and rebuild cost.

Exit criteria:

- Frequent DCC edits no longer cause unnecessary heavy rebuilds.
- Connection loss and mismatch states can recover cleanly.

### Phase 10: Generalize For Additional DCCs

Objective:

- Prove the design is truly adapter-based.

Work:

- Implement a second provider or a provider simulator with different assumptions.
- Validate that no engine-side code is 3ds Max-specific.
- Refine canonical message schema where the first provider exposed gaps.

Exit criteria:

- The engine can support another DCC without reworking the host architecture.

## Suggested Delivery Order

If implementation time is tight, use this order:

1. Phase 1
2. Phase 2
3. Phase 4
4. Phase 5
5. Phase 6
6. Phase 7
7. Phase 8
8. Phase 9

## First Concrete Milestone

The first milestone worth coding in this repo is:

- Create `src/livelink/` core types
- Extract scene mutation primitives from `FinalizeImportedNode`
- Add renderer invalidation flags
- Add a mock delta provider

That gets the engine architecture ready before any 3ds Max SDK work starts.

## Nice Future Features

- Live-link connection status in the status bar
- Per-provider enable/disable controls
- Auto-reconnect on DCC restart
- Full resync button
- Conflict diagnostics panel
- Change statistics and timings
- Session recording for debugging

## Short Rule

Live link should behave like a structured scene-delta pipeline, not like a hidden import button.