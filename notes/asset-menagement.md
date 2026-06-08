# Project Render Asset Management

## Status

Design proposal for a built-in Asset Manager, asset cooking pipeline, and
distributable `.prpak` format.

The spelling of this filename is intentional so it matches the tracked feature
name requested for this note.

## Goals

- Provide an in-engine library for models, materials, textures, scatter
  objects, cloud assets, HDRIs, and presets.
- Allow users to create folders, subfolders, favorites, tags, and searchable
  collections.
- Let users import their own authoring files and use them without manually
  arranging runtime data.
- Ship a small, legally redistributable starter library for grass and clouds.
- Keep saved `.prs` scenes portable when original source files or asset
  libraries are missing.
- Support sharing asset libraries through unencrypted `.prpak` archives.
- Cook authoring formats into predictable, efficient runtime representations.

## Non-Goals

- Asset encryption or DRM.
- Hiding files from users.
- Requiring every user asset to be packed before it can be used.
- Loading raw FBX, VDB, or other heavy authoring formats repeatedly at runtime.
- Replacing `.prs` scene persistence with external asset references.
- Turning the source `assets` directory into an unstructured runtime dumping
  ground.

## Core Design

The Asset Manager is the user-facing browser. It sits above an asset registry,
importers, cookers, a cooked cache, and optional `.prpak` archives.

```text
Authoring Sources
FBX / glTF / images / DDS / EXR / HDR / VDB
        |
        v
Importer + Asset Cooker
        |
        +--> Asset Registry and Metadata
        |
        +--> Cooked Cache
        |
        +--> Optional .prpak Export
        |
        v
Runtime Asset Manager
        |
        +--> Scene
        +--> Materials
        +--> Scatter
        +--> Clouds
```

The registry is the source of library organization and identity. A `.prpak` is
a read-only or mounted distribution container, not the live Asset Manager
database.

## Library Sources

The Asset Manager should present several sources in one browser.

### Built-in Assets

- Shipped with Project Render as one or more read-only `.prpak` archives.
- Contains the starter grass, cloud, material, and preset library.
- Updated with application releases.
- Must never be modified directly by users.

### User Assets

- Stored in a user-configurable library directory.
- The default should be under the user's Documents or application data area,
  not the protected installation directory.
- Supports real folders and subfolders.
- Contains user source files, metadata, and references to cooked cache entries.
- Survives application upgrades and reinstalls when the user data directory is
  preserved.

### Project Assets

- Optional library stored beside or underneath the active project.
- Intended for assets that belong to one project but are not yet embedded in a
  scene.
- Can be moved with the project directory.
- May override display names or organization without changing global assets.

### Mounted Packs

- User-created or third-party `.prpak` files.
- Mounted read-only by default.
- Can be browsed, searched, favorited, and used like built-in assets.
- May be unpacked or duplicated into User Assets when editing is required.

## Proposed Directory Layout

```text
Project Render User Data/
  Assets/
    Models/
    Materials/
    Textures/
    Scatter/
    Clouds/
    HDRI/
    Presets/
  Metadata/
    asset-registry.db
    favorites.json
  Cache/
    Meshes/
    Textures/
    Volumes/
    Thumbnails/
  Packs/
```

The exact user data root should use the platform's standard writable location.
The repository `assets/` directory remains for application-owned source data
and build inputs.

## Asset Identity

Every registered asset needs a stable `AssetId`, independent of its display
name and folder path.

Recommended identity data:

- Stable 128-bit UUID.
- Asset type.
- Display name.
- Virtual library path.
- Source path, when one exists.
- Source content hash and modification timestamp.
- Cooker version.
- Cooked payload hash.
- Dependency Asset IDs.
- Tags.
- Thumbnail reference.
- Import settings.
- License and attribution metadata.

Moving or renaming an asset must not change its `AssetId`. Content hashes
should detect source changes and drive recooking, but should not replace the
stable identity used by scenes and favorites.

## Folders, Tags, and Favorites

- User libraries use real filesystem folders where practical.
- Packs expose virtual folders from their internal index.
- Tags provide organization across folders and packs.
- Favorites are user-local metadata keyed by `AssetId`.
- Favoriting an asset must not modify or rebuild a `.prpak`.
- Missing favorites should remain recorded and display as unavailable until
  the asset returns or the user removes the entry.
- Search should cover name, type, folder, tags, pack, and attribution.

## Asset Types

Initial asset types:

- Model
- Material
- Texture
- Scatter Object
- Scatter Preset
- Cloud Volume
- Cloud Preset
- HDRI
- Environment Preset

Later candidates:

- IES profile
- Decal
- Light preset
- Camera preset
- Render preset
- Material function or layer

## Import and Cooking

Authoring files remain useful for editing and reimport. Runtime data should be
cooked once and cached.

### Models

Model cooking should produce:

- Renderer-native vertex and index streams.
- Mesh bounds and hierarchy.
- Material slot mapping.
- Tangents and normals using the selected import policy.
- Coordinate-system and unit conversion baked consistently.
- Per-mesh local transforms and pivots.
- Optional mesh optimization and LOD data.
- Dependency references to textures and materials.

