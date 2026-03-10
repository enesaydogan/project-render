## Plan: OpenPBR Material Roadmap

Replace the current mixed legacy-plus-archviz material model with an OpenPBR-aligned runtime subset, keep the GPU core material payload at 64 bytes with optional side buffers, redesign the editor around OpenPBR authoring, and prioritize glTF-based DCC interchange first while reserving a clean internal adapter layer for later MaterialX import/export. This fits the renderer’s existing direction toward strict PBR and avoids coupling the rollout to full MaterialX graph evaluation.

**Steps**
1. Phase 0: Freeze scope and compatibility policy. Treat OpenPBR as the new primary material model, do not preserve a parallel legacy authoring UI, prioritize DCC interchange over strict backward compatibility, and define the first supported subset as base color, metalness, roughness, specular/IOR, transmission, coat, thin-walled, and emission.
2. Phase 1: Introduce a canonical engine-side material schema that mirrors the chosen OpenPBR subset and explicitly separates core shading data from optional mapping/auxiliary data. *Blocks phases 2-5.* Replace legacy reflection/refraction authoring fields in the CPU material model with OpenPBR-style fields while keeping a migration path for old scene data.
3. Phase 1a: Add schema/version identifiers to scene serialization and define one-way migration from current PRS/JSON materials into the new schema at load time. *Depends on 2.* Avoid trying to round-trip the old schema indefinitely; migrate on load and save back in the new schema.
4. Phase 1b: Define an internal interchange/IR layer between imported formats and runtime materials. *Parallel with 3 after 2.* This layer should own mapping from glTF extensions today and MaterialX later, so shader/runtime code stays format-agnostic.
5. Phase 2: Repack runtime material upload around the new schema without violating the existing 64-byte DXR core payload target. *Depends on 2.* Keep frequently accessed fields in the core buffer and move optional controls such as UV transforms, triplanar data, and future interchange metadata into secondary buffers/records.
6. Phase 2a: Update the ray tracing and raster material evaluation paths to use the same OpenPBR subset semantics. *Depends on 5.* Bring coat into the path tracer, formalize transmission/thin-walled behavior, and remove remaining dependence on reflectionColor/refractionColor tint logic.
7. Phase 2b: Add a material feature mask/capability system that makes unsupported OpenPBR or imported properties explicit. *Parallel with 6 after 5.* Unsupported features should degrade predictably instead of being silently misinterpreted.
8. Phase 3: Redesign the material editor around OpenPBR authoring instead of the current legacy tabs. *Depends on 2 and should land alongside 6.* Reorganize UI into authoring groups such as Base, Specular, Transmission, Coat, Emission, Geometry/Thin-Walled, Textures, and Mapping, with contextual enable/disable logic instead of exposing legacy color tints and glossiness terminology.
9. Phase 3a: Update presets, validation hints, and dirty-state handling for the new model. *Depends on 8.* Presets should map to the runtime subset and QA should validate OpenPBR ranges, unsupported combinations, and interchange losses.
10. Phase 4: Expand glTF import as the first DCC bridge using KHR_materials_* extensions that map cleanly to the chosen subset. *Depends on 4 and 6.* Prioritize KHR_materials_ior, KHR_materials_transmission, KHR_materials_volume only if absorption is added, KHR_materials_clearcoat, KHR_materials_emissive_strength, and double-sided/alpha behavior. Treat glTF as the production interchange path before MaterialX.
11. Phase 4a: Add glTF export only after import and runtime parity are stable. *Depends on 10 and 8.* Export should serialize only the supported subset and annotate any dropped properties.
12. Phase 5: Add MaterialX as an architecture-backed placeholder rather than a full first-pass deliverable. *Depends on 4.* Integrate the library in CMake/build, define import/export service boundaries, and map the internal interchange/IR layer to MaterialX documents later without forcing shader generation or graph evaluation in the first rollout.
13. Phase 5a: Decide later whether MaterialX support will be document translation only or include node-graph evaluation/shader generation. *Postponed decision; does not block OpenPBR rollout.*
14. Phase 6: Update documentation, sample assets, and regression scenes. *Depends on 6, 8, and 10.* Add at least one opaque dielectric, metal, clearcoated dielectric, emissive, thin-walled glass, and transmissive rough glass validation asset.

