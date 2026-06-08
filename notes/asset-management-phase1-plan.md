# Asset Management — Phase 1 Implementation Plan

Foundation only: registry, identity, metadata, folders/tags/favorites/search,
writable user-library location, the Qt Asset Manager panel, and thumbnail-cache
scaffolding. **No** cooking, `.prpak`, VDB, or scene-portability work in this
phase — those are Phases 2–6 and attach to the spine built here.

Decisions locked for this phase:

- Registry persistence: **JSON** (`asset-registry.json`), versioned schema,
  atomic writes. No new third-party dependency.
- New code lives under a new namespace `assetlib` in `src/asset_library/`, kept
  separate from the existing `Asset::` loader namespace (which keeps decoding
  authoring formats, per "Implementation Boundaries").

## Design constraints honored

- The existing `Asset::` loader (`src/assets/asset_loader.*`) remains the only
  thing that decodes glTF/OBJ/STL/SKP/images. The registry never parses
  authoring formats itself — it records identity + metadata and points at
  sources.
- Scene/Material/Scatter systems are **not** modified in Phase 1. The registry
  is additive and isolated; nothing in the render path depends on it yet.
- All UI is Qt (`src/qt/`), matching the doc's "Add an Asset Manager panel to
  the Qt editor."

## New files

### Core (engine-side, Qt-free)

| File | Responsibility |
|------|----------------|
| `src/asset_library/asset_id.h` | 128-bit `AssetId` value type (two `uint64_t`), generation (random v4-style), parse/format as 32-char hex, equality, hash for use as map key. |
| `src/asset_library/asset_types.h` | `enum class AssetType` (Model, Material, Texture, ScatterObject, ScatterPreset, CloudVolume, CloudPreset, Hdri, EnvironmentPreset) + name/parse helpers. Matches the doc's "Asset Types" list. |
| `src/asset_library/asset_metadata.h` | `AssetMetadata` struct: id, type, displayName, virtualPath, sourcePath, sourceContentHash, sourceTimestamp, cookerVersion, cookedPayloadHash, dependencies (`std::vector<AssetId>`), tags, thumbnailRef, importSettings (opaque JSON string for now), license/attribution, and a `SourceState`/`CookState` enum pair (Available/Missing/Stale/Failed). Mirrors the doc's "Asset Identity" list. Cooking-specific fields are present but unused until Phase 2. |
| `src/asset_library/asset_paths.h/.cpp` | Resolves the user-data root and the `Assets/ Metadata/ Cache/ Packs/` layout from the doc. Core side takes the root as a parameter; the actual platform location is supplied by the Qt layer (`QStandardPaths::AppDataLocation`) so the core stays Qt-free and testable. Creates the directory tree on first use. |
| `src/asset_library/asset_registry.h/.cpp` | The heart of Phase 1. In-memory store keyed by `AssetId`; load/save `asset-registry.json` (atomic: write temp + rename); CRUD (`add/remove/update/get`); virtual-folder operations (create/rename/move/delete affecting `virtualPath`); tag add/remove; **favorites** persisted separately to `favorites.json` keyed by `AssetId` (so favoriting never touches asset records or — later — packs); search over name/type/folder/tags/attribution; missing-state computation (source exists? → `SourceState`). Emits a simple change-callback list so the panel can refresh (same pattern as the scene change-listener used by `ScatterPanel`). |
| `src/asset_library/thumbnail_cache.h/.cpp` | Scaffolding only: maps `AssetId` → cached PNG path under `Cache/Thumbnails/`, get-or-miss API, and a placeholder generator that writes a typed placeholder image. Real GPU thumbnail rendering is deferred (listed as an open decision in the doc); the API is shaped so Phase 2+ can plug a renderer in without callers changing. |
| `src/asset_library/json_util.h` | Tiny JSON read/write helpers. **First check** whether the repo already vendors a JSON lib (tinygltf pulls in `nlohmann/json` or `rapidjson` under `_deps`/`thirdparty`) and reuse it; only add a minimal writer if nothing is available. Resolved before coding starts. |