FBX and glTF remain import sources. The runtime should prefer cooked geometry
after the first successful import.

### Textures

Texture cooking should produce:

- Correct color-space metadata.
- Mip chains.
- GPU-ready compression selected by texture semantic.
- Normal-map convention metadata.
- Alpha and opacity coverage handling where needed.
- Thumbnail or preview data.

Original PNG, TIFF, EXR, HDR, DDS, or other image files can remain available
for reimport, but rendering should use the cooked representation.

### Materials

Material assets should store Project Render material parameters and Asset ID
references to texture dependencies. They should not depend on transient global
texture indices.

### Cloud Volumes

VDB is an authoring and interchange format. Cloud cooking should convert the
selected grid into Project Render's runtime volume format.

The runtime format should eventually support:

- Sparse or bricked density storage.
- Quantized GPU-friendly voxel data where quality permits.
- Empty-brick removal.
- Bounds and voxel transform.
- Mip or level-of-detail representation.
- Compression and per-brick checksums.
- Optional additional channels when the cloud renderer uses them.

Runtime VDB parsing should not be required for a cooked or packed asset.

### Scatter Assets

A scatter object asset should capture:

- One or more model Asset IDs.
- Relative transforms and orientation.
- Per-object probability.
- Scale and rotation ranges.
- Material dependencies.
- Ground alignment policy.

A scatter preset should additionally capture density, edge trim, mesh
clearance, instance spacing, seed behavior, and other Scatter panel settings.

## Cooked Cache

The cooked cache is generated data and can be deleted safely.

- Cache keys combine source content hash, import settings, cooker version, and
  target runtime format.
- Cache writes should be atomic.
- Invalid or interrupted entries should be detected and rebuilt.
- Cache entries should include checksums and format versions.
- The registry should report whether an asset is current, stale, missing, or
  failed.
- Recooking should occur in background jobs with visible progress and errors.
- Runtime use should continue with the previous valid cooked version while a
  changed source is recooked, when possible.

## `.prpak` Format

`.prpak` is Project Render's distributable asset pack.

Required properties:

- No encryption or DRM.
- Versioned header and table of contents.
- Stable Asset IDs.
- Per-entry type, offset, compressed size, uncompressed size, and checksum.
- Dependency table.
- Pack-level metadata and attribution.
- Random access without unpacking the complete archive.
- Compression selected by payload type.
- Corruption detection.
- Duplicate payload elimination by content hash.
- Forward-compatible optional sections.

Large assets should be independently addressable and compressed in chunks so
loading one texture or volume does not require decompressing the entire pack.

Possible logical structure:

```text
PrPakHeader
AssetIndex[]
DependencyIndex[]
StringTable
MetadataBlocks
PayloadChunks[]
Footer/IntegrityData
```

The exact binary ABI must be documented and tested before packs are considered
stable across public releases.

## Scene Persistence

Saved scenes must remain usable when:

- A source FBX, texture, VDB, or pack is moved.
- A user library is not mounted.
- The scene is opened on another computer.
- The original authoring files are deleted.

Therefore:

- `.prs` continues to embed all asset payloads required to render the scene.
- Asset IDs, source information, and import settings are retained for optional
  relinking and reimport.
- External library references improve deduplication and editing but are not the
  only copy required for scene playback.
- Saving should deduplicate identical embedded payloads.
- Opening a scene should prefer its embedded data for correctness.
- Relinking to a library asset must be an explicit operation when content
  differs.

This follows the existing self-contained persistence requirement used for
imported meshes and IES profiles.

## Asset Manager UI

Add an Asset Manager panel to the Qt editor.

### Main Browser

- Library source tree.
- Folder tree with create, rename, move, and delete operations where writable.
- Grid and list views.
- Adjustable thumbnail size.
- Search field.
- Type and tag filters.
- Favorites view.
- Recent assets view.
- Missing and failed assets view.

### Asset Inspector

- Name and type.
- Source and cooked status.
- Asset ID.
- Dimensions, triangle count, texture resolution, or volume statistics.
- Dependencies.
- Import settings.
- Tags.
- License and attribution.
- Reimport and recook actions.
- Open source location.

### Interaction

- Drag a model into the scene.
- Drag a material onto a scene object.
- Drag a texture into a compatible material slot.
- Drag models or scatter objects into the Scatter panel.
- Drag cloud assets or presets into the Clouds panel.
- Double-click an asset to inspect or instantiate it according to type.
- Context menu actions for duplicate, reveal, reimport, recook, pack, and
  remove.

## User Pack Creation

Users should be able to:

1. Select assets or a folder.
2. Choose `Create Asset Pack`.
3. See all transitive dependencies.
4. Review licensing and missing dependency warnings.
5. Choose an output `.prpak`.
6. Build and validate the pack.

Pack creation should never alter the source library. A pack is a snapshot of
the selected assets and dependencies.

## Starter Asset Library

Project Render should ship a compact, high-quality starter pack containing
assets that are legally redistributable.

