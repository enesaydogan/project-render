# 3ds Max LiveLink Material Fidelity Plan

## Purpose

This document defines the implementation plan for improving 3ds Max LiveLink
material extraction and synchronization.

The work covers:

- meshes with no assigned material, using the 3ds Max scene object color;
- 3ds Max Standard material extraction;
- 3ds Max Physical Material extraction;
- bitmap and procedural map support;
- V-Ray material support;
- V-Ray map support;
- compound and layered material translation;
- stable material identity and incremental synchronization;
- diagnostics for unsupported or approximate conversions;
- Release build and fixture-scene verification.

The goal is not to reproduce every renderer-specific algorithm inside Project
Render. The goal is to translate supported 3ds Max and V-Ray shading graphs
into Project Render's canonical material model with predictable fidelity,
clearly defined approximations, and no silent feature loss.

## Production Rules

All work in this plan follows the repository rules in `AGENTS.md`.

- Do not present stubs as completed support.
- Do not disable material features to make synchronization appear successful.
- Do not silently replace unsupported graphs with arbitrary child textures.
- Do not put DCC-specific behavior into renderer shading stages.
- Keep 3ds Max and V-Ray interpretation inside the provider-side translation
  layer.
- Keep the engine-facing payload provider-neutral.
- Preserve shared material identity and Multi/Sub-Object face assignments.
- Version binary payload changes and retain backward compatibility where
  practical.
- Build the renderer and both Max plugins in Release configuration.

## Existing Architecture

### 3ds Max provider

The 2024 and 2025 providers currently live in:

- `tools/3dsmax2024/src/max_livelink_utility.cpp`
- `tools/3dsmax2025/src/max_livelink_utility.cpp`

These files are effectively the same implementation with version-specific
provider names. They currently own:

- scene traversal;
- persistent scene, node, and material identifiers;
- mesh capture and `.prmesh` writing;
- material snapshot extraction;
- embedded texture capture;
- V-Ray parameter extraction;
- material library writing;
- dirty tracking and incremental deltas.

Before adding more material support, shared material extraction should move
into common source files used by both plugin projects. Maintaining thousands
of duplicated lines is too risky once material and map adapters expand.

### LiveLink material payload

The canonical engine-facing payload is:

- `src/livelink/livelink_types.h`
- `LiveLink::MaterialChangedPayload`

It already supports a useful OpenPBR-style subset:

- base color;
- opacity;
- emission;
- roughness and metalness;
- reflection/specular color and weight;
- IOR and transmission;
- thickness and attenuation distance;
- coat;
- anisotropy;
- sheen;
- thin-walled and translucency controls;
- normal, coat-normal, occlusion, emission, specular, and thickness textures;
- packed or split metalness and roughness/glossiness textures;
- UV scale and offset;
- tri-planar mapping;
- alpha mode and cutoff;
- double-sided shading.

### Engine material translation

The provider-neutral payload is applied in:

- `src/material/material_livelink.cpp`
- `src/material/material_system.cpp`
- `src/assets/asset_loader.h`

The engine material model already has fields that the Max exporter does not
fully populate, including:

- separate metalness and roughness/glossiness textures;
- texture amounts;
- normal-map Y flipping;
- bump-map interpretation;
- UV rotation;
- separate authoring workflow selection;
- coat normal;
- material-class inference.

Most initial work should therefore improve Max-side extraction and payload
transport. Renderer changes are required only when a translated feature has no
existing runtime representation.

## Current Behavior and Gaps

### Material extraction

The current generic extraction uses `Mtl` getters for:

- diffuse color;
- transparency;
- shininess;
- shininess strength;
- specular color;
- self-illumination.

It then searches material sub-texture slots by display-name tokens such as
`diffuse`, `rough`, `normal`, or `opacity`.

This gives broad compatibility but has several problems:

- localized slot names can break classification;
- unrelated maps can be assigned to the wrong engine slot;
- Standard and Physical Material semantics are not extracted explicitly;
- enabled state and per-slot amount are not consistently preserved;
- reflection/glossiness and metal/roughness workflows can be conflated;
- bump maps and tangent-space normal maps are not reliably distinguished;
- one global UV transform is shared by all material textures.

### Procedural maps

Current procedural capture:

- recognizes several class-name families;
- asks the map for a 512 by 512 viewport display DIB;
- embeds the resulting pixels in the material library;
- recursively falls back to file-backed child maps;
- has special handling for tri-planar maps.

The current approach is useful but incomplete:

- viewport display output is not guaranteed to match render output;
- bake resolution is fixed;
- color/data interpretation is not recorded;
- output transformations may be lost or applied twice;
- mapping channel and UV rotation are not transported;
- object-, normal-, view-, ray-, and distance-dependent maps cannot be
  faithfully represented by a generic unit-square bake;
