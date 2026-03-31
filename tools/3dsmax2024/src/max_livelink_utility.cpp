#include "max_livelink_pipe_client.h"

#include <max.h>
#include <utilapi.h>
#include <iparamm2.h>
#include <notify.h>
#include <AppDataChunk.h>
#include <pbbitmap.h>
#include <stdmat.h>
#include <units.h>
#include <ISceneEventManager.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <objbase.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

constexpr Class_ID kPersistentIdAppDataClassId(0x21436f0a, 0x5c7a19d1);
constexpr DWORD kSceneGuidAppDataSubId = 0x1001;
constexpr DWORD kNodeGuidAppDataSubId = 0x1002;
constexpr DWORD kResumeStateAppDataSubId = 0x1003;
constexpr DWORD kMaterialGuidAppDataSubId = 0x1004;
constexpr DWORD kSharedObjectGuidAppDataSubId = 0x1005;
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
constexpr const char *kSourceApp = "3dsMax2024";
constexpr UINT_PTR kPollTimerId = 0x5052;
constexpr UINT_PTR kCameraPollTimerId = 0x5053;
constexpr UINT kPollIntervalMs = 50;
constexpr UINT kCameraPollIntervalMs = 33;
constexpr uint64_t kActivePollMinIntervalMs = 125;
constexpr uint64_t kIdlePollMinIntervalMs = 400;
constexpr uint64_t kHeavyPollMinIntervalMs = 1000;
constexpr uint64_t kReconnectPollMinIntervalMs = 1000;
constexpr uint64_t kCameraPollMinIntervalMs = 33;
constexpr uint64_t kTransformVerificationMinIntervalMs = 1500;
constexpr uint64_t kLargeSceneTransformVerificationMinIntervalMs = 4000;
constexpr uint64_t kHugeSceneTransformVerificationMinIntervalMs = 8000;
constexpr uint64_t kSceneOperationSettleDelayMs = 350;
constexpr uint64_t kResumeStatePersistDelayMs = 2500;
constexpr size_t kPayloadRemovalBatchSize = 64;
constexpr uint64_t kSlowPollThresholdMs = 150;
constexpr size_t kLargeSceneNodeThreshold = 250;
constexpr size_t kHugeSceneNodeThreshold = 1500;
constexpr int kUtilityDialogId = 101;
constexpr int kStatusControlId = 1001;
constexpr int kStartControlId = 1002;
constexpr int kStopControlId = 1003;
constexpr int kStartFullControlId = 1004;
constexpr int kDetailsControlId = 1005;

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

json MakeObjectId(const std::string &documentId, const std::string &objectId,
                  const char *objectType);

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

std::string GenerateGuidString() {
  GUID guid = {};
  if (CoCreateGuid(&guid) != S_OK) {
    return {};
  }

  wchar_t buffer[64] = {};
  const int length = StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer)));
  if (length <= 1) {
    return {};
  }

  std::wstring wide(buffer, static_cast<size_t>(length - 1));
  if (!wide.empty() && wide.front() == L'{') {
    wide.erase(wide.begin());
  }
  if (!wide.empty() && wide.back() == L'}') {
    wide.pop_back();
  }

  const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                             static_cast<int>(wide.size()),
                                             nullptr, 0, nullptr, nullptr);
  if (utf8Length <= 0) {
    return {};
  }

  std::string result(static_cast<size_t>(utf8Length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                      result.data(), utf8Length, nullptr, nullptr);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return result;
}

std::string ReadAppDataString(Animatable *owner, DWORD subId) {
  if (!owner) {
    return {};
  }

  AppDataChunk *chunk =
      owner->GetAppDataChunk(kPersistentIdAppDataClassId, UTILITY_CLASS_ID, subId);
  if (!chunk || !chunk->data || chunk->length == 0) {
    return {};
  }

  const char *data = static_cast<const char *>(chunk->data);
  size_t length = 0;
  while (length < chunk->length && data[length] != '\0') {
    ++length;
  }
  return std::string(data, length);
}

bool WriteAppDataString(Animatable *owner, DWORD subId, const std::string &value) {
  if (!owner || value.empty()) {
    return false;
  }

  owner->RemoveAppDataChunk(kPersistentIdAppDataClassId, UTILITY_CLASS_ID, subId);
  void *data = MAX_malloc(static_cast<DWORD>(value.size() + 1));
  if (!data) {
    return false;
  }
  std::memcpy(data, value.c_str(), value.size() + 1);
  owner->AddAppDataChunk(kPersistentIdAppDataClassId, UTILITY_CLASS_ID, subId,
                         static_cast<DWORD>(value.size() + 1), data);
  return true;
}

void ClearAppDataString(Animatable *owner, DWORD subId) {
  if (!owner) {
    return;
  }
  owner->RemoveAppDataChunk(kPersistentIdAppDataClassId, UTILITY_CLASS_ID, subId);
}

std::string GetOrCreateNodeGuid(INode *node) {
  if (!node) {
    return {};
  }

  std::string guid = ReadAppDataString(node, kNodeGuidAppDataSubId);
  if (guid.empty()) {
    guid = GenerateGuidString();
    if (!guid.empty()) {
      WriteAppDataString(node, kNodeGuidAppDataSubId, guid);
    }
  }
  return guid.empty() ? std::string("node:missing") : std::string("node:") + guid;
}

std::string GetOrCreateSceneGuid(Interface *ip) {
  if (!ip) {
    return {};
  }

  Animatable *sceneOwner = ip->GetRootNode();
  std::string guid = ReadAppDataString(sceneOwner, kSceneGuidAppDataSubId);
  if (guid.empty()) {
    guid = GenerateGuidString();
    if (!guid.empty()) {
      WriteAppDataString(sceneOwner, kSceneGuidAppDataSubId, guid);
    }
  }
  return guid.empty() ? std::string("scene:unsaved") : std::string("scene:") + guid;
}

void GatherNodeGuidUsage(INode *node,
                         std::unordered_map<std::string, size_t> *usageCounts) {
  if (!node || !usageCounts) {
    return;
  }

  std::string guid = ReadAppDataString(node, kNodeGuidAppDataSubId);
  if (!guid.empty()) {
    ++(*usageCounts)[guid];
  }
  for (int childIndex = 0; childIndex < node->NumberOfChildren(); ++childIndex) {
    GatherNodeGuidUsage(node->GetChildNode(childIndex), usageCounts);
  }
}

void EnsureUniqueNodeGuid(INode *node, std::unordered_set<std::string> *seenGuids) {
  if (!node || !seenGuids) {
    return;
  }

  std::string rawGuid = ReadAppDataString(node, kNodeGuidAppDataSubId);
  if (rawGuid.empty() || seenGuids->find(rawGuid) != seenGuids->end()) {
    do {
      rawGuid = GenerateGuidString();
    } while (!rawGuid.empty() && seenGuids->find(rawGuid) != seenGuids->end());
    if (!rawGuid.empty()) {
      WriteAppDataString(node, kNodeGuidAppDataSubId, rawGuid);
    }
  }

  if (!rawGuid.empty()) {
    seenGuids->insert(rawGuid);
  }
}

void EnsureUniqueNodeGuidsRecursive(
    INode *node, std::unordered_set<std::string> *seenGuids) {
  if (!node || !seenGuids) {
    return;
  }

  EnsureUniqueNodeGuid(node, seenGuids);
  for (int childIndex = 0; childIndex < node->NumberOfChildren(); ++childIndex) {
    EnsureUniqueNodeGuidsRecursive(node->GetChildNode(childIndex), seenGuids);
  }
}

void EnsureUniqueMaterialGuidsRecursive(
    Mtl *material, std::unordered_map<std::string, Mtl *> *owners,
    std::unordered_set<Mtl *> *visited) {
  if (!material || !owners || !visited || !visited->insert(material).second) {
    return;
  }

  std::string rawGuid = ReadAppDataString(material, kMaterialGuidAppDataSubId);
  auto needsNewGuid = [&]() {
    if (rawGuid.empty()) {
      return true;
    }
    const auto it = owners->find(rawGuid);
    return it != owners->end() && it->second != material;
  };

  while (needsNewGuid()) {
    rawGuid = GenerateGuidString();
  }
  if (!rawGuid.empty()) {
    WriteAppDataString(material, kMaterialGuidAppDataSubId, rawGuid);
    (*owners)[rawGuid] = material;
  }

  for (int subMaterialIndex = 0; subMaterialIndex < material->NumSubMtls();
       ++subMaterialIndex) {
    EnsureUniqueMaterialGuidsRecursive(material->GetSubMtl(subMaterialIndex),
                                       owners, visited);
  }
}

void EnsureUniqueSharedObjectGuid(
    Object *object, std::unordered_map<std::string, Object *> *owners) {
  if (!object || !owners) {
    return;
  }

  std::string rawGuid =
      ReadAppDataString(object, kSharedObjectGuidAppDataSubId);
  auto needsNewGuid = [&]() {
    if (rawGuid.empty()) {
      return true;
    }
    const auto it = owners->find(rawGuid);
    return it != owners->end() && it->second != object;
  };

  while (needsNewGuid()) {
    rawGuid = GenerateGuidString();
  }
  if (!rawGuid.empty()) {
    WriteAppDataString(object, kSharedObjectGuidAppDataSubId, rawGuid);
    (*owners)[rawGuid] = object;
  }
}

void EnsurePersistentIdentifiersRecursive(
    INode *node, std::unordered_set<std::string> *seenNodeGuids,
    std::unordered_map<std::string, Mtl *> *materialGuidOwners,
    std::unordered_set<Mtl *> *visitedMaterials,
    std::unordered_map<std::string, Object *> *sharedObjectGuidOwners) {
  if (!node || !seenNodeGuids || !materialGuidOwners || !visitedMaterials ||
      !sharedObjectGuidOwners) {
    return;
  }

  EnsureUniqueNodeGuid(node, seenNodeGuids);
  if (Mtl *material = node->GetMtl()) {
    EnsureUniqueMaterialGuidsRecursive(material, materialGuidOwners,
                                       visitedMaterials);
  }
  if (Object *objectRef = node->GetObjectRef()) {
    Object *baseObject = objectRef->FindBaseObject();
    EnsureUniqueSharedObjectGuid(baseObject, sharedObjectGuidOwners);
  }

  for (int childIndex = 0; childIndex < node->NumberOfChildren(); ++childIndex) {
    EnsurePersistentIdentifiersRecursive(node->GetChildNode(childIndex),
                                         seenNodeGuids, materialGuidOwners,
                                         visitedMaterials,
                                         sharedObjectGuidOwners);
  }
}

void EnsurePersistentSceneIdentifiers(Interface *ip) {
  if (!ip) {
    return;
  }

  GetOrCreateSceneGuid(ip);
  INode *root = ip->GetRootNode();
  if (!root) {
    return;
  }

  std::unordered_set<std::string> seenNodeGuids;
  std::unordered_map<std::string, Mtl *> materialGuidOwners;
  std::unordered_set<Mtl *> visitedMaterials;
  std::unordered_map<std::string, Object *> sharedObjectGuidOwners;
  for (int childIndex = 0; childIndex < root->NumberOfChildren(); ++childIndex) {
    EnsurePersistentIdentifiersRecursive(root->GetChildNode(childIndex),
                                         &seenNodeGuids, &materialGuidOwners,
                                         &visitedMaterials,
                                         &sharedObjectGuidOwners);
  }
}

std::string MakeDocumentId(Interface *ip) {
  return ip ? GetOrCreateSceneGuid(ip) : std::string("scene:unsaved");
}

std::string MakeDocumentPath(Interface *ip) {
  const std::string currentFile = ip ? ToUtf8(ip->GetCurFileName()) : std::string();
  return currentFile.empty() ? std::string("untitled.max") : currentFile;
}

std::string MakeDocumentDisplayName(Interface *ip) {
  const std::string documentPath = MakeDocumentPath(ip);
  const size_t separator = documentPath.find_last_of("\\/");
  const std::string fileName =
      separator == std::string::npos ? documentPath : documentPath.substr(separator + 1);
  return fileName.empty() ? std::string("untitled.max") : fileName;
}

std::string MakeSessionId() {
  static std::atomic<uint64_t> s_sessionCounter{1};
  const uint64_t sessionOrdinal = s_sessionCounter.fetch_add(1);
  return "3dsmax2024-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(GetTickCount64()) + "-" +
         std::to_string(sessionOrdinal);
}

std::string MakeNodeObjectId(INode *node) {
  if (!node) {
    return {};
  }
  return GetOrCreateNodeGuid(node);
}

std::string MakeMaterialObjectId(const std::string &nodeObjectId, int materialSlot) {
  return "material:" + nodeObjectId +
         ":slot:" + std::to_string((std::max)(0, materialSlot));
}

std::string GetOrCreateMaterialGuid(Mtl *material) {
  if (!material) {
    return {};
  }

  std::string guid = ReadAppDataString(material, kMaterialGuidAppDataSubId);
  if (guid.empty()) {
    guid = GenerateGuidString();
    if (!guid.empty()) {
      WriteAppDataString(material, kMaterialGuidAppDataSubId, guid);
    }
  }
  return guid.empty() ? std::string("missing") : guid;
}

std::string GetOrCreateSharedObjectGuid(Object *object) {
  if (!object) {
    return {};
  }

  std::string guid = ReadAppDataString(object, kSharedObjectGuidAppDataSubId);
  if (guid.empty()) {
    guid = GenerateGuidString();
    if (!guid.empty()) {
      WriteAppDataString(object, kSharedObjectGuidAppDataSubId, guid);
    }
  }
  return guid.empty() ? std::string("missing") : guid;
}

std::string MakeMaterialObjectId(const std::string &nodeObjectId,
                                 int materialSlot,
                                 const std::string &materialStableId) {
  if (!materialStableId.empty()) {
    return "material:id:" + materialStableId;
  }
  return "material:" + nodeObjectId +
         ":slot:" + std::to_string((std::max)(0, materialSlot));
}

