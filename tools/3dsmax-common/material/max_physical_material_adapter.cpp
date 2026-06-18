// tools/3dsmax-common/material/max_physical_material_adapter.cpp
//
// 3ds Max Physical Material -> Project Render canonical material adapter
// (material-plan.md "Target Translation Architecture" -> Shared Max material
// module; Phase 2).
//
// Build model: this codebase shares provider code by textual inclusion -- each
// plugin compiles a thin wrapper that #includes the shared
// src/max_livelink_utility.cpp, which in turn #includes this file *after* the
// provider-neutral material types (MaterialSnapshot, TextureBindingSnapshot,
// diagnostics) and the shared extraction helpers it depends on. It is therefore
// NOT a standalone translation unit and must not be added to CMake directly;
// it is compiled via that inclusion. Keeping it in its own file gives the
// adapter clear ownership (parameter interpretation only -- no wire
// serialization) per the plan, without restructuring the unity build.
//
// Dependencies provided by the including context:
//   - types: MaterialSnapshot, TextureBindingSnapshot, EmbeddedTexturePayload,
//            MaterialExtractionDiagnostic, MaterialDiagnosticSeverity,
//            MaterialConversionOutcome
//   - helpers: TryGetAnimatableParamValueByName / ...ByNames /
//              TryGetAnimatableTexmapParamByName, TryResolveTexmapTextureBinding,
//              ApplyTextureBinding, ColorToArray3 / ColorToArray4,
//              ConvertMaxDistanceToEngine