- layered graphs can silently collapse to a child map when baking fails.

### V-Ray

Current `VRayMtl` extraction covers part of:

- diffuse;
- reflection color and weight;
- reflection glossiness or roughness;
- metalness;
- refraction color and IOR;
- thickness and fog depth;
- self-illumination;
- coat amount, roughness, and IOR;
- anisotropy;
- sheen color;
- thin-walled and translucency settings;
- double-sided rendering;
- selected maps.

Important limitations remain:

- parameter IDs are duplicated locally and can drift between V-Ray versions;
- map amounts and enable states are not comprehensively extracted;
- metalness and roughness maps are collapsed into one packed slot;
- opacity, refraction, fog, coat, sheen, thin-film, and bump semantics are
  incomplete;
- compound V-Ray materials are not translated explicitly;
- many V-Ray maps are handled only by generic traversal or viewport baking;
- unsupported maps do not produce sufficiently actionable diagnostics.

### Meshes without materials

Nodes with no assigned material currently produce mesh geometry but no Max
material snapshot. The engine therefore uses placeholder/default material
behavior instead of the object's visible scene color.

The expected behavior is:

- use the object's 3ds Max wire/scene color as base color;
- create a deterministic node-scoped fallback material;
- bind it to slot 0;
- update it when the object color changes;
- remove or replace it cleanly when a real material is assigned.

## Target Translation Architecture

### Shared Max material module

Create a common source module consumed by both Max plugin projects:

```text
tools/3dsmax-common/material/
  max_material_types.h
  max_material_context.h
  max_material_extractor.h
  max_material_extractor.cpp
  max_standard_material_adapter.cpp
  max_physical_material_adapter.cpp
  max_vray_material_adapter.cpp
  max_compound_material_adapter.cpp
  max_texture_resolver.h
  max_texture_resolver.cpp
  max_texture_baker.h
  max_texture_baker.cpp
  max_material_diagnostics.h
  max_material_diagnostics.cpp
```

The exact file split may be adjusted during implementation, but ownership
should remain clear:

- material adapters interpret material parameters;
- the texture resolver interprets map graphs;
- the baker produces deterministic texture payloads;
- diagnostics report unsupported or approximate behavior;
- wire serialization remains separate from extraction.

### Extraction result

Each material adapter should produce one canonical provider-side snapshot.

The snapshot should not contain 3ds Max SDK types. It should contain:

- stable material identity;
- material model and source class;
- scalar and color parameters;
- typed texture bindings;
- node and slot references;
- conversion quality;
- warnings;
- dependency fingerprints.

### Typed texture binding

Replace the current URI/payload pairs plus one material-wide UV transform with
a typed texture-binding structure.

Proposed provider-side shape:

```cpp
enum class MaterialTextureSemantic {
  BaseColor,
  Opacity,
  Normal,
  Bump,
  Emission,
  Occlusion,
  Metalness,
  Roughness,
  Glossiness,
  SpecularColor,
  Transmission,
  Thickness,
  CoatWeight,
  CoatRoughness,
  CoatNormal,
  SheenColor,
  SheenRoughness
};

enum class TextureValueType {
  ColorSrgb,
  ColorLinear,
  ScalarLinear,
  NormalTangent,
  Height
};

struct TextureTransformSnapshot {
  int mapChannel = 1;
  float scale[2] = {1.0f, 1.0f};
  float offset[2] = {0.0f, 0.0f};
  float rotationDegrees = 0.0f;
  bool mirrorU = false;
  bool mirrorV = false;
  bool tileU = true;
  bool tileV = true;
};

struct MaterialTextureBindingSnapshot {
  MaterialTextureSemantic semantic;
  TextureValueType valueType;
  std::string sourceClass;
  std::string uri;
  EmbeddedTexturePayload payload;
  TextureTransformSnapshot transform;
  float amount = 1.0f;
  bool enabled = true;
  bool invert = false;
  bool flipGreen = false;
};
```

This is an authoring/interchange structure. Runtime material packing remains
the responsibility of `material_system`.

### Texture resolution outcomes

Every map resolution attempt should return one explicit outcome:

1. `Direct`
   - File-backed texture and supported transform.
2. `Translated`
   - Native Project Render representation, such as supported tri-planar
     mapping.
3. `Baked`
   - Deterministic 2D or object-aware baked texture.
4. `Constant`
   - Map evaluates to a constant value that can be folded into a parameter.
5. `Approximate`
   - Supported approximation with a diagnostic.
6. `Unsupported`
   - No safe representation; diagnostic emitted.

No resolver path should silently select an arbitrary child map and call the
graph supported.

## Coverage Policy

### Fidelity levels

