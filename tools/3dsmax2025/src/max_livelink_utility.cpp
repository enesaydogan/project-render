#include "max_livelink_pipe_client.h"

#include <max.h>
#include <utilapi.h>
#include <iparamm2.h>
#include <stdmat.h>
#include <units.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <MeshNormalSpec.h>
#include <triobj.h>

#if defined(PROJECT_RENDER_HAS_VRAY_SDK)
#include <pb2enum.h>
#include <vraygeom.h>
#include <vrayplugins.h>
#endif

using json = nlohmann::json;

namespace {

#if defined(PROJECT_RENDER_HAS_VRAY_SDK)
#define PROJECT_RENDER_VRAYMTL_CLASS_ID Class_ID(0x37bf3f2f, 0x7034695c)

namespace ProjectRenderVrayMtlParameters {
enum {
  vrayMtl_old_id,
  vrayMtl_basic_id,
  vrayMtl_BRDF_id,
  vrayMtl_options_id,
  vrayMtl_maps_id,
  vrayMtl_reflIMap_id,
  vrayMtl_refrIMap_id,
};

enum {
  pb_spin,
  pb_diffuse_color,
  pb_reflect_color,
  pb_reflect_glossiness,
  pb_reflect_subdivs,
  pb_refract_color,
  pb_refract_glossiness,
  pb_refract_subdivs,
  pb_reflect_fresnel,
  pb_refract_ior,
  pb_brdf_type,
  pb_reflect_trace,
  pb_refract_trace,
  pb_doubleSided,
  pb_reflectOnBack,
  pb_diffuse_trace,
  pb_diffuse_subdivs,
  pb_useIrradMap,
  pb_diffuseGlossy,
  pb_traceCaustics,
  pb_reflect_maxDepth,
  pb_refract_maxDepth,
  pb_cutoffThresh,
  pb_refract_fogColor,
  pb_refract_fogMult,
  pb_refract_translucent,
  pb_refract_thickness,
  pb_refract_volSubdivs,
  pb_refract_lightMult_old,
  pb_refract_scatterCoeff,
  pb_refract_scatterDir,
  pb_refract_lightMult,
  pb_fog_affectShadows,
  pb_useInterpolation,
  pb_interpSamples,
  pb_colorThreshold,
  pb_preservationMode,
  pb_translucent_color,
  pb_translucent_map,
  pb_reflect_minRate,
  pb_reflect_maxRate,
  pb_reflect_clrThresh,
  pb_reflect_nrmThresh,
  pb_reflect_interpSamples,
  pb_reflect_preset,
  pb_refract_minRate,
  pb_refract_maxRate,
  pb_refract_clrThresh,
  pb_refract_nrmThresh,
  pb_refract_interpSamples,
  pb_refract_preset,
  pb_reflect_exitColor,
  pb_refract_exitColor,
  pb_refract_exitColor_on,
  pb_brdf_anisotropy,
  pb_brdf_anisotropy_channel,
  pb_reflect_useInterpolation,
  pb_refract_useInterpolation,
  pb_brdf_anisotropy_rotation,
  pb_brdf_anisotropy_derivation,
  pb_brdf_anisotropy_axis,
  pb_refract_affectAlpha,
  pb_hilight_glossiness,
  pb_reflect_ior,
  pb_reflect_glossiness_lock,
  pb_reflect_ior_lock,
  pb_refract_fogBias,
  pb_environment_priority,
  pb_diffuse_roughness,
  pb_brdf_soften,
  pb_clampColors,
  pb_reflect_dimDistance,
  pb_reflect_dimDistanceFallOff,
  pb_reflect_dimDistanceOn,
  pb_refract_dispersion,
  pb_refract_dispersionOn,
  pb_refract_fogUnitScaleOn,
  pb_brdf_fixDarkEdges,
  pb_effect_id,
  pb_override_effect_id,
  pb_reflect_affectAlpha,
  pb_selfIllumination_color,
  pb_selfIllumination_gi,
  pb_selfIllumination_mult,
  pb_opacity_mode,
  pb_reflect_gtr_gamma_not_used,
  pb_brdf_ggx_tail_falloff,
  pb_brdf_ggx_tail_oldFalloff,
  pb_glossyFresnel,
  pb_brdf_useRoughness,
  pb_compensate_cam_exposure,
  pb_reflect_metalness,
  pb_diffuse_roughness_model,
  pb_mtl_preset,
  pb_mtl_sheen_color,
  pb_mtl_sheen_glossiness,
  pb_mtl_coat_amount,
  pb_mtl_coat_color,
  pb_mtl_coat_glossiness,
  pb_mtl_coat_ior,
  pb_mtl_coat_bump_lock,
  pb_mtl_bump_on,
  pb_mtl_bump_amount,
  pb_mtl_coat_bump_on,
  pb_mtl_coat_bump_amount,
  pb_refract_translucency_amount,
  pb_refract_fogDepth,
  pb_refract_thinWalled,
  pb_brdf_newGTRAnisotropy,
  pb_diffuse_color_shortmap,
  pb_diffuse_roughness_shortmap,
  pb_bump_shortmap,
  pb_reflect_color_shortmap,
  pb_reflect_glossiness_shortmap,
  pb_reflect_ior_shortmap,
  pb_reflect_metalness_shortmap,
  pb_refract_color_shortmap,
  pb_refract_glossiness_shortmap,
  pb_refract_ior_shortmap,
  pb_refract_fogColor_shortmap,
  pb_translucent_color_shortmap,
  pb_selfIllumination_color_shortmap,
  pb_mtl_coat_amount_shortmap,
  pb_mtl_coat_glossiness_shortmap,
  pb_mtl_coat_ior_shortmap,
  pb_mtl_coat_color_shortmap,
  pb_mtl_coat_bump_shortmap,
  pb_mtl_sheen_color_shortmap,
  pb_mtl_sheen_glossiness_shortmap,
  pb_brdf_ggx_tail_falloff_shortmap,
  pb_brdf_anisotropy_shortmap,
  pb_brdf_anisotropy_rotation_shortmap,
  pb_cosmos_asset_id,
  pb_gtrEnergyCompensation,
  pb_translucency_surfaceLighting,
  pb_refract_fogDepth_shortmap,
  pb_translucency_amount_shortmap,
  pb_thinFilm_thickness_min,
  pb_thinFilm_ior,
  pb_thinFilm_thickness_shortmap,
  pb_thinFilm_ior_shortmap,
  pb_thinFilm_on,
  pb_thinFilm_thickness_max,
  pb_cosmos_asset_revision,
  pb_openpbr_mode,
  pb_mtl_coat_darkening,
  pb_reflect_weight,
};
} // namespace ProjectRenderVrayMtlParameters
#endif

HINSTANCE g_instance = nullptr;
MaxLiveLinkPipeClient g_pipeClient;
std::atomic<bool> g_exportInProgress{false};
constexpr const char *kPipeName = "project-render-max-livelink";
constexpr const char *kSourceApp = "3dsMax2025";
constexpr UINT_PTR kPollTimerId = 0x5052;
constexpr UINT kPollIntervalMs = 16; // ~60fps LiveLink loop
constexpr int kUtilityDialogId = 101;
constexpr int kStatusControlId = 1001;
constexpr int kStartControlId = 1002;
constexpr int kStopControlId = 1003;

class ProjectRenderLiveLinkUtility;
extern ProjectRenderLiveLinkUtility g_utility;

struct ScopedFlag {
  explicit ScopedFlag(std::atomic<bool> *flag) : m_flag(flag) {
    if (m_flag) {
      m_flag->store(true);
    }
  }

  ~ScopedFlag() {
    if (m_flag) {
      m_flag->store(false);
    }
  }

private:
  std::atomic<bool> *m_flag = nullptr;
};

std::string WStringToUtf8(const std::wstring &value) {
  if (value.empty()) {
    return {};
  }
  const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                                             static_cast<int>(value.size()),
                                             nullptr, 0, nullptr, nullptr);
  if (utf8Length <= 0) {
    return {};
  }
  std::string utf8(utf8Length, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                      utf8.data(), utf8Length, nullptr, nullptr);
  return utf8;
}

std::wstring Utf8ToWString(const std::string &value) {
  if (value.empty()) {
    return {};
  }
  const int wideLength = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                             static_cast<int>(value.size()),
                                             nullptr, 0);
  if (wideLength <= 0) {
    return {};
  }
  std::wstring wide(wideLength, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                      wide.data(), wideLength);
  return wide;
}

std::string ToUtf8(const MCHAR *text) {
  if (!text) {
    return {};
  }
#ifdef UNICODE
  return WStringToUtf8(text);
#else
  return text;
#endif
}

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

std::string GetAnimatableClassNameUtf8(Animatable *animatable) {
  if (!animatable) {
    return {};
  }
  MSTR className;
  animatable->GetClassName(className, FALSE);
  return ToUtf8(className.data());
}