// Phase 2: dedicated 3ds Max Physical Material adapter.
//
// The generic baseline (GetDiffuse/GetShininess/...) approximates a Physical
// Material poorly because those legacy Mtl getters do not expose OpenPBR-style
// semantics. This adapter reads the real parameters straight from the Physical
// Material parameter block by their stable internal names (version-robust: we
// match names, not a copied ParamID enum), then overrides the baseline.
//
// Parameter ranges follow the Autodesk Physical Material spec:
//   base_weight 0..1, base_color color; metalness 0..1; roughness 0..1
//   (authored as roughness, 0 = smooth -- no glossiness inversion);
//   reflectivity 0..1 -> specular weight; refl_color -> specular tint;
//   trans_ior -> IOR; transparency 0..1 + trans_color + trans_depth (scene
//   units) + thin_walled; emission weight 0..1 * emit_color, emit_luminance
//   cd/m^2 (default 1500); coating 0..1 + coat_roughness + coat_ior; anisotropy
//   0..1 + aniso_angle (0..1 normalized rotation); scattering -> translucency.
// Anything the engine cannot represent emits a diagnostic; nothing is silently
// dropped or packed.
void ApplyPhysicalMaterialParameters(Interface *ip, Mtl *material,
                                     MaterialSnapshot *snapshot) {
  if (!ip || !material || !snapshot) {
    return;
  }
  const TimeValue time = ip->GetTime();
  snapshot->materialModel = "PhysicalMaterial";

  auto pushDiag = [&](MaterialDiagnosticSeverity severity,
                      MaterialConversionOutcome outcome, const std::string &msg) {
    snapshot->diagnostics.push_back(MaterialExtractionDiagnostic{
        severity, outcome, snapshot->sourceMaterialClass, msg});
  };
  auto colorMaxComponent = [](const Color &c) {
    return (std::max)({c.r, c.g, c.b, 0.0f});
  };
  auto bindPhysTexture = [&](const char *mapName, const char *onName,
                             std::string *outUri,
                             EmbeddedTexturePayload *outPayload) -> bool {
    if (onName) {
      int enabled = 1;
      if (TryGetAnimatableParamValueByName(material, time, onName, &enabled) &&
          enabled == 0) {
        return false;
      }
    }
    Texmap *texmap = nullptr;
    if (!TryGetAnimatableTexmapParamByName(material, time, mapName, &texmap) ||
        !texmap) {
      return false;
    }
    TextureBindingSnapshot binding;
    if (!TryResolveTexmapTextureBinding(ip, texmap, &binding)) {
      return false;
    }
    ApplyTextureBinding(std::move(binding), outUri, outPayload, snapshot);
    return true;
  };

  // --- Base / diffuse ---
  Color baseColor(1.0f, 1.0f, 1.0f);
  float baseWeight = 1.0f;
  TryGetAnimatableParamValueByName(material, time, "base_weight", &baseWeight);
  baseWeight = (std::clamp)(baseWeight, 0.0f, 1.0f);
  if (TryGetAnimatableParamValueByName(material, time, "base_color", &baseColor)) {
    snapshot->baseColor[0] = baseColor.r * baseWeight;
    snapshot->baseColor[1] = baseColor.g * baseWeight;
    snapshot->baseColor[2] = baseColor.b * baseWeight;
  }

  // --- Metalness / roughness (authored as roughness; no inversion) ---
  float metalness = 0.0f;
  if (TryGetAnimatableParamValueByName(material, time, "metalness", &metalness)) {
    snapshot->metalness = (std::clamp)(metalness, 0.0f, 1.0f);
  }
  float roughness = 0.0f;
  if (TryGetAnimatableParamValueByName(material, time, "roughness", &roughness)) {
    snapshot->roughness = (std::clamp)(roughness, 0.0f, 1.0f);
    snapshot->invertRoughnessTexture = false;
  }
  float diffRoughness = 0.0f;
  if (TryGetAnimatableParamValueByName(material, time, "diff_roughness",
                                       &diffRoughness) &&
      diffRoughness > 1.0e-3f) {
    pushDiag(MaterialDiagnosticSeverity::Approximation,
             MaterialConversionOutcome::Approximate,
             "Physical Material diffuse (Oren-Nayar) roughness has no engine "
             "equivalent; ignored.");
  }

  // --- Reflectivity / specular ---
  float reflectivity = 1.0f;
  if (TryGetAnimatableParamValueByName(material, time, "reflectivity",
                                       &reflectivity)) {
    snapshot->specularWeight = (std::clamp)(reflectivity, 0.0f, 1.0f);
  }
  Color reflColor(1.0f, 1.0f, 1.0f);
  if (TryGetAnimatableParamValueByName(material, time, "refl_color", &reflColor)) {
    snapshot->specularColor = ColorToArray3(reflColor);
  }

  // --- IOR ---
  float ior = snapshot->ior;
  if (TryGetAnimatableParamValueByNames(material, time, {"trans_ior", "ior"},
                                        &ior)) {
    snapshot->ior = (std::max)(1.0f, ior);
  }

  // --- Transparency / volume attenuation ---
  float transparency = 0.0f;
  if (TryGetAnimatableParamValueByName(material, time, "transparency",
                                       &transparency)) {
    snapshot->transmissionWeight = (std::clamp)(transparency, 0.0f, 1.0f);
    snapshot->baseColor[3] = 1.0f - snapshot->transmissionWeight;
    snapshot->alphaMode =
        snapshot->transmissionWeight > 1.0e-3f ? "BLEND" : "OPAQUE";
  }
  Color transColor(1.0f, 1.0f, 1.0f);
  if (TryGetAnimatableParamValueByName(material, time, "trans_color",
                                       &transColor)) {
    snapshot->transmissionColor = ColorToArray3(transColor);
  }
  float transDepth = 0.0f;
  if (TryGetAnimatableParamValueByName(material, time, "trans_depth",
                                       &transDepth) &&
      transDepth > 0.0f) {
    snapshot->attenuationDistance = ConvertMaxDistanceToEngine(transDepth);
  }
  int thinWalled = 0;
  if (TryGetAnimatableParamValueByName(material, time, "thin_walled",
                                       &thinWalled)) {
    snapshot->thinWalled = thinWalled != 0 ? 1.0f : 0.0f;
  }
  float transRoughness = 0.0f;
  if (TryGetAnimatableParamValueByName(material, time, "trans_roughness",
                                       &transRoughness) &&
      transRoughness > 1.0e-3f) {
    pushDiag(MaterialDiagnosticSeverity::Approximation,
             MaterialConversionOutcome::Approximate,
             "Physical Material transparency roughness is approximated by the "
             "shared surface roughness.");
  }

  // --- Emission ---
  float emission = 0.0f;
  const bool hasEmission =
      TryGetAnimatableParamValueByName(material, time, "emission", &emission);
  Color emitColor(0.0f, 0.0f, 0.0f);
  const bool hasEmitColor =
      TryGetAnimatableParamValueByName(material, time, "emit_color", &emitColor);
  if (hasEmission || hasEmitColor) {
    if (hasEmitColor) {
      snapshot->emissiveColor = ColorToArray4(emitColor, 1.0f);
    }
    float luminance = 0.0f;
    const bool hasLuminance = TryGetAnimatableParamValueByName(
        material, time, "emit_luminance", &luminance);
    float intensity = hasEmission ? (std::max)(0.0f, emission) : 1.0f;
    if (hasLuminance && luminance > 0.0f) {
      intensity *= luminance / 1500.0f; // 1500 cd/m^2 is the Max default
    }
    snapshot->emissiveIntensity = intensity;
  }

  // --- Coat ---
  float coating = 0.0f;
  if (TryGetAnimatableParamValueByName(material, time, "coating", &coating)) {
    snapshot->coatWeight = (std::clamp)(coating, 0.0f, 1.0f);
  }
  float coatRoughness = 0.0f;
  if (TryGetAnimatableParamValueByName(material, time, "coat_roughness",
                                       &coatRoughness)) {
    snapshot->coatRoughness = (std::clamp)(coatRoughness, 0.0f, 1.0f);
  }
  float coatIor = snapshot->coatIor;
  if (TryGetAnimatableParamValueByName(material, time, "coat_ior", &coatIor)) {
    snapshot->coatIor = (std::max)(1.0f, coatIor);
  }
  Color coatColor(1.0f, 1.0f, 1.0f);
  if (snapshot->coatWeight > 1.0e-3f &&
      TryGetAnimatableParamValueByName(material, time, "coat_color", &coatColor) &&
      colorMaxComponent(Color(1.0f - coatColor.r, 1.0f - coatColor.g,
                              1.0f - coatColor.b)) > 1.0e-2f) {
    pushDiag(MaterialDiagnosticSeverity::Approximation,
             MaterialConversionOutcome::Approximate,
             "Physical Material coat tint color is not represented; coat "
             "treated as clear.");
  }

  // --- Anisotropy ---
  float anisotropy = 0.0f;
  if (TryGetAnimatableParamValueByName(material, time, "anisotropy",
                                       &anisotropy)) {
    snapshot->anisotropy = (std::clamp)(anisotropy, 0.0f, 1.0f);
  }
  float anisoAngle = 0.0f;
  if (TryGetAnimatableParamValueByName(material, time, "aniso_angle",
                                       &anisoAngle)) {
    snapshot->anisotropyRotation = anisoAngle * 360.0f;
  }

  // --- Subsurface scattering -> translucency approximation ---
  float scattering = 0.0f;
  if (TryGetAnimatableParamValueByName(material, time, "scattering",
                                       &scattering) &&
      scattering > 1.0e-3f) {
    snapshot->translucency = (std::clamp)(scattering, 0.0f, 1.0f);
    pushDiag(MaterialDiagnosticSeverity::Approximation,
             MaterialConversionOutcome::Approximate,
             "Physical Material subsurface scattering approximated as "
             "translucency.");
  }

  // --- Textures that map cleanly onto existing payload fields ---
  bindPhysTexture("base_color_map", "base_color_map_on",
                  &snapshot->baseColorTextureUri,
                  &snapshot->baseColorTexturePayload);
  bindPhysTexture("bump_map", "bump_map_on", &snapshot->normalTextureUri,
                  &snapshot->normalTexturePayload);
  bindPhysTexture("coat_bump_map", "coat_bump_map_on",
                  &snapshot->coatNormalTextureUri,
                  &snapshot->coatNormalTexturePayload);
  bindPhysTexture("emit_color_map", "emit_color_map_on",
                  &snapshot->emissiveTextureUri,
                  &snapshot->emissiveTexturePayload);
  if (bindPhysTexture("cutout_map", nullptr, &snapshot->opacityTextureUri,
                      &snapshot->opacityTexturePayload)) {
    snapshot->alphaMode = "MASK";
  }

  // --- Split metal/rough maps: honest gap, do not pack (needs PMAT v4) ---
  Texmap *roughnessMap = nullptr;
  Texmap *metalnessMap = nullptr;
  const bool hasRoughMap =
      TryGetAnimatableTexmapParamByName(material, time, "roughness_map",
                                        &roughnessMap) &&
      roughnessMap;
  const bool hasMetalMap =
      TryGetAnimatableTexmapParamByName(material, time, "metalness_map",
                                        &metalnessMap) &&
      metalnessMap;
  if (hasRoughMap || hasMetalMap) {
    pushDiag(MaterialDiagnosticSeverity::Warning,
             MaterialConversionOutcome::Unsupported,
             "Physical Material separate roughness/metalness maps require "
             "split-texture transport (PMAT v4); scalar values sent, maps not "
             "yet transported.");
  }

  // --- Displacement: diagnostic only (no transport path yet) ---
  Texmap *displacementMap = nullptr;
  if (TryGetAnimatableTexmapParamByName(material, time, "displacement_map",
                                        &displacementMap) &&
      displacementMap) {
    pushDiag(MaterialDiagnosticSeverity::Warning,
             MaterialConversionOutcome::Unsupported,
             "Physical Material displacement is not transported.");
  }
}