Each material and map family should be assigned a support level:

- **Native**: semantics are represented directly in the engine.
- **Translated**: deterministic conversion into equivalent engine fields.
- **Baked**: graph is rendered to texture while preserving applicable mapping.
- **Approximate**: documented conversion with expected visual differences.
- **Unsupported**: retained as a diagnostic; no false success.

### Prioritization

Priority is based on common architectural visualization workflows:

1. no-material scene color;
2. Standard material;
3. Physical Material;
4. bitmap, output, normal/bump, color correction, mix, and composite maps;
5. full core `VRayMtl`;
6. common V-Ray bitmap, normal, tri-planar, randomization, and composition maps;
7. common V-Ray compound materials;
8. geometry-dependent maps;
9. specialized V-Ray materials.

## Phase 0: Baseline and Refactor

### Status

Implemented on June 14, 2026:

- Max 2024 and Max 2025 now compile the same shared LiveLink utility and
  material extraction implementation through small version-specific wrappers.
- Provider, source-application, and session identity remain version-specific.
- Material-family dispatch now distinguishes Standard, Physical, V-Ray, and
  generic materials using SDK type/class identity.
- Material and map source classes are captured for diagnostics and resume
  comparison.
- Texture resolution records direct, translated, baked, approximate, and
  unsupported outcomes.
- Full-resync diagnostics report counts for each texture conversion outcome.
- Unsupported generic map resolution and representative-child fallback now
  produce internal material diagnostics.
- The existing PMAT and JSON transport behavior remains unchanged.

### Tasks

- Move material extraction into shared Max 2024/2025 sources.
- Keep version-specific provider identity in the existing plugin entrypoints.
- Introduce adapter dispatch based on stable class ID and SDK interfaces.
- Use class-name matching only as a documented fallback.
- Add a material extraction result that includes diagnostics.
- Add counters for direct, translated, baked, approximate, and unsupported
  maps.
- Record source material and map class names in diagnostics.
- Add a content/dependency hash for each material snapshot.

### Acceptance criteria

- Max 2024 and Max 2025 produce byte-equivalent material payloads for the same
  fixture scene, excluding expected provider/session metadata.
- Existing shared material and Multi/Sub-Object behavior remains unchanged.
- Existing LiveLink resume state can still be loaded.
- Both Max plugins build in Release.

## Phase 1: Scene Color for Meshes Without Materials

### Required behavior

For a renderable mesh node where `INode::GetMtl()` returns null:

- read the node wire/scene color;
- convert the packed Max color to normalized RGB;
- create a node-scoped fallback material;
- use slot 0 for every face;
- set alpha to 1;
- set metalness to 0;
- use a neutral roughness, initially 0.8;
- use a moderate dielectric specular response;
- set material model to `MaxSceneColor`;
- set a deterministic stable ID derived from the persistent node ID.

The fallback must be node-scoped because scene color belongs to the node, not
to a shared `Mtl` object.

### Identity

Suggested stable ID:

```text
max-scene-color:<persistent-node-guid>
```

Suggested display name:

```text
<node name> [Scene Color]
```

### Incremental changes

The node geometry/material fingerprint must include:

- whether an explicit material is assigned;
- the scene color value when no material is assigned.

Changing scene color should emit a material change without requiring a mesh
payload rewrite unless bindings changed.

Assigning a real material should:

- bind the real material;
- stop referencing the fallback material;
- remove stale fallback tracking when no node references it.

Removing a real material should recreate or rebind the scene-color material.

### Acceptance scene

- three boxes with no material and different scene colors;
- two instances sharing geometry but with different node colors;
- one object that changes color while connected;
- one object that gains and loses a real material;
- save, close, reopen, and resume.

## Phase 2: 3ds Max Standard Material

### Adapter

Use explicit `StdMat` access when available. Do not make localized slot names
the primary source of slot semantics.

### Core parameters

Translate:

| Standard parameter | Project Render target | Policy |
| --- | --- | --- |
| Diffuse color | Base color | Native |
| Opacity | Base alpha/transmission policy | Translated |
| Filter color | Transmission color | Translated |
| Specular color | Specular color | Native |
| Specular level/strength | Specular weight | Translated |
| Glossiness | Roughness | Invert and clamp |
| IOR | IOR | Native |
| Self-illumination color | Emission color | Native |
| Self-illumination amount | Emission intensity | Translated |
| Two-sided | Double-sided | Native |
| Wire/face-map/faceted modes | Diagnostic | Unsupported initially |

### Shader models

Support the commonly exposed Standard shader models:

- Blinn;
- Phong;
- Metal;
- Anisotropic;
- Oren-Nayar-Blinn;
- Strauss;
- Translucent;
- Multi-Layer.

Initial translation policy:

- Blinn and Phong map to generic dielectric specular shading.
- Metal and Strauss can set or bias the metallic workflow when their
  parameters indicate metallic behavior.
- Anisotropic maps to anisotropy and rotation when accessible.
- Oren-Nayar diffuse roughness maps to the closest available diffuse/material
  roughness behavior.
- Translucent maps to thin-walled/translucency fields where appropriate.
- Multi-Layer uses the closest coat approximation and emits an approximation
  diagnostic.

The adapter must document parameter ranges and conversion formulas in code.

### Standard map slots

Extract by slot ID:

- diffuse;
- opacity;
- bump;
- specular color;
- specular level;
- glossiness;
- self-illumination;
- reflection;
- refraction;
- displacement, as diagnostic until displacement transport exists;
- filter color where exposed.

For each slot preserve:

- enabled state;
- amount;
- map reference;
- output settings;
- UV transform;
- map channel.

### Normal and bump handling

- A grayscale bump map must set `useBumpMap`.
- A Normal Bump wrapper should resolve its normal and bump children
  independently.
- Tangent-space normal maps must preserve green-channel orientation.
- Bump strength and normal strength must be transported separately or folded
  into the existing normal amount only when semantics match.

### Reflection and refraction maps

Project Render does not currently model arbitrary reflection environment maps
per material. Therefore:

- a reflection color/intensity map may feed specular color/weight only when it
  represents a surface parameter;
- environment/reflection-ray maps require an approximation diagnostic;
- refraction color maps may feed transmission color;
- distortion/refraction-ray behavior is not a 2D texture substitution.

### Acceptance scene

Create one object for every supported shader model and map slot. Include:

- animated opacity;
- two-sided foliage-like material;
- bump and normal maps;
- separate glossiness map;
- self-illumination map;
- Multi/Sub-Object material with sparse material IDs;
- shared Standard material across multiple nodes.

## Phase 3: 3ds Max Physical Material

### Rationale

Physical Material maps more directly to the engine's OpenPBR-style authoring
model than generic `Mtl` getters. It should have a dedicated adapter.

### Core parameters

Translate where exposed:

- base weight and base color;
- diffuse roughness;
- reflectivity;
- roughness and roughness inversion;
- metalness;
- IOR;
- transparency amount and color;
- transparency roughness;
- thin-walled mode;
- emission color and luminance/intensity;
- coating weight, color, roughness, and IOR;
- anisotropy and rotation;
- subsurface/translucency approximation;
- cutout;
- displacement as diagnostic.

### Texture slots

Prefer split texture semantics:

- base color;
- roughness or glossiness;
- metalness;
- opacity/cutout;
- normal;
- bump;
- emission;
- transmission;
- coat weight;
- coat roughness;
- coat normal.

Do not pack metalness and roughness in the Max provider. The engine already
has a derived packed-runtime path.

### Acceptance criteria

- Physical Material values match the Max UI at the current time.
- Roughness inversion is preserved.
- Separate metalness and roughness maps arrive in separate payload fields.
- Coat and transparency update incrementally.

## Phase 4: Bitmap and Procedural Map Pipeline

### Direct bitmap support

Support direct file binding for:

- 3ds Max Bitmap;
- `VRayBitmap`;
- file-backed OSL bitmap nodes when a stable source path can be resolved;
- common image formats already accepted by the engine.

Capture:

- canonical source path;
- embedded bytes when required for remote/session-independent use;
- source content hash;
- color/data interpretation;
- map channel;
- UV scale, offset, and rotation;
- tiling, mirroring, crop, and placement where representable;
- frame/time identity for sequences.

### Color management

Texture bindings must identify intended interpretation:

- sRGB color;
- linear color;
- scalar/data;
- tangent-space normal;
- height.

Base color and emission color maps normally use color decoding. Roughness,
metalness, opacity, masks, normals, and height maps must not receive color
transform treatment intended for display color.

The implementation must use Max's available color-management metadata instead
of guessing from filename whenever possible.

### Deterministic baking

Use the 3ds Max SDK texture rendering path for supported procedural graphs.
`Interface::RenderTexmap` supports rendering a texture tree to a bitmap and a
`bake=true` mode that excludes UV scale/rotation so those transforms can be
transported separately.

Replace viewport DIB baking as the primary path. A viewport DIB may remain
only as a clearly reported compatibility fallback for map plugins that cannot
use render baking.

### Bake settings

Add configurable settings:

- default color-map resolution: 1024;
- default scalar-map resolution: 1024;
- normal/height resolution: 1024;
- optional 2048 high-quality mode;
- maximum payload byte budget;
- filtering mode;
- current animation time;
- 2D bake versus object-aware bake.

Do not expose a setting until it is actually honored by the exporter.