bool ContainsAnyToken(const std::string &text,
                      std::initializer_list<const char *> tokens) {
  for (const char *token : tokens) {
    if (token && text.find(token) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool IsVrayLightClassName(const std::string &classNameLower) {
  return classNameLower.find("vray") != std::string::npos &&
         classNameLower.find("light") != std::string::npos;
}

bool IsVrayMaterialClassName(const std::string &classNameLower) {
  return classNameLower.find("vray") != std::string::npos &&
         classNameLower.find("mtl") != std::string::npos;
}

std::string GetObjectClassNameLower(Animatable *animatable) {
  return ToLowerAscii(GetAnimatableClassNameUtf8(animatable));
}

void AppendClassNameHint(Animatable *animatable,
                         std::string *classNameHintsLower) {
  if (!animatable || !classNameHintsLower) {
    return;
  }
  const std::string classNameLower = GetObjectClassNameLower(animatable);
  if (classNameLower.empty() ||
      classNameHintsLower->find(classNameLower) != std::string::npos) {
    return;
  }
  if (!classNameHintsLower->empty()) {
    classNameHintsLower->push_back(' ');
  }
  classNameHintsLower->append(classNameLower);
}

std::string BuildLightClassNameHints(Object *evaluatedObject, Object *baseObject) {
  std::string classNameHintsLower;
  AppendClassNameHint(evaluatedObject, &classNameHintsLower);
  AppendClassNameHint(baseObject, &classNameHintsLower);

  auto appendWrappedLight = [&classNameHintsLower](Object *object) {
    if (!object || object->NumRefs() <= 0) {
      return;
    }
    RefTargetHandle wrappedHandle = object->GetReference(0);
    AppendClassNameHint(dynamic_cast<Animatable *>(wrappedHandle),
                        &classNameHintsLower);
  };

  appendWrappedLight(evaluatedObject);
  appendWrappedLight(baseObject);
  return classNameHintsLower;
}

struct LightSnapshot;
struct MaterialSnapshot;

std::array<float, 4> ColorToArray4(const Color &color, float alpha);
std::array<float, 3> ColorToArray3(const Color &color);

float ConvertMaxDistanceToEngine(float value);

#if defined(PROJECT_RENDER_HAS_VRAY_SDK)
IParamBlock2 *GetVrayLightParamBlock(Object *object) {
  if (!object) {
    return nullptr;
  }
  return object->GetParamBlockByID(VRayLights::vrayLight_params);
}

IParamBlock2 *FindVrayLightParamBlock(Object *evaluatedObject, Object *baseObject) {
  if (IParamBlock2 *paramBlock = GetVrayLightParamBlock(evaluatedObject)) {
    return paramBlock;
  }
  if (IParamBlock2 *paramBlock = GetVrayLightParamBlock(baseObject)) {
    return paramBlock;
  }

  auto findWrappedParamBlock = [](Object *object) -> IParamBlock2 * {
    if (!object || object->NumRefs() <= 0) {
      return nullptr;
    }
    RefTargetHandle wrappedHandle = object->GetReference(0);
    Object *wrappedObject = dynamic_cast<Object *>(wrappedHandle);
    return GetVrayLightParamBlock(wrappedObject);
  };

  if (IParamBlock2 *paramBlock = findWrappedParamBlock(evaluatedObject)) {
    return paramBlock;
  }
  return findWrappedParamBlock(baseObject);
}

bool TryGetVrayInt(IParamBlock2 *paramBlock, ParamID paramId, TimeValue time,
                   int *outValue) {
  if (!paramBlock || !outValue) {
    return false;
  }
  Interval valid = FOREVER;
  int value = 0;
  if (!paramBlock->GetValue(paramId, time, value, valid)) {
    return false;
  }
  *outValue = value;
  return true;
}

std::string ResolveEngineLightTypeFromVrayParam(int vrayLightTypeParam) {
  switch (vrayLightTypeParam) {
  case 0:
    return "AreaRect";
  case 2:
    return "Omni";
  case 4:
    return "AreaDisk";
  default:
    return {};
  }
}

bool TryGetVrayFloat(IParamBlock2 *paramBlock, ParamID paramId, TimeValue time,
                     float *outValue) {
  if (!paramBlock || !outValue) {
    return false;
  }
  Interval valid = FOREVER;
  float value = 0.0f;
  if (!paramBlock->GetValue(paramId, time, value, valid)) {
    return false;
  }
  *outValue = value;
  return true;
}

bool TryGetVrayColor(IParamBlock2 *paramBlock, ParamID paramId, TimeValue time,
                     Color *outValue) {
  if (!paramBlock || !outValue) {
    return false;
  }
  Interval valid = FOREVER;
  Color value(1.0f, 1.0f, 1.0f);
  if (!paramBlock->GetValue(paramId, time, value, valid)) {
    return false;
  }
  *outValue = value;
  return true;
}

float GetPreferredVraySize(IParamBlock2 *paramBlock, TimeValue time,
                           ParamID primaryParamId, ParamID fallbackParamId) {
  float value = 0.0f;
  if (TryGetVrayFloat(paramBlock, primaryParamId, time, &value) && value > 0.0f) {
    return value;
  }
  if (TryGetVrayFloat(paramBlock, fallbackParamId, time, &value) && value > 0.0f) {
    return value;
  }
  return 0.0f;
}

void ApplyVrayLightParameters(Interface *ip, IParamBlock2 *paramBlock,
                              const std::string &classNameHintsLower,
                              LightSnapshot *snapshot);
#endif

bool IsVrayLightObject(Object *object) {
#if defined(PROJECT_RENDER_HAS_VRAY_SDK)
  if (!object) {
    return false;
  }
  if (object->ClassID() == VRAYLIGHT_CLASS_ID) {
    return true;
  }
  if (object->GetInterface(VRENDERLIGHT_INTERFACE) != nullptr) {
    return true;
  }
  if (object->SuperClassID() == LIGHT_CLASS_ID && object->NumRefs() > 0) {
    RefTargetHandle wrappedObject = object->GetReference(0);
    Object *wrappedLight = dynamic_cast<Object *>(wrappedObject);
    if (wrappedLight &&
        (wrappedLight->ClassID() == VRAYLIGHT_CLASS_ID ||
         wrappedLight->GetInterface(VRENDERLIGHT_INTERFACE) != nullptr)) {
      return true;
    }
  }
#else
  (void)object;
#endif
  return false;
}

#if defined(PROJECT_RENDER_HAS_VRAY_SDK)
int GetVrayLightTypeFlagsForObject(Object *object) {
  if (!object) {
    return 0;
  }
  VUtils::LightInterface *lightInterface =
      static_cast<VUtils::LightInterface *>(object->GetInterface(EXT_LIGHT));
  return lightInterface ? lightInterface->getLightType() : 0;
}

int ResolveVrayLightTypeFlags(Object *evaluatedObject, Object *sourceBaseObject) {
  int lightTypeFlags = GetVrayLightTypeFlagsForObject(evaluatedObject);
  if (lightTypeFlags != 0) {
    return lightTypeFlags;
  }

  lightTypeFlags = GetVrayLightTypeFlagsForObject(sourceBaseObject);
  if (lightTypeFlags != 0) {
    return lightTypeFlags;
  }

  auto findWrappedFlags = [](Object *object) {
    if (!object || object->NumRefs() <= 0) {
      return 0;
    }
    RefTargetHandle wrappedHandle = object->GetReference(0);
    Object *wrappedObject = dynamic_cast<Object *>(wrappedHandle);
    return GetVrayLightTypeFlagsForObject(wrappedObject);
  };

  lightTypeFlags = findWrappedFlags(evaluatedObject);
  if (lightTypeFlags != 0) {
    return lightTypeFlags;
  }
  return findWrappedFlags(sourceBaseObject);
}

constexpr int kVrayMtlParamBlockIds[] = {
  ProjectRenderVrayMtlParameters::vrayMtl_old_id,
  ProjectRenderVrayMtlParameters::vrayMtl_basic_id,
  ProjectRenderVrayMtlParameters::vrayMtl_BRDF_id,
  ProjectRenderVrayMtlParameters::vrayMtl_options_id,
  ProjectRenderVrayMtlParameters::vrayMtl_maps_id,
  ProjectRenderVrayMtlParameters::vrayMtl_reflIMap_id,
  ProjectRenderVrayMtlParameters::vrayMtl_refrIMap_id,
};

bool IsVrayMaterial(Mtl *material) {
  if (!material) {
    return false;
  }
  if (material->ClassID() == PROJECT_RENDER_VRAYMTL_CLASS_ID) {
    return true;
  }
  return IsVrayMaterialClassName(GetObjectClassNameLower(material));
}

template <typename TValue>
bool TryGetVrayMtlValue(Mtl *material, ParamID paramId, TimeValue time,
                        TValue *outValue) {
  if (!material || !outValue) {
    return false;
  }

  for (int blockId : kVrayMtlParamBlockIds) {
    IParamBlock2 *paramBlock = material->GetParamBlockByID(blockId);
    if (!paramBlock) {
      continue;
    }

    Interval valid = FOREVER;
    TValue value{};
    if (paramBlock->GetValue(paramId, time, value, valid)) {
      *outValue = value;
      return true;
    }
  }

  return false;
}

bool TryGetVrayMtlTexmap(Mtl *material, ParamID paramId, TimeValue time,
                        Texmap **outTexmap) {
  if (!material || !outTexmap) {
    return false;
  }

  for (int blockId : kVrayMtlParamBlockIds) {
    IParamBlock2 *paramBlock = material->GetParamBlockByID(blockId);
    if (!paramBlock) {
      continue;
    }

    Texmap *texmap = paramBlock->GetTexmap(paramId, time);
    if (texmap) {
      *outTexmap = texmap;
      return true;
    }
  }

  return false;
}

float MaxColorComponent(const Color &color) {
  return (std::max)({color.r, color.g, color.b, 0.0f});
}
#endif

std::string ResolveEngineLightType(const LightState &lightState,
                                   const std::string &classNameLower,
                                   bool isVrayLight,
                                   int vrayLightTypeFlags = 0,
                                   int vrayLightTypeParam = -1) {
#if defined(PROJECT_RENDER_HAS_VRAY_SDK)
  if (isVrayLight) {
    const std::string lightTypeFromParam =
        ResolveEngineLightTypeFromVrayParam(vrayLightTypeParam);
    if (!lightTypeFromParam.empty()) {
      return lightTypeFromParam;
    }
  }
  if (isVrayLight && (vrayLightTypeFlags & LT_PLANE) != 0) {
    if (ContainsAnyToken(classNameLower, {"disk", "disc"})) {
      return "AreaDisk";
    }
    return "AreaRect";
  }
  if (isVrayLight && (vrayLightTypeFlags & LT_INFINITE) != 0) {
    if (classNameLower.find("dome") != std::string::npos) {
      return "Omni";
    }
    return "Directional";
  }
#else
  (void)vrayLightTypeFlags;
  (void)vrayLightTypeParam;
#endif

  if (isVrayLight && classNameLower.find("sun") != std::string::npos) {
    return "Directional";
  }
  if (isVrayLight && classNameLower.find("ies") != std::string::npos) {
    return "IES";
  }
  if (isVrayLight && ContainsAnyToken(classNameLower, {"spot"})) {
    return "Spot";
  }
  if (isVrayLight &&
      ContainsAnyToken(classNameLower,
                       {"rect", "rectangle", "plane", "panel"})) {
    return "AreaRect";
  }
  if (isVrayLight && ContainsAnyToken(classNameLower, {"disk", "disc"})) {
    return "AreaDisk";
  }
  if (isVrayLight &&
      ContainsAnyToken(classNameLower, {"sphere", "mesh", "dome", "omni"})) {
    return "Omni";
  }

  switch (lightState.type) {
  case DIRECT_LGT:
    return "Directional";
  case SPOT_LGT:
    return "Spot";
  case OMNI_LGT:
    return "Omni";
  default:
    return "Omni";
  }
}

std::string MakeDocumentId(Interface *ip) {
  const std::string currentFile = ip ? ToUtf8(ip->GetCurFileName()) : std::string();
  return currentFile.empty() ? std::string("untitled.max") : currentFile;
}

std::string MakeSessionId() {
  static std::atomic<uint64_t> s_sessionCounter{1};
  const uint64_t sessionOrdinal = s_sessionCounter.fetch_add(1);
  return "3dsmax2025-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(GetTickCount64()) + "-" +
         std::to_string(sessionOrdinal);
}

std::string MakeNodeObjectId(INode *node) {
  if (!node) {
    return {};
  }
  return "node:" + std::to_string(static_cast<unsigned long long>(node->GetHandle()));
}

std::string MakeMaterialObjectId(ULONG_PTR nodeHandle, int materialSlot) {
  return "material:node:" +
         std::to_string(static_cast<unsigned long long>(nodeHandle)) +
         ":slot:" + std::to_string((std::max)(0, materialSlot));
}

std::string MakeLightObjectId(INode *node) {
  if (!node) {
    return {};
  }
  return "light:" +
         std::to_string(static_cast<unsigned long long>(node->GetHandle()));
}

Object *GetNodeBaseObject(INode *node) {
  if (!node || !node->GetObjectRef()) {
    return nullptr;
  }
  return node->GetObjectRef()->FindBaseObject();
}

std::vector<std::string> GatherSelectedObjectIds(Interface *ip) {
  std::vector<std::string> selectedObjectIds;
  if (!ip) {
    return selectedObjectIds;
  }

  const int selectedCount = ip->GetSelNodeCount();
  selectedObjectIds.reserve(static_cast<size_t>(selectedCount));
  for (int index = 0; index < selectedCount; ++index) {
    if (INode *node = ip->GetSelNode(index)) {
      selectedObjectIds.push_back(MakeNodeObjectId(node));
    }
  }
  return selectedObjectIds;
}

struct NodeSnapshot {
  ULONG_PTR handle = 0;
  ULONG_PTR parentHandle = 0;
  std::string name;
  bool visible = true;
  std::array<float, 16> worldMatrix = {};
  bool hasMesh = false;
  uint64_t vertexCount = 0;
  uint64_t indexCount = 0;
  uint64_t geometryFingerprint = 0;
};

struct CameraSnapshot {
  bool valid = false;
  std::array<float, 3> position = {0.0f, 1.0f, -5.0f};
  std::array<float, 3> forward = {0.0f, 0.0f, 1.0f};
  std::array<float, 3> up = {0.0f, 1.0f, 0.0f};
  float fovDegrees = 60.0f;
  float nearPlane = 0.01f;
  float farPlane = 1000.0f;
};

struct MaterialSnapshot {
  bool valid = false;
  ULONG_PTR nodeHandle = 0;
  int materialSlot = 0;
  std::string objectId;
  std::string name;
  std::string materialModel = "OpenPBR";
  std::array<float, 4> baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
  std::string baseColorTextureUri;
  std::string normalTextureUri;
  std::string emissiveTextureUri;
  std::array<float, 4> emissiveColor = {0.0f, 0.0f, 0.0f, 1.0f};
  float emissiveIntensity = 0.0f;
  float roughness = 0.5f;
  float metalness = 0.0f;
  float specularWeight = 1.0f;
  float ior = 1.5f;
  float transmissionWeight = 0.0f;
  std::array<float, 3> transmissionColor = {1.0f, 1.0f, 1.0f};
  float coatWeight = 0.0f;
  float coatRoughness = 0.1f;
  float thinWalled = 0.0f;
  float translucency = 0.0f;
  std::array<float, 2> uvScale = {1.0f, 1.0f};
  std::array<float, 2> uvOffset = {0.0f, 0.0f};
  bool doubleSided = false;
  std::string alphaMode = "OPAQUE";
};

struct TextureBindingSnapshot {
  std::string uri;
  std::array<float, 2> uvScale = {1.0f, 1.0f};
  std::array<float, 2> uvOffset = {0.0f, 0.0f};
};

using MaterialStateMap = std::unordered_map<std::string, MaterialSnapshot>;

struct LightSnapshot {
  bool valid = false;
  ULONG_PTR handle = 0;
  std::string objectId;
  std::string name;
  std::string lightType = "Omni";
  std::array<float, 3> position = {0.0f, 2.0f, 0.0f};
  std::array<float, 3> direction = {0.0f, -1.0f, 0.0f};
  std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
  float intensity = 0.0f;
  float radius = 0.1f;
  float innerConeDegrees = 30.0f;
  float outerConeDegrees = 45.0f;
  std::array<float, 2> areaExtents = {1.0f, 1.0f};
};

struct NativeMeshPayloadHeader {
  uint32_t magic = 0x48534D50; // PMSH
  uint32_t version = 2;
  uint32_t meshCount = 0;
  uint32_t reserved = 0;
};

struct NativeMeshPayloadMeshHeader {
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  int32_t materialSlot = 0;
  uint32_t reserved = 0;
};

struct NativeMeshPayloadVertex {
  float position[3] = {0.0f, 0.0f, 0.0f};
  float normal[3] = {0.0f, 1.0f, 0.0f};
  float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  float uv[2] = {0.0f, 0.0f};
};

uint64_t HashCombine(uint64_t seed, uint64_t value) {
  return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
}

uint64_t HashFloat(float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(value));
  return bits;
}

uint64_t HashPoint3Value(uint64_t seed, const Point3 &value) {
  seed = HashCombine(seed, HashFloat(value.x));
  seed = HashCombine(seed, HashFloat(value.y));
  seed = HashCombine(seed, HashFloat(value.z));
  return seed;
}

float GetMaxUnitsToMetersScale() {
  const double scale = GetSystemUnitScale(UNITS_METERS);
  return scale > 0.0 ? static_cast<float>(scale) : 1.0f;
}

Point3 ConvertMaxPointToEngine(const Point3 &point) {
  const float unitScale = GetMaxUnitsToMetersScale();
  return Point3(-point.x * unitScale, point.z * unitScale, point.y * unitScale);
}

Point3 ConvertMaxVectorToEngine(const Point3 &vector) {
  return Point3(-vector.x, vector.z, vector.y);
}

std::array<float, 3> Point3ToArray(const Point3 &point) {
  return {point.x, point.y, point.z};
}

void NormalizePoint3(Point3 *value, const Point3 &fallback) {
  if (!value) {
    return;
  }
  const float lenSq = value->x * value->x + value->y * value->y + value->z * value->z;
  if (lenSq <= 1.0e-12f) {
    *value = fallback;
    return;
  }
  const float invLen = 1.0f / std::sqrt(lenSq);
  value->x *= invLen;
  value->y *= invLen;
  value->z *= invLen;
}

Point3 CrossPoint3(const Point3 &lhs, const Point3 &rhs) {
  return Point3(lhs.y * rhs.z - lhs.z * rhs.y,
                lhs.z * rhs.x - lhs.x * rhs.z,
                lhs.x * rhs.y - lhs.y * rhs.x);
}

Point3 GetFaceCornerNormal(Mesh &mesh, int faceIndex, int corner) {
  MeshNormalSpec *specifiedNormals = mesh.GetSpecifiedNormals();
  if (specifiedNormals && specifiedNormals->GetNumFaces() == mesh.getNumFaces() &&
      specifiedNormals->GetNumNormals() > 0) {
    return specifiedNormals->GetNormal(faceIndex, corner);
  }

  const Face &face = mesh.faces[faceIndex];
  RVertex *renderVertex = mesh.getRVertPtr(face.getVert(corner));
  if (!renderVertex) {
    return mesh.getFaceNormal(faceIndex);
  }

  if ((renderVertex->rFlags & SPECIFIED_NORMAL) != 0) {
    return renderVertex->rn.getNormal();
  }

  const DWORD normalCount = renderVertex->rFlags & NORCT_MASK;
  if (normalCount == 0) {
    return mesh.getFaceNormal(faceIndex);
  }

  if (normalCount == 1) {
    return renderVertex->rn.getNormal();
  }

  for (DWORD normalIndex = 0; normalIndex < normalCount; ++normalIndex) {
    RNormal &renderNormal = renderVertex->ern[normalIndex];
    if ((renderNormal.getSmGroup() & face.smGroup) != 0) {
      return renderNormal.getNormal();
    }
  }

  return renderVertex->ern[0].getNormal();
}

Point3 BuildStableCameraUp(const Point3 &forward) {
  const Point3 worldUp(0.0f, 1.0f, 0.0f);
  Point3 right = CrossPoint3(worldUp, forward);
  NormalizePoint3(&right, Point3(1.0f, 0.0f, 0.0f));
  Point3 up = CrossPoint3(forward, right);
  NormalizePoint3(&up, worldUp);
  return up;
}

bool SameVector3(const std::array<float, 3> &lhs, const std::array<float, 3> &rhs) {
  return std::fabs(lhs[0] - rhs[0]) <= 1.0e-4f &&
         std::fabs(lhs[1] - rhs[1]) <= 1.0e-4f &&
         std::fabs(lhs[2] - rhs[2]) <= 1.0e-4f;
}

bool SameCamera(const CameraSnapshot &lhs, const CameraSnapshot &rhs) {
  return lhs.valid == rhs.valid && SameVector3(lhs.position, rhs.position) &&
         SameVector3(lhs.forward, rhs.forward) && SameVector3(lhs.up, rhs.up) &&
         std::fabs(lhs.fovDegrees - rhs.fovDegrees) <= 1.0e-4f &&
         std::fabs(lhs.nearPlane - rhs.nearPlane) <= 1.0e-4f &&
         std::fabs(lhs.farPlane - rhs.farPlane) <= 1.0e-4f;
}

bool NearlyEqual(float a, float b) {
  return std::fabs(a - b) <= 1.0e-4f;
}

bool SameVector2(const std::array<float, 2> &lhs, const std::array<float, 2> &rhs) {
  return std::fabs(lhs[0] - rhs[0]) <= 1.0e-4f &&
         std::fabs(lhs[1] - rhs[1]) <= 1.0e-4f;
}

bool SameVector4(const std::array<float, 4> &lhs, const std::array<float, 4> &rhs) {
  return std::fabs(lhs[0] - rhs[0]) <= 1.0e-4f &&
         std::fabs(lhs[1] - rhs[1]) <= 1.0e-4f &&
         std::fabs(lhs[2] - rhs[2]) <= 1.0e-4f &&
         std::fabs(lhs[3] - rhs[3]) <= 1.0e-4f;
}

bool SameMaterial(const MaterialSnapshot &lhs, const MaterialSnapshot &rhs) {
  return lhs.valid == rhs.valid && lhs.objectId == rhs.objectId &&
         lhs.materialSlot == rhs.materialSlot && lhs.name == rhs.name &&
         lhs.materialModel == rhs.materialModel &&
         SameVector4(lhs.baseColor, rhs.baseColor) &&
         lhs.baseColorTextureUri == rhs.baseColorTextureUri &&
         lhs.normalTextureUri == rhs.normalTextureUri &&
         lhs.emissiveTextureUri == rhs.emissiveTextureUri &&
         SameVector4(lhs.emissiveColor, rhs.emissiveColor) &&
         NearlyEqual(lhs.emissiveIntensity, rhs.emissiveIntensity) &&
         NearlyEqual(lhs.roughness, rhs.roughness) &&
         NearlyEqual(lhs.metalness, rhs.metalness) &&
         NearlyEqual(lhs.specularWeight, rhs.specularWeight) &&
         NearlyEqual(lhs.ior, rhs.ior) &&
         NearlyEqual(lhs.transmissionWeight, rhs.transmissionWeight) &&
         SameVector3(lhs.transmissionColor, rhs.transmissionColor) &&
         NearlyEqual(lhs.coatWeight, rhs.coatWeight) &&
         NearlyEqual(lhs.coatRoughness, rhs.coatRoughness) &&
         NearlyEqual(lhs.thinWalled, rhs.thinWalled) &&
         NearlyEqual(lhs.translucency, rhs.translucency) &&
         SameVector2(lhs.uvScale, rhs.uvScale) &&
         SameVector2(lhs.uvOffset, rhs.uvOffset) &&
         lhs.doubleSided == rhs.doubleSided && lhs.alphaMode == rhs.alphaMode;
}

bool SameLight(const LightSnapshot &lhs, const LightSnapshot &rhs) {
  return lhs.valid == rhs.valid && lhs.objectId == rhs.objectId &&
         lhs.name == rhs.name && lhs.lightType == rhs.lightType &&
         SameVector3(lhs.position, rhs.position) &&
         SameVector3(lhs.direction, rhs.direction) &&
         SameVector3(lhs.color, rhs.color) &&
         NearlyEqual(lhs.intensity, rhs.intensity) &&
         NearlyEqual(lhs.radius, rhs.radius) &&
         NearlyEqual(lhs.innerConeDegrees, rhs.innerConeDegrees) &&
         NearlyEqual(lhs.outerConeDegrees, rhs.outerConeDegrees) &&
         SameVector2(lhs.areaExtents, rhs.areaExtents);
}

bool SameMatrix(const std::array<float, 16> &lhs,
                const std::array<float, 16> &rhs) {
  for (size_t index = 0; index < lhs.size(); ++index) {
    if (!NearlyEqual(lhs[index], rhs[index])) {
      return false;
    }
  }
  return true;
}

std::array<float, 16> MultiplyColumnMajor4x4(const std::array<float, 16> &lhs,
                                             const std::array<float, 16> &rhs) {
  std::array<float, 16> result = {};
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) {
        sum += lhs[k * 4 + row] * rhs[col * 4 + k];
      }
      result[col * 4 + row] = sum;
    }
  }
  return result;
}