### Qt UI

| File | Responsibility |
|------|----------------|
| `src/qt/AssetManagerPanel.h/.cpp` | The browser. Left: library-source tree (Built-in / User / Project / Mounted Packs — only User is functional in Phase 1, others shown as empty/disabled placeholders) + folder tree with create/rename/move/delete on the writable User library. Center: grid/list view with adjustable thumbnail size, double-click to inspect. Right: Asset Inspector (name, type, AssetId, source/cooked status, dependencies, tags, license, "Open source location", "Reveal"). Top: search field + type/tag filters; Favorites / Recent / Missing toggle views. Drag-and-drop **out** of the panel is stubbed in Phase 1 (wired to Scene/Scatter/Clouds in Phases 2/3/5). Holds a pointer to the shared `assetlib::AssetRegistry`. |

### Tests

| File | Responsibility |
|------|----------------|
| `src/tests/asset_registry_tests.cpp` | Headless unit tests (matching whatever harness `src/tests/` already uses): AssetId round-trip/uniqueness; registry add/get/remove; JSON save→load round-trip preserves all metadata; folder rename updates virtualPaths; favorites persist independently and survive a missing asset; search matches across fields; missing-state flips when a source file is deleted. These don't need the renderer, so they satisfy verification without a Release GPU run. |

## Edited files

| File | Change |
|------|--------|
| `CMakeLists.txt` | Add the new `src/asset_library/*.cpp` and `src/qt/AssetManagerPanel.cpp` to the existing source lists (alongside the `src/qt/*Panel.cpp` block at lines ~152–165); add the test file to the test target. |
| `src/qt/MainWindow.cpp` | In `createDocks()` (≈line 1542), add an `"Assets"` `QDockWidget` wrapping `AssetManagerPanel`, registered via the existing `registerDockPanel()` helper — exactly like the Scatter dock at lines 1564–1567. Construct/own the single `assetlib::AssetRegistry` instance here (or in `main.cpp`) and hand it to the panel. Resolve the user-data root with `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)` and pass it to `asset_paths`. |
| `src/qt/MainWindow.h` | Forward-declare `AssetManagerPanel`; add member if the registry is owned here. |

No changes to `scene*.cpp`, `scatter.cpp`, `material*`, importers, or shaders in
Phase 1.

## Sequencing (smallest reviewable steps)

1. Resolve the JSON-lib question (reuse vs. minimal writer).
2. `asset_id.h`, `asset_types.h`, `asset_metadata.h` + tests for AssetId.
3. `asset_paths`, then `asset_registry` (CRUD + JSON + folders/tags) + tests.
4. `favorites` + search + missing-state + tests.
5. `thumbnail_cache` scaffolding.
6. `AssetManagerPanel` (read-only browse first, then folder edit ops).
7. CMake + MainWindow wiring; build; run Release to confirm the dock appears and
   a manually-seeded registry browses correctly.

## Verification

- Unit tests (step-by-step above) run headless — primary correctness gate.
- Release build + launch: confirm the "Assets" dock registers, the User library
  root is created under AppData, folders create/rename/delete, favorites persist
  across restart, and search/missing-state behave. (Per the project rule, the
  manual pass is done in a Release build.)

## Explicitly deferred (later phases, not this PR)

- Cooking (mesh/texture/material/volume), cooked cache invalidation — Phase 2/5.
- `.prpak` reader/writer/mounting + built-in pack — Phase 4 / 7.
- Drag-and-drop into Scene/Scatter/Clouds actually instantiating — Phase 2/3/5.
- `.prs` embedding/relink/portability rework — Phase 6.
- Real GPU-rendered thumbnails — open decision; placeholder for now.

## Open items to confirm during implementation

- Which JSON facility already exists in the tree to reuse.
- Whether the registry instance is owned by `MainWindow` or `main.cpp` (prefer
  a single shared instance accessible to future panels).
- Test harness style in `src/tests/` (to match it rather than introduce a new
  one).