### Bake cache key

The cache key should include:

- map class ID;
- stable map identity where available;
- source asset path and modification metadata;
- relevant parameter values;
- child-map hashes;
- current time for animated maps;
- bake resolution;
- value type/color interpretation;
- object identity for object-aware bakes;
- mapping mode.

### Standard procedural map priority

Tier 1:

- Checker;
- Noise;
- Cellular;
- Tiles;
- Gradient and Gradient Ramp;
- Mix;
- Composite;
- Falloff where it is UV/evaluable without view dependence;
- Color Correction;
- Output;
- RGB Multiply;
- Normal Bump.

Tier 2:

- Marble;
- Speckle;
- Dent;
- Smoke;
- Wood;
- Perlin Marble;
- Vertex Color;
- Map-to-Material wrappers;
- supported OSL maps.

### Context-dependent maps

The following cannot automatically be treated as ordinary UV textures:

- view-dependent falloff;
- camera-normal maps;
- world/object-position maps;
- distance-to-object maps;
- ray-type switches;
- lighting-dependent maps;
- ambient-occlusion/dirt maps;
- curvature and edge maps;
- object-ID and material-ID randomization.

Each needs one of:

- a native engine representation;
- object-aware baking;
- constant folding for the current object;
- an approximation;
- an unsupported diagnostic.

### Per-texture transforms

The current material-wide UV scale and offset are insufficient. Extend the
payload so each texture binding can carry its own:

- map channel;
- scale;
- offset;
- rotation;
- wrap/mirror policy.

Maintain legacy material-wide fields while reading older payloads. New
providers should populate the per-binding representation.

## Phase 5: Core VRayMtl Completion

### SDK strategy

Use official V-Ray SDK descriptors and parameter-block metadata where
available.

Avoid relying solely on a copied enum of parameter IDs. If a fixed ID is
required:

- isolate it behind a versioned adapter;
- validate the parameter's internal name and type;
- emit a diagnostic when validation fails;
- maintain separate tested mappings for supported V-Ray versions.

### Basic and diffuse

Support:

- diffuse color;
- diffuse map and amount;
- diffuse roughness;
- diffuse roughness map;
- OpenPBR mode where present.

### Reflection

Support:

- reflection color;
- reflection map and amount;
- reflection weight;
- Fresnel enable;
- reflection IOR;
- IOR lock behavior;
- glossiness or roughness mode;
- roughness/glossiness map and amount;
- metalness and metalness map;
- anisotropy;
- anisotropy rotation;
- BRDF type as a documented approximation where the engine BRDF differs;
- GGX/GTR tail controls as approximation diagnostics until represented.

### Refraction and volume attenuation

Support:

- refraction color;
- refraction map and amount;
- refraction IOR;
- refraction glossiness/roughness;
- fog color;
- fog multiplier/depth;
- thickness;
- thin-walled mode;
- affect-alpha policy;
- translucency amount and color;
- dispersion as unsupported or approximate until the renderer supports it.

Fog and attenuation conversion must be physically documented. Do not equate
an arbitrary V-Ray fog multiplier directly with meters without a verified
conversion.

### Coat

Support:

- coat amount;
- coat amount map;
- coat color;
- coat color map;
- coat glossiness/roughness;
- coat roughness map;
- coat IOR;
- coat normal/bump;
- coat bump amount;
- coat darkening as approximation or future engine field.

### Sheen

Support:

- sheen color;
- sheen color map;
- sheen amount derived from explicit weight where available;
- sheen glossiness/roughness;
- sheen map amounts.

### Thin film

Support:

- enable state;
- minimum/maximum thickness;
- thickness map;
- thin-film IOR.

This likely requires new engine material fields and shader work. It should not
be declared supported until raster and DXR paths both implement it.

### Emission and opacity

Support:

- self-illumination color;
- self-illumination map and amount;
- multiplier;
- compensate-camera-exposure policy;
- opacity map;
- opacity mode;
- alpha cutoff where applicable.

### Normal and bump

Support:

- main bump enable;
- main bump amount;
- normal-map wrapper interpretation;
- coat bump enable and amount;
- tangent-space orientation.

### Split workflows

Populate:

- `workflow = ReflectionGlossiness` for classic reflection/glossiness setups;
- separate metalness texture;
- separate roughness or glossiness texture;
- inversion flag only for a true glossiness texture.

Do not place a single-channel metalness map into a packed metal-rough texture
slot.

## Phase 6: Common V-Ray Maps

### Tier 1: direct or near-direct

#### VRayBitmap

Capture:

- file path or asset identity;
- color space;
- UV channel;
- tiling and placement;
- crop;
- sequence/frame behavior;
- filtering;
- alpha source;
- output multiplier.

#### VRayColor