std::array<float, 16> MakeMaxToEngineBasisMatrix() {
  const float unitScale = GetMaxUnitsToMetersScale();
  return {
      -unitScale, 0.0f,      0.0f,      0.0f,
      0.0f,      0.0f,      unitScale, 0.0f,
      0.0f,      unitScale, 0.0f,      0.0f,
      0.0f,      0.0f,      0.0f,      1.0f,
  };
}

std::array<float, 16> MakeEngineToMaxBasisMatrix() {
  const float unitScale = GetMaxUnitsToMetersScale();
  const float inverseScale = unitScale > 0.0f ? 1.0f / unitScale : 1.0f;
  return {
      -inverseScale, 0.0f,         0.0f,         0.0f,
      0.0f,         0.0f,         inverseScale, 0.0f,
      0.0f,         inverseScale, 0.0f,         0.0f,
      0.0f,         0.0f,         0.0f,         1.0f,
  };
}

std::array<float, 16> Matrix3ToColumnMajor4x4(const Matrix3 &matrix) {
  const Point3 row0 = matrix.GetRow(0);
  const Point3 row1 = matrix.GetRow(1);
  const Point3 row2 = matrix.GetRow(2);
  const Point3 translation = matrix.GetTrans();
  const std::array<float, 16> maxMatrix = {
      row0.x,         row0.y,         row0.z,         0.0f,
      row1.x,         row1.y,         row1.z,         0.0f,
      row2.x,         row2.y,         row2.z,         0.0f,
      translation.x,  translation.y,  translation.z,  1.0f,
  };
      const std::array<float, 16> basisChange = MakeMaxToEngineBasisMatrix();
      const std::array<float, 16> inverseBasisChange =
        MakeEngineToMaxBasisMatrix();
  return MultiplyColumnMajor4x4(
        basisChange, MultiplyColumnMajor4x4(maxMatrix, inverseBasisChange));
}

Matrix3 ComputeObjectToNodeTransform(Interface *ip, INode *node) {
  if (!ip || !node) {
    return Matrix3();
  }

  const TimeValue time = ip->GetTime();
  const Matrix3 nodeTm = node->GetNodeTM(time);
  const Matrix3 objectTm = node->GetObjectTM(time);
  return objectTm * Inverse(nodeTm);
}

Point3 TransformPointByMatrix3(const Matrix3 &matrix, const Point3 &point) {
  const Point3 row0 = matrix.GetRow(0);
  const Point3 row1 = matrix.GetRow(1);
  const Point3 row2 = matrix.GetRow(2);
  const Point3 translation = matrix.GetTrans();
  return Point3(point.x * row0.x + point.y * row1.x + point.z * row2.x +
                    translation.x,
                point.x * row0.y + point.y * row1.y + point.z * row2.y +
                    translation.y,
                point.x * row0.z + point.y * row1.z + point.z * row2.z +
                    translation.z);
}

Point3 TransformVectorByMatrix3(const Matrix3 &matrix, const Point3 &vector) {
  const Point3 row0 = matrix.GetRow(0);
  const Point3 row1 = matrix.GetRow(1);
  const Point3 row2 = matrix.GetRow(2);
  return Point3(vector.x * row0.x + vector.y * row1.x + vector.z * row2.x,
                vector.x * row0.y + vector.y * row1.y + vector.z * row2.y,
                vector.x * row0.z + vector.y * row1.z + vector.z * row2.z);
}

std::string PathToUtf8(const std::filesystem::path &path) {
  return WStringToUtf8(path.wstring());
}

std::string NormalizeTextureUri(Interface *ip, const std::string &rawPath) {
  if (rawPath.empty()) {
    return {};
  }

  std::filesystem::path texturePath(Utf8ToWString(rawPath));
  if (texturePath.empty()) {
    return {};
  }

  if (texturePath.is_relative() && ip) {
    const std::string scenePathUtf8 = ToUtf8(ip->GetCurFileName());
    if (!scenePathUtf8.empty()) {
      const std::filesystem::path scenePath(Utf8ToWString(scenePathUtf8));
      if (!scenePath.empty()) {
        texturePath = scenePath.parent_path() / texturePath;
      }
    }
  }

  std::error_code error;
  if (std::filesystem::exists(texturePath, error)) {
    const std::filesystem::path canonicalPath =
        std::filesystem::weakly_canonical(texturePath, error);
    if (!error && !canonicalPath.empty()) {
      texturePath = canonicalPath;
    }
  }

  return PathToUtf8(texturePath.lexically_normal());
}

bool TryResolveBitmapTextureBinding(Interface *ip, BitmapTex *bitmapTex,
                                    TextureBindingSnapshot *outBinding) {
  if (!bitmapTex || !outBinding) {
    return false;
  }

  const std::string textureUri =
      NormalizeTextureUri(ip, ToUtf8(bitmapTex->GetMapName()));
  if (textureUri.empty()) {
    return false;
  }

  outBinding->uri = textureUri;

  if (StdUVGen *uvGenerator = bitmapTex->GetUVGen()) {
    const TimeValue time = ip ? ip->GetTime() : 0;
    outBinding->uvScale[0] = uvGenerator->GetUScl(time);
    outBinding->uvScale[1] = uvGenerator->GetVScl(time);
    outBinding->uvOffset[0] = uvGenerator->GetUOffs(time);
    outBinding->uvOffset[1] = uvGenerator->GetVOffs(time);
  }

  return true;
}