**Relevant files**
- `d:\project-render\src\assets\asset_loader.h` — Replace `Asset::Material` with the canonical OpenPBR-subset schema and define migration defaults.
- `d:\project-render\src\assets\asset_loader.cpp` — Rework `LoadGltf()` mappings from glTF metallic-roughness and `KHR_materials_*` extensions into the new interchange/runtime schema.
- `d:\project-render\src\main.cpp` — Update `DxrMaterialData`, `DxrMaterialExtraData`, material flags, and CPU-to-GPU packing while preserving the 64-byte hot-path design.
- `d:\project-render\shaders\path_tracer_core.hlsl` — Align path tracing sampling/evaluation with OpenPBR subset semantics, especially coat and transmission routing.
- `d:\project-render\shaders\raytracing\hit.hlsl` — Update closest-hit payload packing and per-hit material interpretation to the new schema.
- `d:\project-render\shaders\pbr_mesh.hlsl` — Keep raster preview behavior visually aligned with the path tracer for the same OpenPBR parameters.
- `d:\project-render\shaders\brdf_lib.hlsl` — Centralize any new coat/transmission helpers used by both raster and ray tracing paths.
- `d:\project-render\src\material_editor.cpp` — Replace the current legacy tab layout with an OpenPBR-first editor and revise presets/QA messaging.
- `d:\project-render\src\scene_io.cpp` — Add material schema versioning, one-way migration, and new serialization keys for the OpenPBR subset.
- `d:\project-render\CMakeLists.txt` — Add future MaterialX dependency wiring and keep tinygltf/glTF import as the first shipping interchange path.
- `d:\project-render\README.md` and related roadmap docs — Update documentation to describe the new material model, supported interchange subset, and known exclusions.

**Verification**
1. Build the renderer in Debug and Release and verify no size regression for the hot-path DXR material struct beyond the existing 64-byte target in `DxrMaterialData`.
2. Import representative glTF assets covering metallic-roughness, IOR, transmission, clearcoat, emissive strength, thin-walled/double-sided behavior, and verify parity between raster preview and path tracing.
3. Edit each supported OpenPBR subset property in the material editor and verify accumulation reset, BLAS/TLAS dirtiness, and shader updates occur only when required.
4. Save and reload scenes after migration and verify old scenes convert deterministically into the new schema and re-save without falling back to legacy keys.
5. Validate unsupported properties from interchange inputs are surfaced in UI/logging instead of silently dropped.
6. Add a small regression scene pack and screenshot-based comparisons for opaque dielectric, metal, coat, transmission, and emission.

**Decisions**
- OpenPBR is the replacement material model, not a side-by-side mode.
- The material editor should become OpenPBR-centric instead of preserving the current legacy/simple view.
- DCC interchange is a higher priority than preserving perfect backward compatibility for existing PRS/JSON scenes.
- MaterialX is a long-term format target, but the first roadmap only reserves architecture/build hooks and a clean adapter boundary.
- The first shipped subset excludes full OpenPBR graph coverage such as sheen, subsurface, iridescence, and general MaterialX node-graph execution.

**Further Considerations**
1. Transmission scope recommendation: Option A add only thin-walled plus surface transmission now; Option B also add volumetric absorption/attenuation now. Recommendation: Option A first unless rough colored glass is a core requirement.
2. Export scope recommendation: Option A ship glTF import only first; Option B also add glTF export in the same program. Recommendation: Option A first to keep parity/debugging manageable.
3. MaterialX rollout recommendation: Option A document translation only; Option B full MaterialX graph/shader support. Recommendation: Option A for the first architectural milestone.