- fold to a constant color when untextured;
- preserve multiplier and color-space interpretation;
- avoid generating a texture for a constant.

#### VRayNormalMap

- resolve wrapped bitmap/procedural input;
- mark tangent-space normal semantics;
- preserve green-channel flip;
- preserve strength;
- report unsupported object/world normal modes.

#### VRayTriplanarTex

The engine already has tri-planar support. Translate:

- texture source;
- world/object scale;
- blend amount/sharpness;
- normal handling;
- axis texture differences.

The current engine supports one source texture. If different textures are
assigned per axis:

- add a future multi-axis engine representation; or
- report an approximation using the primary axis texture.

Do not silently discard axis differences.

#### V-Ray color correction and output maps

- fold simple gain, offset, gamma, invert, and tint into a baked result;
- preserve scalar versus color interpretation;
- cache by full parameter state.

### Tier 2: graph composition and randomization

#### VRayCompTex

- bake the full layer graph;
- preserve blend modes and masks;
- do not fall back to the topmost child on bake failure.

#### VRayMultiSubTex

This map can depend on object, material, element, instance, or random IDs.

Policy:

- resolve per-node/per-material selection when deterministic;
- include selection-driving identity in the cache key;
- use object-aware bake when necessary;
- report animation/random-seed behavior.

#### VRayUVWRandomizer

Project Render does not currently transport arbitrary per-instance map
randomization.

Possible implementation order:

1. deterministic per-node transform folding;
2. per-instance UV variation fields in the material/instance data;
3. full stochastic behavior where renderer parity warrants it.

Until then, mark the conversion approximate.

#### VRayColor2Bump

- preserve height semantics;
- use engine bump-map interpretation;
- preserve amount and inversion.

### Tier 3: geometry-dependent maps

#### VRayDirt

`VRayDirt` depends on scene geometry and can represent ambient occlusion,
occlusion radius, distribution, and inclusion/exclusion rules.

Implementation choices:

- translate to a native AO/dirt material feature;
- bake per object with geometry context;
- report unsupported rules that cannot be represented.

A generic 2D bake is not sufficient.

#### Curvature, Distance, and Edge maps

These require geometry-aware evaluation. Add support only through:

- native renderer shading features; or
- object-aware texture baking tied to a mesh revision.

Any baked result must be invalidated when relevant geometry changes.

## Phase 7: V-Ray Compound Materials

### VRay2SidedMtl

Target behavior:

- identify front and back materials;
- use a native two-sided material representation if added;
- otherwise select the dominant/front material and emit an approximation;
- preserve translucency where it maps to the engine's thin-sheet model.

Setting only `doubleSided=true` is not equivalent when front and back materials
differ.

### VRayBlendMtl

Target behavior:

- base material plus coat layers and masks;
- flatten simple one-coat cases into base plus engine coat;
- bake color-only layered graphs when physically valid;
- require a future layered-material runtime for general cases;
- report unsupported blend modes or nested materials.

### VRayLightMtl

Translate:

- emission color;
- texture;
- multiplier;
- opacity where applicable;
- double-sided emission;
- compensate-camera-exposure behavior as a documented policy.

### VRayOverrideMtl

The renderer currently has one surface material per binding and does not model
separate GI, reflection, refraction, and shadow override materials.

Initial policy:

- use the base material;
- report every active override;
- add native override semantics only if renderer architecture adopts them.

### Later specialized materials

Evaluate after common archviz coverage:

- VRayFastSSS2;
- VRayCarPaintMtl;
- VRayFlakesMtl;
- VRaySwitchMtl;
- VRayMtlWrapper;
- VRayScannedMtl;
- V-Ray hair and skin materials.

Each requires a separate fidelity proposal. They should not be routed through
generic `VRayMtl` extraction merely because their class names contain `vray`
and `mtl`.

## Payload and Serialization Changes

### Native material library

The current PMAT material library is version 3. Add version 4 for:

- split metalness and roughness/glossiness textures;
- per-texture amount;
- per-texture value type;
- per-texture UV transform;
- map channel;
- UV rotation;
- bump versus normal semantics;
- green-channel flip;
- source map class;
- conversion outcome;
- optional diagnostic references.

### Backward compatibility

Engine reader behavior:

- continue reading versions 1 through 3;
- synthesize typed bindings from legacy fields;
- use material-wide UV scale/offset for legacy payloads;
- retain packed metal-rough behavior for old providers.

Provider behavior:

- write version 4 after the engine reader supports it;
- do not write partially initialized version 4 records;
- update JSON fallback deltas with the same semantics.

### ABI discipline

For every payload change update together:

- provider-side structures;
- binary writer;
- engine-side structures;
- binary reader;
- JSON delta writer/parser;
- resume-state serialization;
- material equality/fingerprint logic;
- diagnostics and counters;
- version constants.