bool TryResolveTexmapTextureBinding(Interface *ip, Texmap *texmap,
                                    TextureBindingSnapshot *outBinding) {
  if (!texmap || !outBinding) {
    return false;
  }

  if (BitmapTex *bitmapTex = dynamic_cast<BitmapTex *>(texmap)) {
    if (TryResolveBitmapTextureBinding(ip, bitmapTex, outBinding)) {
      return true;
    }
  }

  for (int subTexmapIndex = 0; subTexmapIndex < texmap->NumSubTexmaps(); ++subTexmapIndex) {
    if (Texmap *subTexmap = texmap->GetSubTexmap(subTexmapIndex)) {
      if (TryResolveTexmapTextureBinding(ip, subTexmap, outBinding)) {
        return true;
      }
    }
  }

  return false;
}

void ApplyTextureBinding(TextureBindingSnapshot binding, std::string *outUri,
                         MaterialSnapshot *snapshot) {
  if (!outUri || !snapshot || binding.uri.empty()) {
    return;
  }

  *outUri = std::move(binding.uri);
  if (NearlyEqual(snapshot->uvScale[0], 1.0f) &&
      NearlyEqual(snapshot->uvScale[1], 1.0f) &&
      NearlyEqual(snapshot->uvOffset[0], 0.0f) &&
      NearlyEqual(snapshot->uvOffset[1], 0.0f)) {
    snapshot->uvScale = binding.uvScale;
    snapshot->uvOffset = binding.uvOffset;
  }
}

void CaptureGenericMaterialTextures(Interface *ip, Mtl *material,
                                    MaterialSnapshot *snapshot) {
  if (!ip || !material || !snapshot) {
    return;
  }

  for (int subTexmapIndex = 0; subTexmapIndex < material->NumSubTexmaps(); ++subTexmapIndex) {
    Texmap *texmap = material->GetSubTexmap(subTexmapIndex);
    if (!texmap) {
      continue;
    }

    const MSTR slotName = material->GetSubTexmapSlotName(subTexmapIndex, false);
    const std::string slotNameLower = ToLowerAscii(ToUtf8(slotName.data()));
    TextureBindingSnapshot binding;
    if (!TryResolveTexmapTextureBinding(ip, texmap, &binding)) {
      continue;
    }

    if (snapshot->baseColorTextureUri.empty() &&
        ContainsAnyToken(slotNameLower,
                         {"diffuse", "albedo", "base color", "basecolor"})) {
      ApplyTextureBinding(std::move(binding), &snapshot->baseColorTextureUri,
                          snapshot);
      continue;
    }
    if (snapshot->normalTextureUri.empty() &&
        ContainsAnyToken(slotNameLower, {"normal", "bump"})) {
      ApplyTextureBinding(std::move(binding), &snapshot->normalTextureUri,
                          snapshot);
      continue;
    }
    if (snapshot->emissiveTextureUri.empty() &&
        ContainsAnyToken(slotNameLower, {"self", "emiss", "illum"})) {
      ApplyTextureBinding(std::move(binding), &snapshot->emissiveTextureUri,
                          snapshot);
    }
  }
}

std::string MakeCameraObjectId() {
  return "camera:active";
}

INode *FindNodeByHandleRecursive(INode *node, ULONG_PTR handle) {
  if (!node) {
    return nullptr;
  }
  if (node->GetHandle() == handle) {
    return node;
  }
  for (int childIndex = 0; childIndex < node->NumberOfChildren(); ++childIndex) {
    if (INode *match = FindNodeByHandleRecursive(node->GetChildNode(childIndex), handle)) {
      return match;
    }
  }
  return nullptr;
}

INode *FindNodeByHandle(Interface *ip, ULONG_PTR handle) {
  if (!ip) {
    return nullptr;
  }
  INode *root = ip->GetRootNode();
  if (!root) {
    return nullptr;
  }
  for (int childIndex = 0; childIndex < root->NumberOfChildren(); ++childIndex) {
    if (INode *match =
            FindNodeByHandleRecursive(root->GetChildNode(childIndex), handle)) {
      return match;
    }
  }
  return nullptr;
}

std::array<float, 4> ColorToArray4(const Color &color, float alpha) {
  return {color.r, color.g, color.b, alpha};
}

std::array<float, 3> ColorToArray3(const Color &color) {
  return {color.r, color.g, color.b};
}

float ConvertMaxDistanceToEngine(float value) {
  return (std::max)(0.0f, value) * GetMaxUnitsToMetersScale();
}

#if defined(PROJECT_RENDER_HAS_VRAY_SDK)
void ApplyVrayMaterialParameters(Interface *ip, Mtl *material,
                                 MaterialSnapshot *snapshot) {
  if (!ip || !material || !snapshot) {
    return;
  }

  const TimeValue time = ip->GetTime();
  snapshot->materialModel = "VRayMtl";

  Color diffuseColor(1.0f, 1.0f, 1.0f);
  if (TryGetVrayMtlValue(material, ProjectRenderVrayMtlParameters::pb_diffuse_color, time,
                         &diffuseColor)) {
    snapshot->baseColor[0] = diffuseColor.r;
    snapshot->baseColor[1] = diffuseColor.g;
    snapshot->baseColor[2] = diffuseColor.b;
  }

  float reflectGlossiness = 0.8f;
  if (TryGetVrayMtlValue(material,
                         ProjectRenderVrayMtlParameters::pb_reflect_glossiness, time,
                         &reflectGlossiness)) {
    int useRoughness = 0;
    TryGetVrayMtlValue(material, ProjectRenderVrayMtlParameters::pb_brdf_useRoughness, time,
                       &useRoughness);
    snapshot->roughness =
        (std::clamp)(useRoughness != 0 ? reflectGlossiness
                                       : (1.0f - reflectGlossiness),
                     0.04f, 1.0f);
  }

  float reflectMetalness = 0.0f;
  if (TryGetVrayMtlValue(material, ProjectRenderVrayMtlParameters::pb_reflect_metalness,
                         time, &reflectMetalness)) {
    snapshot->metalness = (std::clamp)(reflectMetalness, 0.0f, 1.0f);
  }

  Color reflectColor(1.0f, 1.0f, 1.0f);
  const bool hasReflectColor =
      TryGetVrayMtlValue(material, ProjectRenderVrayMtlParameters::pb_reflect_color, time,
                         &reflectColor);
  float reflectWeight = 1.0f;
  const bool hasReflectWeight =
      TryGetVrayMtlValue(material, ProjectRenderVrayMtlParameters::pb_reflect_weight, time,
                         &reflectWeight);
  if (hasReflectColor || hasReflectWeight) {
    const float colorWeight =
        hasReflectColor ? MaxColorComponent(reflectColor) : 1.0f;
    const float layerWeight = hasReflectWeight ? reflectWeight : 1.0f;
    snapshot->specularWeight =
        (std::clamp)(colorWeight * layerWeight, 0.0f, 1.0f);
  }

  Color refractColor(0.0f, 0.0f, 0.0f);
  if (TryGetVrayMtlValue(material, ProjectRenderVrayMtlParameters::pb_refract_color, time,
                         &refractColor)) {
    snapshot->transmissionColor = ColorToArray3(refractColor);
    snapshot->transmissionWeight =
        (std::clamp)(MaxColorComponent(refractColor), 0.0f, 1.0f);
    snapshot->baseColor[3] = 1.0f - snapshot->transmissionWeight;
    snapshot->alphaMode =
        snapshot->transmissionWeight > 1.0e-3f ? "BLEND" : "OPAQUE";
  }

  float refractIor = snapshot->ior;
  if (TryGetVrayMtlValue(material, ProjectRenderVrayMtlParameters::pb_refract_ior, time,
                         &refractIor)) {
    snapshot->ior = (std::max)(1.0f, refractIor);
  }

  Color selfIllumColor(0.0f, 0.0f, 0.0f);
  if (TryGetVrayMtlValue(material,
                         ProjectRenderVrayMtlParameters::pb_selfIllumination_color, time,
                         &selfIllumColor)) {
    snapshot->emissiveColor = ColorToArray4(selfIllumColor, 1.0f);
  }

  float selfIllumMultiplier = 0.0f;
  if (TryGetVrayMtlValue(material,
                         ProjectRenderVrayMtlParameters::pb_selfIllumination_mult, time,
                         &selfIllumMultiplier)) {
    snapshot->emissiveIntensity = (std::max)(0.0f, selfIllumMultiplier);
  }

  float coatAmount = 0.0f;
  if (TryGetVrayMtlValue(material, ProjectRenderVrayMtlParameters::pb_mtl_coat_amount, time,
                         &coatAmount)) {
    snapshot->coatWeight = (std::clamp)(coatAmount, 0.0f, 1.0f);
  }

  float coatGlossiness = 1.0f;
  if (TryGetVrayMtlValue(material,
                         ProjectRenderVrayMtlParameters::pb_mtl_coat_glossiness, time,
                         &coatGlossiness)) {
    snapshot->coatRoughness =
        (std::clamp)(1.0f - coatGlossiness, 0.0f, 1.0f);
  }

  int thinWalled = 0;
  if (TryGetVrayMtlValue(material,
                         ProjectRenderVrayMtlParameters::pb_refract_thinWalled, time,
                         &thinWalled)) {
    snapshot->thinWalled = thinWalled != 0 ? 1.0f : 0.0f;
  }

  float translucencyAmount = 0.0f;
  if (TryGetVrayMtlValue(material,
                         ProjectRenderVrayMtlParameters::pb_refract_translucency_amount,
                         time, &translucencyAmount)) {
    snapshot->translucency = (std::clamp)(translucencyAmount, 0.0f, 1.0f);
  } else {
    int translucent = 0;
    if (TryGetVrayMtlValue(material,
                           ProjectRenderVrayMtlParameters::pb_refract_translucent, time,
                           &translucent)) {
      snapshot->translucency = translucent != 0 ? 1.0f : 0.0f;
    }
  }

  int doubleSided = 0;
  if (TryGetVrayMtlValue(material, ProjectRenderVrayMtlParameters::pb_doubleSided, time,
                         &doubleSided)) {
    snapshot->doubleSided = doubleSided != 0;
  }

  Texmap *baseColorTexmap = nullptr;
  if (TryGetVrayMtlTexmap(material,
                          ProjectRenderVrayMtlParameters::pb_diffuse_color_shortmap,
                          time, &baseColorTexmap)) {
    TextureBindingSnapshot binding;
    if (TryResolveTexmapTextureBinding(ip, baseColorTexmap, &binding)) {
      ApplyTextureBinding(std::move(binding), &snapshot->baseColorTextureUri,
                          snapshot);
    }
  }

  Texmap *normalTexmap = nullptr;
  if (TryGetVrayMtlTexmap(material,
                          ProjectRenderVrayMtlParameters::pb_bump_shortmap,
                          time, &normalTexmap)) {
    TextureBindingSnapshot binding;
    if (TryResolveTexmapTextureBinding(ip, normalTexmap, &binding)) {
      ApplyTextureBinding(std::move(binding), &snapshot->normalTextureUri,
                          snapshot);
    }
  }

  Texmap *emissiveTexmap = nullptr;
  if (TryGetVrayMtlTexmap(
          material,
          ProjectRenderVrayMtlParameters::pb_selfIllumination_color_shortmap,
          time, &emissiveTexmap)) {
    TextureBindingSnapshot binding;
    if (TryResolveTexmapTextureBinding(ip, emissiveTexmap, &binding)) {
      ApplyTextureBinding(std::move(binding), &snapshot->emissiveTextureUri,
                          snapshot);
    }
  }
}

void ApplyVrayLightParameters(Interface *ip, IParamBlock2 *paramBlock,
                              const std::string &classNameHintsLower,
                              LightSnapshot *snapshot) {
  if (!ip || !paramBlock || !snapshot) {
    return;
  }

  const TimeValue time = ip->GetTime();

  Color vrayColor(1.0f, 1.0f, 1.0f);
  if (TryGetVrayColor(paramBlock, VRayLights::pb_color, time, &vrayColor)) {
    snapshot->color = ColorToArray3(vrayColor);
  }

  float multiplier = 0.0f;
  if (TryGetVrayFloat(paramBlock, VRayLights::pb_multiplier, time, &multiplier)) {
    snapshot->intensity = (std::max)(0.0f, multiplier);
  }

  const float sizeX = GetPreferredVraySize(paramBlock, time,
                                           VRayLights::pb_size0_new,
                                           VRayLights::pb_size0);
  const float sizeY = GetPreferredVraySize(paramBlock, time,
                                           VRayLights::pb_size1_new,
                                           VRayLights::pb_size1);
  const float sizeZ = GetPreferredVraySize(paramBlock, time,
                                           VRayLights::pb_size2,
                                           VRayLights::pb_size2);
  const float extentX = ConvertMaxDistanceToEngine(sizeX);
  const float extentY = ConvertMaxDistanceToEngine(sizeY > 0.0f ? sizeY : sizeX);

  if (snapshot->lightType == "AreaRect") {
    snapshot->areaExtents = {(std::max)(0.01f, extentX),
                             (std::max)(0.01f, extentY)};
  }

  if (snapshot->lightType == "AreaDisk") {
    const float diskRadius =
        ConvertMaxDistanceToEngine(sizeX > 0.0f ? sizeX : sizeY) * 0.5f;
    snapshot->radius = (std::max)(0.01f, diskRadius);
  }

  if (snapshot->lightType == "Omni") {
    if (ContainsAnyToken(classNameHintsLower, {"sphere", "mesh"})) {
      const float sphereRadius =
          ConvertMaxDistanceToEngine(sizeX > 0.0f ? sizeX : (sizeY > 0.0f ? sizeY : sizeZ)) *
          0.5f;
      if (sphereRadius > 0.0f) {
        snapshot->radius = (std::max)(0.01f, sphereRadius);
      }
    }

    if (ContainsAnyToken(classNameHintsLower, {"dome"})) {
      int finiteDome = 0;
      float domeEmitRadius = 0.0f;
      if (TryGetVrayInt(paramBlock, VRayLights::pb_dome_finite, time,
                        &finiteDome) &&
          finiteDome != 0 &&
          TryGetVrayFloat(paramBlock, VRayLights::pb_dome_emitRadius, time,
                          &domeEmitRadius)) {
        const float finiteRadius = ConvertMaxDistanceToEngine(domeEmitRadius);
        if (finiteRadius > 0.0f) {
          snapshot->radius = (std::max)(0.01f, finiteRadius);
        }
      }
    }
  }
}
#endif