Initial grass content:

- Several distinct grass clumps.
- Upright pivots and real-world scale.
- Base color, normal, roughness, opacity, and optional translucency maps.
- Material variants.
- Scatter object definitions.
- A few practical scatter presets.

Initial cloud content:

- Base shape volumes.
- Detail volumes.
- Weather and coverage maps.
- Several cloud presets.
- Clear attribution and license metadata.

Optional small additions:

- Basic rocks.
- Ground materials.
- Neutral HDRIs.
- Demonstration scatter presets.

The starter pack should teach the workflow without trying to become a complete
commercial asset library. Users are expected to import or author additional
content.

## Reliability and Diagnostics

The Asset Manager should expose real state rather than silently falling back.

Useful states and diagnostics:

- Source available or missing.
- Cooked payload available, stale, corrupt, or failed.
- Pack mounted or unavailable.
- Dependency count and missing dependencies.
- Last import or cook error.
- Cooker and payload versions.
- CPU and GPU memory estimates.
- Triangle, texture, and voxel counts.

Pack validation should be available both in the UI and through a command-line
tool for release and automated testing.

## Implementation Boundaries

The existing asset loader remains responsible for decoding authoring formats.
New responsibilities should be separated as follows:

- `AssetRegistry`: identity, metadata, folder mapping, tags, and lookup.
- `AssetImporter`: source format decoding and dependency discovery.
- `AssetCooker`: conversion into versioned runtime payloads.
- `AssetCache`: generated payload storage and invalidation.
- `PrPakReader` / `PrPakWriter`: archive serialization and mounting.
- `AssetRuntime`: resolves Asset IDs into runtime mesh, material, texture, and
  volume resources.
- `AssetManagerPanel`: Qt browsing and editing interface.

Scene, Material, Scatter, and Cloud systems should consume resolved runtime
assets through explicit APIs. They should not parse the registry database or
pack file directly.

## Implementation Roadmap

### Phase 1: Registry and Browser Foundation

- Define `AssetId`, asset types, metadata, and dependency records.
- Choose and implement the registry persistence format.
- Add writable user-library location handling.
- Add folder, tag, favorite, search, and missing-state behavior.
- Build the initial Qt Asset Manager panel.
- Add thumbnail cache infrastructure.

### Phase 2: Model, Material, and Texture Cooking

- Connect existing model and texture import paths to the registry.
- Define versioned cooked mesh and texture payloads.
- Persist material dependencies by Asset ID.
- Add background import and recook jobs.
- Integrate drag-and-drop with Scene and Materials.

### Phase 3: Scatter Integration

- Add scatter object and scatter preset asset types.
- Preserve imported orientation, scale, pivots, and material links.
- Add drag-and-drop into the Scatter panel.
- Ensure source material edits invalidate rendered scatter instances.

### Phase 4: `.prpak`

- Specify and document the binary format.
- Implement reader, writer, checksums, compression, and random access.
- Mount built-in and user packs in the registry.
- Add pack creation and validation UI.
- Add command-line pack validation.

### Phase 5: Cloud and VDB Cooking

- Add VDB importer support.
- Define Project Render's sparse/bricked runtime volume format.
- Implement volume cooking, previews, and statistics.
- Integrate cloud assets and presets with the Clouds panel.

### Phase 6: Scene Portability

- Embed cooked dependencies into `.prs`.
- Deduplicate embedded payloads.
- Preserve source/reimport metadata.
- Add missing-library and relink workflows.
- Test scenes after deleting all original source files.

### Phase 7: Starter Pack and Release

- Curate redistributable grass and cloud assets.
- Normalize scale, pivots, materials, and texture semantics.
- Add attribution metadata.
- Build `starter.prpak`.
- Test fresh installation, pack mounting, drag-and-drop, scene save/load, and
  missing-source recovery in Release.

## Acceptance Criteria

- Users can create nested asset folders and organize assets without changing
  stable Asset IDs.
- Favorites survive restart and do not modify packs.
- Imported models and textures are cooked once and reused.
- A user can drag a grass asset into Scatter with correct orientation and
  materials.
- Changing a source material updates all scatter instances using that source.
- A user can create, validate, mount, and use an unencrypted `.prpak`.
- A `.prs` scene renders correctly after its source files and original pack are
  removed.
- Cloud assets load from the cooked runtime representation without requiring
  VDB parsing.
- Corrupt packs and missing dependencies produce visible, actionable errors.
- Built-in assets are read-only while user and project libraries remain
  editable.
- All renderer verification is performed using Release builds.

## Open Decisions

- Registry persistence: SQLite versus a compact versioned metadata database.
- Compression libraries and payload-specific compression policy.
- Exact cooked mesh, texture, and sparse-volume binary layouts.
- Whether Project Assets live beside a `.prs` file or require a project
  container concept.
- How source control workflows should handle registry metadata and generated
  cache data.
- Whether packs can contain optional source files for editable distribution.
- Thumbnail rendering ownership and background GPU scheduling.
- Public stability policy for `.prpak` across Project Render versions.