## Material Identity and Binding

### Existing explicit materials

Continue using persistent material GUIDs stored on Max material objects.

### Shared materials

One Max material instance referenced by multiple nodes and slots must map to
one engine material with multiple references.

### Multi/Sub-Object materials

Preserve:

- face material IDs;
- sparse IDs;
- disabled or empty slots;
- stable leaf material IDs;
- slot-to-leaf bindings.

Reordering Multi/Sub slots must update references without creating duplicate
logical materials.

### Procedural dependencies

Material fingerprints must include all map dependencies that affect exported
results. A parent material does not remain clean if a child procedural map,
bitmap asset, output node, or nested material changes.

## Diagnostics

### Required diagnostic fields

Each warning should include:

- node name and persistent node ID;
- material name and stable ID;
- material class name and class ID;
- map name, class name, and class ID where applicable;
- material slot;
- texture semantic;
- conversion outcome;
- short reason;
- suggested user action when available.

### Severity

- `Info`: native or expected baked conversion.
- `Approximation`: supported with known visual differences.
- `Warning`: feature ignored or partially represented.
- `Error`: material/map could not be exported safely.

### UI summary

Extend LiveLink diagnostics with:

- materials extracted;
- direct textures;
- translated maps;
- baked maps;
- approximate maps;
- unsupported maps;
- bake cache hits/misses;
- total bake time;
- largest embedded texture payload;
- material extraction failures.

### No silent fallback

Examples that must generate diagnostics:

- different tri-planar textures per axis collapsed to one;
- view-dependent falloff baked as a static texture;
- `VRayDirt` ignored;
- active `VRayOverrideMtl` overrides ignored;
- unsupported displacement;
- unsupported material layers;
- failed procedural bake followed by child-map selection.

## Performance and Incremental Sync

### Avoid rebaking unchanged maps

Use content/dependency hashes and a persistent session cache.

### Debounce

Material edits can trigger many notifications while a spinner is dragged.
Debounce extraction enough to avoid repeated baking while preserving
interactive feedback.

Suggested behavior:

- scalar/color-only updates remain fast and incremental;
- expensive procedural baking waits for a short quiet period;
- the UI reports pending material extraction;
- a final update is guaranteed after interaction settles.

### Background work

Only move baking to background threads if the Max SDK APIs involved are
documented as safe for that use. Otherwise:

- capture/evaluate on Max's required thread;
- move encoding, hashing, and file writing off-thread;
- never call unsafe Max SDK objects from worker threads.

### Payload budget

Large scenes can contain many repeated baked maps.

- deduplicate texture blobs by content hash;
- reuse URI-backed textures;
- compress or encode baked textures appropriately;
- avoid embedding identical payloads for each material;
- expose transfer size in diagnostics.

## Fixture Scenes

Create versioned test scenes under an appropriate test-assets location.
Do not depend only on one large production scene.

### Required fixtures

1. `max-no-material-scene-color`
2. `max-standard-core`
3. `max-standard-maps`
4. `max-physical-core`
5. `max-procedural-2d`
6. `max-procedural-context-dependent`
7. `max-multisub-shared-materials`
8. `vraymtl-core`
9. `vraymtl-maps`
10. `vray-maps-common`
11. `vray-compound-materials`
12. `max-material-resume`

### Fixture controls

Each fixture should include:

- expected source values;
- expected Project Render material values;
- expected conversion outcome per map;
- expected diagnostics;
- expected material reference counts;
- incremental edits to perform;
- screenshots or numeric probes where visual comparison is required.

## Verification Strategy

### Static verification

- compile both plugin projects with `/W4`;
- run `git diff --check`;
- verify PMAT version reader/writer sizes;
- verify material equality includes every serialized field;
- verify resume-state serialization includes every extracted field;
- verify new map semantics are included in dependency hashes.

### Engine tests

Add focused tests where the repository test structure permits:

- LiveLink payload to `Asset::Material` translation;
- old PMAT version compatibility;
- PMAT version 4 round trip;
- texture semantic and transform round trip;
- material identity/reference merge;
- scene-color fallback identity;
- split metalness/roughness workflow;
- unsupported-feature diagnostic formatting.

### Live Max verification

For each phase:

1. open the fixture in Max;
2. start LiveLink;
3. confirm initial material state;
4. edit every supported parameter;
5. replace maps and change map parameters;
6. assign and remove materials;
7. reorder Multi/Sub slots;
8. disconnect and reconnect;
9. save Max and Project Render scenes;
10. reopen and resume;
11. confirm no duplicate materials;
12. confirm diagnostics match expected limitations.