std::string ResolveMaterialModelName(Mtl *material) {
  if (!material) {
    return "OpenPBR";
  }
#if defined(PROJECT_RENDER_HAS_VRAY_SDK)
  if (IsVrayMaterial(material)) {
    return "VRayMtl";
  }
#endif
  return "3dsMaxMaterial";
}

Mtl *ResolveLeafMaterial(Mtl *material) {
  if (!material) {
    return nullptr;
  }

  while (material->NumSubMtls() == 1 && material->GetSubMtl(0)) {
    material = material->GetSubMtl(0);
  }
  return material;
}

bool GetTriObjectForNode(Interface *ip, INode *node, TriObject **outTriObject,
                         bool *outNeedsDelete);

std::vector<int> GatherUsedMaterialSlots(Interface *ip, INode *node,
                                         Mtl *rootMaterial) {
  std::vector<int> usedSlots;
  if (!node || !rootMaterial) {
    return usedSlots;
  }

  const int subMaterialCount = rootMaterial->NumSubMtls();
  if (subMaterialCount <= 1) {
    usedSlots.push_back(0);
    return usedSlots;
  }

  TriObject *triObject = nullptr;
  bool needsDelete = false;
  if (!GetTriObjectForNode(ip, node, &triObject, &needsDelete)) {
    for (int slot = 0; slot < subMaterialCount; ++slot) {
      if (rootMaterial->GetSubMtl(slot)) {
        usedSlots.push_back(slot);
      }
    }
    if (usedSlots.empty()) {
      usedSlots.push_back(0);
    }
    return usedSlots;
  }

  Mesh &mesh = triObject->GetMesh();
  std::vector<bool> used(static_cast<size_t>(subMaterialCount), false);
  for (int faceIndex = 0; faceIndex < mesh.getNumFaces(); ++faceIndex) {
    int slot = mesh.faces[faceIndex].getMatID();
    slot = slot > 0 ? (slot - 1) : 0;
    slot = (std::clamp)(slot, 0, subMaterialCount - 1);
    used[static_cast<size_t>(slot)] = true;
  }

  if (needsDelete) {
    triObject->DeleteThis();
  }

  for (int slot = 0; slot < subMaterialCount; ++slot) {
    if (used[static_cast<size_t>(slot)] && rootMaterial->GetSubMtl(slot)) {
      usedSlots.push_back(slot);
    }
  }
  if (usedSlots.empty()) {
    usedSlots.push_back(0);
  }
  return usedSlots;
}

Mtl *ResolveMaterialForSlot(Mtl *rootMaterial, int materialSlot) {
  if (!rootMaterial) {
    return nullptr;
  }

  const int subMaterialCount = rootMaterial->NumSubMtls();
  if (subMaterialCount > 1) {
    const int clampedSlot = (std::clamp)(materialSlot, 0, subMaterialCount - 1);
    if (Mtl *slotMaterial = rootMaterial->GetSubMtl(clampedSlot)) {
      return ResolveLeafMaterial(slotMaterial);
    }
  }

  if (subMaterialCount == 1 && rootMaterial->GetSubMtl(0)) {
    return ResolveLeafMaterial(rootMaterial->GetSubMtl(0));
  }

  return ResolveLeafMaterial(rootMaterial);
}

bool CaptureMaterialSnapshot(Interface *ip, INode *node, int materialSlot, Mtl *material,
                             MaterialSnapshot *outSnapshot) {
  if (!ip || !node || !material || !outSnapshot) {
    return false;
  }

  const float transparency = (std::clamp)(material->GetXParency(), 0.0f, 1.0f);
  const float shininess = (std::clamp)(material->GetShininess(), 0.0f, 1.0f);
  const float shinStrength = (std::clamp)(material->GetShinStr(), 0.0f, 1.0f);
  const float selfIllum = (std::max)(0.0f, material->GetSelfIllum());
  const bool selfIllumColorOn = material->GetSelfIllumColorOn() != FALSE;
  const Color diffuse = material->GetDiffuse();
  const Color specular = material->GetSpecular();
  const Color emissive = selfIllumColorOn
                             ? material->GetSelfIllumColor()
                             : Color(selfIllum, selfIllum, selfIllum);

  MaterialSnapshot snapshot;
  snapshot.valid = true;
  snapshot.nodeHandle = node->GetHandle();
  snapshot.materialSlot = (std::max)(0, materialSlot);
  snapshot.objectId =
      MakeMaterialObjectId(snapshot.nodeHandle, snapshot.materialSlot);
  snapshot.name = ToUtf8(material->GetName());
  if (snapshot.name.empty()) {
    snapshot.name = ToUtf8(node->GetName()) + " [slot " +
                    std::to_string(snapshot.materialSlot) + "]";
  }
  snapshot.materialModel = ResolveMaterialModelName(material);
  snapshot.baseColor = ColorToArray4(diffuse, 1.0f - transparency);
  snapshot.emissiveColor = ColorToArray4(emissive, 1.0f);
  snapshot.emissiveIntensity =
      selfIllumColorOn ? (std::max)({emissive.r, emissive.g, emissive.b, 0.0f})
                       : selfIllum;
  snapshot.roughness = (std::clamp)(1.0f - shininess, 0.04f, 1.0f);
  snapshot.metalness = 0.0f;
  snapshot.specularWeight =
      (std::clamp)((std::max)({specular.r, specular.g, specular.b, shinStrength}),
                   0.0f, 1.0f);
  snapshot.ior = transparency > 1.0e-3f ? 1.52f : 1.5f;
  snapshot.transmissionWeight = transparency;
  snapshot.transmissionColor = transparency > 1.0e-3f
                                   ? ColorToArray3(diffuse)
                                   : std::array<float, 3>{1.0f, 1.0f, 1.0f};
  snapshot.doubleSided = false;
  snapshot.alphaMode = transparency > 1.0e-3f ? "BLEND" : "OPAQUE";
  CaptureGenericMaterialTextures(ip, material, &snapshot);
#if defined(PROJECT_RENDER_HAS_VRAY_SDK)
  if (IsVrayMaterial(material)) {
    ApplyVrayMaterialParameters(ip, material, &snapshot);
  }
#endif
  *outSnapshot = snapshot;
  return true;
}

void GatherMaterialSnapshots(
    Interface *ip, const std::unordered_map<ULONG_PTR, NodeSnapshot> &nodeState,
    MaterialStateMap *outState) {
  if (!ip || !outState) {
    return;
  }

  for (const auto &[handle, nodeSnapshot] : nodeState) {
    if (!nodeSnapshot.hasMesh) {
      continue;
    }
    INode *node = FindNodeByHandle(ip, handle);
    if (!node) {
      continue;
    }
    Mtl *rootMaterial = node->GetMtl();
    if (!rootMaterial) {
      continue;
    }

    const std::vector<int> usedSlots = GatherUsedMaterialSlots(ip, node, rootMaterial);
    for (int materialSlot : usedSlots) {
      MaterialSnapshot materialSnapshot;
      if (CaptureMaterialSnapshot(ip, node, materialSlot,
                                  ResolveMaterialForSlot(rootMaterial, materialSlot),
                                  &materialSnapshot)) {
        outState->insert_or_assign(materialSnapshot.objectId, materialSnapshot);
      }
    }
  }
}

bool CaptureLightSnapshot(Interface *ip, INode *node, LightSnapshot *outSnapshot) {
  if (!ip || !node || !outSnapshot) {
    return false;
  }

  ObjectState objectState = node->EvalWorldState(ip->GetTime());
  if (!objectState.obj) {
    return false;
  }

  Object *baseObject = GetNodeBaseObject(node);
  if (!baseObject) {
    baseObject = objectState.obj;
  }
  const std::string classNameLower =
      BuildLightClassNameHints(objectState.obj, baseObject);
  LightObject *lightObject = dynamic_cast<LightObject *>(objectState.obj);
  const bool isVrayLight = IsVrayLightObject(baseObject) ||
                           IsVrayLightClassName(classNameLower);
  int vrayLightTypeFlags = 0;
  int vrayLightTypeParam = -1;
#if defined(PROJECT_RENDER_HAS_VRAY_SDK)
  if (isVrayLight) {
    vrayLightTypeFlags =
        ResolveVrayLightTypeFlags(objectState.obj, baseObject);
    if (IParamBlock2 *vrayParamBlock =
            FindVrayLightParamBlock(objectState.obj, baseObject)) {
      TryGetVrayInt(vrayParamBlock, VRayLights::pb_type, ip->GetTime(),
                    &vrayLightTypeParam);
    }
  }
#endif
  const bool allowGenericLightEval =
      lightObject != nullptr || isVrayLight;
  if (!allowGenericLightEval || !lightObject) {
    return false;
  }

  LightState lightState;
  if (lightObject->EvalLightState(ip->GetTime(), &lightState) != REF_SUCCEED ||
      !lightState.on) {
    return false;
  }

  LightSnapshot snapshot;
  snapshot.valid = true;
  snapshot.handle = node->GetHandle();
  snapshot.objectId = MakeLightObjectId(node);
  snapshot.name = ToUtf8(node->GetName());
  snapshot.lightType =
      ResolveEngineLightType(lightState, classNameLower, isVrayLight,
                 vrayLightTypeFlags, vrayLightTypeParam);

  const Matrix3 worldTM = node->GetNodeTM(ip->GetTime());
  const Point3 position = ConvertMaxPointToEngine(worldTM.GetRow(3));
  Point3 direction = ConvertMaxVectorToEngine(-worldTM.GetRow(2));
  NormalizePoint3(&direction, Point3(0.0f, -1.0f, 0.0f));
  snapshot.position = Point3ToArray(position);
  snapshot.direction = Point3ToArray(direction);
  snapshot.color = {lightState.color.r, lightState.color.g, lightState.color.b};
  snapshot.intensity = (std::max)(0.0f, lightState.intens);
  snapshot.radius = 0.1f;
  snapshot.innerConeDegrees = lightState.hotsize;
  snapshot.outerConeDegrees = lightState.fallsize > 0.0f ? lightState.fallsize
                                                          : lightState.hotsize;
  snapshot.areaExtents = {1.0f, (std::max)(1.0f, lightState.aspect)};
#if defined(PROJECT_RENDER_HAS_VRAY_SDK)
  if (isVrayLight) {
    if (IParamBlock2 *vrayParamBlock = FindVrayLightParamBlock(objectState.obj,
                                                               baseObject)) {
      ApplyVrayLightParameters(ip, vrayParamBlock, classNameLower, &snapshot);
    }
  }
#endif
  *outSnapshot = snapshot;
  return true;
}

void GatherLightSnapshots(Interface *ip, INode *node,
                          std::unordered_map<ULONG_PTR, LightSnapshot> *outState) {
  if (!ip || !node || !outState) {
    return;
  }

  LightSnapshot snapshot;
  if (CaptureLightSnapshot(ip, node, &snapshot)) {
    outState->insert_or_assign(snapshot.handle, snapshot);
  }
  for (int childIndex = 0; childIndex < node->NumberOfChildren(); ++childIndex) {
    GatherLightSnapshots(ip, node->GetChildNode(childIndex), outState);
  }
}

bool GetTriObjectForNode(Interface *ip, INode *node, TriObject **outTriObject,
                         bool *outNeedsDelete) {
  if (!ip || !node || !outTriObject || !outNeedsDelete) {
    return false;
  }

  ObjectState objectState = node->EvalWorldState(ip->GetTime());
  if (!objectState.obj ||
      !objectState.obj->CanConvertToType(Class_ID(TRIOBJ_CLASS_ID, 0))) {
    return false;
  }

  TriObject *triObject =
      static_cast<TriObject *>(objectState.obj->ConvertToType(ip->GetTime(),
                                                              Class_ID(TRIOBJ_CLASS_ID, 0)));
  if (!triObject) {
    return false;
  }

  *outTriObject = triObject;
  *outNeedsDelete = triObject != objectState.obj;
  return true;
}