std::string MakeLightObjectId(INode *node) {
  if (!node) {
    return {};
  }
  return "light:" + GetOrCreateNodeGuid(node);
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
  std::string objectId;
  std::string parentObjectId;
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

struct MaterialReferenceSnapshot {
  std::string nodeObjectId;
  int materialSlot = 0;

  bool operator==(const MaterialReferenceSnapshot &) const = default;
};

struct MaterialSnapshot {
  bool valid = false;
  ULONG_PTR nodeHandle = 0;
  int materialSlot = 0;
  std::string nodeObjectId;
  std::string materialStableId;
  std::vector<MaterialReferenceSnapshot> references;
  std::string objectId;
  std::string name;
  std::string materialModel = "OpenPBR";
  std::array<float, 4> baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
  std::string baseColorTextureUri;
  std::string normalTextureUri;
  std::string emissiveTextureUri;
  std::string occlusionTextureUri;
  std::string metalRoughTextureUri;
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
  float triPlanarEnabled = 0.0f;
  float triPlanarScale = 1.0f;
  float triPlanarSharpness = 4.0f;
  float triPlanarNormalStrength = 1.0f;
  bool doubleSided = false;
  std::string alphaMode = "OPAQUE";
  bool invertRoughnessTexture = false;
};

struct TextureBindingSnapshot {
  std::string uri;
  std::array<float, 2> uvScale = {1.0f, 1.0f};
  std::array<float, 2> uvOffset = {0.0f, 0.0f};
  float triPlanarEnabled = 0.0f;
  float triPlanarScale = 1.0f;
  float triPlanarSharpness = 4.0f;
  float triPlanarNormalStrength = 1.0f;
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
  uint32_t version = 5;
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

enum : uint32_t {
  kNativeMaterialFlagDoubleSided = 1u << 0,
  kNativeMaterialFlagInvertRoughnessTexture = 1u << 1,
};

struct NativeMeshPayloadMaterialHeader {
  int32_t materialSlot = 0;
  uint32_t flags = 0;
  float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float emissiveColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float emissiveIntensity = 1.0f;
  float roughness = 0.5f;
  float metalness = 0.0f;
  float specularWeight = 1.0f;
  float ior = 1.5f;
  float transmissionWeight = 0.0f;
  float transmissionColor[3] = {1.0f, 1.0f, 1.0f};
  float coatWeight = 0.0f;
  float coatRoughness = 0.1f;
  float thinWalled = 0.0f;
  float translucency = 0.0f;
  float uvScale[2] = {1.0f, 1.0f};
  float uvOffset[2] = {0.0f, 0.0f};
  float triPlanarEnabled = 0.0f;
  float triPlanarScale = 1.0f;
  float triPlanarSharpness = 4.0f;
  float triPlanarNormalStrength = 1.0f;
  uint32_t nameLength = 0;
  uint32_t materialStableIdLength = 0;
  uint32_t materialModelLength = 0;
  uint32_t alphaModeLength = 0;
  uint32_t baseColorTextureUriLength = 0;
  uint32_t normalTextureUriLength = 0;
  uint32_t emissiveTextureUriLength = 0;
  uint32_t occlusionTextureUriLength = 0;
  uint32_t metalRoughTextureUriLength = 0;
};

struct NativeMeshPayloadMaterialBindingHeader {
  int32_t materialSlot = 0;
  uint32_t materialStableIdLength = 0;
  uint32_t nameLength = 0;
  uint32_t reserved = 0;
};

struct NativeMaterialLibraryHeader {
  uint32_t magic = 0x54414D50; // PMAT
  uint32_t version = 1;
  uint32_t materialCount = 0;
  uint32_t reserved = 0;
};

struct NativeMaterialLibraryReferenceHeader {
  int32_t materialSlot = 0;
  uint32_t nodeObjectIdLength = 0;
};

struct NativeMaterialLibraryMaterialHeader {
  uint32_t flags = 0;
  float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float emissiveColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float emissiveIntensity = 1.0f;
  float roughness = 0.5f;
  float metalness = 0.0f;
  float specularWeight = 1.0f;
  float ior = 1.5f;
  float transmissionWeight = 0.0f;
  float transmissionColor[3] = {1.0f, 1.0f, 1.0f};
  float coatWeight = 0.0f;
  float coatRoughness = 0.1f;
  float thinWalled = 0.0f;
  float translucency = 0.0f;
  float uvScale[2] = {1.0f, 1.0f};
  float uvOffset[2] = {0.0f, 0.0f};
  float triPlanarEnabled = 0.0f;
  float triPlanarScale = 1.0f;
  float triPlanarSharpness = 4.0f;
  float triPlanarNormalStrength = 1.0f;
  uint32_t objectIdLength = 0;
  uint32_t nameLength = 0;
  uint32_t materialStableIdLength = 0;
  uint32_t materialModelLength = 0;
  uint32_t alphaModeLength = 0;
  uint32_t baseColorTextureUriLength = 0;
  uint32_t normalTextureUriLength = 0;
  uint32_t emissiveTextureUriLength = 0;
  uint32_t occlusionTextureUriLength = 0;
  uint32_t metalRoughTextureUriLength = 0;
  uint32_t referenceCount = 0;
};

bool WriteNativePayloadString(std::ofstream &stream, const std::string &value) {
  if (value.empty()) {
    return true;
  }

  stream.write(value.data(), static_cast<std::streamsize>(value.size()));
  return static_cast<bool>(stream);
}

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
  if (face.smGroup == 0) {
    return mesh.getFaceNormal(faceIndex);
  }

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

  return mesh.getFaceNormal(faceIndex);
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
  const bool hasStableIdentity =
      !lhs.materialStableId.empty() && lhs.materialStableId == rhs.materialStableId;
  auto normalizedReferences = [](const MaterialSnapshot &snapshot) {
    std::vector<MaterialReferenceSnapshot> references = snapshot.references;
    if (references.empty() && !snapshot.nodeObjectId.empty()) {
      references.push_back(MaterialReferenceSnapshot{snapshot.nodeObjectId,
                                                     snapshot.materialSlot});
    }
    std::sort(references.begin(), references.end(),
              [](const MaterialReferenceSnapshot &lhsRef,
                 const MaterialReferenceSnapshot &rhsRef) {
                if (lhsRef.nodeObjectId != rhsRef.nodeObjectId) {
                  return lhsRef.nodeObjectId < rhsRef.nodeObjectId;
                }
                return lhsRef.materialSlot < rhsRef.materialSlot;
              });
    references.erase(std::unique(references.begin(), references.end()),
                     references.end());
    return references;
  };

  return lhs.valid == rhs.valid && lhs.objectId == rhs.objectId &&
         (hasStableIdentity || lhs.materialSlot == rhs.materialSlot) &&
         lhs.name == rhs.name &&
         (hasStableIdentity || lhs.nodeObjectId == rhs.nodeObjectId) &&
         lhs.materialStableId == rhs.materialStableId &&
         normalizedReferences(lhs) == normalizedReferences(rhs) &&
         lhs.materialModel == rhs.materialModel &&
         SameVector4(lhs.baseColor, rhs.baseColor) &&
         lhs.baseColorTextureUri == rhs.baseColorTextureUri &&
         lhs.normalTextureUri == rhs.normalTextureUri &&
         lhs.emissiveTextureUri == rhs.emissiveTextureUri &&
         lhs.occlusionTextureUri == rhs.occlusionTextureUri &&
         lhs.metalRoughTextureUri == rhs.metalRoughTextureUri &&
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
         NearlyEqual(lhs.triPlanarEnabled, rhs.triPlanarEnabled) &&
         NearlyEqual(lhs.triPlanarScale, rhs.triPlanarScale) &&
         NearlyEqual(lhs.triPlanarSharpness, rhs.triPlanarSharpness) &&
         NearlyEqual(lhs.triPlanarNormalStrength, rhs.triPlanarNormalStrength) &&
         lhs.doubleSided == rhs.doubleSided && lhs.alphaMode == rhs.alphaMode &&
         lhs.invertRoughnessTexture == rhs.invertRoughnessTexture;
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

struct PersistedLiveLinkState {
  std::string documentId;
  std::unordered_map<std::string, NodeSnapshot> nodeStateByObjectId;
  MaterialStateMap materialState;
  std::unordered_map<std::string, LightSnapshot> lightStateByObjectId;
  std::vector<std::string> selectedObjectIds;
  CameraSnapshot cameraSnapshot;
};

json SerializeNodeSnapshot(const NodeSnapshot &snapshot) {
  return json{{"oi", snapshot.objectId},
              {"pi", snapshot.parentObjectId},
              {"n", snapshot.name},
              {"v", snapshot.visible},
              {"wm", snapshot.worldMatrix},
              {"hm", snapshot.hasMesh},
              {"vc", snapshot.vertexCount},
              {"ic", snapshot.indexCount},
              {"gf", snapshot.geometryFingerprint}};
}

json SerializeMaterialSnapshot(const MaterialSnapshot &snapshot) {
  json references = json::array();
  for (const MaterialReferenceSnapshot &reference : snapshot.references) {
    references.push_back(json{{"ni", reference.nodeObjectId},
                              {"ms", reference.materialSlot}});
  }
  return json{{"oi", snapshot.objectId},
              {"ni", snapshot.nodeObjectId},
              {"si", snapshot.materialStableId},
              {"ms", snapshot.materialSlot},
              {"rf", references},
              {"n", snapshot.name},
              {"mm", snapshot.materialModel},
              {"bc", snapshot.baseColor},
              {"bct", snapshot.baseColorTextureUri},
              {"nt", snapshot.normalTextureUri},
              {"et", snapshot.emissiveTextureUri},
              {"ot", snapshot.occlusionTextureUri},
              {"mrt", snapshot.metalRoughTextureUri},
              {"ec", snapshot.emissiveColor},
              {"ei", snapshot.emissiveIntensity},
              {"r", snapshot.roughness},
              {"m", snapshot.metalness},
              {"sw", snapshot.specularWeight},
              {"io", snapshot.ior},
              {"tw", snapshot.transmissionWeight},
              {"tc", snapshot.transmissionColor},
              {"cw", snapshot.coatWeight},
              {"cr", snapshot.coatRoughness},
              {"th", snapshot.thinWalled},
              {"tr", snapshot.translucency},
              {"us", snapshot.uvScale},
              {"uo", snapshot.uvOffset},
              {"te", snapshot.triPlanarEnabled},
              {"ts", snapshot.triPlanarScale},
              {"ths", snapshot.triPlanarSharpness},
              {"tns", snapshot.triPlanarNormalStrength},
              {"ds", snapshot.doubleSided},
              {"am", snapshot.alphaMode},
              {"irt", snapshot.invertRoughnessTexture}};
}

json SerializeLightSnapshot(const LightSnapshot &snapshot) {
  return json{{"oi", snapshot.objectId},
              {"n", snapshot.name},
              {"lt", snapshot.lightType},
              {"p", snapshot.position},
              {"d", snapshot.direction},
              {"c", snapshot.color},
              {"i", snapshot.intensity},
              {"r", snapshot.radius},
              {"ic", snapshot.innerConeDegrees},
              {"oc", snapshot.outerConeDegrees},
              {"ae", snapshot.areaExtents}};
}

json SerializeCameraSnapshot(const CameraSnapshot &snapshot) {
  return json{{"v", snapshot.valid},
              {"p", snapshot.position},
              {"f", snapshot.forward},
              {"u", snapshot.up},
              {"fv", snapshot.fovDegrees},
              {"np", snapshot.nearPlane},
              {"fp", snapshot.farPlane}};
}

bool DeserializeNodeSnapshot(const json &value, NodeSnapshot *outSnapshot) {
  if (!outSnapshot || !value.is_object()) {
    return false;
  }
  NodeSnapshot snapshot;
  snapshot.objectId = value.value("oi", std::string());
  snapshot.parentObjectId = value.value("pi", std::string());
  snapshot.name = value.value("n", std::string());
  snapshot.visible = value.value("v", true);
  snapshot.hasMesh = value.value("hm", false);
  snapshot.vertexCount = value.value("vc", uint64_t(0));
  snapshot.indexCount = value.value("ic", uint64_t(0));
  snapshot.geometryFingerprint = value.value("gf", uint64_t(0));
  if (value.contains("wm") && value["wm"].is_array() && value["wm"].size() == 16) {
    snapshot.worldMatrix = value["wm"].get<std::array<float, 16>>();
  }
  if (snapshot.objectId.empty()) {
    return false;
  }
  *outSnapshot = snapshot;
  return true;
}

bool DeserializeMaterialSnapshot(const json &value, MaterialSnapshot *outSnapshot) {
  if (!outSnapshot || !value.is_object()) {
    return false;
  }
  MaterialSnapshot snapshot;
  snapshot.valid = true;
  snapshot.objectId = value.value("oi", std::string());
  snapshot.nodeObjectId = value.value("ni", std::string());
  snapshot.materialStableId = value.value("si", std::string());
  snapshot.materialSlot = value.value("ms", 0);
  if (value.contains("rf") && value["rf"].is_array()) {
    for (const json &referenceValue : value["rf"]) {
      if (!referenceValue.is_object()) {
        continue;
      }
      MaterialReferenceSnapshot reference;
      reference.nodeObjectId = referenceValue.value("ni", std::string());
      reference.materialSlot = referenceValue.value("ms", 0);
      if (!reference.nodeObjectId.empty()) {
        snapshot.references.push_back(std::move(reference));
      }
    }
  }
  if (snapshot.references.empty() && !snapshot.nodeObjectId.empty()) {
    snapshot.references.push_back(
        MaterialReferenceSnapshot{snapshot.nodeObjectId, snapshot.materialSlot});
  }
  snapshot.name = value.value("n", std::string());
  snapshot.materialModel = value.value("mm", std::string("OpenPBR"));
  if (value.contains("bc")) snapshot.baseColor = value["bc"].get<std::array<float, 4>>();
  snapshot.baseColorTextureUri = value.value("bct", std::string());
  snapshot.normalTextureUri = value.value("nt", std::string());
  snapshot.emissiveTextureUri = value.value("et", std::string());
  snapshot.occlusionTextureUri = value.value("ot", std::string());
  snapshot.metalRoughTextureUri = value.value("mrt", std::string());
  if (value.contains("ec")) snapshot.emissiveColor = value["ec"].get<std::array<float, 4>>();
  snapshot.emissiveIntensity = value.value("ei", 0.0f);
  snapshot.roughness = value.value("r", 0.5f);
  snapshot.metalness = value.value("m", 0.0f);
  snapshot.specularWeight = value.value("sw", 1.0f);
  snapshot.ior = value.value("io", 1.5f);
  snapshot.transmissionWeight = value.value("tw", 0.0f);
  if (value.contains("tc")) snapshot.transmissionColor = value["tc"].get<std::array<float, 3>>();
  snapshot.coatWeight = value.value("cw", 0.0f);
  snapshot.coatRoughness = value.value("cr", 0.1f);
  snapshot.thinWalled = value.value("th", 0.0f);
  snapshot.translucency = value.value("tr", 0.0f);
  if (value.contains("us")) snapshot.uvScale = value["us"].get<std::array<float, 2>>();
  if (value.contains("uo")) snapshot.uvOffset = value["uo"].get<std::array<float, 2>>();
  snapshot.triPlanarEnabled = value.value("te", 0.0f);
  snapshot.triPlanarScale = value.value("ts", 1.0f);
  snapshot.triPlanarSharpness = value.value("ths", 4.0f);
  snapshot.triPlanarNormalStrength = value.value("tns", 1.0f);
  snapshot.doubleSided = value.value("ds", false);
  snapshot.alphaMode = value.value("am", std::string("OPAQUE"));
  snapshot.invertRoughnessTexture = value.value("irt", false);
  if (snapshot.objectId.empty()) {
    return false;
  }
  *outSnapshot = snapshot;
  return true;
}

bool DeserializeLightSnapshot(const json &value, LightSnapshot *outSnapshot) {
  if (!outSnapshot || !value.is_object()) {
    return false;
  }
  LightSnapshot snapshot;
  snapshot.valid = true;
  snapshot.objectId = value.value("oi", std::string());
  snapshot.name = value.value("n", std::string());
  snapshot.lightType = value.value("lt", std::string("Omni"));
  if (value.contains("p")) snapshot.position = value["p"].get<std::array<float, 3>>();
  if (value.contains("d")) snapshot.direction = value["d"].get<std::array<float, 3>>();
  if (value.contains("c")) snapshot.color = value["c"].get<std::array<float, 3>>();
  snapshot.intensity = value.value("i", 0.0f);
  snapshot.radius = value.value("r", 0.1f);
  snapshot.innerConeDegrees = value.value("ic", 30.0f);
  snapshot.outerConeDegrees = value.value("oc", 45.0f);
  if (value.contains("ae")) snapshot.areaExtents = value["ae"].get<std::array<float, 2>>();
  if (snapshot.objectId.empty()) {
    return false;
  }
  *outSnapshot = snapshot;
  return true;
}

void DeserializeCameraSnapshot(const json &value, CameraSnapshot *outSnapshot) {
  if (!outSnapshot || !value.is_object()) {
    return;
  }
  outSnapshot->valid = value.value("v", false);
  if (value.contains("p")) outSnapshot->position = value["p"].get<std::array<float, 3>>();
  if (value.contains("f")) outSnapshot->forward = value["f"].get<std::array<float, 3>>();
  if (value.contains("u")) outSnapshot->up = value["u"].get<std::array<float, 3>>();
  outSnapshot->fovDegrees = value.value("fv", 60.0f);
  outSnapshot->nearPlane = value.value("np", 0.01f);
  outSnapshot->farPlane = value.value("fp", 1000.0f);
}

bool ReadPersistedLiveLinkState(Interface *ip, PersistedLiveLinkState *outState) {
  if (!ip || !outState) {
    return false;
  }

  const std::string raw = ReadAppDataString(ip->GetRootNode(), kResumeStateAppDataSubId);
  if (raw.empty()) {
    return false;
  }

  json value;
  try {
    value = json::parse(raw);
  } catch (...) {
    return false;
  }
  if (!value.is_object() || value.value("version", 0) != 1) {
    return false;
  }

  PersistedLiveLinkState state;
  state.documentId = value.value("documentId", std::string());
  if (value.contains("nodes") && value["nodes"].is_array()) {
    for (const auto &nodeValue : value["nodes"]) {
      NodeSnapshot snapshot;
      if (DeserializeNodeSnapshot(nodeValue, &snapshot)) {
        state.nodeStateByObjectId.emplace(snapshot.objectId, std::move(snapshot));
      }
    }
  }
  if (value.contains("materials") && value["materials"].is_array()) {
    for (const auto &materialValue : value["materials"]) {
      MaterialSnapshot snapshot;
      if (DeserializeMaterialSnapshot(materialValue, &snapshot)) {
        state.materialState.emplace(snapshot.objectId, std::move(snapshot));
      }
    }
  }
  if (value.contains("lights") && value["lights"].is_array()) {
    for (const auto &lightValue : value["lights"]) {
      LightSnapshot snapshot;
      if (DeserializeLightSnapshot(lightValue, &snapshot)) {
        state.lightStateByObjectId.emplace(snapshot.objectId, std::move(snapshot));
      }
    }
  }
  if (value.contains("selection") && value["selection"].is_array()) {
    state.selectedObjectIds = value["selection"].get<std::vector<std::string>>();
  }
  if (value.contains("camera")) {
    DeserializeCameraSnapshot(value["camera"], &state.cameraSnapshot);
  }
  if (state.documentId.empty()) {
    return false;
  }
  *outState = std::move(state);
  return true;
}

void WritePersistedLiveLinkState(
    Interface *ip, const std::string &documentId,
    const std::unordered_map<ULONG_PTR, NodeSnapshot> &nodeState,
    const MaterialStateMap &materialState,
    const std::unordered_map<ULONG_PTR, LightSnapshot> &lightState,
    const std::vector<std::string> &selectedObjectIds,
    const CameraSnapshot &cameraSnapshot) {
  if (!ip || documentId.empty()) {
    return;
  }

  json value;
  value["version"] = 1;
  value["documentId"] = documentId;
  value["nodes"] = json::array();
  for (const auto &[_, snapshot] : nodeState) {
    value["nodes"].push_back(SerializeNodeSnapshot(snapshot));
  }
  value["materials"] = json::array();
  for (const auto &[_, snapshot] : materialState) {
    value["materials"].push_back(SerializeMaterialSnapshot(snapshot));
  }
  value["lights"] = json::array();
  for (const auto &[_, snapshot] : lightState) {
    value["lights"].push_back(SerializeLightSnapshot(snapshot));
  }
  value["selection"] = selectedObjectIds;
  value["camera"] = SerializeCameraSnapshot(cameraSnapshot);

  WriteAppDataString(ip->GetRootNode(), kResumeStateAppDataSubId, value.dump());
}

void ClearPersistedLiveLinkState(Interface *ip) {
  if (!ip) {
    return;
  }
  ClearAppDataString(ip->GetRootNode(), kResumeStateAppDataSubId);
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

uint64_t HashMatrix3Value(const Matrix3 &matrix) {
  uint64_t hash = 1469598103934665603ull;
  const Point3 row0 = matrix.GetRow(0);
  const Point3 row1 = matrix.GetRow(1);
  const Point3 row2 = matrix.GetRow(2);
  const Point3 translation = matrix.GetTrans();
  hash = HashCombine(hash, HashFloat(row0.x));
  hash = HashCombine(hash, HashFloat(row0.y));
  hash = HashCombine(hash, HashFloat(row0.z));
  hash = HashCombine(hash, HashFloat(row1.x));
  hash = HashCombine(hash, HashFloat(row1.y));
  hash = HashCombine(hash, HashFloat(row1.z));
  hash = HashCombine(hash, HashFloat(row2.x));
  hash = HashCombine(hash, HashFloat(row2.y));
  hash = HashCombine(hash, HashFloat(row2.z));
  hash = HashCombine(hash, HashFloat(translation.x));
  hash = HashCombine(hash, HashFloat(translation.y));
  hash = HashCombine(hash, HashFloat(translation.z));
  return hash;
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

bool LooksLikeTexturePath(const std::string &rawPath) {
  if (rawPath.empty()) {
    return false;
  }

  const std::filesystem::path path(Utf8ToWString(rawPath));
  if (path.empty() || !path.has_extension()) {
    return false;
  }

  const std::string extension = ToLowerAscii(path.extension().string());
  return ContainsAnyToken(
      extension,
      {".bmp", ".dds", ".exr", ".gif", ".hdr", ".iff", ".jpg", ".jpeg",
       ".jp2", ".j2k", ".ktx", ".ktx2", ".png", ".psd", ".pic", ".tga",
       ".tif", ".tiff", ".tx", ".webp"});
}

bool TryNormalizeTextureUri(Interface *ip, const std::string &rawPath,
                            std::string *outUri) {
  if (!outUri || !LooksLikeTexturePath(rawPath)) {
    return false;
  }

  const std::string normalized = NormalizeTextureUri(ip, rawPath);
  if (normalized.empty()) {
    return false;
  }

  *outUri = normalized;
  return true;
}

bool ParamNameSuggestsTexturePath(const std::string &paramNameLower) {
  return ContainsAnyToken(paramNameLower,
                          {"file", "filename", "filepath", "path", "bitmap",
                           "bitmapbuffer", "mapname", "asset"});
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
                                    TextureBindingSnapshot *outBinding);

bool TryFindAnimatableParam(Animatable *animatable, const char *paramName,
                            IParamBlock2 **outParamBlock, ParamID *outParamId) {
  if (!animatable || !paramName || !outParamBlock || !outParamId) {
    return false;
  }

  const std::wstring paramNameWide = Utf8ToWString(paramName);
  if (paramNameWide.empty()) {
    return false;
  }

  for (int paramBlockIndex = 0; paramBlockIndex < animatable->NumParamBlocks();
       ++paramBlockIndex) {
    IParamBlock2 *paramBlock = animatable->GetParamBlock(paramBlockIndex);
    if (!paramBlock) {
      continue;
    }

    ParamBlockDesc2 *desc = paramBlock->GetDesc();
    if (!desc) {
      continue;
    }

    const int paramIndex = desc->NameToIndex(paramNameWide.c_str());
    if (paramIndex < 0) {
      continue;
    }

    const ParamID paramId = desc->IndextoID(paramIndex);
    if (paramId < 0) {
      continue;
    }

    *outParamBlock = paramBlock;
    *outParamId = paramId;
    return true;
  }

  return false;
}

template <typename TValue>
bool TryGetAnimatableParamValueByName(Animatable *animatable, TimeValue time,
                                      const char *paramName,
                                      TValue *outValue) {
  if (!outValue) {
    return false;
  }

  IParamBlock2 *paramBlock = nullptr;
  ParamID paramId = -1;
  if (!TryFindAnimatableParam(animatable, paramName, &paramBlock, &paramId) ||
      !paramBlock) {
    return false;
  }

  Interval valid = FOREVER;
  TValue value{};
  if (!paramBlock->GetValue(paramId, time, value, valid)) {
    return false;
  }

  *outValue = value;
  return true;
}

template <typename TValue>
bool TryGetAnimatableParamValueByNames(
    Animatable *animatable, TimeValue time,
    std::initializer_list<const char *> paramNames, TValue *outValue) {
  if (!outValue) {
    return false;
  }

  for (const char *paramName : paramNames) {
    if (TryGetAnimatableParamValueByName(animatable, time, paramName,
                                         outValue)) {
      return true;
    }
  }

  return false;
}

bool TryGetAnimatableTexmapParamByName(Animatable *animatable, TimeValue time,
                                       const char *paramName,
                                       Texmap **outTexmap) {
  if (!outTexmap) {
    return false;
  }

  IParamBlock2 *paramBlock = nullptr;
  ParamID paramId = -1;
  if (!TryFindAnimatableParam(animatable, paramName, &paramBlock, &paramId) ||
      !paramBlock) {
    return false;
  }

  Texmap *texmap = paramBlock->GetTexmap(paramId, time);
  if (!texmap) {
    return false;
  }

  *outTexmap = texmap;
  return true;
}

template <typename TVisitor>
bool VisitAnimatableChildTexmaps(Animatable *animatable, TimeValue time,
                                 TVisitor &&visitor) {
  if (!animatable) {
    return false;
  }

  bool visitedAny = false;

  auto visitTexmap = [&](Texmap *texmap) {
    if (!texmap) {
      return false;
    }
    visitedAny = true;
    return visitor(texmap);
  };

  if (Texmap *texmap = dynamic_cast<Texmap *>(animatable)) {
    for (int subTexmapIndex = 0; subTexmapIndex < texmap->NumSubTexmaps();
         ++subTexmapIndex) {
      if (visitTexmap(texmap->GetSubTexmap(subTexmapIndex))) {
        return true;
      }
    }
  }

  for (int paramBlockIndex = 0; paramBlockIndex < animatable->NumParamBlocks();
       ++paramBlockIndex) {
    IParamBlock2 *paramBlock = animatable->GetParamBlock(paramBlockIndex);
    if (!paramBlock) {
      continue;
    }

    ParamBlockDesc2 *desc = paramBlock->GetDesc();
    if (!desc) {
      continue;
    }

    for (int paramIndex = 0; paramIndex < desc->Count(); ++paramIndex) {
      const ParamID paramId = desc->IndextoID(paramIndex);
      if (paramId < 0) {
        continue;
      }

      const ParamType2 paramType = paramBlock->GetParameterType(paramId);
      if (root_type(paramType) != TYPE_TEXMAP) {
        continue;
      }

      const int count = is_tab(paramType) ? (std::max)(0, paramBlock->Count(paramId))
                                          : 1;
      Interval valid = FOREVER;
      for (int elementIndex = 0; elementIndex < count; ++elementIndex) {
        Texmap *childTexmap =
            paramBlock->GetTexmap(paramId, time, valid, elementIndex);
        if (visitTexmap(childTexmap)) {
          return true;
        }
      }
    }
  }

  return visitedAny;
}

bool TryResolveAnimatableTextureUriRecursive(
    Interface *ip, Animatable *animatable, int depthRemaining,
    std::unordered_set<const Animatable *> *visited, std::string *outUri) {
  if (!animatable || !visited || !outUri || depthRemaining < 0) {
    return false;
  }

  if (!visited->insert(animatable).second) {
    return false;
  }

  const TimeValue time = ip ? ip->GetTime() : 0;
  for (int paramBlockIndex = 0; paramBlockIndex < animatable->NumParamBlocks();
       ++paramBlockIndex) {
    IParamBlock2 *paramBlock = animatable->GetParamBlock(paramBlockIndex);
    if (!paramBlock) {
      continue;
    }

    ParamBlockDesc2 *desc = paramBlock->GetDesc();
    if (!desc) {
      continue;
    }

    for (int paramIndex = 0; paramIndex < desc->Count(); ++paramIndex) {
      const ParamDef *paramDef = desc->GetParamDefByIndex(paramIndex);
      if (!paramDef) {
        continue;
      }

      const ParamID paramId = desc->IndextoID(paramIndex);
      if (paramId < 0) {
        continue;
      }

      const std::string paramNameLower =
          ToLowerAscii(ToUtf8(paramDef->int_name));
      const ParamType2 paramType = root_type(paramBlock->GetParameterType(paramId));
      Interval valid = FOREVER;

      if (paramType == TYPE_FILENAME || paramType == TYPE_STRING) {
        const MCHAR *rawValue = paramBlock->GetStr(paramId, time, valid);
        const std::string rawPath = ToUtf8(rawValue);
        if ((ParamNameSuggestsTexturePath(paramNameLower) ||
             LooksLikeTexturePath(rawPath)) &&
            TryNormalizeTextureUri(ip, rawPath, outUri)) {
          return true;
        }
        continue;
      }

      if (paramType == TYPE_BITMAP) {
        if (PBBitmap *bitmap = paramBlock->GetBitmap(paramId, time, valid)) {
          const std::string rawPath = ToUtf8(bitmap->bi.Name());
          if ((ParamNameSuggestsTexturePath(paramNameLower) ||
               LooksLikeTexturePath(rawPath)) &&
              TryNormalizeTextureUri(ip, rawPath, outUri)) {
            return true;
          }
        }
      }
    }
  }

  if (depthRemaining == 0) {
    return false;
  }

  VisitAnimatableChildTexmaps(
      animatable, time,
      [&](Texmap *childTexmap) {
        return TryResolveAnimatableTextureUriRecursive(
            ip, childTexmap, depthRemaining - 1, visited, outUri);
      });
  if (!outUri->empty()) {
    return true;
  }

  if (ReferenceMaker *referenceMaker = dynamic_cast<ReferenceMaker *>(animatable)) {
    for (int referenceIndex = 0; referenceIndex < referenceMaker->NumRefs();
         ++referenceIndex) {
      Animatable *referenceAnimatable =
          dynamic_cast<Animatable *>(referenceMaker->GetReference(referenceIndex));
      if (TryResolveAnimatableTextureUriRecursive(
              ip, referenceAnimatable, depthRemaining - 1, visited, outUri)) {
        return true;
      }
    }
  }

  return false;
}

bool TryResolveFileBackedTextureBinding(Interface *ip, Texmap *texmap,
                                        TextureBindingSnapshot *outBinding) {
  if (!texmap || !outBinding) {
    return false;
  }

  std::unordered_set<const Animatable *> visited;
  std::string textureUri;
  if (!TryResolveAnimatableTextureUriRecursive(ip, texmap, 2, &visited,
                                               &textureUri) ||
      textureUri.empty()) {
    return false;
  }

  outBinding->uri = std::move(textureUri);
  return true;
}

bool IsTriPlanarTexmap(Texmap *texmap) {
  if (!texmap) {
    return false;
  }

  const std::string classNameLower = GetObjectClassNameLower(texmap);
  return classNameLower.find("triplanar") != std::string::npos ||
         classNameLower.find("tri planar") != std::string::npos;
}

float ResolveTriPlanarSharpnessFromBlend(float blend) {
  const float safeBlend = (std::max)(blend, 1.0f / 16.0f);
  return (std::clamp)(1.0f / safeBlend, 0.25f, 16.0f);
}

bool TryGetTriPlanarParameters(Interface *ip, Texmap *texmap,
                               float *outScale, float *outSharpness,
                               float *outNormalStrength) {
  if (!ip || !texmap || !outScale || !outSharpness || !outNormalStrength ||
      !IsTriPlanarTexmap(texmap)) {
    return false;
  }

  const TimeValue time = ip->GetTime();
  float triPlanarScale = 1.0f;
  float size = 0.0f;
  if (TryGetAnimatableParamValueByNames(texmap, time, {"size"}, &size) &&
      size > 1.0e-6f) {
    const float sizeInMeters = ConvertMaxDistanceToEngine(size);
    if (sizeInMeters > 1.0e-6f) {
      triPlanarScale = 1.0f / sizeInMeters;
    }
  } else {
    float scale = 1.0f;
    if (TryGetAnimatableParamValueByNames(texmap, time, {"scale"}, &scale) &&
        scale > 1.0e-6f) {
      triPlanarScale = scale;
    }
  }

  float blend = 1.0f;
  TryGetAnimatableParamValueByNames(texmap, time, {"blend"}, &blend);

  *outScale = (std::max)(triPlanarScale, 1.0e-3f);
  *outSharpness = ResolveTriPlanarSharpnessFromBlend(blend);
  *outNormalStrength = 1.0f;
  return true;
}

bool TryApplyTriPlanarSettingsToSnapshot(Interface *ip, Texmap *texmap,
                                         MaterialSnapshot *snapshot) {
  if (!snapshot) {
    return false;
  }

  float triPlanarScale = 1.0f;
  float triPlanarSharpness = 4.0f;
  float triPlanarNormalStrength = 1.0f;
  if (!TryGetTriPlanarParameters(ip, texmap, &triPlanarScale,
                                 &triPlanarSharpness,
                                 &triPlanarNormalStrength)) {
    return false;
  }

  snapshot->triPlanarEnabled = 1.0f;
  snapshot->triPlanarScale = triPlanarScale;
  snapshot->triPlanarSharpness = triPlanarSharpness;
  snapshot->triPlanarNormalStrength = triPlanarNormalStrength;
  snapshot->uvScale = {1.0f, 1.0f};
  snapshot->uvOffset = {0.0f, 0.0f};
  return true;
}

bool TryFindTriPlanarTexmapRecursive(
    Animatable *animatable, int depthRemaining,
    std::unordered_set<const Animatable *> *visited, Texmap **outTexmap) {
  if (!animatable || !visited || !outTexmap || depthRemaining < 0) {
    return false;
  }

  if (!visited->insert(animatable).second) {
    return false;
  }

  if (Texmap *texmap = dynamic_cast<Texmap *>(animatable)) {
    if (IsTriPlanarTexmap(texmap)) {
      *outTexmap = texmap;
      return true;
    }
  }

  if (depthRemaining == 0) {
    return false;
  }

  const TimeValue time = 0;
  if (VisitAnimatableChildTexmaps(
          animatable, time,
          [&](Texmap *childTexmap) {
            return TryFindTriPlanarTexmapRecursive(
                childTexmap, depthRemaining - 1, visited, outTexmap);
          })) {
    if (*outTexmap) {
      return true;
    }
  }

  if (ReferenceMaker *referenceMaker = dynamic_cast<ReferenceMaker *>(animatable)) {
    for (int referenceIndex = 0; referenceIndex < referenceMaker->NumRefs();
         ++referenceIndex) {
      if (TryFindTriPlanarTexmapRecursive(
              dynamic_cast<Animatable *>(
                  referenceMaker->GetReference(referenceIndex)),
              depthRemaining - 1, visited, outTexmap)) {
        return true;
      }
    }
  }

  return false;
}

bool TryResolveTriPlanarTextureBinding(Interface *ip, Texmap *texmap,
                                       TextureBindingSnapshot *outBinding) {
  if (!ip || !texmap || !outBinding || !IsTriPlanarTexmap(texmap)) {
    return false;
  }

  const TimeValue time = ip->GetTime();
  const char *axisParamNames[] = {"texture_x", "texture_y", "texture_z",
                                  "texture_negx", "texture_negy",
                                  "texture_negz"};

  TextureBindingSnapshot primaryBinding;
  bool foundPrimaryBinding = false;

  for (const char *axisParamName : axisParamNames) {
    Texmap *axisTexmap = nullptr;
    if (!TryGetAnimatableTexmapParamByName(texmap, time, axisParamName,
                                           &axisTexmap) ||
        !axisTexmap) {
      continue;
    }

    TextureBindingSnapshot axisBinding;
    if (!TryResolveTexmapTextureBinding(ip, axisTexmap, &axisBinding) ||
        axisBinding.uri.empty()) {
      continue;
    }

    if (!foundPrimaryBinding) {
      primaryBinding = std::move(axisBinding);
      foundPrimaryBinding = true;
    }
  }

  if (!foundPrimaryBinding) {
    TextureBindingSnapshot fallbackBinding;
    if (TryResolveFileBackedTextureBinding(ip, texmap, &fallbackBinding) &&
        !fallbackBinding.uri.empty()) {
      primaryBinding = std::move(fallbackBinding);
      foundPrimaryBinding = true;
    }
  }

  if (!foundPrimaryBinding) {
    for (int subTexmapIndex = 0; subTexmapIndex < texmap->NumSubTexmaps();
         ++subTexmapIndex) {
      Texmap *subTexmap = texmap->GetSubTexmap(subTexmapIndex);
      if (!subTexmap) {
        continue;
      }

      TextureBindingSnapshot subBinding;
      if (!TryResolveTexmapTextureBinding(ip, subTexmap, &subBinding) ||
          subBinding.uri.empty()) {
        continue;
      }

      primaryBinding = std::move(subBinding);
      foundPrimaryBinding = true;
      break;
    }
  }

  if (!foundPrimaryBinding || primaryBinding.uri.empty()) {
    return false;
  }

  float triPlanarScale = 1.0f;
  float triPlanarSharpness = 4.0f;
  float triPlanarNormalStrength = 1.0f;
  if (!TryGetTriPlanarParameters(ip, texmap, &triPlanarScale,
                                 &triPlanarSharpness,
                                 &triPlanarNormalStrength)) {
    return false;
  }

  primaryBinding.uvScale = {1.0f, 1.0f};
  primaryBinding.uvOffset = {0.0f, 0.0f};
  primaryBinding.triPlanarEnabled = 1.0f;
  primaryBinding.triPlanarScale = triPlanarScale;
  primaryBinding.triPlanarSharpness = triPlanarSharpness;
  primaryBinding.triPlanarNormalStrength = triPlanarNormalStrength;

  *outBinding = std::move(primaryBinding);
  return true;
}

bool TryResolveTexmapTextureBinding(Interface *ip, Texmap *texmap,
                                    TextureBindingSnapshot *outBinding) {
  if (!texmap || !outBinding) {
    return false;
  }

  if (TryResolveTriPlanarTextureBinding(ip, texmap, outBinding)) {
    return true;
  }

  if (BitmapTex *bitmapTex = dynamic_cast<BitmapTex *>(texmap)) {
    if (TryResolveBitmapTextureBinding(ip, bitmapTex, outBinding)) {
      return true;
    }
  }

  if (TryResolveFileBackedTextureBinding(ip, texmap, outBinding)) {
    return true;
  }

  const TimeValue time = ip ? ip->GetTime() : 0;
  if (VisitAnimatableChildTexmaps(
          texmap, time,
          [&](Texmap *childTexmap) {
            return TryResolveTexmapTextureBinding(ip, childTexmap, outBinding);
          })) {
    if (!outBinding->uri.empty()) {
      return true;
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
  if (binding.triPlanarEnabled > 0.5f) {
    if (snapshot->triPlanarEnabled <= 0.5f) {
      snapshot->triPlanarEnabled = 1.0f;
      snapshot->triPlanarScale = (std::max)(binding.triPlanarScale, 1.0e-3f);
      snapshot->triPlanarSharpness =
          (std::clamp)(binding.triPlanarSharpness, 0.25f, 16.0f);
      snapshot->triPlanarNormalStrength =
          (std::max)(binding.triPlanarNormalStrength, 0.0f);
      snapshot->uvScale = {1.0f, 1.0f};
      snapshot->uvOffset = {0.0f, 0.0f};
    }
    return;
  }

  if (NearlyEqual(snapshot->uvScale[0], 1.0f) &&
      NearlyEqual(snapshot->uvScale[1], 1.0f) &&
      NearlyEqual(snapshot->uvOffset[0], 0.0f) &&
      NearlyEqual(snapshot->uvOffset[1], 0.0f) &&
      snapshot->triPlanarEnabled <= 0.5f) {
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
      continue;
    }
    if (snapshot->occlusionTextureUri.empty() &&
        ContainsAnyToken(slotNameLower, {"occlusion", "ambient occlusion", "ao"})) {
      ApplyTextureBinding(std::move(binding), &snapshot->occlusionTextureUri,
                          snapshot);
      continue;
    }
    if (snapshot->metalRoughTextureUri.empty() &&
        ContainsAnyToken(slotNameLower,
                         {"metal", "metallic", "metalness", "rough", "gloss"})) {
      ApplyTextureBinding(std::move(binding), &snapshot->metalRoughTextureUri,
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
  snapshot->emissiveColor = {0.0f, 0.0f, 0.0f, 1.0f};
  snapshot->emissiveIntensity = 0.0f;
  snapshot->translucency = 0.0f;

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

  const bool genericEmissionEnabled =
      material->GetSelfIllumColorOn() != FALSE || material->GetSelfIllum() > 1.0e-3f;
  int selfIllumGi = 0;
  TryGetVrayMtlValue(material,
                     ProjectRenderVrayMtlParameters::pb_selfIllumination_gi, time,
                     &selfIllumGi);
  Color selfIllumColor(0.0f, 0.0f, 0.0f);
  const bool hasSelfIllumColor =
      TryGetVrayMtlValue(material,
                         ProjectRenderVrayMtlParameters::pb_selfIllumination_color, time,
                         &selfIllumColor);
  float selfIllumMultiplier = 0.0f;
  const bool hasSelfIllumMultiplier =
      TryGetVrayMtlValue(material,
                         ProjectRenderVrayMtlParameters::pb_selfIllumination_mult, time,
                         &selfIllumMultiplier);

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

  int translucent = 0;
  const bool hasTranslucentToggle =
      TryGetVrayMtlValue(material,
                         ProjectRenderVrayMtlParameters::pb_refract_translucent, time,
                         &translucent);
  float translucencyAmount = 0.0f;
  const bool hasTranslucencyAmount =
      TryGetVrayMtlValue(material,
                         ProjectRenderVrayMtlParameters::pb_refract_translucency_amount,
                         time, &translucencyAmount);
  if ((hasTranslucentToggle && translucent != 0) ||
      (!hasTranslucentToggle && hasTranslucencyAmount && translucencyAmount > 1.0e-3f)) {
    snapshot->translucency =
        hasTranslucencyAmount ? (std::clamp)(translucencyAmount, 0.0f, 1.0f)
                              : 1.0f;
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

  Texmap *metalnessTexmap = nullptr;
  if (TryGetVrayMtlTexmap(material,
                          ProjectRenderVrayMtlParameters::pb_reflect_metalness_shortmap,
                          time, &metalnessTexmap)) {
    TextureBindingSnapshot binding;
    if (TryResolveTexmapTextureBinding(ip, metalnessTexmap, &binding)) {
      ApplyTextureBinding(std::move(binding), &snapshot->metalRoughTextureUri,
                          snapshot);
    }
  }

  if (snapshot->metalRoughTextureUri.empty()) {
    Texmap *roughnessTexmap = nullptr;
    if (TryGetVrayMtlTexmap(material,
                            ProjectRenderVrayMtlParameters::pb_reflect_glossiness_shortmap,
                            time, &roughnessTexmap)) {
      TextureBindingSnapshot binding;
      if (TryResolveTexmapTextureBinding(ip, roughnessTexmap, &binding)) {
        ApplyTextureBinding(std::move(binding), &snapshot->metalRoughTextureUri,
                            snapshot);
      }
    }
  }

  int useRoughness = 0;
  TryGetVrayMtlValue(material, ProjectRenderVrayMtlParameters::pb_brdf_useRoughness, time, &useRoughness);
  snapshot->invertRoughnessTexture = (useRoughness == 0);

  Texmap *emissiveTexmap = nullptr;
  const bool hasEmissiveTexmap = TryGetVrayMtlTexmap(
          material,
          ProjectRenderVrayMtlParameters::pb_selfIllumination_color_shortmap,
          time, &emissiveTexmap);
  if (hasEmissiveTexmap) {
    TextureBindingSnapshot binding;
    if (TryResolveTexmapTextureBinding(ip, emissiveTexmap, &binding)) {
      ApplyTextureBinding(std::move(binding), &snapshot->emissiveTextureUri,
                          snapshot);
    }
  }

  if (genericEmissionEnabled || selfIllumGi != 0 || hasEmissiveTexmap) {
    if (hasSelfIllumColor) {
      snapshot->emissiveColor = ColorToArray4(selfIllumColor, 1.0f);
    }
    if (hasSelfIllumMultiplier) {
      snapshot->emissiveIntensity = (std::max)(0.0f, selfIllumMultiplier);
    } else if (hasSelfIllumColor && MaxColorComponent(selfIllumColor) > 1.0e-3f) {
      snapshot->emissiveIntensity = 1.0f;
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

constexpr BlockID kMultiMaterialParamBlockId = 0;
constexpr ParamID kMultiMaterialIdsParamId = 3;

bool TryGetMultiMaterialIds(Mtl *material, std::vector<int> *outMaterialIds) {
  if (!material || !outMaterialIds || !material->IsMultiMtl()) {
    return false;
  }

  IParamBlock2 *paramBlock = material->GetParamBlockByID(kMultiMaterialParamBlockId);
  if (!paramBlock) {
    return false;
  }

  const int count = paramBlock->Count(kMultiMaterialIdsParamId);
  if (count <= 0) {
    return false;
  }

  outMaterialIds->clear();
  outMaterialIds->reserve(static_cast<size_t>(count));
  for (int index = 0; index < count; ++index) {
    int materialId = 0;
    Interval interval;
    paramBlock->GetValue(kMultiMaterialIdsParamId, 0, materialId, interval, index);
    outMaterialIds->push_back(materialId);
  }
  return true;
}

int ResolveFaceMaterialSlot(Mtl *rootMaterial, int faceMaterialId) {
  if (!rootMaterial) {
    return 0;
  }

  if (rootMaterial->IsMultiMtl()) {
    return (std::max)(0, faceMaterialId);
  }

  const int subMaterialCount = rootMaterial->NumSubMtls();
  if (subMaterialCount > 1) {
    int slot = faceMaterialId;
    slot = slot > 0 ? (slot - 1) : 0;
    slot = (std::clamp)(slot, 0, subMaterialCount - 1);
    return slot;
  }

  return 0;
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
    std::vector<int> explicitMaterialIds;
    if (TryGetMultiMaterialIds(rootMaterial, &explicitMaterialIds)) {
      for (int materialId : explicitMaterialIds) {
        if (rootMaterial->GetSubMtl(materialId)) {
          usedSlots.push_back(materialId);
        }
      }
    } else {
      for (int slot = 0; slot < subMaterialCount; ++slot) {
        if (rootMaterial->GetSubMtl(slot)) {
          usedSlots.push_back(slot);
        }
      }
    }
    if (usedSlots.empty()) {
      usedSlots.push_back(0);
    }
    return usedSlots;
  }

  Mesh &mesh = triObject->GetMesh();
  for (int faceIndex = 0; faceIndex < mesh.getNumFaces(); ++faceIndex) {
    const int slot = ResolveFaceMaterialSlot(rootMaterial,
                                             mesh.faces[faceIndex].getMatID());
    if (std::find(usedSlots.begin(), usedSlots.end(), slot) == usedSlots.end()) {
      usedSlots.push_back(slot);
    }
  }

  if (needsDelete) {
    triObject->DeleteThis();
  }

  usedSlots.erase(std::remove_if(usedSlots.begin(), usedSlots.end(),
                                 [rootMaterial](int slot) {
                                   return rootMaterial->GetSubMtl(slot) == nullptr;
                                 }),
                  usedSlots.end());
  std::sort(usedSlots.begin(), usedSlots.end());
  if (usedSlots.empty()) {
    usedSlots.push_back(0);
  }
  return usedSlots;
}

Mtl *ResolveMaterialForSlot(Mtl *rootMaterial, int materialSlot) {
  if (!rootMaterial) {
    return nullptr;
  }

  if (rootMaterial->IsMultiMtl()) {
    if (Mtl *slotMaterial = rootMaterial->GetSubMtl((std::max)(0, materialSlot))) {
      return ResolveLeafMaterial(slotMaterial);
    }
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
  snapshot.nodeObjectId = MakeNodeObjectId(node);
  snapshot.materialStableId = GetOrCreateMaterialGuid(material);
  snapshot.references.push_back(
      MaterialReferenceSnapshot{snapshot.nodeObjectId, snapshot.materialSlot});
  snapshot.objectId = MakeMaterialObjectId(snapshot.nodeObjectId,
                                           snapshot.materialSlot,
                                           snapshot.materialStableId);
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
  if (snapshot.triPlanarEnabled <= 0.5f &&
      (!snapshot.baseColorTextureUri.empty() ||
       !snapshot.normalTextureUri.empty() ||
       !snapshot.emissiveTextureUri.empty() ||
       !snapshot.occlusionTextureUri.empty() ||
       !snapshot.metalRoughTextureUri.empty())) {
    std::unordered_set<const Animatable *> visited;
    Texmap *triPlanarTexmap = nullptr;
    if (TryFindTriPlanarTexmapRecursive(material, 4, &visited,
                                        &triPlanarTexmap)) {
      TryApplyTriPlanarSettingsToSnapshot(ip, triPlanarTexmap, &snapshot);
    }
  }
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
        auto existingIt = outState->find(materialSnapshot.objectId);
        if (existingIt == outState->end()) {
          outState->emplace(materialSnapshot.objectId, std::move(materialSnapshot));
        } else {
          MaterialSnapshot &existingSnapshot = existingIt->second;
          std::vector<MaterialReferenceSnapshot> mergedReferences =
              existingSnapshot.references;
          existingSnapshot = materialSnapshot;
          mergedReferences.insert(mergedReferences.end(),
                                  materialSnapshot.references.begin(),
                                  materialSnapshot.references.end());
          existingSnapshot.references = std::move(mergedReferences);
          std::sort(existingSnapshot.references.begin(),
                    existingSnapshot.references.end(),
                    [](const MaterialReferenceSnapshot &lhs,
                       const MaterialReferenceSnapshot &rhs) {
                      if (lhs.nodeObjectId != rhs.nodeObjectId) {
                        return lhs.nodeObjectId < rhs.nodeObjectId;
                      }
                      return lhs.materialSlot < rhs.materialSlot;
                    });
          existingSnapshot.references.erase(
              std::unique(existingSnapshot.references.begin(),
                          existingSnapshot.references.end()),
              existingSnapshot.references.end());
        }
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
  mesh.buildNormals();
  mesh.buildRenderNormals();
  MeshNormalSpec *specNormals = mesh.GetSpecifiedNormals();
  if (specNormals) {
    specNormals->CheckNormals();
  }
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

    Mtl *rootMaterial = node->GetMtl();
    if (rootMaterial) {
      const std::string materialGuid = GetOrCreateMaterialGuid(rootMaterial);
      for (char c : materialGuid) {
        fingerprint = HashCombine(fingerprint, static_cast<uint64_t>(c));
      }
    } else {
      fingerprint = HashCombine(fingerprint, 0x01010101ull);
    }
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

std::string SanitizePathComponent(std::string value) {
  for (char &ch : value) {
    const bool alphaNumeric =
        (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9');
    if (!alphaNumeric && ch != '-' && ch != '_') {
      ch = '_';
    }
  }
  if (value.empty()) {
    return "unnamed";
  }
  return value;
}

std::filesystem::path GetDocumentPayloadDirectory(const std::string &documentId) {
  const std::filesystem::path rootDirectory = GetPayloadRootDirectory();
  if (rootDirectory.empty()) {
    return {};
  }

  std::error_code error;
  const std::filesystem::path documentDirectory =
      rootDirectory / "documents" / SanitizePathComponent(documentId);
  std::filesystem::create_directories(documentDirectory, error);
  if (error) {
    return {};
  }
  return documentDirectory;
}

std::filesystem::path GetNodePayloadPath(const std::string &documentId,
                                         const std::string &nodeObjectId) {
  const std::filesystem::path documentDirectory =
      GetDocumentPayloadDirectory(documentId);
  if (documentDirectory.empty()) {
    return {};
  }
  return documentDirectory /
         (SanitizePathComponent(nodeObjectId) + std::string(".prmesh"));
}

std::filesystem::path GetMaterialLibraryPayloadPath(
    const std::string &documentId) {
  const std::filesystem::path documentDirectory =
      GetDocumentPayloadDirectory(documentId);
  if (documentDirectory.empty()) {
    return {};
  }
  return documentDirectory / "_scene_materials.prmat";
}

std::string BuildSharedPayloadKey(Interface *ip, INode *node) {
  (void)ip;
  (void)node;
  // Keep LiveLink payloads node-local. Sharing .prmesh files across different
  // Max nodes can let one node's freshly exported local-space mesh replace
  // another node's content, which shows up in-engine as random teleports or
  // rotations after material/topology edits.
  return {};
}

std::filesystem::path GetSharedPayloadPath(const std::string &documentId,
                                           const std::string &payloadKey) {
  const std::filesystem::path documentDirectory =
      GetDocumentPayloadDirectory(documentId);
  if (documentDirectory.empty() || payloadKey.empty()) {
    return {};
  }
  return documentDirectory /
         (SanitizePathComponent(payloadKey) + std::string(".prmesh"));
}

void RemoveNodePayloadFile(const std::string &documentId,
                           const std::string &nodeObjectId) {
  if (documentId.empty() || nodeObjectId.empty()) {
    return;
  }

  std::error_code error;
  const std::filesystem::path payloadPath =
      GetNodePayloadPath(documentId, nodeObjectId);
  if (!payloadPath.empty()) {
    std::filesystem::remove(payloadPath, error);
  }
}

bool WriteMaterialLibraryPayload(const std::string &documentId,
                                 const MaterialStateMap &materialState,
                                 std::string *outPayloadUri) {
  if (!outPayloadUri) {
    return false;
  }

  const std::filesystem::path payloadPath =
      GetMaterialLibraryPayloadPath(documentId);
  if (payloadPath.empty()) {
    return false;
  }

  std::ofstream stream(payloadPath, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }

  std::vector<MaterialSnapshot> serializedMaterials;
  serializedMaterials.reserve(materialState.size());
  for (const auto &[_, snapshot] : materialState) {
    serializedMaterials.push_back(snapshot);
  }
  std::sort(serializedMaterials.begin(), serializedMaterials.end(),
            [](const MaterialSnapshot &lhs, const MaterialSnapshot &rhs) {
              if (lhs.name != rhs.name) {
                return lhs.name < rhs.name;
              }
              return lhs.objectId < rhs.objectId;
            });

  NativeMaterialLibraryHeader header;
  header.materialCount = static_cast<uint32_t>(serializedMaterials.size());
  stream.write(reinterpret_cast<const char *>(&header), sizeof(header));

  for (const MaterialSnapshot &material : serializedMaterials) {
    NativeMaterialLibraryMaterialHeader materialHeader;
    materialHeader.flags =
        material.doubleSided ? kNativeMaterialFlagDoubleSided : 0u;
    if (material.invertRoughnessTexture) {
      materialHeader.flags |= kNativeMaterialFlagInvertRoughnessTexture;
    }
    std::copy(material.baseColor.begin(), material.baseColor.end(),
              std::begin(materialHeader.baseColor));
    std::copy(material.emissiveColor.begin(), material.emissiveColor.end(),
              std::begin(materialHeader.emissiveColor));
    materialHeader.emissiveIntensity = material.emissiveIntensity;
    materialHeader.roughness = material.roughness;
    materialHeader.metalness = material.metalness;
    materialHeader.specularWeight = material.specularWeight;
    materialHeader.ior = material.ior;
    materialHeader.transmissionWeight = material.transmissionWeight;
    std::copy(material.transmissionColor.begin(), material.transmissionColor.end(),
              std::begin(materialHeader.transmissionColor));
    materialHeader.coatWeight = material.coatWeight;
    materialHeader.coatRoughness = material.coatRoughness;
    materialHeader.thinWalled = material.thinWalled;
    materialHeader.translucency = material.translucency;
    materialHeader.uvScale[0] = material.uvScale[0];
    materialHeader.uvScale[1] = material.uvScale[1];
    materialHeader.uvOffset[0] = material.uvOffset[0];
    materialHeader.uvOffset[1] = material.uvOffset[1];
    materialHeader.triPlanarEnabled = material.triPlanarEnabled;
    materialHeader.triPlanarScale = material.triPlanarScale;
    materialHeader.triPlanarSharpness = material.triPlanarSharpness;
    materialHeader.triPlanarNormalStrength = material.triPlanarNormalStrength;
    materialHeader.objectIdLength = static_cast<uint32_t>(material.objectId.size());
    materialHeader.nameLength = static_cast<uint32_t>(material.name.size());
    materialHeader.materialStableIdLength =
        static_cast<uint32_t>(material.materialStableId.size());
    materialHeader.materialModelLength =
        static_cast<uint32_t>(material.materialModel.size());
    materialHeader.alphaModeLength =
        static_cast<uint32_t>(material.alphaMode.size());
    materialHeader.baseColorTextureUriLength =
        static_cast<uint32_t>(material.baseColorTextureUri.size());
    materialHeader.normalTextureUriLength =
        static_cast<uint32_t>(material.normalTextureUri.size());
    materialHeader.emissiveTextureUriLength =
        static_cast<uint32_t>(material.emissiveTextureUri.size());
    materialHeader.occlusionTextureUriLength =
        static_cast<uint32_t>(material.occlusionTextureUri.size());
    materialHeader.metalRoughTextureUriLength =
        static_cast<uint32_t>(material.metalRoughTextureUri.size());
    materialHeader.referenceCount =
        static_cast<uint32_t>(material.references.size());
    stream.write(reinterpret_cast<const char *>(&materialHeader),
                 sizeof(materialHeader));
    if (!WriteNativePayloadString(stream, material.objectId) ||
        !WriteNativePayloadString(stream, material.name) ||
        !WriteNativePayloadString(stream, material.materialStableId) ||
        !WriteNativePayloadString(stream, material.materialModel) ||
        !WriteNativePayloadString(stream, material.alphaMode) ||
        !WriteNativePayloadString(stream, material.baseColorTextureUri) ||
        !WriteNativePayloadString(stream, material.normalTextureUri) ||
        !WriteNativePayloadString(stream, material.emissiveTextureUri) ||
        !WriteNativePayloadString(stream, material.occlusionTextureUri) ||
        !WriteNativePayloadString(stream, material.metalRoughTextureUri)) {
      return false;
    }

    for (const MaterialReferenceSnapshot &reference : material.references) {
      NativeMaterialLibraryReferenceHeader referenceHeader;
      referenceHeader.materialSlot = reference.materialSlot;
      referenceHeader.nodeObjectIdLength =
          static_cast<uint32_t>(reference.nodeObjectId.size());
      stream.write(reinterpret_cast<const char *>(&referenceHeader),
                   sizeof(referenceHeader));
      if (!WriteNativePayloadString(stream, reference.nodeObjectId)) {
        return false;
      }
    }
  }

  if (!stream.good()) {
    return false;
  }

  *outPayloadUri = PathToUtf8(payloadPath);
  return true;
}

void AppendMaterialLibraryDelta(const std::string &documentId,
                                const std::string &payloadUri,
                                uint64_t *revision, json *outDeltas) {
  if (!revision || !outDeltas || payloadUri.empty()) {
    return;
  }

  outDeltas->push_back(
      json{{"kind", "MaterialLibraryChanged"},
           {"target", MakeObjectId(documentId, "material-library", "Material")},
           {"revision", (*revision)++},
           {"debugLabel", "Scene material library"},
           {"payload", json{{"payloadUri", payloadUri},
                            {"payloadHash", payloadUri}}}});
}

void RemoveDocumentPayloadDirectoryIfEmpty(const std::string &documentId) {
  if (documentId.empty()) {
    return;
  }

  std::error_code error;
  const std::filesystem::path documentDirectory =
      GetDocumentPayloadDirectory(documentId);
  if (!documentDirectory.empty() &&
      std::filesystem::is_directory(documentDirectory, error) &&
      std::filesystem::is_empty(documentDirectory, error)) {
      std::filesystem::remove(documentDirectory, error);
  }
}

void RemoveDocumentPayloadDirectory(const std::string &documentId) {
  if (documentId.empty()) {
    return;
  }

  std::error_code error;
  const std::filesystem::path documentDirectory =
      GetDocumentPayloadDirectory(documentId);
  if (!documentDirectory.empty()) {
    std::filesystem::remove_all(documentDirectory, error);
  }
}

void RemoveNodePayloadFileAndMaybeCleanupDirectory(const std::string &documentId,
                                                   const std::string &nodeObjectId) {
  RemoveNodePayloadFile(documentId, nodeObjectId);
  const std::filesystem::path payloadPath =
      GetNodePayloadPath(documentId, nodeObjectId);
  if (payloadPath.empty()) {
    return;
  }
  RemoveDocumentPayloadDirectoryIfEmpty(documentId);
}

void CleanupPayloadRootDirectory() {
  const std::filesystem::path rootDirectory = GetPayloadRootDirectory();
  if (rootDirectory.empty()) {
    return;
  }

  constexpr auto kMaxPayloadAge = std::chrono::hours(24 * 14);
  const auto now = std::filesystem::file_time_type::clock::now();
  std::error_code error;
  for (const auto &entry : std::filesystem::directory_iterator(rootDirectory, error)) {
    if (error) {
      break;
    }
    const std::filesystem::path entryPath = entry.path();
    const std::string directoryName = entryPath.filename().string();
    if (directoryName == "documents") {
      for (const auto &documentEntry : std::filesystem::directory_iterator(entryPath, error)) {
        if (error) {
          break;
        }
        const auto lastWriteTime = std::filesystem::last_write_time(documentEntry.path(), error);
        if (error) {
          error.clear();
          continue;
        }
        if (now - lastWriteTime > kMaxPayloadAge) {
          std::filesystem::remove_all(documentEntry.path(), error);
          error.clear();
        }
      }
      continue;
    }

    const auto lastWriteTime = std::filesystem::last_write_time(entryPath, error);
    if (error) {
      error.clear();
      continue;
    }
    if (now - lastWriteTime > kMaxPayloadAge) {
      std::filesystem::remove_all(entryPath, error);
      error.clear();
    }
  }
}

bool ExportNodeAsNativeMeshPayload(Interface *ip, INode *node,
                                   const std::string &documentId,
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

  std::filesystem::path payloadPath;
  const std::string sharedPayloadKey = BuildSharedPayloadKey(ip, node);
  if (!sharedPayloadKey.empty()) {
    payloadPath = GetSharedPayloadPath(documentId, sharedPayloadKey);
  }
  if (payloadPath.empty()) {
    payloadPath = GetNodePayloadPath(documentId, snapshot.objectId);
  }
  if (payloadPath.empty()) {
    if (needsDelete) {
      triObject->DeleteThis();
    }
    return false;
  }

  std::ofstream stream(payloadPath, std::ios::binary | std::ios::trunc);
  if (!stream) {
    if (needsDelete) {
      triObject->DeleteThis();
    }
    return false;
  }

  Mesh &mesh = triObject->GetMesh();
  mesh.buildNormals();
  mesh.buildRenderNormals();
  MeshNormalSpec *specNormals = mesh.GetSpecifiedNormals();
  if (specNormals) {
    specNormals->CheckNormals();
  }
  const Matrix3 objectToNode = ComputeObjectToNodeTransform(ip, node);
  struct ExportSubmesh {
    int materialSlot = 0;
    std::vector<NativeMeshPayloadVertex> vertices;
    std::vector<uint32_t> indices;
  };
  struct TrianglePositionKey {
    std::array<uint32_t, 9> bits{};

    bool operator==(const TrianglePositionKey &) const = default;
  };
  struct TrianglePositionKeyHasher {
    size_t operator()(const TrianglePositionKey &key) const noexcept {
      size_t hash = 1469598103934665603ull;
      for (uint32_t value : key.bits) {
        hash ^= static_cast<size_t>(value);
        hash *= 1099511628211ull;
      }
      return hash;
    }
  };
  std::vector<ExportSubmesh> submeshes;
  std::unordered_map<int, size_t> submeshBySlot;
  std::vector<MaterialSnapshot> serializedMaterials;

  const bool hasTexcoords = mesh.tvFace != nullptr && mesh.tVerts != nullptr &&
                            mesh.getNumTVerts() > 0;
  Mtl *rootMaterial = node->GetMtl();
  for (int faceIndex = 0; faceIndex < mesh.getNumFaces(); ++faceIndex) {
    Face &face = mesh.faces[faceIndex];
    const int materialSlot = ResolveFaceMaterialSlot(rootMaterial,
                                                     face.getMatID());

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

  auto makeTrianglePositionKey =
      [](const NativeMeshPayloadVertex &v0, const NativeMeshPayloadVertex &v1,
         const NativeMeshPayloadVertex &v2) {
        std::array<std::array<uint32_t, 3>, 3> positions = {};
        const NativeMeshPayloadVertex *triangleVertices[3] = {&v0, &v1, &v2};
        for (size_t vertexIndex = 0; vertexIndex < 3; ++vertexIndex) {
          for (size_t axis = 0; axis < 3; ++axis) {
            std::memcpy(&positions[vertexIndex][axis],
                        &triangleVertices[vertexIndex]->position[axis],
                        sizeof(uint32_t));
          }
        }
        std::sort(positions.begin(), positions.end());

        TrianglePositionKey key;
        size_t bitIndex = 0;
        for (const auto &position : positions) {
          for (uint32_t component : position) {
            key.bits[bitIndex++] = component;
          }
        }
        return key;
      };

  for (ExportSubmesh &submesh : submeshes) {
    if (submesh.indices.size() < 6 || submesh.indices.size() % 3 != 0) {
      continue;
    }

    std::unordered_set<TrianglePositionKey, TrianglePositionKeyHasher>
        seenTriangles;
    ExportSubmesh deduplicatedSubmesh;
    deduplicatedSubmesh.materialSlot = submesh.materialSlot;
    deduplicatedSubmesh.vertices.reserve(submesh.vertices.size());
    deduplicatedSubmesh.indices.reserve(submesh.indices.size());

    for (size_t triangleBase = 0; triangleBase < submesh.indices.size();
         triangleBase += 3) {
      const NativeMeshPayloadVertex &v0 =
          submesh.vertices[submesh.indices[triangleBase + 0]];
      const NativeMeshPayloadVertex &v1 =
          submesh.vertices[submesh.indices[triangleBase + 1]];
      const NativeMeshPayloadVertex &v2 =
          submesh.vertices[submesh.indices[triangleBase + 2]];
      const TrianglePositionKey key = makeTrianglePositionKey(v0, v1, v2);
      if (!seenTriangles.insert(key).second) {
        continue;
      }

      deduplicatedSubmesh.indices.push_back(
          static_cast<uint32_t>(deduplicatedSubmesh.vertices.size()));
      deduplicatedSubmesh.vertices.push_back(v0);
      deduplicatedSubmesh.indices.push_back(
          static_cast<uint32_t>(deduplicatedSubmesh.vertices.size()));
      deduplicatedSubmesh.vertices.push_back(v1);
      deduplicatedSubmesh.indices.push_back(
          static_cast<uint32_t>(deduplicatedSubmesh.vertices.size()));
      deduplicatedSubmesh.vertices.push_back(v2);
    }

    submesh = std::move(deduplicatedSubmesh);
  }

  if (rootMaterial) {
    std::vector<int> usedSlots;
    usedSlots.reserve(submeshBySlot.size());
    for (const auto &[materialSlot, _] : submeshBySlot) {
      usedSlots.push_back(materialSlot);
    }
    std::sort(usedSlots.begin(), usedSlots.end());

    serializedMaterials.reserve(usedSlots.size());
    for (const int materialSlot : usedSlots) {
      Mtl *slotMaterial = ResolveMaterialForSlot(rootMaterial, materialSlot);
      if (!slotMaterial) {
        continue;
      }

      MaterialSnapshot materialSnapshot;
      if (!CaptureMaterialSnapshot(ip, node, materialSlot, slotMaterial,
                                   &materialSnapshot)) {
        continue;
      }

      serializedMaterials.push_back(std::move(materialSnapshot));
    }
  }

  NativeMeshPayloadHeader header;
  header.meshCount = static_cast<uint32_t>(submeshes.size());
  header.reserved = static_cast<uint32_t>(serializedMaterials.size());
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

  for (const MaterialSnapshot &material : serializedMaterials) {
    NativeMeshPayloadMaterialBindingHeader bindingHeader;
    bindingHeader.materialSlot = material.materialSlot;
    bindingHeader.materialStableIdLength =
        static_cast<uint32_t>(material.materialStableId.size());
    bindingHeader.nameLength = static_cast<uint32_t>(material.name.size());
    stream.write(reinterpret_cast<const char *>(&bindingHeader),
                 sizeof(bindingHeader));
    if (!WriteNativePayloadString(stream, material.materialStableId) ||
        !WriteNativePayloadString(stream, material.name)) {
      if (needsDelete) {
        triObject->DeleteThis();
      }
      return false;
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
    snapshot.objectId = MakeNodeObjectId(node);
    snapshot.parentObjectId =
      (node->GetParentNode() && !node->GetParentNode()->IsRootNode())
        ? MakeNodeObjectId(node->GetParentNode())
        : std::string();
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

  outDeltas->push_back(json{{"kind", "NodeAdded"},
                            {"target", MakeObjectId(documentId, snapshot.objectId, "Node")},
                            {"revision", (*revision)++},
                            {"debugLabel", snapshot.name},
                            {"payload", json{{"parentObjectId", snapshot.parentObjectId},
                                              {"displayName", snapshot.name}}}});
}

void AppendNodeTransformDelta(const std::string &documentId,
                              const NodeSnapshot &snapshot, uint64_t *revision,
                              json *outDeltas) {
  if (!revision || !outDeltas) {
    return;
  }

  outDeltas->push_back(
      json{{"kind", "NodeTransformChanged"},
         {"target", MakeObjectId(documentId, snapshot.objectId, "Node")},
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

  outDeltas->push_back(
      json{{"kind", "NodeVisibilityChanged"},
           {"target", MakeObjectId(documentId, snapshot.objectId, "Node")},
           {"revision", (*revision)++},
           {"debugLabel", snapshot.name},
           {"payload", json{{"visible", snapshot.visible}}}});
}

void AppendNodeRemovedDelta(const std::string &documentId,
                            const NodeSnapshot &snapshot,
                            uint64_t *revision, json *outDeltas) {
  if (!revision || !outDeltas) {
    return;
  }

  outDeltas->push_back(
      json{{"kind", "NodeRemoved"},
           {"target", MakeObjectId(documentId, snapshot.objectId, "Node")},
           {"revision", (*revision)++},
           {"debugLabel", snapshot.name.empty() ? snapshot.objectId : snapshot.name},
           {"payload", json{{"removeChildren", true}}}});
}

void AppendMeshPayloadDelta(const std::string &documentId,
                            const NodeSnapshot &snapshot,
                            const std::string &payloadUri,
                            uint64_t *revision, json *outDeltas) {
  if (!revision || !outDeltas || payloadUri.empty()) {
    return;
  }

  outDeltas->push_back(json{{"kind", "MeshPayloadChanged"},
                            {"target", MakeObjectId(documentId, snapshot.objectId, "Node")},
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
  if (!ExportNodeAsNativeMeshPayload(ip, node, documentId, snapshot,
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

bool TryCaptureNodeSnapshotByHandle(Interface *ip, ULONG_PTR handle,
                                    NodeSnapshot *outSnapshot) {
  if (!ip || handle == 0 || !outSnapshot) {
    return false;
  }

  INode *node = FindNodeByHandle(ip, handle);
  if (!node) {
    return false;
  }

  *outSnapshot = CaptureNodeSnapshot(ip, node);
  return outSnapshot->handle != 0;
}

bool TryCaptureLightSnapshotByHandle(Interface *ip, ULONG_PTR handle,
                                     LightSnapshot *outSnapshot) {
  if (!ip || handle == 0 || !outSnapshot) {
    return false;
  }

  INode *node = FindNodeByHandle(ip, handle);
  if (!node) {
    return false;
  }

  return CaptureLightSnapshot(ip, node, outSnapshot);
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
                                              {"nodeObjectId", snapshot.nodeObjectId},
                                              {"materialStableId", snapshot.materialStableId},
                                              {"materialSlot", snapshot.materialSlot},
                                              {"references", [&snapshot]() {
                                                 json references = json::array();
                                                 for (const MaterialReferenceSnapshot &reference : snapshot.references) {
                                                   references.push_back(json{{"nodeObjectId", reference.nodeObjectId},
                                                                             {"materialSlot", reference.materialSlot}});
                                                 }
                                                 return references;
                                               }()},
                                              {"name", snapshot.name},
                                              {"materialModel", snapshot.materialModel},
                                              {"baseColor", snapshot.baseColor},
                                              {"baseColorTextureUri", snapshot.baseColorTextureUri},
                                              {"emissiveColor", snapshot.emissiveColor},
                                              {"normalTextureUri", snapshot.normalTextureUri},
                                              {"emissiveTextureUri", snapshot.emissiveTextureUri},
                                              {"occlusionTextureUri", snapshot.occlusionTextureUri},
                                              {"metalRoughTextureUri", snapshot.metalRoughTextureUri},
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
                                              {"triPlanarEnabled",
                                               snapshot.triPlanarEnabled},
                                              {"triPlanarScale",
                                               snapshot.triPlanarScale},
                                              {"triPlanarSharpness",
                                               snapshot.triPlanarSharpness},
                                              {"triPlanarNormalStrength",
                                               snapshot.triPlanarNormalStrength},
                                              {"doubleSided", snapshot.doubleSided},
                                              {"alphaMode", snapshot.alphaMode},
                                              {"invertRoughnessTexture", snapshot.invertRoughnessTexture}}}});
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
  batch["providerName"] = "3dsMax2024Pipe";
  batch["sessionId"] = sessionId;
  batch["sequence"] = sequence;
  batch["fullSync"] = fullSync;
  batch["deltas"] = deltas;
  const std::string payload = batch.dump();
  if (g_pipeClient.SendJsonLine(payload)) {
    return true;
  }

  // Max can keep a stale write handle when the engine process exits and
  // reopens. Reconnect once and retry the batch so a fresh startup/resync
  // works on the first button press.
  g_pipeClient.Disconnect();
  if (!EnsurePipeConnected()) {
    return false;
  }
  return g_pipeClient.SendJsonLine(payload);
}

bool SendInitialSnapshot(Interface *ip,
                         std::unordered_map<ULONG_PTR, NodeSnapshot> *outState,
                         MaterialStateMap *outMaterialState,
                         std::unordered_map<ULONG_PTR, LightSnapshot> *outLightState,
                         std::vector<std::string> *outSelectedObjectIds,
                         CameraSnapshot *outCameraSnapshot,
                         size_t *outDeltaCount,
                         std::string *outSessionId,
                         std::string *outDocumentId,
                         uint64_t *outNextSequence,
                         uint64_t *outNextRevision,
                         bool clearsExistingScene = false) {
  if (!ip || !EnsurePipeConnected()) {
    return false;
  }

  EnsurePersistentSceneIdentifiers(ip);
  CleanupPayloadRootDirectory();

  const std::string documentId = MakeDocumentId(ip);
  const std::string documentPath = MakeDocumentPath(ip);
  const std::string documentDisplayName = MakeDocumentDisplayName(ip);
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
      {"payload", json{{"documentPath", documentPath},
            {"displayName", documentDisplayName}}},
  });
  deltas.push_back(json{
      {"kind", "FullSceneSync"},
      {"payload", json{{"clearsExistingScene", clearsExistingScene}}},
  });

  uint64_t revision = 1;
  std::string materialLibraryPayloadUri;
  const bool wroteMaterialLibrary =
      WriteMaterialLibraryPayload(documentId, materialState,
                                  &materialLibraryPayloadUri);
  if (wroteMaterialLibrary && !materialLibraryPayloadUri.empty()) {
    AppendMaterialLibraryDelta(documentId, materialLibraryPayloadUri, &revision,
                               &deltas);
  }
  const std::vector<std::string> selectedObjectIds = GatherSelectedObjectIds(ip);
  CameraSnapshot cameraSnapshot;
  CaptureActiveCameraSnapshot(ip, &cameraSnapshot);
  for (const auto &[_, snapshot] : state) {
    AppendNodeAddedDelta(documentId, snapshot, &revision, &deltas);
    AppendNodeTransformDelta(documentId, snapshot, &revision, &deltas);
    AppendNodeVisibilityDelta(documentId, snapshot, &revision, &deltas);
    AppendMeshPayloadDeltaIfAvailable(ip, documentId, snapshot,
                                      &revision, &deltas);
  }
  if (!wroteMaterialLibrary || materialLibraryPayloadUri.empty()) {
    for (const auto &[_, snapshot] : materialState) {
      AppendMaterialDelta(documentId, snapshot, &revision, &deltas);
    }
  }
  for (const auto &[_, snapshot] : lightState) {
    AppendLightDelta(documentId, snapshot, &revision, &deltas);
  }
  deltas.push_back(json{{"kind", "SelectionChanged"},
                        {"target", MakeObjectId(documentId, "selection", "Selection")},
                        {"revision", revision++},
                        {"debugLabel", "3ds Max selection"},
                        {"payload", json{{"selectedObjectIds", selectedObjectIds}}}});
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
  if (outSelectedObjectIds) {
    *outSelectedObjectIds = selectedObjectIds;
  }
  if (outCameraSnapshot) {
    *outCameraSnapshot = cameraSnapshot;
  }
  if (outDeltaCount) {
    *outDeltaCount = deltas.size();
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

bool SendResumeSnapshot(Interface *ip,
                        std::unordered_map<ULONG_PTR, NodeSnapshot> *outState,
                        MaterialStateMap *outMaterialState,
                        std::unordered_map<ULONG_PTR, LightSnapshot> *outLightState,
                        std::vector<std::string> *outSelectedObjectIds,
                        CameraSnapshot *outCameraSnapshot,
                        size_t *outDeltaCount,
                        std::string *outSessionId,
                        std::string *outDocumentId,
                        uint64_t *outNextSequence,
                        uint64_t *outNextRevision) {
  if (!ip || !EnsurePipeConnected()) {
    return false;
  }

  EnsurePersistentSceneIdentifiers(ip);
  CleanupPayloadRootDirectory();

  const std::string documentId = MakeDocumentId(ip);
  PersistedLiveLinkState persistedState;
  if (!ReadPersistedLiveLinkState(ip, &persistedState) ||
      persistedState.documentId != documentId) {
    return false;
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
  const std::vector<std::string> selectedObjectIds = GatherSelectedObjectIds(ip);
  CameraSnapshot cameraSnapshot;
  CaptureActiveCameraSnapshot(ip, &cameraSnapshot);

  std::unordered_map<std::string, NodeSnapshot> currentNodesByObjectId;
  currentNodesByObjectId.reserve(currentState.size());
  for (const auto &[_, snapshot] : currentState) {
    currentNodesByObjectId.emplace(snapshot.objectId, snapshot);
  }

  std::unordered_map<std::string, LightSnapshot> currentLightsByObjectId;
  currentLightsByObjectId.reserve(currentLightState.size());
  for (const auto &[_, snapshot] : currentLightState) {
    currentLightsByObjectId.emplace(snapshot.objectId, snapshot);
  }

  const std::string sessionId = MakeSessionId();
  const std::string documentPath = MakeDocumentPath(ip);
  const std::string documentDisplayName = MakeDocumentDisplayName(ip);
  json deltas = json::array();
  deltas.push_back(json{{"kind", "SessionOpened"},
                        {"target", json{{"sourceApp", kSourceApp},
                                          {"documentId", documentId},
                                          {"objectId", "session"},
                                          {"objectType", "Unknown"}}},
                        {"payload", json{{"documentPath", documentPath},
                                          {"displayName", documentDisplayName}}}});

  uint64_t revision = 1;
  std::string materialLibraryPayloadUri;
  const bool wroteMaterialLibrary =
      WriteMaterialLibraryPayload(documentId, currentMaterialState,
                                  &materialLibraryPayloadUri);
  if (wroteMaterialLibrary && !materialLibraryPayloadUri.empty()) {
    AppendMaterialLibraryDelta(documentId, materialLibraryPayloadUri, &revision,
                               &deltas);
  }
  for (const auto &[objectId, snapshot] : currentNodesByObjectId) {
    const auto previousIt = persistedState.nodeStateByObjectId.find(objectId);
    if (previousIt == persistedState.nodeStateByObjectId.end()) {
      AppendNodeAddedDelta(documentId, snapshot, &revision, &deltas);
      AppendNodeTransformDelta(documentId, snapshot, &revision, &deltas);
      AppendNodeVisibilityDelta(documentId, snapshot, &revision, &deltas);
      AppendMeshPayloadDeltaIfAvailable(ip, documentId, snapshot, &revision, &deltas);
      continue;
    }

    const NodeSnapshot &previous = previousIt->second;
    if (previous.parentObjectId != snapshot.parentObjectId ||
        previous.name != snapshot.name) {
      AppendNodeAddedDelta(documentId, snapshot, &revision, &deltas);
    }
    if (!SameMatrix(previous.worldMatrix, snapshot.worldMatrix)) {
      AppendNodeTransformDelta(documentId, snapshot, &revision, &deltas);
    }
    if (previous.visible != snapshot.visible) {
      AppendNodeVisibilityDelta(documentId, snapshot, &revision, &deltas);
    }
    if (snapshot.hasMesh &&
        (!previous.hasMesh ||
         previous.geometryFingerprint != snapshot.geometryFingerprint)) {
      AppendMeshPayloadDeltaIfAvailable(ip, documentId, snapshot, &revision, &deltas);
    }
  }

  for (const auto &[objectId, snapshot] : persistedState.nodeStateByObjectId) {
    if (currentNodesByObjectId.find(objectId) == currentNodesByObjectId.end()) {
      RemoveNodePayloadFile(documentId, snapshot.objectId);
      AppendNodeRemovedDelta(documentId, snapshot, &revision, &deltas);
    }
  }

  if (!wroteMaterialLibrary || materialLibraryPayloadUri.empty()) {
    for (const auto &[objectId, snapshot] : currentMaterialState) {
      const auto previousIt = persistedState.materialState.find(objectId);
      if (previousIt == persistedState.materialState.end() ||
          !SameMaterial(snapshot, previousIt->second)) {
        AppendMaterialDelta(documentId, snapshot, &revision, &deltas);
      }
    }

    for (const auto &[objectId, snapshot] : persistedState.materialState) {
      if (currentMaterialState.find(objectId) == currentMaterialState.end()) {
        AppendMaterialRemovedDelta(documentId, snapshot, &revision, &deltas);
      }
    }
  }

  for (const auto &[objectId, snapshot] : currentLightsByObjectId) {
    const auto previousIt = persistedState.lightStateByObjectId.find(objectId);
    if (previousIt == persistedState.lightStateByObjectId.end() ||
        !SameLight(snapshot, previousIt->second)) {
      AppendLightDelta(documentId, snapshot, &revision, &deltas);
    }
  }

  for (const auto &[objectId, snapshot] : persistedState.lightStateByObjectId) {
    if (currentLightsByObjectId.find(objectId) == currentLightsByObjectId.end()) {
      AppendLightRemovedDelta(documentId, snapshot, &revision, &deltas);
    }
  }

  if (selectedObjectIds != persistedState.selectedObjectIds) {
    deltas.push_back(json{{"kind", "SelectionChanged"},
                          {"target", MakeObjectId(documentId, "selection", "Selection")},
                          {"revision", revision++},
                          {"debugLabel", "3ds Max selection"},
                          {"payload", json{{"selectedObjectIds", selectedObjectIds}}}});
  }

  if (cameraSnapshot.valid && !SameCamera(cameraSnapshot, persistedState.cameraSnapshot)) {
    AppendCameraDelta(documentId, cameraSnapshot, &revision, &deltas);
  }

  if (!SendBatch(sessionId, 1, false, deltas)) {
    return false;
  }

  if (outState) {
    *outState = std::move(currentState);
  }
  if (outMaterialState) {
    *outMaterialState = std::move(currentMaterialState);
  }
  if (outLightState) {
    *outLightState = std::move(currentLightState);
  }
  if (outSelectedObjectIds) {
    *outSelectedObjectIds = selectedObjectIds;
  }
  if (outCameraSnapshot) {
    *outCameraSnapshot = cameraSnapshot;
  }
  if (outDeltaCount) {
    *outDeltaCount = deltas.size();
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
    MarkSelectionDirty();
  }

  void DeleteThis() override {}

private:
  using Clock = std::chrono::steady_clock;
  using CallbackKey = SceneEventNamespace::CallbackKey;
  using NodeKeyTab = NodeEventNamespace::NodeKeyTab;

  enum DirtyFlags : uint32_t {
    DirtyNone = 0,
    DirtyNodeState = 1u << 0,
    DirtyMesh = 1u << 1,
    DirtyMaterial = 1u << 2,
    DirtyLight = 1u << 3,
    DirtySelection = 1u << 4,
  };

  class LiveLinkNodeEventCallback final : public INodeEventCallback {
  public:
    explicit LiveLinkNodeEventCallback(ProjectRenderLiveLinkUtility *owner)
        : m_owner(owner) {}

    void Added(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState | DirtyMesh | DirtyMaterial | DirtyLight); }
    void Deleted(NodeKeyTab & /*nodes*/) override { FullResync(); }
    void LinkChanged(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState); }
    void LayerChanged(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState); }
    void GroupChanged(NodeKeyTab & /*nodes*/) override { FullResync(); }
    void HierarchyOtherEvent(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState); }
    void ModelStructured(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState | DirtyMesh | DirtyMaterial | DirtyLight); }
    void GeometryChanged(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState | DirtyMesh | DirtyMaterial); }
    void TopologyChanged(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState | DirtyMesh | DirtyMaterial); }
    void MappingChanged(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState | DirtyMesh | DirtyMaterial); }
    void ExtentionChannelChanged(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState | DirtyMesh); }
    void ModelOtherEvent(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState | DirtyMesh | DirtyLight); }
    void MaterialStructured(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState | DirtyMesh | DirtyMaterial); }
    void MaterialOtherEvent(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState | DirtyMesh | DirtyMaterial); }
    void ControllerStructured(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState | DirtyMesh | DirtyMaterial | DirtyLight); }
    void ControllerOtherEvent(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState | DirtyMesh | DirtyMaterial | DirtyLight); }
    void NameChanged(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState); }
    void WireColorChanged(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState); }
    void RenderPropertiesChanged(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState | DirtyLight); }
    void DisplayPropertiesChanged(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState); }
    void UserPropertiesChanged(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState); }
    void PropertiesOtherEvent(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState); }
    void SelectionChanged(NodeKeyTab &nodes) override { Mark(nodes, DirtySelection); }
    void HideChanged(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState | DirtyLight); }
    void FreezeChanged(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState); }
    void DisplayOtherEvent(NodeKeyTab &nodes) override { Mark(nodes, DirtyNodeState); }

  private:
    void Mark(NodeKeyTab &nodes, uint32_t flags) {
      if (m_owner) {
        m_owner->MarkNodeKeysDirty(nodes, flags);
        if (flags != DirtySelection) {
          m_owner->DeferSceneProcessing(kSceneOperationSettleDelayMs);
        }
      }
    }

    void FullResync() {
      if (m_owner) {
        m_owner->MarkFullResyncNeeded();
        m_owner->DeferSceneProcessing(kSceneOperationSettleDelayMs);
      }
    }

    ProjectRenderLiveLinkUtility *m_owner = nullptr;
  };

  static Clock::time_point ComputeNextPollDeadline(uint64_t delayMs) {
    return Clock::now() + std::chrono::milliseconds(delayMs);
  }

  static uint64_t ComputePollDelayMs(size_t nodeCount, size_t deltaCount,
                                     uint64_t scanDurationMs) {
    uint64_t delayMs =
        deltaCount > 0 ? kActivePollMinIntervalMs : kIdlePollMinIntervalMs;
    if (scanDurationMs >= kSlowPollThresholdMs) {
      delayMs = (std::max)(delayMs, kHeavyPollMinIntervalMs);
    }
    if (deltaCount == 0 && nodeCount >= kLargeSceneNodeThreshold) {
      delayMs = (std::max)(delayMs, kHeavyPollMinIntervalMs);
    }
    return delayMs;
  }

  static uint64_t ComputeVerificationDelayMs(size_t nodeCount) {
    if (nodeCount >= kHugeSceneNodeThreshold) {
      return kHugeSceneTransformVerificationMinIntervalMs;
    }
    if (nodeCount >= kLargeSceneNodeThreshold) {
      return kLargeSceneTransformVerificationMinIntervalMs;
    }
    return kTransformVerificationMinIntervalMs;
  }

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
        utility->StartLiveSync(false);
        return TRUE;
      case kStartFullControlId:
        utility->StartLiveSync(true);
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
    g_utility.TryPollSceneChanges();
  }

  static void CALLBACK CameraPollTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    if (g_exportInProgress.load()) {
      return;
    }
    g_utility.TryPollCameraChanges();
  }

  static void NotificationProc(void *param, NotifyInfo *info) {
    ProjectRenderLiveLinkUtility *utility =
        reinterpret_cast<ProjectRenderLiveLinkUtility *>(param);
    if (!utility || !info) {
      return;
    }

    switch (info->intcode) {
    case NOTIFY_FILE_POST_MERGE3:
      if (info->callParam != nullptr) {
        const NotifyPostMerge3Param *mergeParam =
            static_cast<const NotifyPostMerge3Param *>(info->callParam);
        if (mergeParam->mergeSuccess) {
          utility->MarkFullResyncNeeded();
        }
      } else {
        utility->MarkFullResyncNeeded();
      }
      break;
    case NOTIFY_SCENE_PRE_DELETED_NODE:
      if (info->callParam != nullptr) {
        utility->MarkNodeDirty(static_cast<INode *>(info->callParam),
                               DirtyNodeState | DirtyMesh | DirtyMaterial |
                                   DirtyLight | DirtySelection);
      } else {
        utility->MarkSelectionDirty();
      }
      utility->DeferSceneProcessing(kSceneOperationSettleDelayMs);
      break;
    case NOTIFY_SEL_NODES_PRE_DELETE:
      if (info->callParam != nullptr) {
        utility->MarkNodesDirty(*static_cast<Tab<INode *> *>(info->callParam),
                                DirtyNodeState | DirtyMesh | DirtyMaterial |
                                    DirtyLight | DirtySelection);
      } else {
        utility->MarkSelectionDirty();
      }
      utility->DeferSceneProcessing(kSceneOperationSettleDelayMs);
      break;
    case NOTIFY_SCENE_UNDO:
    case NOTIFY_SCENE_REDO:
    case NOTIFY_PRE_NODES_CLONED:
    case NOTIFY_POST_NODES_CLONED:
      utility->MarkSelectionDirty();
      utility->DeferSceneProcessing(kSceneOperationSettleDelayMs);
      break;
    case NOTIFY_SYSTEM_POST_RESET:
    case NOTIFY_SYSTEM_POST_NEW:
    case NOTIFY_SCENE_XREF_POST_MERGE:
      utility->MarkFullResyncNeeded();
      break;
    case NOTIFY_SELECTIONSET_CHANGED:
    case NOTIFY_SV_SELECTIONSET_CHANGED:
      utility->MarkSelectionDirty();
      break;
    default:
      break;
    }
  }

  void RegisterSceneCallbacks() {
    if (m_sceneEventManager == nullptr) {
      m_sceneEventManager = GetISceneEventManager();
    }
    if (m_sceneEventManager != nullptr && m_sceneEventCallbackKey == 0) {
      m_sceneEventCallbackKey =
          m_sceneEventManager->RegisterCallback(&m_sceneEventCallback, TRUE);
    }

    if (m_notificationsRegistered) {
      return;
    }

    const int notificationCodes[] = {
        NOTIFY_SCENE_PRE_DELETED_NODE, NOTIFY_SEL_NODES_PRE_DELETE,
        NOTIFY_SCENE_UNDO,             NOTIFY_SCENE_REDO,
        NOTIFY_SYSTEM_POST_RESET,      NOTIFY_SYSTEM_POST_NEW,
        NOTIFY_SCENE_XREF_POST_MERGE,  NOTIFY_FILE_POST_MERGE3,
        NOTIFY_PRE_NODES_CLONED,
        NOTIFY_POST_NODES_CLONED,      NOTIFY_SELECTIONSET_CHANGED,
        NOTIFY_SV_SELECTIONSET_CHANGED,
    };
    for (int code : notificationCodes) {
      RegisterNotification(&ProjectRenderLiveLinkUtility::NotificationProc,
                           this, code);
    }
    m_notificationsRegistered = true;
  }

  void UnregisterSceneCallbacks() {
    if (m_sceneEventManager != nullptr && m_sceneEventCallbackKey != 0) {
      m_sceneEventManager->UnRegisterCallback(m_sceneEventCallbackKey);
      m_sceneEventCallbackKey = 0;
    }

    if (m_notificationsRegistered) {
      UnRegisterNotification(&ProjectRenderLiveLinkUtility::NotificationProc,
                             this);
      m_notificationsRegistered = false;
    }
  }

  void MarkSelectionDirty() { m_selectionDirty = true; }

  void MarkNodesDirty(const Tab<INode *> &nodes, uint32_t flags) {
    for (int index = 0; index < nodes.Count(); ++index) {
      MarkNodeDirty(nodes[index], flags);
    }
  }

  void MarkFullResyncNeeded() {
    m_forceFullResync = true;
    m_selectionDirty = true;
    DeferSceneProcessing(kSceneOperationSettleDelayMs);
  }

  void DeferSceneProcessing(uint64_t delayMs) {
    const Clock::time_point deadline = ComputeNextPollDeadline(delayMs);
    if (deadline > m_nextPollDeadline) {
      m_nextPollDeadline = deadline;
    }
    if (deadline > m_nextVerificationDeadline) {
      m_nextVerificationDeadline = deadline;
    }
    if (deadline > m_sceneOperationSettleDeadline) {
      m_sceneOperationSettleDeadline = deadline;
    }
  }

  void MarkNodeDirty(ULONG_PTR handle, uint32_t flags) {
    if (handle == 0) {
      return;
    }

    if ((flags & DirtyNodeState) != 0u) {
      m_dirtyNodeHandles.insert(handle);
    }
    if ((flags & DirtyMesh) != 0u) {
      m_dirtyMeshHandles.insert(handle);
    }
    if ((flags & DirtyMaterial) != 0u) {
      m_dirtyMaterialHandles.insert(handle);
    }
    if ((flags & DirtyLight) != 0u) {
      m_dirtyLightHandles.insert(handle);
    }
    if ((flags & DirtySelection) != 0u) {
      m_selectionDirty = true;
    }
  }

  void MarkNodeDirty(INode *node, uint32_t flags) {
    if (!node) {
      if ((flags & DirtySelection) != 0u) {
        m_selectionDirty = true;
      }
      return;
    }
    MarkNodeDirty(node->GetHandle(), flags);
  }

  void MarkNodeKeysDirty(const NodeKeyTab &nodes, uint32_t flags) {
    for (int index = 0; index < nodes.Count(); ++index) {
      INode *node = NodeEventNamespace::GetNodeByKey(nodes[index]);
      if (node) {
        MarkNodeDirty(node, flags);
      } else if ((flags & DirtySelection) != 0u) {
        m_selectionDirty = true;
      } else {
        m_forceFullResync = true;
      }
    }
  }

  Interface *GetLiveInterface() const {
    return m_interface ? m_interface : GetCOREInterface();
  }

  size_t CountTrackedMeshNodes_NoLock() const {
    size_t meshNodeCount = 0;
    for (const auto &[_, snapshot] : m_lastNodeState) {
      if (snapshot.hasMesh) {
        ++meshNodeCount;
      }
    }
    return meshNodeCount;
  }

  size_t CountTrackedTextureUris_NoLock() const {
    std::unordered_set<std::string> textureUris;
    for (const auto &[_, snapshot] : m_lastMaterialState) {
      if (!snapshot.baseColorTextureUri.empty()) {
        textureUris.insert(snapshot.baseColorTextureUri);
      }
      if (!snapshot.normalTextureUri.empty()) {
        textureUris.insert(snapshot.normalTextureUri);
      }
      if (!snapshot.emissiveTextureUri.empty()) {
        textureUris.insert(snapshot.emissiveTextureUri);
      }
      if (!snapshot.occlusionTextureUri.empty()) {
        textureUris.insert(snapshot.occlusionTextureUri);
      }
      if (!snapshot.metalRoughTextureUri.empty()) {
        textureUris.insert(snapshot.metalRoughTextureUri);
      }
    }
    return textureUris.size();
  }

  size_t CountDirtyItems_NoLock() const {
    size_t dirtyCount = m_dirtyNodeHandles.size() + m_dirtyMeshHandles.size() +
                        m_dirtyMaterialHandles.size() +
                        m_dirtyLightHandles.size();
    if (m_selectionDirty) {
      ++dirtyCount;
    }
    return dirtyCount;
  }

  const char *GetResyncStatusText_NoLock() const {
    if (m_forceFullSnapshotOnConnect) {
      return "full snapshot on connect";
    }
    if (m_forceFullResync) {
      return "full resync pending";
    }
    if (m_syncActive) {
      return "incremental";
    }
    return "inactive";
  }

  void RecordBatchSent_NoLock(size_t deltaCount) {
    ++m_batchesSent;
    m_totalDeltasSent += deltaCount;
    m_lastBatchDeltaCount = deltaCount;
  }

  void ResetBatchStats_NoLock() {
    m_batchesSent = 0;
    m_totalDeltasSent = 0;
    m_lastBatchDeltaCount = 0;
  }

  void ScheduleVerificationSweep_NoLock() {
    m_nextVerificationDeadline =
        ComputeNextPollDeadline(ComputeVerificationDelayMs(m_lastNodeState.size()));
  }

  std::string BuildDetailsText_NoLock() const {
    const std::string startupSummary =
        m_lastStartupSummary.empty() ? std::string("Last startup: none.")
                                     : m_lastStartupSummary;
    return startupSummary +
           "\r\nSync: " + GetResyncStatusText_NoLock() +
           " | session=" + (m_sessionId.empty() ? std::string("none") : m_sessionId) +
           " | dirty=" + std::to_string(CountDirtyItems_NoLock()) +
           "\r\nScene: nodes=" + std::to_string(m_lastNodeState.size()) +
           " | meshNodes=" + std::to_string(CountTrackedMeshNodes_NoLock()) +
           " | lights=" + std::to_string(m_lastLightState.size()) +
           " | materials=" + std::to_string(m_lastMaterialState.size()) +
           " | textures=" + std::to_string(CountTrackedTextureUris_NoLock()) +
           "\r\nBatches: sent=" + std::to_string(m_batchesSent) +
           " | deltas=" + std::to_string(m_totalDeltasSent) +
           " | lastBatch=" + std::to_string(m_lastBatchDeltaCount);
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
    const std::wstring detailsText = Utf8ToWString(BuildDetailsText_NoLock());

    EnableWindow(GetDlgItem(rollupHwnd, kStartControlId),
           m_syncActive ? FALSE : TRUE);
    EnableWindow(GetDlgItem(rollupHwnd, kStartFullControlId),
           m_syncActive ? FALSE : TRUE);
    EnableWindow(GetDlgItem(rollupHwnd, kStopControlId),
                 m_syncActive ? TRUE : FALSE);

    SetDlgItemTextW(rollupHwnd, kStatusControlId, statusText.c_str());
    SetDlgItemTextW(rollupHwnd, kDetailsControlId, detailsText.c_str());
  }

  void RefreshRollupUI() {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    RefreshRollupUI_NoLock();
  }

  void PersistResumeStateLocked(Interface *ip) {
    if (!ip || m_documentId.empty()) {
      return;
    }
    WritePersistedLiveLinkState(ip, m_documentId, m_lastNodeState,
                                m_lastMaterialState, m_lastLightState,
                                m_lastSelectedObjectIds, m_lastCameraSnapshot);
  }

  void ResetTrackedState_NoLock() {
    m_lastNodeState.clear();
    m_lastMaterialState.clear();
    m_lastLightState.clear();
    m_lastSelectedObjectIds.clear();
    m_lastCameraSnapshot = CameraSnapshot{};
    m_sessionId.clear();
    m_documentId.clear();
    m_nextSequence = 1;
    m_nextRevision = 1;
    m_nextPollDeadline = Clock::time_point{};
    m_nextCameraPollDeadline = Clock::time_point{};
    m_nextVerificationDeadline = Clock::time_point{};
    m_nextResumePersistDeadline = Clock::time_point{};
    m_forceFullResync = false;
    m_selectionDirty = false;
    m_resumeStateDirty = false;
    m_dirtyNodeHandles.clear();
    m_dirtyMeshHandles.clear();
    m_dirtyMaterialHandles.clear();
    m_dirtyLightHandles.clear();
    m_pendingPayloadRemovals.clear();
  }

  void MarkResumeStateDirty_NoLock(uint64_t delayMs = kResumeStatePersistDelayMs) {
    m_resumeStateDirty = true;
    m_nextResumePersistDeadline = ComputeNextPollDeadline(delayMs);
  }

  void MaybePersistResumeStateLocked(Interface *ip, bool force) {
    if (!m_resumeStateDirty || !ip || m_documentId.empty()) {
      return;
    }
    if (!force &&
        m_nextResumePersistDeadline != Clock::time_point{} &&
        Clock::now() < m_nextResumePersistDeadline) {
      return;
    }
    PersistResumeStateLocked(ip);
    m_resumeStateDirty = false;
    m_nextResumePersistDeadline = Clock::time_point{};
  }

  bool EnsureConnectedSession(Interface *ip) {
    if (!ip) {
      return false;
    }

    RegisterSceneCallbacks();

    const bool hadPipeConnection = g_pipeClient.IsConnected();
    if (!EnsurePipeConnected()) {
      return false;
    }

    if (hadPipeConnection && !m_sessionId.empty() && !m_documentId.empty()) {
      return true;
    }

    size_t startupDeltaCount = 0;
    bool usedResumeStartup = false;
    const bool clearsExistingScene = m_forceFullSnapshotOnConnect;
    if (!m_forceFullSnapshotOnConnect &&
        SendResumeSnapshot(ip, &m_lastNodeState, &m_lastMaterialState,
                           &m_lastLightState, &m_lastSelectedObjectIds,
                           &m_lastCameraSnapshot, &startupDeltaCount,
                           &m_sessionId, &m_documentId, &m_nextSequence,
                           &m_nextRevision)) {
      usedResumeStartup = true;
    } else {
      if (!SendInitialSnapshot(ip, &m_lastNodeState, &m_lastMaterialState,
                               &m_lastLightState, &m_lastSelectedObjectIds,
                               &m_lastCameraSnapshot, &startupDeltaCount,
                               &m_sessionId,
                               &m_documentId, &m_nextSequence,
                               &m_nextRevision, clearsExistingScene)) {
        return false;
      }
    }
    m_forceFullSnapshotOnConnect = false;
    ResetBatchStats_NoLock();
    RecordBatchSent_NoLock(startupDeltaCount);
    m_lastStartupSummary = std::string("Last startup: ") +
                           (usedResumeStartup
                                ? "resume sync"
                                : (clearsExistingScene ? "full resync"
                                                       : "full snapshot")) +
                           " (deltas=" + std::to_string(startupDeltaCount) + ")";

    PersistResumeStateLocked(ip);
    m_resumeStateDirty = false;
    m_nextResumePersistDeadline = Clock::time_point{};
    m_nextPollDeadline = ComputeNextPollDeadline(kActivePollMinIntervalMs);
    m_nextCameraPollDeadline = ComputeNextPollDeadline(kCameraPollMinIntervalMs);
    ScheduleVerificationSweep_NoLock();
    m_forceFullResync = false;
    m_selectionDirty = false;
    m_dirtyNodeHandles.clear();
    m_dirtyMeshHandles.clear();
    m_dirtyMaterialHandles.clear();
    m_dirtyLightHandles.clear();
    return true;
  }

  bool StartLiveSync(bool forceFullResync) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (m_syncActive && !forceFullResync) {
      RefreshRollupUI_NoLock();
      return true;
    }

    Interface *ip = GetLiveInterface();
    if (!ip) {
      RefreshRollupUI_NoLock();
      return false;
    }

    if (forceFullResync) {
      if (!m_sessionId.empty()) {
        SendSessionClosed(m_sessionId, m_nextSequence++);
      }
      FlushQueuedPayloadRemovals_NoLock(true);
      ClearPersistedLiveLinkState(ip);
      RemoveDocumentPayloadDirectory(MakeDocumentId(ip));
      g_pipeClient.Disconnect();
      ResetTrackedState_NoLock();
    }

    m_forceFullSnapshotOnConnect = forceFullResync;

    if (!EnsureConnectedSession(ip)) {
      RefreshRollupUI_NoLock();
      return false;
    }

    if (m_pollTimer == 0) {
      m_pollTimer = SetTimer(nullptr, kPollTimerId, kPollIntervalMs,
                             &ProjectRenderLiveLinkUtility::PollTimerProc);
    }
    if (m_cameraPollTimer == 0) {
      m_cameraPollTimer = SetTimer(nullptr, kCameraPollTimerId,
                                   kCameraPollIntervalMs,
                                   &ProjectRenderLiveLinkUtility::CameraPollTimerProc);
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
    Interface *ip = GetLiveInterface();

    if (m_pollTimer != 0) {
      KillTimer(nullptr, m_pollTimer);
      m_pollTimer = 0;
    }
    if (m_cameraPollTimer != 0) {
      KillTimer(nullptr, m_cameraPollTimer);
      m_cameraPollTimer = 0;
    }
    SendSessionClosed(m_sessionId, m_nextSequence++);
    MaybePersistResumeStateLocked(ip, true);
    FlushQueuedPayloadRemovals_NoLock(true);
    UnregisterSceneCallbacks();
    g_pipeClient.Disconnect();
    ResetTrackedState_NoLock();
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

  void TryPollSceneChanges() {
    std::unique_lock<std::mutex> lock(m_sendMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      return;
    }
    PollSceneChangesLocked();
  }

  void TryPollCameraChanges() {
    std::unique_lock<std::mutex> lock(m_sendMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      return;
    }
    PollCameraChangesLocked();
  }

  void ClearDirtyState() {
    m_forceFullResync = false;
    m_selectionDirty = false;
    m_dirtyNodeHandles.clear();
    m_dirtyMeshHandles.clear();
    m_dirtyMaterialHandles.clear();
    m_dirtyLightHandles.clear();
  }

  void ApplyStagedMaterialState(ULONG_PTR handle,
                                const MaterialStateMap &materialState) {
    for (auto it = m_lastMaterialState.begin(); it != m_lastMaterialState.end();) {
      if (it->second.nodeHandle == handle) {
        it = m_lastMaterialState.erase(it);
      } else {
        ++it;
      }
    }
    for (const auto &[objectId, snapshot] : materialState) {
      m_lastMaterialState[objectId] = snapshot;
    }
  }

  void PollCameraChangesLocked() {
    const Clock::time_point now = Clock::now();
    if (m_nextCameraPollDeadline != Clock::time_point{} &&
        now < m_nextCameraPollDeadline) {
      return;
    }

    Interface *ip = GetLiveInterface();
    if (!m_syncActive || !ip || !EnsureConnectedSession(ip)) {
      m_nextCameraPollDeadline =
          ComputeNextPollDeadline(kReconnectPollMinIntervalMs);
      return;
    }

    CameraSnapshot currentCamera;
    CaptureActiveCameraSnapshot(ip, &currentCamera);
    m_nextCameraPollDeadline =
        ComputeNextPollDeadline(kCameraPollMinIntervalMs);
    if (!currentCamera.valid || SameCamera(currentCamera, m_lastCameraSnapshot)) {
      return;
    }

    json deltas = json::array();
    AppendCameraDelta(m_documentId, currentCamera, &m_nextRevision, &deltas);
    if (SendBatch(m_sessionId, m_nextSequence, false, deltas)) {
      ++m_nextSequence;
      RecordBatchSent_NoLock(deltas.size());
      m_lastCameraSnapshot = currentCamera;
      MarkResumeStateDirty_NoLock();
      RefreshRollupUI_NoLock();
    }
  }

  void QueuePayloadRemoval_NoLock(const std::string &nodeObjectId) {
    if (m_documentId.empty() || nodeObjectId.empty()) {
      return;
    }
    m_pendingPayloadRemovals.insert(nodeObjectId);
  }

  bool FlushQueuedPayloadRemovals_NoLock(bool force) {
    if (m_documentId.empty() || m_pendingPayloadRemovals.empty()) {
      return false;
    }

    size_t removedCount = 0;
    const size_t maxCount =
        force ? m_pendingPayloadRemovals.size() : kPayloadRemovalBatchSize;
    for (auto it = m_pendingPayloadRemovals.begin();
         it != m_pendingPayloadRemovals.end() && removedCount < maxCount;) {
      RemoveNodePayloadFile(m_documentId, *it);
      it = m_pendingPayloadRemovals.erase(it);
      ++removedCount;
    }

    if (force || m_pendingPayloadRemovals.empty()) {
      RemoveDocumentPayloadDirectoryIfEmpty(m_documentId);
    }
    return removedCount > 0;
  }

  void PollSceneChangesLocked() {
    const Clock::time_point now = Clock::now();
    if (m_sceneOperationSettleDeadline != Clock::time_point{} &&
        now < m_sceneOperationSettleDeadline) {
      return;
    }
    m_sceneOperationSettleDeadline = Clock::time_point{};
    const bool transformVerificationDue =
        !m_forceFullResync &&
        m_nextVerificationDeadline != Clock::time_point{} &&
        now >= m_nextVerificationDeadline;
    const bool resumePersistDue =
        m_resumeStateDirty &&
        m_nextResumePersistDeadline != Clock::time_point{} &&
        now >= m_nextResumePersistDeadline;

    if (!m_forceFullResync && !transformVerificationDue && !resumePersistDue &&
        m_nextPollDeadline != Clock::time_point{} &&
        now < m_nextPollDeadline) {
      return;
    }

    if (m_sceneEventManager != nullptr && m_sceneEventCallbackKey != 0) {
      m_sceneEventManager->TriggerMessages(m_sceneEventCallbackKey);
    }

    Interface *ip = GetLiveInterface();
    if (!m_syncActive || !ip || !EnsureConnectedSession(ip)) {
      m_nextPollDeadline = ComputeNextPollDeadline(kReconnectPollMinIntervalMs);
      return;
    }

    const bool hasDirtyWorkPending =
        m_forceFullResync || transformVerificationDue || m_selectionDirty ||
        !m_dirtyNodeHandles.empty() || !m_dirtyMeshHandles.empty() ||
        !m_dirtyMaterialHandles.empty() || !m_dirtyLightHandles.empty();
    if (resumePersistDue && !hasDirtyWorkPending) {
      MaybePersistResumeStateLocked(ip, false);
      m_nextPollDeadline = ComputeNextPollDeadline(kIdlePollMinIntervalMs);
      RefreshRollupUI_NoLock();
      return;
    }
    if (!hasDirtyWorkPending && FlushQueuedPayloadRemovals_NoLock(false)) {
      m_nextPollDeadline = ComputeNextPollDeadline(kIdlePollMinIntervalMs);
      RefreshRollupUI_NoLock();
      return;
    }

    const Clock::time_point scanStart = Clock::now();
    json deltas = json::array();

    size_t scannedNodeCount = m_lastNodeState.size();
    std::vector<std::string> selectedObjectIds = m_lastSelectedObjectIds;
    std::unordered_map<ULONG_PTR, NodeSnapshot> stagedNodes;
    std::unordered_map<ULONG_PTR, LightSnapshot> stagedLights;
    std::vector<ULONG_PTR> removedLightHandles;
    std::unordered_set<ULONG_PTR> removedLightHandleSet;
    std::vector<std::pair<ULONG_PTR, MaterialStateMap>> stagedMaterialStates;
    std::unordered_set<ULONG_PTR> stagedMaterialHandles;

    const auto captureMaterialStateForSnapshot =
        [&](const NodeSnapshot &snapshot) -> MaterialStateMap {
      MaterialStateMap materialStateForNode;
      if (!snapshot.hasMesh) {
        return materialStateForNode;
      }

      std::unordered_map<ULONG_PTR, NodeSnapshot> nodeState;
      nodeState.emplace(snapshot.handle, snapshot);
      GatherMaterialSnapshots(ip, nodeState, &materialStateForNode);
      return materialStateForNode;
    };

    const auto appendMaterialStateDiff =
        [&](const MaterialStateMap &materialStateForNode) {
      for (const auto &[objectId, materialSnapshot] : materialStateForNode) {
        const auto previousMaterialIt = m_lastMaterialState.find(objectId);
        if (previousMaterialIt == m_lastMaterialState.end() ||
            !SameMaterial(materialSnapshot, previousMaterialIt->second)) {
          AppendMaterialDelta(m_documentId, materialSnapshot, &m_nextRevision,
                              &deltas);
        }
      }
    };

    const auto appendTrackedMaterialRemovals =
        [&](ULONG_PTR handle, const MaterialStateMap *replacementState) {
      for (const auto &[objectId, previousMaterial] : m_lastMaterialState) {
        if (previousMaterial.nodeHandle != handle) {
          continue;
        }
        if (replacementState != nullptr &&
            replacementState->find(objectId) != replacementState->end()) {
          continue;
        }
        AppendMaterialRemovedDelta(m_documentId, previousMaterial,
                                   &m_nextRevision, &deltas);
      }
    };

    const auto stageMaterialState =
        [&](ULONG_PTR handle, MaterialStateMap materialStateForNode) {
      if (!stagedMaterialHandles.insert(handle).second) {
        return;
      }
      stagedMaterialStates.push_back(
          std::make_pair(handle, std::move(materialStateForNode)));
    };

    const auto appendTrackedLightRemoval = [&](ULONG_PTR handle) {
      const auto previousIt = m_lastLightState.find(handle);
      if (previousIt == m_lastLightState.end() ||
          !removedLightHandleSet.insert(handle).second) {
        return;
      }
      AppendLightRemovedDelta(m_documentId, previousIt->second, &m_nextRevision,
                              &deltas);
      removedLightHandles.push_back(handle);
    };

    if (m_forceFullResync) {
      EnsurePersistentSceneIdentifiers(ip);
      std::unordered_map<ULONG_PTR, NodeSnapshot> currentState;
      MaterialStateMap currentMaterialState;
      std::unordered_map<ULONG_PTR, LightSnapshot> currentLightState;
      if (INode *root = ip->GetRootNode()) {
        for (int childIndex = 0; childIndex < root->NumberOfChildren(); ++childIndex) {
          GatherNodeSnapshots(ip, root->GetChildNode(childIndex), &currentState);
          GatherLightSnapshots(ip, root->GetChildNode(childIndex),
                               &currentLightState);
        }
      }
      GatherMaterialSnapshots(ip, currentState, &currentMaterialState);
      scannedNodeCount = currentState.size();
      std::string materialLibraryPayloadUri;
      const bool wroteMaterialLibrary =
          WriteMaterialLibraryPayload(m_documentId, currentMaterialState,
                                      &materialLibraryPayloadUri);
      if (wroteMaterialLibrary && !materialLibraryPayloadUri.empty()) {
        AppendMaterialLibraryDelta(m_documentId, materialLibraryPayloadUri,
                                   &m_nextRevision, &deltas);
      }

      for (const auto &[handle, snapshot] : currentState) {
        auto previousIt = m_lastNodeState.find(handle);
        if (previousIt == m_lastNodeState.end()) {
          AppendNodeAddedDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
          AppendNodeTransformDelta(m_documentId, snapshot, &m_nextRevision,
                                   &deltas);
          AppendNodeVisibilityDelta(m_documentId, snapshot, &m_nextRevision,
                                    &deltas);
          AppendMeshPayloadDeltaIfAvailable(ip, m_documentId,
                                            snapshot, &m_nextRevision, &deltas);
          continue;
        }

        const NodeSnapshot &previous = previousIt->second;
        if (previous.objectId != snapshot.objectId) {
          QueuePayloadRemoval_NoLock(previous.objectId);
          AppendNodeRemovedDelta(m_documentId, previous, &m_nextRevision, &deltas);
          AppendNodeAddedDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
          AppendNodeTransformDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
          AppendNodeVisibilityDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
          AppendMeshPayloadDeltaIfAvailable(ip, m_documentId, snapshot, &m_nextRevision, &deltas);
          continue;
        }
        if (previous.parentHandle != snapshot.parentHandle ||
            previous.name != snapshot.name) {
          AppendNodeAddedDelta(m_documentId, snapshot, &m_nextRevision,
                               &deltas);
        }
        if (!SameMatrix(previous.worldMatrix, snapshot.worldMatrix)) {
          AppendNodeTransformDelta(m_documentId, snapshot, &m_nextRevision,
                                   &deltas);
        }
        if (previous.visible != snapshot.visible) {
          AppendNodeVisibilityDelta(m_documentId, snapshot, &m_nextRevision,
                                    &deltas);
        }
        if (snapshot.hasMesh &&
            (!previous.hasMesh ||
             previous.geometryFingerprint != snapshot.geometryFingerprint)) {
          AppendMeshPayloadDeltaIfAvailable(ip, m_documentId,
                                            snapshot, &m_nextRevision, &deltas);
        }
      }

      if (!wroteMaterialLibrary || materialLibraryPayloadUri.empty()) {
        for (const auto &[objectId, snapshot] : currentMaterialState) {
          const auto previousMaterialIt = m_lastMaterialState.find(objectId);
          if (previousMaterialIt == m_lastMaterialState.end() ||
              !SameMaterial(snapshot, previousMaterialIt->second)) {
            AppendMaterialDelta(m_documentId, snapshot, &m_nextRevision,
                                &deltas);
          }
        }

        for (const auto &[objectId, snapshot] : m_lastMaterialState) {
          if (currentMaterialState.find(objectId) == currentMaterialState.end()) {
            AppendMaterialRemovedDelta(m_documentId, snapshot, &m_nextRevision,
                                       &deltas);
          }
        }
      }

      for (const auto &[handle, previousSnapshot] : m_lastNodeState) {
        if (currentState.find(handle) == currentState.end()) {
          QueuePayloadRemoval_NoLock(previousSnapshot.objectId);
          AppendNodeRemovedDelta(m_documentId, previousSnapshot,
                                 &m_nextRevision, &deltas);
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

      selectedObjectIds = GatherSelectedObjectIds(ip);
      if (selectedObjectIds != m_lastSelectedObjectIds) {
        AppendSelectionDelta(selectedObjectIds, &deltas);
      }

      const uint64_t scanDurationMs = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                                scanStart)
              .count());
      m_nextPollDeadline = ComputeNextPollDeadline(
          ComputePollDelayMs(currentState.size(), deltas.size(), scanDurationMs));

      if (!deltas.empty() &&
          SendBatch(m_sessionId, m_nextSequence, false, deltas)) {
        ++m_nextSequence;
        RecordBatchSent_NoLock(deltas.size());
        m_lastNodeState = std::move(currentState);
        m_lastMaterialState = std::move(currentMaterialState);
        m_lastLightState = std::move(currentLightState);
        m_lastSelectedObjectIds = selectedObjectIds;
        CaptureActiveCameraSnapshot(ip, &m_lastCameraSnapshot);
        MarkResumeStateDirty_NoLock();
        ScheduleVerificationSweep_NoLock();
        ClearDirtyState();
      } else if (deltas.empty()) {
        m_lastNodeState = std::move(currentState);
        m_lastMaterialState = std::move(currentMaterialState);
        m_lastLightState = std::move(currentLightState);
        m_lastSelectedObjectIds = selectedObjectIds;
        CaptureActiveCameraSnapshot(ip, &m_lastCameraSnapshot);
        MarkResumeStateDirty_NoLock();
        ScheduleVerificationSweep_NoLock();
        ClearDirtyState();
      }

      MaybePersistResumeStateLocked(ip, false);
      RefreshRollupUI_NoLock();
      return;
    }

    for (ULONG_PTR handle : m_dirtyNodeHandles) {
      const auto previousIt = m_lastNodeState.find(handle);
      NodeSnapshot snapshot;
      if (!TryCaptureNodeSnapshotByHandle(ip, handle, &snapshot)) {
        if (previousIt != m_lastNodeState.end()) {
          QueuePayloadRemoval_NoLock(previousIt->second.objectId);
          AppendNodeRemovedDelta(m_documentId, previousIt->second,
                                 &m_nextRevision, &deltas);
          appendTrackedMaterialRemovals(handle, nullptr);
          stageMaterialState(handle, MaterialStateMap{});
          appendTrackedLightRemoval(handle);
        }
        continue;
      }
      stagedNodes[handle] = snapshot;

      if (previousIt == m_lastNodeState.end()) {
        AppendNodeAddedDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
        AppendNodeTransformDelta(m_documentId, snapshot, &m_nextRevision,
                                 &deltas);
        AppendNodeVisibilityDelta(m_documentId, snapshot, &m_nextRevision,
                                  &deltas);
      } else {
        const NodeSnapshot &previous = previousIt->second;
        const bool nodeIdentityChanged = previous.objectId != snapshot.objectId;
        const bool meshWasRemoved = previous.hasMesh && !snapshot.hasMesh;
        if (nodeIdentityChanged || meshWasRemoved) {
          const MaterialStateMap replacementMaterialState =
              captureMaterialStateForSnapshot(snapshot);
          QueuePayloadRemoval_NoLock(previous.objectId);
          AppendNodeRemovedDelta(m_documentId, previous, &m_nextRevision, &deltas);
          appendTrackedMaterialRemovals(handle, &replacementMaterialState);
          stageMaterialState(handle, MaterialStateMap(replacementMaterialState));
          AppendNodeAddedDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
          AppendNodeTransformDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
          AppendNodeVisibilityDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
          AppendMeshPayloadDeltaIfAvailable(ip, m_documentId, snapshot, &m_nextRevision, &deltas);
          appendMaterialStateDiff(replacementMaterialState);
          continue;
        }
        if (previous.parentHandle != snapshot.parentHandle ||
            previous.name != snapshot.name) {
          AppendNodeAddedDelta(m_documentId, snapshot, &m_nextRevision,
                               &deltas);
        }
        if (!SameMatrix(previous.worldMatrix, snapshot.worldMatrix)) {
          AppendNodeTransformDelta(m_documentId, snapshot, &m_nextRevision,
                                   &deltas);
        }
        if (previous.visible != snapshot.visible) {
          AppendNodeVisibilityDelta(m_documentId, snapshot, &m_nextRevision,
                                    &deltas);
        }
      }

      if (m_dirtyMeshHandles.find(handle) != m_dirtyMeshHandles.end() &&
          snapshot.hasMesh) {
        const auto previousItForMesh = m_lastNodeState.find(handle);
        if (previousItForMesh == m_lastNodeState.end() ||
            !previousItForMesh->second.hasMesh ||
            previousItForMesh->second.geometryFingerprint !=
                snapshot.geometryFingerprint) {
          AppendMeshPayloadDeltaIfAvailable(ip, m_documentId,
                                            snapshot, &m_nextRevision, &deltas);
        }
      }
    }

    for (ULONG_PTR handle : m_dirtyMaterialHandles) {
      if (stagedMaterialHandles.find(handle) != stagedMaterialHandles.end()) {
        continue;
      }

      NodeSnapshot snapshot;
      auto stagedIt = stagedNodes.find(handle);
      if (stagedIt != stagedNodes.end()) {
        snapshot = stagedIt->second;
      } else if (!TryCaptureNodeSnapshotByHandle(ip, handle, &snapshot)) {
        appendTrackedMaterialRemovals(handle, nullptr);
        stageMaterialState(handle, MaterialStateMap{});
        continue;
      }

      MaterialStateMap materialStateForNode =
          captureMaterialStateForSnapshot(snapshot);
      appendMaterialStateDiff(materialStateForNode);
      appendTrackedMaterialRemovals(handle, &materialStateForNode);
      stageMaterialState(handle, std::move(materialStateForNode));
    }

    for (ULONG_PTR handle : m_dirtyLightHandles) {
      if (removedLightHandleSet.find(handle) != removedLightHandleSet.end()) {
        continue;
      }

      LightSnapshot snapshot;
      if (TryCaptureLightSnapshotByHandle(ip, handle, &snapshot)) {
        stagedLights[handle] = snapshot;
        const auto previousIt = m_lastLightState.find(handle);
        if (previousIt == m_lastLightState.end() ||
            !SameLight(previousIt->second, snapshot)) {
          AppendLightDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
        }
      } else {
        const auto previousIt = m_lastLightState.find(handle);
        if (previousIt != m_lastLightState.end()) {
          appendTrackedLightRemoval(handle);
        }
      }
    }

    if (transformVerificationDue) {
      // INodeEventCallback can miss transform updates during interactive
      // manipulations, IK evaluation, or playback, so verify transforms on a
      // slower cadence instead of every poll.
      if (INode *root = ip->GetRootNode()) {
        std::vector<INode *> traversalStack;
        for (int i = 0; i < root->NumberOfChildren(); ++i) {
          traversalStack.push_back(root->GetChildNode(i));
        }
        while (!traversalStack.empty()) {
          INode *node = traversalStack.back();
          traversalStack.pop_back();
          if (node) {
            ULONG_PTR handle = node->GetHandle();
            if (stagedNodes.find(handle) == stagedNodes.end()) {
              auto it = m_lastNodeState.find(handle);
              if (it != m_lastNodeState.end()) {
                std::array<float, 16> currentMatrix =
                    Matrix3ToColumnMajor4x4(node->GetNodeTM(ip->GetTime()));
                if (!SameMatrix(it->second.worldMatrix, currentMatrix)) {
                  NodeSnapshot updatedSnapshot = it->second;
                  updatedSnapshot.worldMatrix = currentMatrix;
                  stagedNodes[handle] = updatedSnapshot;
                  AppendNodeTransformDelta(m_documentId, updatedSnapshot,
                                           &m_nextRevision, &deltas);
                }
              }
            }
            for (int i = 0; i < node->NumberOfChildren(); ++i) {
              traversalStack.push_back(node->GetChildNode(i));
            }
          }
        }
      }
      ScheduleVerificationSweep_NoLock();
    }

    if (m_selectionDirty) {
      selectedObjectIds = GatherSelectedObjectIds(ip);
      if (selectedObjectIds != m_lastSelectedObjectIds) {
        AppendSelectionDelta(selectedObjectIds, &deltas);
      }
    }

    const uint64_t scanDurationMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                              scanStart)
            .count());
    m_nextPollDeadline = ComputeNextPollDeadline(ComputePollDelayMs(
        scannedNodeCount, deltas.size(), scanDurationMs));

    const bool selectionChanged =
        m_selectionDirty && selectedObjectIds != m_lastSelectedObjectIds;
    const bool hadPendingDirtyWork =
        m_selectionDirty || !m_dirtyNodeHandles.empty() ||
        !m_dirtyMeshHandles.empty() || !m_dirtyMaterialHandles.empty() ||
        !m_dirtyLightHandles.empty();
    const bool hasIncrementalStateUpdates =
        !stagedNodes.empty() || !stagedLights.empty() ||
        !removedLightHandles.empty() || !stagedMaterialStates.empty() ||
        selectionChanged;

    if (!deltas.empty() && SendBatch(m_sessionId, m_nextSequence, false, deltas)) {
      ++m_nextSequence;
      RecordBatchSent_NoLock(deltas.size());
      for (const auto &[handle, snapshot] : stagedNodes) {
        m_lastNodeState[handle] = snapshot;
      }
      for (const auto &[handle, snapshot] : stagedLights) {
        m_lastLightState[handle] = snapshot;
      }
      for (ULONG_PTR handle : removedLightHandles) {
        m_lastLightState.erase(handle);
      }
      for (const auto &[handle, materialState] : stagedMaterialStates) {
        ApplyStagedMaterialState(handle, materialState);
      }
      if (m_selectionDirty) {
        m_lastSelectedObjectIds = selectedObjectIds;
      }
      MarkResumeStateDirty_NoLock();
      ClearDirtyState();
    } else if (deltas.empty() && (hasIncrementalStateUpdates || hadPendingDirtyWork)) {
      for (const auto &[handle, snapshot] : stagedNodes) {
        m_lastNodeState[handle] = snapshot;
      }
      for (const auto &[handle, snapshot] : stagedLights) {
        m_lastLightState[handle] = snapshot;
      }
      for (ULONG_PTR handle : removedLightHandles) {
        m_lastLightState.erase(handle);
      }
      for (const auto &[handle, materialState] : stagedMaterialStates) {
        ApplyStagedMaterialState(handle, materialState);
      }
      if (m_selectionDirty) {
        m_lastSelectedObjectIds = selectedObjectIds;
      }
      if (hasIncrementalStateUpdates) {
        MarkResumeStateDirty_NoLock();
      }
      ClearDirtyState();
    }

    MaybePersistResumeStateLocked(ip, false);
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
      RecordBatchSent_NoLock(deltas.size());
      m_lastSelectedObjectIds = selectedObjectIds;
      MarkResumeStateDirty_NoLock();
    }

    MaybePersistResumeStateLocked(ip, false);
    RefreshRollupUI_NoLock();
  }

  Interface *m_interface = nullptr;
  IUtil *m_iu = nullptr;
  HWND m_rollupHwnd = nullptr;
  std::mutex m_sendMutex;
  bool m_syncActive = false;
  UINT_PTR m_pollTimer = 0;
  UINT_PTR m_cameraPollTimer = 0;
  Clock::time_point m_nextPollDeadline = Clock::time_point{};
  Clock::time_point m_nextCameraPollDeadline = Clock::time_point{};
  Clock::time_point m_nextVerificationDeadline = Clock::time_point{};
  Clock::time_point m_sceneOperationSettleDeadline = Clock::time_point{};
  Clock::time_point m_nextResumePersistDeadline = Clock::time_point{};
  ISceneEventManager *m_sceneEventManager = nullptr;
  CallbackKey m_sceneEventCallbackKey = 0;
  LiveLinkNodeEventCallback m_sceneEventCallback{this};
  bool m_notificationsRegistered = false;
  bool m_forceFullResync = false;
  bool m_selectionDirty = false;
  bool m_resumeStateDirty = false;
  std::unordered_set<ULONG_PTR> m_dirtyNodeHandles;
  std::unordered_set<ULONG_PTR> m_dirtyMeshHandles;
  std::unordered_set<ULONG_PTR> m_dirtyMaterialHandles;
  std::unordered_set<ULONG_PTR> m_dirtyLightHandles;
  std::unordered_set<std::string> m_pendingPayloadRemovals;
  std::unordered_map<ULONG_PTR, NodeSnapshot> m_lastNodeState;
  MaterialStateMap m_lastMaterialState;
  std::unordered_map<ULONG_PTR, LightSnapshot> m_lastLightState;
  std::vector<std::string> m_lastSelectedObjectIds;
  CameraSnapshot m_lastCameraSnapshot;
  bool m_forceFullSnapshotOnConnect = false;
  std::string m_lastStartupSummary;
  uint64_t m_batchesSent = 0;
  uint64_t m_totalDeltasSent = 0;
  uint64_t m_lastBatchDeltaCount = 0;
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
  return _T("project-render LiveLink for 3ds Max 2024");
}

extern "C" __declspec(dllexport) int LibNumberClasses() { return 1; }

extern "C" __declspec(dllexport) ClassDesc *LibClassDesc(int index) {
  return index == 0 ? &g_classDesc : nullptr;
}

extern "C" __declspec(dllexport) ULONG LibVersion() { return VERSION_3DSMAX; }

extern "C" __declspec(dllexport) ULONG CanAutoDefer() { return 1; }
