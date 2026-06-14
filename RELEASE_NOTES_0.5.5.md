# Project Render 0.5.5 Release Notes

**Release name:** WHERE IS MY ASSETS

Changes since `ed52cf7cdc8b38a998a282853d9a141a7fd49dfd`.

## Highlights

- Delivered phases 1-6 of the Asset Manager roadmap: registry and browser, cooked assets, scatter integration, `.prpak`, VDB volumes, and scene portability.
- Added a full Qt Asset Manager with stable Asset IDs, folders, tags, favorites, search, filters, thumbnails, recent assets, missing-state views, and detailed runtime status.
- Added versioned cooking and cache pipelines for models, materials, textures, scatter assets, and sparse VDB volumes.
- Added unencrypted `.prpak` creation, validation, mounting, checksums, payload deduplication, dependency collection, and random-access loading.
- Added animated VDB sequence playback with density and heat-grid selection, stable bounds, timeline control, export synchronization, and Z-up conversion.
- Made scenes more portable through embedded cooked payloads, texture deduplication, library links, relink/reimport workflows, and missing-source recovery.
- Expanded scatter authoring, Scene panel hierarchy tools, Frame Selected, and discoverable keyboard-shortcut help.
- Improved load performance and renderer reliability with batched GPU texture uploads, interrupted-cook recovery, bounded GPU waits, and wavefront ABI cleanup.

## Asset Manager And Registry

- Added stable 128-bit Asset IDs independent of display names and folder paths.
- Added versioned JSON registry persistence with atomic writes.
- Added writable user-library paths and dedicated metadata, cache, pack, and thumbnail locations.
- Added nested virtual folders, rename and delete operations, tags, favorites, recent assets, search, type filters, and missing/failed views.
- Added source availability, cook-state, dependency, attribution, and runtime-readiness diagnostics.
- Added a Qt Asset Manager dock with grid browsing, adjustable thumbnails, inspector details, source reveal, and drag-and-drop payloads.
- Added change notifications so browser state updates as imports and cook jobs complete.
- Added clearer library summaries and toolbar controls for importing, packing, mounting, and cleanup.

## Cooking And Runtime Assets

- Added versioned cooked payloads for models, materials, textures, and volumes.
- Connected model and texture imports to the asset registry and cooked cache.
- Added material and texture dependency tracking by Asset ID.
- Added background cook jobs with pending/current/stale/failed states and progress reporting.
- Added atomic batch cooking so multi-output imports do not publish partial results.
- Added staged uncompressed payloads followed by controlled recompression.
- Added cooked-file header validation and interrupted-write recovery.
- Added dependency-aware runtime readiness checks.
- Added batched GPU texture uploads to reduce scene-load submission and synchronization overhead.
- Preserved existing valid cooked data while updated sources are being processed where possible.

## Scatter Integration

- Added scatter object and preset asset support.
- Added Asset Manager drag-and-drop into scatter workflows.
- Preserved source orientation, scale, pivots, mesh relationships, and material links.
- Added scene persistence for expanded scatter distribution, camera-distance, fade, collision, and light-avoidance settings.
- Added camera-aware scatter cache invalidation and more targeted refresh paths.
- Added weighted triangle selection improvements for large scatter populations.
- Added baking generated scatter instances into ordinary scene nodes.
- Kept source material changes connected to rendered scatter content.

## `.prpak` Asset Packs

- Added a versioned, unencrypted Project Render asset-pack format.
- Added per-entry metadata, dependency records, offsets, sizes, hashes, and checksums.
- Added payload deduplication by content hash.
- Added random-access reading without unpacking an entire archive.
- Added pack creation with transitive dependency collection.
- Added corruption detection and command-line pack validation.
- Added read-only mounting for built-in, user, and third-party packs.
- Added saved mount lists and mounted-pack browsing in the Asset Manager.
- Kept favorites as user-local metadata so favoriting never modifies a pack.

## VDB And Volume Assets

- Added OpenVDB import and grid inspection.
- Added explicit density/fog and optional temperature/flame/heat channel selection.
- Added conversion into Project Render's sparse bricked runtime volume payload.
- Added level-set-to-fog conversion for renderable level-set sources.
- Added volume dimensions, active-voxel counts, brick statistics, and temperature statistics.
- Added animated VDB sequence detection, background cooking, playback modes, frame offsets, looping, and source FPS handling.
- Added sequence-wide bounding-box overrides so animated volumes keep a stable spatial anchor.
- Added deterministic export-frame preparation so camera animation and Timeline-mode VDB frames agree before accumulation begins.
- Added automatic Z-up to Y-up placement for applicable heat volumes while preserving existing Y-up assets.
- Added density normalization, heat emission controls, volume light proxies, and improved export accumulation stability.

## Scene Portability And Relinking

- Added embedded cooked volume payloads so saved scenes can render without the original VDB source.
- Added model Asset ID links while retaining self-contained scene geometry.
- Added texture save deduplication and material-index remapping.
- Added relink and reimport actions for scene nodes connected to library assets.
- Preserved source paths, import settings, and library identity for optional future updates.
- Improved direct scene imports so hierarchical import roots retain material and library linkage to child meshes.
- Added missing-library and missing-source handling without silently discarding scene content.

## Editor And Workflow

- Reworked the Scene panel into a more readable hierarchy-oriented outliner.
- Added branch visibility controls and alternating-row readability.
- Added Frame Selected from the toolbar and the `F` shortcut with text-entry guards.
- Added a Help-menu keyboard-shortcuts dialog documenting editor, viewport, selection, and render bindings.
- Improved material assignment by supporting viewport-position drops.
- Improved Asset Manager cooking indicators and image thumbnails.
- Restricted camera controls to the focused 3D viewport.

## Rendering, Loading, And Reliability

- Added bounded GPU queue waits and safer renderer synchronization.
- Consolidated wavefront guide data into the hit-record ABI and updated the shader/runtime contract.
- Improved wavefront shadow and visibility handling.
- Improved scene-load performance through batched texture uploads.
- Added cancellation and cleanup for staged GPU upload work.
- Improved native path handling across asset import and recook operations.
- Added deterministic volumetric jitter and export integration improvements.
- Added Release-mode asset-registry coverage for registry, cooking, volume payloads, packs, corruption handling, dependency readiness, and texture deduplication.

## Notes

- This release covers Asset Manager roadmap phases 1-6. The Phase 7 starter asset pack is not included yet.
- `.prpak` files are intentionally unencrypted and are designed for distribution, validation, and random-access runtime loading.
- Saved scenes remain self-contained where supported; library links are retained for organization, relinking, and reimport rather than being the only copy.
- VDB is treated as an authoring format. Cooked volume assets use Project Render's runtime payload instead of reparsing VDB data every time.
- Background cooking can continue while the application is open, and incomplete staged outputs are validated before reuse.