void CaptureMeshSnapshot(Interface *ip, INode *node, NodeSnapshot *snapshot) {
  if (!ip || !node || !snapshot) {
    return;
  }

  TriObject *triObject = nullptr;
  bool needsDelete = false;
  if (!GetTriObjectForNode(ip, node, &triObject, &needsDelete)) {
    return;
  }

  Mesh &mesh = triObject->GetMesh();
  mesh.checkNormals(TRUE);
  const uint64_t vertexCount = static_cast<uint64_t>(mesh.getNumVerts());
  const uint64_t faceCount = static_cast<uint64_t>(mesh.getNumFaces());
  if (vertexCount == 0 || faceCount == 0) {
    if (needsDelete) {
      triObject->DeleteThis();
    }
    return;
  }

  mesh.buildBoundingBox();
  const Box3 bounds = mesh.getBoundingBox();
  const Matrix3 objectToNode = ComputeObjectToNodeTransform(ip, node);

  snapshot->hasMesh = true;
  snapshot->vertexCount = vertexCount;
  snapshot->indexCount = faceCount * 3ull;

  uint64_t fingerprint = 1469598103934665603ull;
  fingerprint = HashCombine(fingerprint, vertexCount);
  fingerprint = HashCombine(fingerprint, faceCount);
  fingerprint = HashCombine(fingerprint, HashFloat(GetMaxUnitsToMetersScale()));
  fingerprint = HashCombine(fingerprint, HashFloat(bounds.Min().x));
  fingerprint = HashCombine(fingerprint, HashFloat(bounds.Min().y));
  fingerprint = HashCombine(fingerprint, HashFloat(bounds.Min().z));
  fingerprint = HashCombine(fingerprint, HashFloat(bounds.Max().x));
  fingerprint = HashCombine(fingerprint, HashFloat(bounds.Max().y));
  fingerprint = HashCombine(fingerprint, HashFloat(bounds.Max().z));
  const Point3 row0 = objectToNode.GetRow(0);
  const Point3 row1 = objectToNode.GetRow(1);
  const Point3 row2 = objectToNode.GetRow(2);
  const Point3 translation = objectToNode.GetTrans();
  fingerprint = HashCombine(fingerprint, HashFloat(row0.x));
  fingerprint = HashCombine(fingerprint, HashFloat(row0.y));
  fingerprint = HashCombine(fingerprint, HashFloat(row0.z));
  fingerprint = HashCombine(fingerprint, HashFloat(row1.x));
  fingerprint = HashCombine(fingerprint, HashFloat(row1.y));
  fingerprint = HashCombine(fingerprint, HashFloat(row1.z));
  fingerprint = HashCombine(fingerprint, HashFloat(row2.x));
  fingerprint = HashCombine(fingerprint, HashFloat(row2.y));
  fingerprint = HashCombine(fingerprint, HashFloat(row2.z));
  fingerprint = HashCombine(fingerprint, HashFloat(translation.x));
  fingerprint = HashCombine(fingerprint, HashFloat(translation.y));
  fingerprint = HashCombine(fingerprint, HashFloat(translation.z));

  const bool hasTexcoords = mesh.tvFace != nullptr && mesh.tVerts != nullptr &&
                            mesh.getNumTVerts() > 0;
  for (int faceIndex = 0; faceIndex < mesh.getNumFaces(); ++faceIndex) {
    Face &face = mesh.faces[faceIndex];
    fingerprint = HashCombine(
        fingerprint,
        static_cast<uint64_t>(static_cast<uint32_t>(face.getMatID())));

    for (int corner = 0; corner < 3; ++corner) {
      const int vertexIndex = face.getVert(corner);
      if (vertexIndex >= 0 && vertexIndex < mesh.getNumVerts()) {
        const Point3 position = ConvertMaxPointToEngine(
            TransformPointByMatrix3(objectToNode, mesh.verts[vertexIndex]));
        fingerprint = HashPoint3Value(fingerprint, position);
      }

      Point3 normal = ConvertMaxVectorToEngine(
          TransformVectorByMatrix3(objectToNode,
                                   GetFaceCornerNormal(mesh, faceIndex, corner)));
      NormalizePoint3(&normal, Point3(0.0f, 1.0f, 0.0f));
      fingerprint = HashPoint3Value(fingerprint, normal);

      if (hasTexcoords) {
        const TVFace &tvFace = mesh.tvFace[faceIndex];
        const int texcoordIndex = tvFace.t[corner];
        if (texcoordIndex >= 0 && texcoordIndex < mesh.getNumTVerts()) {
          const UVVert &uv = mesh.tVerts[texcoordIndex];
          fingerprint = HashCombine(fingerprint, HashFloat(uv.x));
          fingerprint = HashCombine(fingerprint, HashFloat(uv.y));
          fingerprint = HashCombine(fingerprint, HashFloat(uv.z));
        }
      }
    }
  }
  fingerprint =
      HashCombine(fingerprint, static_cast<uint64_t>(node->GetObjectRef() != nullptr));
  snapshot->geometryFingerprint = fingerprint;

  if (needsDelete) {
    triObject->DeleteThis();
  }
}

std::filesystem::path GetPayloadRootDirectory() {
  std::error_code error;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path(error) / "project-render" /
      "max-livelink";
  if (error) {
    return {};
  }
  std::filesystem::create_directories(root, error);
  if (error) {
    return {};
  }
  return root;
}

bool ExportNodeAsNativeMeshPayload(Interface *ip, INode *node,
                                   const std::string &sessionId,
                                   const NodeSnapshot &snapshot,
                                   std::string *outPayloadUri) {
  if (!ip || !node || !snapshot.hasMesh || !outPayloadUri) {
    return false;
  }

  ScopedFlag exportGuard(&g_exportInProgress);

  TriObject *triObject = nullptr;
  bool needsDelete = false;
  if (!GetTriObjectForNode(ip, node, &triObject, &needsDelete)) {
    return false;
  }

  const std::filesystem::path rootDirectory = GetPayloadRootDirectory();
  if (rootDirectory.empty()) {
    if (needsDelete) {
      triObject->DeleteThis();
    }
    return false;
  }

  std::error_code error;
  const std::filesystem::path sessionDirectory = rootDirectory / sessionId;
  std::filesystem::create_directories(sessionDirectory, error);
  if (error) {
    if (needsDelete) {
      triObject->DeleteThis();
    }
    return false;
  }

  const std::filesystem::path payloadPath =
      sessionDirectory /
      (std::string("node-") + std::to_string(snapshot.handle) + "-" +
       std::to_string(snapshot.geometryFingerprint) + ".prmesh");

  std::ofstream stream(payloadPath, std::ios::binary | std::ios::trunc);
  if (!stream) {
    if (needsDelete) {
      triObject->DeleteThis();
    }
    return false;
  }

  Mesh &mesh = triObject->GetMesh();
  mesh.checkNormals(TRUE);
  const Matrix3 objectToNode = ComputeObjectToNodeTransform(ip, node);
  struct ExportSubmesh {
    int materialSlot = 0;
    std::vector<NativeMeshPayloadVertex> vertices;
    std::vector<uint32_t> indices;
  };
  std::vector<ExportSubmesh> submeshes;
  std::unordered_map<int, size_t> submeshBySlot;

  const bool hasTexcoords = mesh.tvFace != nullptr && mesh.tVerts != nullptr &&
                            mesh.getNumTVerts() > 0;
  Mtl *rootMaterial = node->GetMtl();
  const int subMaterialCount = rootMaterial ? rootMaterial->NumSubMtls() : 0;
  for (int faceIndex = 0; faceIndex < mesh.getNumFaces(); ++faceIndex) {
    Face &face = mesh.faces[faceIndex];
    int materialSlot = 0;
    if (subMaterialCount > 1) {
      materialSlot = face.getMatID();
      materialSlot = materialSlot > 0 ? (materialSlot - 1) : 0;
      materialSlot = (std::clamp)(materialSlot, 0, subMaterialCount - 1);
    }

    const auto [submeshIt, inserted] =
        submeshBySlot.emplace(materialSlot, submeshes.size());
    if (inserted) {
      ExportSubmesh submesh;
      submesh.materialSlot = materialSlot;
      submeshes.push_back(std::move(submesh));
    }
    ExportSubmesh &submesh = submeshes[submeshIt->second];

    const Point3 positions[3] = {
      ConvertMaxPointToEngine(
        TransformPointByMatrix3(objectToNode, mesh.verts[face.getVert(0)])),
      ConvertMaxPointToEngine(
        TransformPointByMatrix3(objectToNode, mesh.verts[face.getVert(1)])),
      ConvertMaxPointToEngine(
        TransformPointByMatrix3(objectToNode, mesh.verts[face.getVert(2)])),
    };
    const int vertexOrder[3] = {0, 1, 2};
    for (int corner = 0; corner < 3; ++corner) {
      NativeMeshPayloadVertex vertex;
      const int sourceCorner = vertexOrder[corner];
      const Point3 &position = positions[sourceCorner];
      Point3 normal = ConvertMaxVectorToEngine(
        TransformVectorByMatrix3(
          objectToNode, GetFaceCornerNormal(mesh, faceIndex, sourceCorner)));
      NormalizePoint3(&normal, Point3(0.0f, 1.0f, 0.0f));
      vertex.position[0] = position.x;
      vertex.position[1] = position.y;
      vertex.position[2] = position.z;
      vertex.normal[0] = normal.x;
      vertex.normal[1] = normal.y;
      vertex.normal[2] = normal.z;
      if (hasTexcoords) {
        const TVFace &tvFace = mesh.tvFace[faceIndex];
        const UVVert &uv = mesh.tVerts[tvFace.t[sourceCorner]];
        vertex.uv[0] = uv.x;
        vertex.uv[1] = 1.0f - uv.y;
      }
      submesh.indices.push_back(static_cast<uint32_t>(submesh.vertices.size()));
      submesh.vertices.push_back(vertex);
    }
  }

  NativeMeshPayloadHeader header;
  header.meshCount = static_cast<uint32_t>(submeshes.size());
  stream.write(reinterpret_cast<const char *>(&header), sizeof(header));
  for (const ExportSubmesh &submesh : submeshes) {
    NativeMeshPayloadMeshHeader meshHeader;
    meshHeader.vertexCount = static_cast<uint32_t>(submesh.vertices.size());
    meshHeader.indexCount = static_cast<uint32_t>(submesh.indices.size());
    meshHeader.materialSlot = submesh.materialSlot;
    stream.write(reinterpret_cast<const char *>(&meshHeader), sizeof(meshHeader));
    if (!submesh.vertices.empty()) {
      stream.write(reinterpret_cast<const char *>(submesh.vertices.data()),
                   static_cast<std::streamsize>(submesh.vertices.size() *
                                                sizeof(submesh.vertices[0])));
    }
    if (!submesh.indices.empty()) {
      stream.write(reinterpret_cast<const char *>(submesh.indices.data()),
                   static_cast<std::streamsize>(submesh.indices.size() *
                                                sizeof(submesh.indices[0])));
    }
  }

  if (needsDelete) {
    triObject->DeleteThis();
  }
  if (!stream.good()) {
    return false;
  }

  *outPayloadUri = PathToUtf8(payloadPath);
  return true;
}

json MakeObjectId(const std::string &documentId, const std::string &objectId,
                  const char *objectType) {
  return json{
      {"sourceApp", kSourceApp},
      {"documentId", documentId},
      {"objectId", objectId},
      {"objectType", objectType},
  };
}

NodeSnapshot CaptureNodeSnapshot(Interface *ip, INode *node) {
  NodeSnapshot snapshot;
  if (!ip || !node) {
    return snapshot;
  }

  snapshot.handle = node->GetHandle();
  snapshot.parentHandle =
      (node->GetParentNode() && !node->GetParentNode()->IsRootNode())
          ? node->GetParentNode()->GetHandle()
          : 0;
  snapshot.name = ToUtf8(node->GetName());
  snapshot.visible = !node->IsNodeHidden(TRUE);
  snapshot.worldMatrix = Matrix3ToColumnMajor4x4(node->GetNodeTM(ip->GetTime()));
  CaptureMeshSnapshot(ip, node, &snapshot);
  return snapshot;
}

void GatherNodeSnapshots(Interface *ip, INode *node,
                         std::unordered_map<ULONG_PTR, NodeSnapshot> *outState) {
  if (!ip || !node || !outState) {
    return;
  }

  NodeSnapshot snapshot = CaptureNodeSnapshot(ip, node);
  outState->insert_or_assign(snapshot.handle, snapshot);
  for (int childIndex = 0; childIndex < node->NumberOfChildren(); ++childIndex) {
    GatherNodeSnapshots(ip, node->GetChildNode(childIndex), outState);
  }
}

void AppendNodeAddedDelta(const std::string &documentId,
                          const NodeSnapshot &snapshot, uint64_t *revision,
                          json *outDeltas) {
  if (!revision || !outDeltas) {
    return;
  }

  const std::string objectId = "node:" + std::to_string(snapshot.handle);
  const std::string parentId = snapshot.parentHandle != 0
                                   ? "node:" + std::to_string(snapshot.parentHandle)
                                   : std::string();
  outDeltas->push_back(json{{"kind", "NodeAdded"},
                            {"target", MakeObjectId(documentId, objectId, "Node")},
                            {"revision", (*revision)++},
                            {"debugLabel", snapshot.name},
                            {"payload", json{{"parentObjectId", parentId},
                                              {"displayName", snapshot.name}}}});
}

void AppendNodeTransformDelta(const std::string &documentId,
                              const NodeSnapshot &snapshot, uint64_t *revision,
                              json *outDeltas) {
  if (!revision || !outDeltas) {
    return;
  }

  const std::string objectId = "node:" + std::to_string(snapshot.handle);
  outDeltas->push_back(
      json{{"kind", "NodeTransformChanged"},
           {"target", MakeObjectId(documentId, objectId, "Node")},
           {"revision", (*revision)++},
           {"debugLabel", snapshot.name},
           {"payload", json{{"worldMatrix", snapshot.worldMatrix}}}});
}

