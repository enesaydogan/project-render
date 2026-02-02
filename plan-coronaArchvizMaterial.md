# Corona Archviz Material — Implementation Plan

## Table of Contents
1. [Goals](#goals)
2. [Scope](#scope)
3. [Assumptions & Tools](#assumptions--tools)
4. [Project Standards](#project-standards)
5. [Library Structure & Naming](#library-structure--naming)
6. [Material Authoring Pipeline](#material-authoring-pipeline)
7. [Implementation Details by Material Type](#implementation-details-by-material-type)
8. [Validation & QA Checklist](#validation--qa-checklist)
9. [Performance Guidelines](#performance-guidelines)
10. [Milestones & Acceptance Criteria](#milestones--acceptance-criteria)

---

## Goals
- Produce a consistent, physically plausible **Corona Renderer** material set for archviz.
- Ensure materials are:
  - **PBR-consistent** (energy conservation, plausible IOR/roughness, correct albedo ranges)
  - **Artist-friendly** (clear parameters, presets, predictable behavior)
  - **Production-ready** (fast enough, stable under different lighting, minimal artifacts)

## Scope
- Material creation + lookdev workflow for common archviz categories:
  - Walls/paint, plaster, concrete, stone, brick
  - Wood (varnished/oiled/raw), laminate
  - Metals (brushed/polished), plastics
  - Glass (clear/frosted/tinted), liquids (optional)
  - Fabrics/leather, carpets
  - Tiles (ceramic/porcelain), grouts
  - Vegetation leaf materials (basic translucency setup)

Non-goals (explicitly out of scope unless added later):
- Full scene lighting plan, asset modeling, UV unwrapping rules beyond what’s required for materials.
- Proprietary scanned libraries integration (unless license and paths are defined).

## Assumptions & Tools
- DCC: 3ds Max + Corona Renderer (adjust naming if using C4D/Blender bridge).
- Color management:
  - Use **linear workflow** (sRGB for albedo/basecolor, linear for data maps).
- Texture inputs available per material where possible:
  - BaseColor/Albedo, Roughness (or Gloss), Normal, Height/Displacement, AO (optional), Opacity (optional).

---

## Project Standards

### Units & Scale
- Scene uses real-world units (cm or mm).  
- Textures use **real-world mapping** (e.g., 2.0m plank, 0.6m tile) whenever possible.

### PBR Rules of Thumb
- **Avoid pure black/white** albedo:
  - Typical albedo range for dielectrics: ~30–240 sRGB (very rough guide; verify by reference).
- **Metals**:
  - Use metalness/IOR conventions consistently (Corona typically uses IOR workflow; keep it physically plausible).
- **Roughness**:
  - Avoid 0.0 except for mirror-like polished surfaces; clamp minimum (e.g., 0.02–0.05) to reduce fireflies.
- **Normals/Height**:
  - Normal maps as primary microdetail; displacement reserved for silhouette/large relief.

### Color Management & Gamma
- BaseColor/Albedo: sRGB (gamma 2.2)
- Roughness/Gloss, Normal, Height, Metalness, AO, Opacity: **linear** (gamma 1.0)
- Document any exceptions per vendor texture set.

---

## Library Structure & Naming

### Folder Layout (logical)
- `Materials/`
  - `00_Master/` (template masters)
  - `01_Walls_Paint_Plaster/`
  - `02_Concrete_Stone_Brick/`
  - `03_Wood_Laminate/`
  - `04_Tiles_Grout/`
  - `05_Glass_Liquid/`
  - `06_Metal_Plastic/`
  - `07_Fabric_Leather_Carpet/`
  - `08_Vegetation/`
  - `99_Utilities/` (triplanar, UV randomizer, dirt/masks)

### Naming Convention
- `M_<Category>_<Subtype>_<Descriptor>_<Variant>`
  - Examples:
    - `M_Wood_Oak_Varnished_Satin_A`
    - `M_Concrete_Poured_Smooth_Light_A`
    - `M_Glass_Clear_Window_A`
- Texture naming:
  - `T_<MaterialName>_<MapType>_<Res>`
  - MapType: `ALB`, `RGH`, `NRM`, `HGT`, `AO`, `OPC`

### Master Materials
Maintain a small set of masters with consistent controls:
- `MSTR_Dielectric_Generic`
- `MSTR_Metal_Generic`
- `MSTR_Glass_Generic`
- `MSTR_Fabric_Generic`
Each master exposes:
- UV scale (real-world), rotation, offset
- Roughness level + roughness map strength
- Normal strength
- Optional dirt/edge wear mask slot (off by default)

---

## Material Authoring Pipeline

### 1) Reference & Target Definition
For each new material:
- Capture 3–5 reference photos (different lighting).
- Decide physical intent:
  - dielectric vs metal
  - polished vs matte
  - coated vs uncoated
  - translucency needs (fabric/leaves)

### 2) Texture Intake & Calibration
- Verify map types and gamma.
- Validate resolution vs usage distance:
  - hero surfaces: 2K–8K (only when needed)
  - background: 1K–2K
- Remove baked lighting from albedo when possible (avoid strong AO in albedo).

### 3) Shader Assembly (CoronaMtl baseline)
- Base: CoronaMtl
- Set:
  - Diffuse/BaseColor = Albedo (sRGB)
  - Reflection enabled, IOR plausible
  - Roughness from map (linear), tune min/max
  - Normal from Normal map (linear), set strength
  - Displacement only if required

### 4) Lookdev Scene & Preview
- Use a neutral test scene:
  - gray card, chrome ball, diffuse ball, calibrated light rig
  - include grazing angles and strong highlights
- Check under:
  - HDRI daylight
  - warm interior lighting
  - high-contrast sun

### 5) Validation, Optimization, Packaging
- Run QA checklist (below).
- Reduce complexity (disable unused branches).
- Save material + texture paths consistent with project structure.

---

## Implementation Details by Material Type

### Paint / Plaster (Walls)
- Base: dielectric with subtle roughness variation.
- Key points:
  - Roughness typically mid-high (e.g., 0.5–0.85) with low-frequency breakup.
  - Add micro bump/noise (very subtle) to avoid “perfect flat” highlights.
- Optional:
  - Dirt mask near floor/corners (keep mild; should not read as grunge unless intended).

### Concrete
- Base: dielectric.
- Use:
  - Roughness map for realism; avoid over-contrasty roughness.
  - Normal + (optional) displacement for chipped edges / board-formed relief.
- Displacement guidance:
  - Only for large features; keep displacement amount physically plausible (mm scale).

### Brick / Stone
- Brick:
  - Separate grout control recommended (blend two materials or use mask).
- Stone:
  - Use triplanar only when UVs are unreliable; prefer proper UVs for hero assets.
- Avoid:
  - overly deep displacement that creates shadow acne; validate at grazing angles.

### Wood (Raw / Oiled / Varnished)
- Raw wood:
  - Higher roughness; subtle anisotropy only if supported/needed.
- Varnished:
  - Consider coat behavior:
    - Option A: single-layer approximation (lower roughness + stronger reflection)
    - Option B: clearcoat/layered approach if available (keep simple for performance)
- Always:
  - Respect grain direction (UV orientation matters).

### Tiles + Grout
- Tiles:
  - Use gloss/roughness for ceramic sheen.
  - Slight bevel illusion via normal (or actual bevel in geometry for hero shots).
- Grout:
  - Higher roughness, subtle height/normal.
- Provide a parameter:
  - “Grout darkness/roughness” for quick art direction.

### Metals (Brushed / Polished)
- Use physically plausible reflection model:
  - metal = strong reflection; colored reflection depending on metal type.
- Brushed:
  - anisotropy (if available) or directional roughness map.
- Polished:
  - keep roughness above zero to reduce fireflies; clamp and denoise if needed.

### Plastics
- Dielectric with higher IOR feel (but still plausible).
- Add subtle surface variation:
  - micro roughness noise + fingerprint smudge mask (optional).

### Glass (Clear / Frosted / Tinted)
- Clear window glass:
  - Thin glass workflow if appropriate; keep absorption subtle unless tinted.
- Frosted:
  - Use higher roughness; validate refraction noise cost.
- Tinted:
  - Prefer absorption/tint through volume where supported; keep color subtle.

### Fabric / Leather / Carpet
- Fabric:
  - Use subtle fuzz/velvet effect if available; keep it conservative.
- Leather:
  - Use roughness variation + fine normal detail.
- Carpet:
  - If no geometry fur: fake with normal + AO; ensure it doesn’t shimmer at distance.

### Vegetation (Leaves)
- Two-sided material where needed.
- Use translucency/SSS model appropriate for Corona:
  - keep scatter color plausible (not neon).
- Validate backlit scenarios (sun behind leaves).

---

## Validation & QA Checklist

### Physical Plausibility
- [ ] No impossible albedo (pure white/black except rare cases)
- [ ] Energy-conserving behavior (no “glowing” diffuse + strong mirror unless intentional emissive)
- [ ] IOR/roughness consistent with material type

### Mapping & Scale
- [ ] Real-world scale looks correct at human-eye distances
- [ ] No visible tiling in hero areas (use variation masks/UVW randomization if needed)
- [ ] Normal map orientation correct (no inverted bumps)

### Render Robustness
- [ ] Stable under multiple lighting conditions (daylight + warm interior)
- [ ] No excessive fireflies (clamp roughness minima, adjust gloss, consider light samples)
- [ ] No displacement artifacts (edge tearing, shadow acne)

### Consistency
- [ ] Naming follows convention
- [ ] Texture gamma settings correct
- [ ] Parameters exposed match master material controls

---

## Performance Guidelines
- Prefer normal maps over displacement for microdetail.
- Use displacement only:
  - on hero assets
  - with controlled max subdiv / screen size
- Avoid stacking heavy maps:
  - multiple 8K maps + triplanar + displacement on large surfaces.
- For distant assets:
  - switch to simplified material instances (lower res, fewer maps).
- Keep utility nodes reusable:
  - a single UV randomizer / triplanar function used across materials.

---

## Milestones & Acceptance Criteria

### Milestone 1 — Foundations
- Deliver:
  - Master materials (dielectric/metal/glass/fabric) with documented parameters
  - Lookdev test scene
- Acceptance:
  - 10 sample materials pass QA checklist and match reference direction.

### Milestone 2 — Core Library
- Deliver:
  - 30–60 materials across key categories (walls, floors, wood, concrete, glass, metal)
- Acceptance:
  - Materials render cleanly in 2–3 lighting setups with no manual per-shot hacks.

### Milestone 3 — Polish & Optimization
- Deliver:
  - Utility masks (dirt/edge wear), UV randomization presets, LOD/simplified variants
- Acceptance:
  - Render time impact is predictable; heavy materials have “lite” alternatives.

### Milestone 4 — Packaging
- Deliver:
  - Final library structure, naming audit, texture relinking/paths verified
- Acceptance:
  - Clean import into a fresh scene with no missing textures and consistent results.

---

## Notes / Open Questions
- Target Corona version + DCC version?
- Preferred texture library sources (Megascans, Poliigon, custom scans)?
- Required deliverables format: `.mat` library, scene with material editor slots, or both?