Project Render UI automation is not part of this workflow. Use source tracing,
Release builds, logs, diagnostics, fixture captures, and user-reported
interactive observations.

### Visual comparison

For translated materials, compare:

- base color;
- highlight width;
- reflection strength;
- transparency;
- normal/bump direction;
- emission;
- coat;
- texture placement;
- procedural pattern scale.

Exact pixel parity with V-Ray is not the universal acceptance criterion
because the renderers use different shading implementations. The acceptance
criterion is correct parameter interpretation, stable conversion, and a
documented approximation where physical models differ.

## Release Build Gates

### Renderer

```powershell
cmake --build build --config Release --target project-render
```

### 3ds Max 2024 plugin

```powershell
cmake --build build-max2024 --config Release
```

### 3ds Max 2025 plugin

```powershell
cmake --build build-max2025 --config Release
```

All three Release builds are required before a phase is considered complete.

## Recommended Work Order

### Milestone A: correctness foundation

1. Extract shared Max material module.
2. Add typed diagnostics and conversion outcomes.
3. Add scene-color fallback material.
4. Add fixture and incremental checks for no-material nodes.

### Milestone B: Autodesk materials

1. Implement explicit Standard material adapter.
2. Implement typed Standard map-slot extraction.
3. Implement Physical Material adapter.
4. Send split metalness and roughness/glossiness textures.
5. Add per-texture transform transport.

### Milestone C: procedural maps

1. Add deterministic `RenderTexmap` baking.
2. Add content-addressed bake cache.
3. Implement Tier 1 Standard procedural maps.
4. Add color/data semantics.
5. Add context-dependent map diagnostics.

### Milestone D: VRayMtl

1. Replace brittle parameter lookup with validated version adapters.
2. Complete core scalar/color extraction.
3. Complete texture enable/amount extraction.
4. Complete coat, sheen, opacity, normal/bump, and fog translation.
5. Add thin-film engine support only as a separate raster/DXR feature.

### Milestone E: V-Ray maps and compound materials

1. VRayBitmap.
2. VRayNormalMap.
3. VRayTriplanarTex.
4. VRayColor and color-correction/output maps.
5. VRayCompTex and VRayMultiSubTex.
6. VRayUVWRandomizer.
7. VRay2SidedMtl and VRayLightMtl.
8. VRayBlendMtl and VRayOverrideMtl.
9. Geometry-dependent maps.

## First Implementation Slice

The first code change should be narrowly scoped and fully shippable:

### Scope

- add a no-material scene-color snapshot;
- bind it to slot 0;
- update dirty tracking for scene-color changes;
- preserve it through full sync, incremental sync, reconnect, and resume;
- add clear diagnostics indicating a scene-color fallback was created;
- keep Max 2024 and Max 2025 behavior identical.

### Definition of done

- differently colored no-material meshes match their Max scene colors;
- color edits update without mesh re-export;
- assigning a material replaces the fallback;
- removing a material restores the fallback;
- instanced geometry can have different node colors;
- no duplicate fallback materials are produced for the same node;
- renderer, Max 2024 plugin, and Max 2025 plugin build in Release;
- `git diff --check` passes.

After this slice, implement the shared extraction refactor and Standard
material adapter before expanding V-Ray coverage.

## External Technical References

- Autodesk 3ds Max SDK material and texture classes:
  <https://help.autodesk.com/cloudhelp/2022/ENU/Max-Developer-Help/3ds_max_sdk_features/rendering/materials_textures_and_maps/principal_classes_for_materials_.html>
- Autodesk Standard Material properties:
  <https://help.autodesk.com/cloudhelp/2016/ENU/MAXScript-Help/files/GUID-57F5EBBA-5F54-4CD4-8993-0B07A3571293.htm>
- Autodesk Physical Material properties:
  <https://help.autodesk.com/cloudhelp/2017/ENU/MAXScript-Help/files/GUID-57562F6A-A8A1-4A28-BAE1-0D4729411214.htm>
- Autodesk `Interface::RenderTexmap` API:
  <https://help.autodesk.com/cloudhelp/2025/ENU/MAXDEV-CPP-API-REF/class_interface.html>
- Chaos VRayMtl:
  <https://documentation.chaos.com/space/VMAX/113586760/VRayMtl>
- Chaos V-Ray textures:
  <https://documentation.chaos.com/space/VMAX/113586972/Textures>
- Chaos VRayBitmap:
  <https://documentation.chaos.com/space/VMAX/113575830>
- Chaos VRayNormalMap:
  <https://documentation.chaos.com/space/VMAX/113585344>
- Chaos VRayTriplanarTex:
  <https://documentation.chaos.com/space/VMAX/113575361>
- Chaos VRayDirt:
  <https://documentation.chaos.com/space/VMAX/113575816>