void AppendNodeVisibilityDelta(const std::string &documentId,
                               const NodeSnapshot &snapshot, uint64_t *revision,
                               json *outDeltas) {
  if (!revision || !outDeltas) {
    return;
  }

  const std::string objectId = "node:" + std::to_string(snapshot.handle);
  outDeltas->push_back(
      json{{"kind", "NodeVisibilityChanged"},
           {"target", MakeObjectId(documentId, objectId, "Node")},
           {"revision", (*revision)++},
           {"debugLabel", snapshot.name},
           {"payload", json{{"visible", snapshot.visible}}}});
}

void AppendNodeRemovedDelta(const std::string &documentId, ULONG_PTR handle,
                            uint64_t *revision, json *outDeltas) {
  if (!revision || !outDeltas) {
    return;
  }

  const std::string objectId = "node:" + std::to_string(handle);
  outDeltas->push_back(
      json{{"kind", "NodeRemoved"},
           {"target", MakeObjectId(documentId, objectId, "Node")},
           {"revision", (*revision)++},
           {"debugLabel", objectId},
           {"payload", json{{"removeChildren", true}}}});
}

void AppendMeshPayloadDelta(const std::string &documentId,
                            const NodeSnapshot &snapshot,
                            const std::string &payloadUri,
                            uint64_t *revision, json *outDeltas) {
  if (!revision || !outDeltas || payloadUri.empty()) {
    return;
  }

  const std::string objectId = "node:" + std::to_string(snapshot.handle);
  outDeltas->push_back(json{{"kind", "MeshPayloadChanged"},
                            {"target", MakeObjectId(documentId, objectId, "Node")},
                            {"revision", (*revision)++},
                            {"debugLabel", snapshot.name},
                            {"payload", json{{"geometryRevision", snapshot.geometryFingerprint},
                                              {"vertexCount", snapshot.vertexCount},
                                              {"indexCount", snapshot.indexCount},
                                              {"topologyChanged", true},
                                              {"payloadUri", payloadUri},
                                              {"payloadHash", std::to_string(snapshot.geometryFingerprint)}}}});
}

void AppendMeshPayloadDeltaIfAvailable(Interface *ip,
                                       const std::string &sessionId,
                                       const std::string &documentId,
                                       const NodeSnapshot &snapshot,
                                       uint64_t *revision, json *outDeltas) {
  if (!snapshot.hasMesh || !ip) {
    return;
  }

  INode *node = FindNodeByHandle(ip, snapshot.handle);
  if (!node) {
    return;
  }

  std::string payloadUri;
  if (!ExportNodeAsNativeMeshPayload(ip, node, sessionId, snapshot,
                                     &payloadUri)) {
    return;
  }

  AppendMeshPayloadDelta(documentId, snapshot, payloadUri, revision, outDeltas);
}

bool CaptureActiveCameraSnapshot(Interface *ip, CameraSnapshot *outSnapshot) {
  if (!ip || !outSnapshot) {
    return false;
  }

  ViewExp &view = ip->GetActiveViewExp();

  CameraSnapshot snapshot;
  snapshot.valid = true;

  Matrix3 viewAffine;
  view.GetAffineTM(viewAffine);
  const Matrix3 worldTM = Inverse(viewAffine);
  Point3 position = ConvertMaxPointToEngine(worldTM.GetRow(3));
  Point3 forward = ConvertMaxVectorToEngine(-worldTM.GetRow(2));
  Point3 up = ConvertMaxVectorToEngine(worldTM.GetRow(1));

  NormalizePoint3(&forward, Point3(0.0f, 0.0f, -1.0f));
  NormalizePoint3(&up, Point3(0.0f, 1.0f, 0.0f));

  snapshot.position = Point3ToArray(position);
  snapshot.forward = Point3ToArray(forward);
  snapshot.up = Point3ToArray(up);
  snapshot.fovDegrees = view.GetFOV() * (180.0f / 3.14159265359f);
  snapshot.nearPlane = 0.01f;
  snapshot.farPlane = 1000.0f;
  *outSnapshot = snapshot;
  return true;
}

void AppendCameraDelta(const std::string &documentId,
                       const CameraSnapshot &snapshot, uint64_t *revision,
                       json *outDeltas) {
  if (!revision || !outDeltas || !snapshot.valid) {
    return;
  }

  outDeltas->push_back(json{{"kind", "CameraChanged"},
                            {"target", MakeObjectId(documentId, MakeCameraObjectId(), "Camera")},
                            {"revision", (*revision)++},
                            {"debugLabel", "3ds Max active camera"},
                            {"payload", json{{"position", snapshot.position},
                                              {"forward", snapshot.forward},
                                              {"up", snapshot.up},
                                              {"fovDegrees", snapshot.fovDegrees},
                                              {"nearPlane", snapshot.nearPlane},
                                              {"farPlane", snapshot.farPlane}}}});
}

void AppendMaterialDelta(const std::string &documentId,
                         const MaterialSnapshot &snapshot, uint64_t *revision,
                         json *outDeltas) {
  if (!revision || !outDeltas || !snapshot.valid) {
    return;
  }

  outDeltas->push_back(json{{"kind", "MaterialChanged"},
                            {"target", MakeObjectId(documentId, snapshot.objectId, "Material")},
                            {"revision", (*revision)++},
                            {"debugLabel", snapshot.name},
                            {"payload", json{{"parametersChanged", true},
                                              {"texturesChanged", true},
                                              {"materialModel", snapshot.materialModel},
                                              {"baseColor", snapshot.baseColor},
                                              {"baseColorTextureUri", snapshot.baseColorTextureUri},
                                              {"emissiveColor", snapshot.emissiveColor},
                                              {"normalTextureUri", snapshot.normalTextureUri},
                                              {"emissiveTextureUri", snapshot.emissiveTextureUri},
                                              {"emissiveIntensity", snapshot.emissiveIntensity},
                                              {"roughness", snapshot.roughness},
                                              {"metalness", snapshot.metalness},
                                              {"specularWeight", snapshot.specularWeight},
                                              {"ior", snapshot.ior},
                                              {"transmissionWeight", snapshot.transmissionWeight},
                                              {"transmissionColor", snapshot.transmissionColor},
                                              {"coatWeight", snapshot.coatWeight},
                                              {"coatRoughness", snapshot.coatRoughness},
                                              {"thinWalled", snapshot.thinWalled},
                                              {"translucency", snapshot.translucency},
                                              {"uvScale", snapshot.uvScale},
                                              {"uvOffset", snapshot.uvOffset},
                                              {"doubleSided", snapshot.doubleSided},
                                              {"alphaMode", snapshot.alphaMode}}}});
}

void AppendMaterialRemovedDelta(const std::string &documentId,
                                const MaterialSnapshot &snapshot,
                                uint64_t *revision, json *outDeltas) {
  if (!revision || !outDeltas || !snapshot.valid) {
    return;
  }

  outDeltas->push_back(json{{"kind", "NodeRemoved"},
                            {"target", MakeObjectId(documentId, snapshot.objectId, "Material")},
                            {"revision", (*revision)++},
                            {"debugLabel", snapshot.name},
                            {"payload", json{{"removeChildren", true}}}});
}

void AppendLightDelta(const std::string &documentId,
                      const LightSnapshot &snapshot, uint64_t *revision,
                      json *outDeltas) {
  if (!revision || !outDeltas || !snapshot.valid) {
    return;
  }

  outDeltas->push_back(json{{"kind", "LightChanged"},
                            {"target", MakeObjectId(documentId, snapshot.objectId, "Light")},
                            {"revision", (*revision)++},
                            {"debugLabel", snapshot.name},
                            {"payload", json{{"lightType", snapshot.lightType},
                                              {"intensity", snapshot.intensity},
                                              {"color", snapshot.color},
                                              {"position", snapshot.position},
                                              {"direction", snapshot.direction},
                                              {"radius", snapshot.radius},
                                              {"innerConeDegrees", snapshot.innerConeDegrees},
                                              {"outerConeDegrees", snapshot.outerConeDegrees},
                                              {"areaExtents", snapshot.areaExtents}}}});
}

void AppendLightRemovedDelta(const std::string &documentId,
                             const LightSnapshot &snapshot, uint64_t *revision,
                             json *outDeltas) {
  if (!revision || !outDeltas || !snapshot.valid) {
    return;
  }

  outDeltas->push_back(json{{"kind", "NodeRemoved"},
                            {"target", MakeObjectId(documentId, snapshot.objectId, "Light")},
                            {"revision", (*revision)++},
                            {"debugLabel", snapshot.name},
                            {"payload", json{{"removeChildren", true}}}});
}

bool EnsurePipeConnected() {
  return g_pipeClient.IsConnected() || g_pipeClient.Connect(kPipeName);
}

bool SendBatch(const std::string &sessionId, uint64_t sequence, bool fullSync,
               const json &deltas) {
  if (sessionId.empty() || !EnsurePipeConnected()) {
    return false;
  }

  json batch;
  batch["providerName"] = "3dsMax2025Pipe";
  batch["sessionId"] = sessionId;
  batch["sequence"] = sequence;
  batch["fullSync"] = fullSync;
  batch["deltas"] = deltas;
  return g_pipeClient.SendJsonLine(batch.dump());
}

bool SendInitialSnapshot(Interface *ip,
                         std::unordered_map<ULONG_PTR, NodeSnapshot> *outState,
                         MaterialStateMap *outMaterialState,
                         std::unordered_map<ULONG_PTR, LightSnapshot> *outLightState,
                         std::string *outSessionId,
                         std::string *outDocumentId,
                         uint64_t *outNextSequence,
                         uint64_t *outNextRevision) {
  if (!ip || !EnsurePipeConnected()) {
    return false;
  }

  const std::string documentId = MakeDocumentId(ip);
  const std::string sessionId = MakeSessionId();

  std::unordered_map<ULONG_PTR, NodeSnapshot> state;
  MaterialStateMap materialState;
  std::unordered_map<ULONG_PTR, LightSnapshot> lightState;
  if (INode *root = ip->GetRootNode()) {
    for (int childIndex = 0; childIndex < root->NumberOfChildren(); ++childIndex) {
      GatherNodeSnapshots(ip, root->GetChildNode(childIndex), &state);
      GatherLightSnapshots(ip, root->GetChildNode(childIndex), &lightState);
    }
  }
  GatherMaterialSnapshots(ip, state, &materialState);

  json deltas = json::array();
  deltas.push_back(json{
      {"kind", "SessionOpened"},
      {"target", json{{"sourceApp", kSourceApp},
                       {"documentId", documentId},
                       {"objectId", "session"},
                       {"objectType", "Unknown"}}},
      {"payload", json{{"documentPath", documentId},
                        {"displayName", documentId}}},
  });
  deltas.push_back(json{
      {"kind", "FullSceneSync"},
      {"payload", json{{"clearsExistingScene", false}}},
  });

  uint64_t revision = 1;
  CameraSnapshot cameraSnapshot;
  CaptureActiveCameraSnapshot(ip, &cameraSnapshot);
  for (const auto &[_, snapshot] : state) {
    AppendNodeAddedDelta(documentId, snapshot, &revision, &deltas);
    AppendNodeTransformDelta(documentId, snapshot, &revision, &deltas);
    AppendNodeVisibilityDelta(documentId, snapshot, &revision, &deltas);
    AppendMeshPayloadDeltaIfAvailable(ip, sessionId, documentId, snapshot,
                                      &revision, &deltas);
  }
  for (const auto &[_, snapshot] : materialState) {
    AppendMaterialDelta(documentId, snapshot, &revision, &deltas);
  }
  for (const auto &[_, snapshot] : lightState) {
    AppendLightDelta(documentId, snapshot, &revision, &deltas);
  }
  AppendCameraDelta(documentId, cameraSnapshot, &revision, &deltas);

  if (!SendBatch(sessionId, 1, true, deltas)) {
    return false;
  }

  if (outState) {
    *outState = std::move(state);
  }
  if (outMaterialState) {
    *outMaterialState = std::move(materialState);
  }
  if (outLightState) {
    *outLightState = std::move(lightState);
  }
  if (outSessionId) {
    *outSessionId = sessionId;
  }
  if (outDocumentId) {
    *outDocumentId = documentId;
  }
  if (outNextSequence) {
    *outNextSequence = 2;
  }
  if (outNextRevision) {
    *outNextRevision = revision;
  }
  return true;
}

void SendSessionClosed(const std::string &sessionId, uint64_t sequence) {
  if (sessionId.empty() || !g_pipeClient.IsConnected()) {
    return;
  }

  SendBatch(sessionId, sequence, false,
            json::array({json{{"kind", "SessionClosed"},
                              {"payload", json{{"reason", "3ds Max utility closed"},
                                                {"graceful", true}}}}}));
}

class ProjectRenderLiveLinkUtility final : public UtilityObj {
public:
  void BeginEditParams(Interface *ip, IUtil *iu) override {
    m_interface = ip;
    m_iu = iu;
    if (!m_rollupHwnd && ip) {
      m_rollupHwnd = ip->AddRollupPage(
          g_instance, MAKEINTRESOURCE(kUtilityDialogId),
          &ProjectRenderLiveLinkUtility::RollupDlgProc,
          _T("project-render LiveLink"), reinterpret_cast<LPARAM>(this));
    }
    RefreshRollupUI();
  }

  void EndEditParams(Interface *ip, IUtil * /*iu*/) override {
    if (ip && m_rollupHwnd) {
      ip->DeleteRollupPage(m_rollupHwnd);
    }
    m_rollupHwnd = nullptr;
    m_interface = nullptr;
    m_iu = nullptr;
  }

  void SelectionSetChanged(Interface *ip, IUtil *iu) override {
    if (g_exportInProgress.load()) {
      return;
    }
    UtilityObj::SelectionSetChanged(ip, iu);
    SendSelectionDeltaIfNeeded();
  }

  void DeleteThis() override {}

private:
  static INT_PTR CALLBACK RollupDlgProc(HWND hwnd, UINT message, WPARAM wParam,
                                        LPARAM lParam) {
    ProjectRenderLiveLinkUtility *utility =
        reinterpret_cast<ProjectRenderLiveLinkUtility *>(
            GetWindowLongPtr(hwnd, GWLP_USERDATA));

    if (message == WM_INITDIALOG) {
      utility = reinterpret_cast<ProjectRenderLiveLinkUtility *>(lParam);
      SetWindowLongPtr(hwnd, GWLP_USERDATA,
                       reinterpret_cast<LONG_PTR>(utility));
      if (utility) {
        utility->m_rollupHwnd = hwnd;
        utility->RefreshRollupUI();
      }
      return TRUE;
    }

    if (!utility) {
      return FALSE;
    }

    if (message == WM_COMMAND) {
      switch (LOWORD(wParam)) {
      case kStartControlId:
        utility->StartLiveSync();
        return TRUE;
      case kStopControlId:
        utility->StopLiveSync();
        return TRUE;
      default:
        break;
      }
    }

    return FALSE;
  }

  static void CALLBACK PollTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    if (g_exportInProgress.load()) {
      return;
    }
    g_utility.PollSceneChanges();
  }

  Interface *GetLiveInterface() const {
    return m_interface ? m_interface : GetCOREInterface();
  }

  void RefreshRollupUI_NoLock() {
    HWND rollupHwnd = m_rollupHwnd;
    if (!rollupHwnd) {
      return;
    }

    const std::wstring statusText =
        m_syncActive
            ? Utf8ToWString(std::string("Background sync is active. Session: ") +
                            m_sessionId)
            : std::wstring(L"Background sync is inactive.");

    EnableWindow(GetDlgItem(rollupHwnd, kStartControlId),
           m_syncActive ? FALSE : TRUE);
    EnableWindow(GetDlgItem(rollupHwnd, kStopControlId),
                 m_syncActive ? TRUE : FALSE);

    SetDlgItemTextW(rollupHwnd, kStatusControlId, statusText.c_str());
  }

  void RefreshRollupUI() {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    RefreshRollupUI_NoLock();
  }

  bool EnsureConnectedSession(Interface *ip) {
    if (!ip) {
      return false;
    }

    const bool hadPipeConnection = g_pipeClient.IsConnected();
    if (!EnsurePipeConnected()) {
      return false;
    }

    if (hadPipeConnection && !m_sessionId.empty() && !m_documentId.empty()) {
      return true;
    }

    if (!SendInitialSnapshot(ip, &m_lastNodeState, &m_lastMaterialState,
                             &m_lastLightState, &m_sessionId, &m_documentId,
                             &m_nextSequence, &m_nextRevision)) {
      return false;
    }

    m_lastSelectedObjectIds = GatherSelectedObjectIds(ip);
    CaptureActiveCameraSnapshot(ip, &m_lastCameraSnapshot);
    return true;
  }

  bool StartLiveSync() {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (m_syncActive) {
      RefreshRollupUI_NoLock();
      return true;
    }

    Interface *ip = GetLiveInterface();
    if (!ip) {
      RefreshRollupUI_NoLock();
      return false;
    }

    if (!EnsureConnectedSession(ip)) {
      RefreshRollupUI_NoLock();
      return false;
    }

    if (m_pollTimer == 0) {
      m_pollTimer = SetTimer(nullptr, kPollTimerId, kPollIntervalMs,
                             &ProjectRenderLiveLinkUtility::PollTimerProc);
    }
    m_syncActive = true;
    RefreshRollupUI_NoLock();
    return true;
  }

  void StopLiveSync() {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (!m_syncActive) {
      RefreshRollupUI_NoLock();
      return;
    }

    if (m_pollTimer != 0) {
      KillTimer(nullptr, m_pollTimer);
      m_pollTimer = 0;
    }
    SendSessionClosed(m_sessionId, m_nextSequence++);
    g_pipeClient.Disconnect();
    m_lastNodeState.clear();
    m_lastMaterialState.clear();
    m_lastLightState.clear();
    m_lastSelectedObjectIds.clear();
    m_lastCameraSnapshot = CameraSnapshot{};
    m_sessionId.clear();
    m_documentId.clear();
    m_nextSequence = 1;
    m_nextRevision = 1;
    m_syncActive = false;
    RefreshRollupUI_NoLock();
  }

  void AppendSelectionDelta(const std::vector<std::string> &selectedObjectIds,
                            json *outDeltas) {
    if (!outDeltas || m_documentId.empty()) {
      return;
    }

    outDeltas->push_back(json{{"kind", "SelectionChanged"},
                              {"target", MakeObjectId(m_documentId, "selection", "Selection")},
                              {"revision", m_nextRevision++},
                              {"debugLabel", "3ds Max selection"},
                              {"payload", json{{"selectedObjectIds", selectedObjectIds}}}});
  }

  void PollSceneChanges() {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    Interface *ip = GetLiveInterface();
    if (!m_syncActive || !ip || !EnsureConnectedSession(ip)) {
      return;
    }

    std::unordered_map<ULONG_PTR, NodeSnapshot> currentState;
    MaterialStateMap currentMaterialState;
    std::unordered_map<ULONG_PTR, LightSnapshot> currentLightState;
    if (INode *root = ip->GetRootNode()) {
      for (int childIndex = 0; childIndex < root->NumberOfChildren(); ++childIndex) {
        GatherNodeSnapshots(ip, root->GetChildNode(childIndex), &currentState);
        GatherLightSnapshots(ip, root->GetChildNode(childIndex), &currentLightState);
      }
    }
    GatherMaterialSnapshots(ip, currentState, &currentMaterialState);

    json deltas = json::array();
    CameraSnapshot currentCamera;
    CaptureActiveCameraSnapshot(ip, &currentCamera);
    for (const auto &[handle, snapshot] : currentState) {
      auto previousIt = m_lastNodeState.find(handle);
      if (previousIt == m_lastNodeState.end()) {
        AppendNodeAddedDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
        AppendNodeTransformDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
        AppendNodeVisibilityDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
        AppendMeshPayloadDeltaIfAvailable(ip, m_sessionId, m_documentId,
                                          snapshot, &m_nextRevision, &deltas);
        continue;
      }

      const NodeSnapshot &previous = previousIt->second;
      if (previous.parentHandle != snapshot.parentHandle ||
          previous.name != snapshot.name) {
        AppendNodeAddedDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
      }
      if (!SameMatrix(previous.worldMatrix, snapshot.worldMatrix)) {
        AppendNodeTransformDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
      }
      if (previous.visible != snapshot.visible) {
        AppendNodeVisibilityDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
      }
      if (snapshot.hasMesh && (!previous.hasMesh ||
                               previous.geometryFingerprint != snapshot.geometryFingerprint)) {
        AppendMeshPayloadDeltaIfAvailable(ip, m_sessionId, m_documentId,
                                          snapshot, &m_nextRevision, &deltas);
      }

    }

    for (const auto &[objectId, snapshot] : currentMaterialState) {
      const auto previousMaterialIt = m_lastMaterialState.find(objectId);
      if (previousMaterialIt == m_lastMaterialState.end() ||
          !SameMaterial(snapshot, previousMaterialIt->second)) {
        AppendMaterialDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
      }
    }

    for (const auto &[handle, _] : m_lastNodeState) {
      if (currentState.find(handle) == currentState.end()) {
        AppendNodeRemovedDelta(m_documentId, handle, &m_nextRevision, &deltas);
      }
    }

    for (const auto &[objectId, snapshot] : m_lastMaterialState) {
      if (currentMaterialState.find(objectId) == currentMaterialState.end()) {
        AppendMaterialRemovedDelta(m_documentId, snapshot, &m_nextRevision,
                                   &deltas);
      }
    }

    for (const auto &[handle, snapshot] : currentLightState) {
      const auto previousIt = m_lastLightState.find(handle);
      if (previousIt == m_lastLightState.end() ||
          !SameLight(previousIt->second, snapshot)) {
        AppendLightDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
      }
    }

    for (const auto &[handle, snapshot] : m_lastLightState) {
      if (currentLightState.find(handle) == currentLightState.end()) {
        AppendLightRemovedDelta(m_documentId, snapshot, &m_nextRevision,
                                &deltas);
      }
    }

    const std::vector<std::string> selectedObjectIds = GatherSelectedObjectIds(ip);
    const bool selectionChanged = selectedObjectIds != m_lastSelectedObjectIds;
    if (selectionChanged) {
      AppendSelectionDelta(selectedObjectIds, &deltas);
    }
    if (currentCamera.valid && !SameCamera(currentCamera, m_lastCameraSnapshot)) {
      AppendCameraDelta(m_documentId, currentCamera, &m_nextRevision, &deltas);
    }

    if (!deltas.empty() && SendBatch(m_sessionId, m_nextSequence, false, deltas)) {
      ++m_nextSequence;
      m_lastNodeState = std::move(currentState);
      m_lastMaterialState = std::move(currentMaterialState);
      m_lastLightState = std::move(currentLightState);
      m_lastSelectedObjectIds = selectedObjectIds;
      m_lastCameraSnapshot = currentCamera;
    } else if (deltas.empty()) {
      m_lastNodeState = std::move(currentState);
      m_lastMaterialState = std::move(currentMaterialState);
      m_lastLightState = std::move(currentLightState);
      m_lastSelectedObjectIds = selectedObjectIds;
      m_lastCameraSnapshot = currentCamera;
    }

    RefreshRollupUI_NoLock();
  }

  void SendSelectionDeltaIfNeeded() {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    Interface *ip = GetLiveInterface();
    if (!m_syncActive || !ip || !EnsureConnectedSession(ip)) {
      return;
    }

    const std::vector<std::string> selectedObjectIds = GatherSelectedObjectIds(ip);
    if (selectedObjectIds == m_lastSelectedObjectIds) {
      return;
    }

    json deltas = json::array();
    AppendSelectionDelta(selectedObjectIds, &deltas);
    if (SendBatch(m_sessionId, m_nextSequence, false, deltas)) {
      ++m_nextSequence;
      m_lastSelectedObjectIds = selectedObjectIds;
    }

    RefreshRollupUI_NoLock();
  }

  Interface *m_interface = nullptr;
  IUtil *m_iu = nullptr;
  HWND m_rollupHwnd = nullptr;
  std::mutex m_sendMutex;
  bool m_syncActive = false;
  UINT_PTR m_pollTimer = 0;
  std::unordered_map<ULONG_PTR, NodeSnapshot> m_lastNodeState;
  MaterialStateMap m_lastMaterialState;
  std::unordered_map<ULONG_PTR, LightSnapshot> m_lastLightState;
  std::vector<std::string> m_lastSelectedObjectIds;
  CameraSnapshot m_lastCameraSnapshot;
  std::string m_sessionId;
  std::string m_documentId;
  uint64_t m_nextSequence = 1;
  uint64_t m_nextRevision = 1;
};

ProjectRenderLiveLinkUtility g_utility;

class ProjectRenderLiveLinkClassDesc final : public ClassDesc2 {
public:
  int IsPublic() override { return TRUE; }
  void *Create(BOOL /*loading*/) override { return &g_utility; }
  const TCHAR *ClassName() override { return _T("project-render LiveLink"); }
  const TCHAR *NonLocalizedClassName() override {
    return _T("project-render LiveLink");
  }
  SClass_ID SuperClassID() override { return UTILITY_CLASS_ID; }
  Class_ID ClassID() override { return Class_ID(0x5e5824a1, 0x3a0f6b4d); }
  const TCHAR *Category() override { return _T("project-render"); }
  const TCHAR *InternalName() override { return _T("ProjectRenderLiveLink"); }
  HINSTANCE HInstance() override { return g_instance; }
};

ProjectRenderLiveLinkClassDesc g_classDesc;

} // namespace

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID /*lpvReserved*/) {
  if (fdwReason == DLL_PROCESS_ATTACH) {
    g_instance = hinstDLL;
    DisableThreadLibraryCalls(hinstDLL);
  }
  return TRUE;
}

extern "C" __declspec(dllexport) const TCHAR *LibDescription() {
  return _T("project-render LiveLink for 3ds Max 2025");
}

extern "C" __declspec(dllexport) int LibNumberClasses() { return 1; }

extern "C" __declspec(dllexport) ClassDesc *LibClassDesc(int index) {
  return index == 0 ? &g_classDesc : nullptr;
}

extern "C" __declspec(dllexport) ULONG LibVersion() { return VERSION_3DSMAX; }

extern "C" __declspec(dllexport) ULONG CanAutoDefer() { return 1; }