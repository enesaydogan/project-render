#include "asset_loader.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <stdio.h>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef USE_SKETCHUP_SDK
// SketchUp C API headers
#include <SketchUpAPI/geometry/point3d.h>
#include <SketchUpAPI/geometry/transformation.h>
#include <SketchUpAPI/geometry/vector3d.h>
#include <SketchUpAPI/initialize.h>
#include <SketchUpAPI/model/component_definition.h>
#include <SketchUpAPI/model/component_instance.h>
#include <SketchUpAPI/model/drawing_element.h>
#include <SketchUpAPI/model/entities.h>
#include <SketchUpAPI/model/entity.h>
#include <SketchUpAPI/model/entity_list.h>
#include <SketchUpAPI/model/entity_list_iterator.h>
#include <SketchUpAPI/model/face.h>
#include <SketchUpAPI/model/group.h>
#include <SketchUpAPI/model/image_rep.h>
#include <SketchUpAPI/model/layer.h>
#include <SketchUpAPI/model/material.h>
#include <SketchUpAPI/model/mesh_helper.h>
#include <SketchUpAPI/model/model.h>
#include <SketchUpAPI/model/texture.h>
#include <SketchUpAPI/model/texture_writer.h>
#include <SketchUpAPI/model/uv_helper.h>
#include <SketchUpAPI/unicodestring.h>
#endif

using namespace std;

namespace Asset {

#ifdef USE_SKETCHUP_SDK

// Helper: convert SUStringRef -> std::string (UTF-8)
static std::string SUStringToStdString(SUStringRef suStr) {
  if (SUIsInvalid(suStr))
    return {};
  size_t len = 0;
  SUStringGetUTF8Length(suStr, &len);
  if (len == 0)
    return {};
  std::string out;
  out.resize(len + 1);
  size_t copied = 0;
  SUStringGetUTF8(suStr, (len + 1), &out[0], &copied);
  if (!out.empty() && out.back() == '\0')
    out.pop_back();
  return out;
}

static SUTransformation MakeIdentityTransform() {
  SUTransformation t = {};
  t.values[0] = t.values[5] = t.values[10] = t.values[15] = 1.0;
  return t;
}

// SketchUp uses inches and Z-up coordinates. Engine space is meters and Y-up.
static inline void ConvertSketchUpPointToEngine(const SUPoint3D &p,
                                                float out[3]) {
  constexpr double kInchToMeter = 0.0254;
  // Keep X, move Z->Y, and map Y->-Z to preserve handedness.
  out[0] = static_cast<float>(p.x * kInchToMeter);
  out[1] = static_cast<float>(p.z * kInchToMeter);
  out[2] = static_cast<float>(-p.y * kInchToMeter);
}

static inline void ConvertSketchUpVectorToEngine(const SUVector3D &v,
                                                 float out[3]) {
  out[0] = static_cast<float>(v.x);
  out[1] = static_cast<float>(v.z);
  out[2] = static_cast<float>(-v.y);
}

static inline void Normalize3(float v[3]) {
  const float lenSq = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
  if (lenSq <= 1e-20f) {
    v[0] = 0.0f;
    v[1] = 1.0f;
    v[2] = 0.0f;
    return;
  }
  const float invLen = 1.0f / std::sqrt(lenSq);
  v[0] *= invLen;
  v[1] *= invLen;
  v[2] *= invLen;
}

static inline float Clamp01(double value) {
  return static_cast<float>((std::max)(0.0, (std::min)(1.0, value)));
}

// Compute the 3x3 inverse-transpose of the upper-left 3x3 of an SUTransformation.
// This is the correct transform for surface normals under non-uniform scaling.
// SUTransformation.values is column-major.
static bool ComputeNormalMatrix(const SUTransformation &t, double out[9]) {
  const double a = t.values[0], d = t.values[1], g = t.values[2];
  const double b = t.values[4], e = t.values[5], h = t.values[6];
  const double c = t.values[8], f = t.values[9], i = t.values[10];
  const double c00 = e * i - f * h;
  const double c01 = -(d * i - f * g);
  const double c02 = d * h - e * g;
  const double c10 = -(b * i - c * h);
  const double c11 = a * i - c * g;
  const double c12 = -(a * h - b * g);
  const double c20 = b * f - c * e;
  const double c21 = -(a * f - c * d);
  const double c22 = a * e - b * d;
  const double det = a * c00 + b * c01 + c * c02;
  if (std::abs(det) < 1e-30)
    return false;
  const double invDet = 1.0 / det;
  out[0] = c00 * invDet; out[1] = c01 * invDet; out[2] = c02 * invDet;
  out[3] = c10 * invDet; out[4] = c11 * invDet; out[5] = c12 * invDet;
  out[6] = c20 * invDet; out[7] = c21 * invDet; out[8] = c22 * invDet;
  return true;
}

static inline void TransformNormal3x3(const double m[9], SUVector3D &v) {
  const double x = v.x, y = v.y, z = v.z;
  v.x = m[0] * x + m[1] * y + m[2] * z;
  v.y = m[3] * x + m[4] * y + m[5] * z;
  v.z = m[6] * x + m[7] * y + m[8] * z;
}

static inline bool NormalizeSUVector(SUVector3D &v) {
  const double lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
  if (lenSq <= 1e-30)
    return false;
  const double invLen = 1.0 / std::sqrt(lenSq);
  v.x *= invLen;
  v.y *= invLen;
  v.z *= invLen;
  return true;
}

static inline SUVector3D SubPoint(const SUPoint3D &a, const SUPoint3D &b) {
  SUVector3D r = {};
  r.x = a.x - b.x;
  r.y = a.y - b.y;
  r.z = a.z - b.z;
  return r;
}

static inline SUVector3D CrossVec(const SUVector3D &a, const SUVector3D &b) {
  SUVector3D r = {};
  r.x = a.y * b.z - a.z * b.y;
  r.y = a.z * b.x - a.x * b.z;
  r.z = a.x * b.y - a.y * b.x;
  return r;
}

static inline double DotVec(const SUVector3D &a, const SUVector3D &b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

static bool ComputeTriangleNormal(const SUPoint3D *verts, size_t vertCount,
                                  const std::vector<size_t> &indices,
                                  SUVector3D &outNormal) {
  if (indices.size() < 3 || vertCount == 0)
    return false;
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    const size_t i0 = indices[i + 0];
    const size_t i1 = indices[i + 1];
    const size_t i2 = indices[i + 2];
    if (i0 >= vertCount || i1 >= vertCount || i2 >= vertCount)
      continue;
    SUVector3D e1 = SubPoint(verts[i1], verts[i0]);
    SUVector3D e2 = SubPoint(verts[i2], verts[i0]);
    SUVector3D n = CrossVec(e1, e2);
    if (NormalizeSUVector(n)) {
      outNormal = n;
      return true;
    }
  }
  return false;
}

static bool ShouldUseBackSideMaterial(SUFaceRef face,
                                      const SUPoint3D *verts, size_t vertCount,
                                      const std::vector<size_t> &indices) {
  SUVector3D triNormal = {};
  if (!ComputeTriangleNormal(verts, vertCount, indices, triNormal))
    return false;

  SUVector3D faceNormal = {};
  if (SU_ERROR_NONE != SUFaceGetNormal(face, &faceNormal))
    return false;

  // If tessellated winding opposes face-front normal, this mesh represents the
  // back side and should use back-side material/UVs.
  return DotVec(triNormal, faceNormal) < 0.0;
}

// Returns true if the drawing element is visible (not individually hidden and
// its layer/tag is visible).  Defaults to visible when a check fails.
static bool IsDrawingElementVisible(SUDrawingElementRef elem) {
  if (SUIsInvalid(elem))
    return true;
  bool hidden = false;
  if (SU_ERROR_NONE == SUDrawingElementGetHidden(elem, &hidden) && hidden)
    return false;
  SULayerRef layer = SU_INVALID;
  if (SU_ERROR_NONE == SUDrawingElementGetLayer(elem, &layer) &&
      !SUIsInvalid(layer)) {
    bool visible = true;
    if (SU_ERROR_NONE == SULayerGetVisibility(layer, &visible) && !visible)
      return false;
  }
  return true;
}

static bool LoadTextureFromImageRep(SUImageRepRef image, Asset::Texture &outTex) {
  if (SUIsInvalid(image))
    return false;

  if (SU_ERROR_NONE != SUImageRepConvertTo32BitsPerPixel(image))
    return false;

  size_t width = 0, height = 0;
  if (SU_ERROR_NONE != SUImageRepGetPixelDimensions(image, &width, &height) ||
      width == 0 || height == 0) {
    return false;
  }

  size_t dataSize = 0;
  size_t bitsPerPixel = 0;
  if (SU_ERROR_NONE != SUImageRepGetDataSize(image, &dataSize, &bitsPerPixel) ||
      bitsPerPixel != 32 || dataSize < width * height * 4) {
    return false;
  }

  std::vector<uint8_t> rgba(dataSize);
  if (SU_ERROR_NONE !=
      SUImageRepGetData(image, dataSize, reinterpret_cast<SUByte *>(rgba.data()))) {
    return false;
  }

#ifdef _WIN32
  // SketchUp image reps expose 32-bit bitmap data as BGRA on Windows.
  // Convert to the RGBA ordering expected by the engine upload path.
  const size_t pixelCount = width * height;
  for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
    std::swap(rgba[pixel * 4 + 0], rgba[pixel * 4 + 2]);
  }
#endif

  Asset::Texture tex = Asset::LoadTextureFromMemory(
      rgba.data(), static_cast<int>(width), static_cast<int>(height),
      DXGI_FORMAT_R8G8B8A8_UNORM);
  if (!tex.resource)
    return false;

  outTex = std::move(tex);
  return true;
}

static bool LoadTextureFromWriterTextureId(SUTextureWriterRef writer,
                                           long textureId,
                                           Asset::Texture &outTex) {
  if (SUIsInvalid(writer) || textureId == 0)
    return false;

  SUImageRepRef image = SU_INVALID;
  if (SU_ERROR_NONE != SUImageRepCreate(&image))
    return false;

  const bool ok =
      (SU_ERROR_NONE == SUTextureWriterGetImageRep(writer, textureId, &image)) &&
      LoadTextureFromImageRep(image, outTex);
  SUImageRepRelease(&image);
  return ok;
}

static bool LoadTextureFromSketchUpTexture(SUTextureRef texture, bool colorized,
                                           Asset::Texture &outTex) {
  if (SUIsInvalid(texture))
    return false;

  SUImageRepRef image = SU_INVALID;
  if (SU_ERROR_NONE != SUImageRepCreate(&image))
    return false;

  SUResult result =
      colorized ? SUTextureGetColorizedImageRep(texture, &image)
                : SUTextureGetImageRep(texture, &image);
  if (result != SU_ERROR_NONE && colorized) {
    result = SUTextureGetImageRep(texture, &image);
  }

  const bool ok = (result == SU_ERROR_NONE) && LoadTextureFromImageRep(image, outTex);
  SUImageRepRelease(&image);
  return ok;
}

struct TextureAlphaInfo {
  bool hasNonOpaqueAlpha = false;
  bool hasSmoothAlpha = false;
};

static TextureAlphaInfo AnalyzeTextureAlpha(const Asset::Texture &texture) {
  TextureAlphaInfo info;
  if (texture.format != DXGI_FORMAT_R8G8B8A8_UNORM || texture.width == 0 ||
      texture.height == 0) {
    return info;
  }

  const size_t pixelCount =
      static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height);
  const size_t neededBytes = pixelCount * 4;
  if (texture.cpuData.size() < neededBytes) {
    return info;
  }

  for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
    const uint8_t alpha = texture.cpuData[pixel * 4 + 3];
    if (alpha < 250) {
      info.hasNonOpaqueAlpha = true;
      if (alpha > 4 && alpha < 250) {
        info.hasSmoothAlpha = true;
        break;
      }
    }
  }
  return info;
}

static void ComputeTangents(std::vector<Vertex> &vertices,
                            const std::vector<uint32_t> &indices) {
  if (vertices.empty() || indices.size() < 3 || (indices.size() % 3) != 0) {
    return;
  }

  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    const uint32_t i0 = indices[i + 0];
    const uint32_t i1 = indices[i + 1];
    const uint32_t i2 = indices[i + 2];
    if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
      continue;
    }

    Vertex &v0 = vertices[i0];
    Vertex &v1 = vertices[i1];
    Vertex &v2 = vertices[i2];

    const float dx1 = v1.pos[0] - v0.pos[0];
    const float dy1 = v1.pos[1] - v0.pos[1];
    const float dz1 = v1.pos[2] - v0.pos[2];
    const float dx2 = v2.pos[0] - v0.pos[0];
    const float dy2 = v2.pos[1] - v0.pos[1];
    const float dz2 = v2.pos[2] - v0.pos[2];

    const float du1 = v1.uv[0] - v0.uv[0];
    const float dv1 = v1.uv[1] - v0.uv[1];
    const float du2 = v2.uv[0] - v0.uv[0];
    const float dv2 = v2.uv[1] - v0.uv[1];
    const float det = du1 * dv2 - dv1 * du2;
    if (std::abs(det) <= 1e-8f) {
      continue;
    }

    const float invDet = 1.0f / det;
    const float tx = (dv2 * dx1 - dv1 * dx2) * invDet;
    const float ty = (dv2 * dy1 - dv1 * dy2) * invDet;
    const float tz = (dv2 * dz1 - dv1 * dz2) * invDet;

    auto applyTangent = [&](Vertex &vertex) {
      const float nx = vertex.normal[0];
      const float ny = vertex.normal[1];
      const float nz = vertex.normal[2];
      const float dot = nx * tx + ny * ty + nz * tz;
      float ox = tx - nx * dot;
      float oy = ty - ny * dot;
      float oz = tz - nz * dot;
      const float lenSq = ox * ox + oy * oy + oz * oz;
      if (lenSq <= 1e-12f) {
        return;
      }
      const float invLen = 1.0f / std::sqrt(lenSq);
      vertex.tangent[0] = ox * invLen;
      vertex.tangent[1] = oy * invLen;
      vertex.tangent[2] = oz * invLen;
    };

    applyTangent(v0);
    applyTangent(v1);
    applyTangent(v2);
  }
}

static SUMaterialRef ResolveFaceMaterial(SUFaceRef face, bool preferBackSide,
                                         SUMaterialRef inheritedMaterial) {
  SUMaterialRef front = SU_INVALID;
  SUMaterialRef back = SU_INVALID;
  const bool hasFront = (SU_ERROR_NONE == SUFaceGetFrontMaterial(face, &front) &&
                         !SUIsInvalid(front));
  const bool hasBack = (SU_ERROR_NONE == SUFaceGetBackMaterial(face, &back) &&
                        !SUIsInvalid(back));

  if (preferBackSide) {
    if (hasBack)
      return back;
    if (hasFront)
      return front;
  } else {
    if (hasFront)
      return front;
    if (hasBack)
      return back;
  }
  return inheritedMaterial;
}

static bool MaterialHasTextureSlot(SUMaterialRef material) {
  if (SUIsInvalid(material))
    return false;

  SUMaterialType materialType = SUMaterialType_Colored;
  if (SU_ERROR_NONE == SUMaterialGetType(material, &materialType) &&
      (materialType == SUMaterialType_Textured ||
       materialType == SUMaterialType_ColorizedTexture)) {
    return true;
  }

  SUMaterialWorkflow workflow = SUMaterialWorkflow_Classic;
  if (SU_ERROR_NONE != SUMaterialGetWorkflow(material, &workflow) ||
      workflow != SUMaterialWorkflow_PBRMetallicRoughness) {
    return false;
  }

  SUTextureRef texture = SU_INVALID;
  return (SU_ERROR_NONE == SUMaterialGetMetallicTexture(material, &texture) &&
          !SUIsInvalid(texture)) ||
         (SU_ERROR_NONE == SUMaterialGetRoughnessTexture(material, &texture) &&
          !SUIsInvalid(texture)) ||
         (SU_ERROR_NONE == SUMaterialGetNormalTexture(material, &texture) &&
          !SUIsInvalid(texture)) ||
         (SU_ERROR_NONE == SUMaterialGetAOTexture(material, &texture) &&
          !SUIsInvalid(texture));
}

static bool FaceMayNeedTextureWriter(SUFaceRef face,
                                     SUMaterialRef inheritedMaterial) {
  SUMaterialRef front = SU_INVALID;
  SUMaterialRef back = SU_INVALID;
  if (SU_ERROR_NONE == SUFaceGetFrontMaterial(face, &front) &&
      MaterialHasTextureSlot(front)) {
    return true;
  }
  if (SU_ERROR_NONE == SUFaceGetBackMaterial(face, &back) &&
      MaterialHasTextureSlot(back)) {
    return true;
  }
  return MaterialHasTextureSlot(inheritedMaterial);
}

bool LoadSkp(const std::string &path, std::vector<GpuMesh> &outMeshes,
             std::vector<Material> *outMaterials,
             std::vector<Texture> *outTextures, const float *rootTranslation,
             std::vector<ImportedSceneNode> *outSceneNodes) {
  (void)outSceneNodes;
  if (s_progressCb)
    s_progressCb(0.0f, std::string("Importing SKP: ") + path);

  SUModelRef model = SU_INVALID;
  SUModelLoadStatus status = SUModelLoadStatus_Success;
  SUResult r = SUModelCreateFromFileWithStatus(&model, path.c_str(), &status);
  if (r != SU_ERROR_NONE) {
    fprintf(stderr, "LoadSkp: SUModelCreateFromFileWithStatus failed (%d)\n",
            static_cast<int>(r));
    return false;
  }
  if (status == SUModelLoadStatus_Success_MoreRecent) {
    fprintf(stderr,
            "LoadSkp: file was saved by a newer SketchUp version; some geometry may be missing.\n");
  }
  fprintf(stderr,
          "LoadSkp: applying unit/axis conversion (inches->meters, Z-up->Y-up)\n");

  SUModelStatistics modelStats = {};
  if (SU_ERROR_NONE == SUModelGetStatistics(model, &modelStats)) {
    fprintf(stderr,
            "LoadSkp: stats edges=%d faces=%d instances=%d groups=%d images=%d definitions=%d\n",
            modelStats.entity_counts[SUModelStatistics::SUEntityType_Edge],
            modelStats.entity_counts[SUModelStatistics::SUEntityType_Face],
            modelStats.entity_counts[SUModelStatistics::SUEntityType_ComponentInstance],
            modelStats.entity_counts[SUModelStatistics::SUEntityType_Group],
            modelStats.entity_counts[SUModelStatistics::SUEntityType_Image],
            modelStats.entity_counts[SUModelStatistics::SUEntityType_ComponentDefinition]);
  }

  SUTextureWriterRef texWriter = SU_INVALID;
  const bool textureWriterValid =
      (SU_ERROR_NONE == SUTextureWriterCreate(&texWriter) && !SUIsInvalid(texWriter));
  if (!textureWriterValid) {
    fprintf(stderr,
            "LoadSkp: texture writer unavailable; continuing without texture extraction.\n");
  }

  std::unordered_map<int64_t, int> materialMap;
  std::unordered_map<int64_t, int> directTextureMap;
  std::unordered_map<long, int> writerTextureMap;

  SUEntitiesRef entities = SU_INVALID;
  if (SU_ERROR_NONE != SUModelGetEntities(model, &entities) || SUIsInvalid(entities)) {
    fprintf(stderr, "LoadSkp: failed to get model entities.\n");
    if (textureWriterValid)
      SUTextureWriterRelease(&texWriter);
    SUModelRelease(&model);
    return false;
  }

  struct FaceJob {
    SUFaceRef face = SU_INVALID;
    SUTransformation transform = MakeIdentityTransform();
    SUMaterialRef inheritedMaterial = SU_INVALID;
  };

  auto isImageDefinition = [](SUComponentDefinitionRef definition) -> bool {
    if (SUIsInvalid(definition)) {
      return false;
    }
    SUComponentType type = SUComponentType_Normal;
    return SU_ERROR_NONE == SUComponentDefinitionGetType(definition, &type) &&
           type == SUComponentType_Image;
  };

  std::vector<FaceJob> faceJobs;
  std::unordered_map<int64_t, bool> seenFaceIds;
  size_t skippedImageDefinitionInstances = 0;
  size_t skippedHiddenFaces = 0;
  size_t skippedHiddenGroups = 0;
  size_t skippedHiddenInstances = 0;
  const int statFaceCount =
      modelStats.entity_counts[SUModelStatistics::SUEntityType_Face];
  if (statFaceCount > 0) {
    faceJobs.reserve(static_cast<size_t>(statFaceCount));
    seenFaceIds.reserve(static_cast<size_t>(statFaceCount));
  }

  auto addFaceJob = [&](SUFaceRef face, const SUTransformation *parentTrans,
                        SUMaterialRef inheritedMaterial,
                        bool dedupeExistingFace) {
    if (SUIsInvalid(face))
      return;

    // Skip hidden faces and faces on invisible layers/tags.
    if (!IsDrawingElementVisible(SUFaceToDrawingElement(face))) {
      ++skippedHiddenFaces;
      return;
    }

    int64_t facePid = 0;
    SUEntityGetPersistentID(SUFaceToEntity(face), &facePid);
    if (facePid != 0) {
      const bool alreadySeen = seenFaceIds.find(facePid) != seenFaceIds.end();
      if (dedupeExistingFace && alreadySeen) {
        return;
      }
      seenFaceIds[facePid] = true;
    }

    FaceJob job;
    job.face = face;
    job.transform = parentTrans ? *parentTrans : MakeIdentityTransform();
    job.inheritedMaterial = inheritedMaterial;
    faceJobs.emplace_back(job);
  };

  std::function<void(SUEntitiesRef, const SUTransformation *, SUMaterialRef, bool)>
      collectRec;
  collectRec = [&](SUEntitiesRef ents, const SUTransformation *parentTrans,
                   SUMaterialRef inheritedMaterial, bool dedupeExistingFaces) {
    if (SUIsInvalid(ents))
      return;

    size_t localFaceCount = 0;
    if (SU_ERROR_NONE != SUEntitiesGetNumFaces(ents, &localFaceCount))
      localFaceCount = 0;
    if (localFaceCount > 0) {
      std::vector<SUFaceRef> localFaces(localFaceCount);
      if (SU_ERROR_NONE !=
          SUEntitiesGetFaces(ents, localFaceCount, localFaces.data(), &localFaceCount)) {
        localFaceCount = 0;
      }
      for (size_t i = 0; i < localFaceCount; ++i) {
        addFaceJob(localFaces[i], parentTrans, inheritedMaterial,
                   dedupeExistingFaces);
      }
    } else {
      SUEntityListRef faceList = SU_INVALID;
      if (SU_ERROR_NONE == SUEntityListCreate(&faceList)) {
        if (SU_ERROR_NONE == SUEntitiesEntityListFill(ents, SURefType_Face, faceList)) {
          size_t faceListCount = 0;
          if (SU_ERROR_NONE == SUEntityListSize(faceList, &faceListCount) &&
              faceListCount > 0) {
            SUEntityListIteratorRef it = SU_INVALID;
            if (SU_ERROR_NONE == SUEntityListIteratorCreate(&it)) {
              if (SU_ERROR_NONE == SUEntityListBegin(faceList, &it)) {
                bool inRange = false;
                while (SU_ERROR_NONE == SUEntityListIteratorIsInRange(it, &inRange) &&
                       inRange) {
                  SUEntityRef ent = SU_INVALID;
                  if (SU_ERROR_NONE == SUEntityListIteratorGetEntity(it, &ent)) {
                    SUFaceRef faceRef = SUFaceFromEntity(ent);
                    if (!SUIsInvalid(faceRef)) {
                      addFaceJob(faceRef, parentTrans, inheritedMaterial,
                                 dedupeExistingFaces);
                    }
                  }
                  if (SU_ERROR_NONE != SUEntityListIteratorNext(it))
                    break;
                }
              }
              SUEntityListIteratorRelease(&it);
            }
          }
        }
        SUEntityListRelease(&faceList);
      }
    }

    size_t groupCount = 0;
    if (SU_ERROR_NONE != SUEntitiesGetNumGroups(ents, &groupCount))
      groupCount = 0;
    if (groupCount > 0) {
      std::vector<SUGroupRef> groups(groupCount);
      if (SU_ERROR_NONE !=
          SUEntitiesGetGroups(ents, groupCount, groups.data(), &groupCount)) {
        groupCount = 0;
      }
      for (size_t gi = 0; gi < groupCount; ++gi) {
        // Skip hidden groups and groups on invisible layers.
        if (!IsDrawingElementVisible(SUGroupToDrawingElement(groups[gi]))) {
          ++skippedHiddenGroups;
          continue;
        }
        SUEntitiesRef groupEnts = SU_INVALID;
        if (SU_ERROR_NONE != SUGroupGetEntities(groups[gi], &groupEnts))
          continue;

        SUTransformation gtrans;
        if (SU_ERROR_NONE != SUGroupGetTransform(groups[gi], &gtrans))
          gtrans = MakeIdentityTransform();

        SUTransformation combined;
        if (parentTrans) {
          SUTransformationMultiply(parentTrans, &gtrans, &combined);
        } else {
          combined = gtrans;
        }

        SUMaterialRef childInherited = inheritedMaterial;
        SUMaterialRef groupMaterial = SU_INVALID;
        SUDrawingElementRef groupElem = SUGroupToDrawingElement(groups[gi]);
        if (SU_ERROR_NONE ==
                SUDrawingElementGetMaterial(groupElem, &groupMaterial) &&
            !SUIsInvalid(groupMaterial)) {
          childInherited = groupMaterial;
        }
        collectRec(groupEnts, &combined, childInherited, dedupeExistingFaces);
      }
    }

    size_t instCount = 0;
    if (SU_ERROR_NONE != SUEntitiesGetNumInstances(ents, &instCount))
      instCount = 0;
    if (instCount > 0) {
      std::vector<SUComponentInstanceRef> instances(instCount);
      if (SU_ERROR_NONE !=
          SUEntitiesGetInstances(ents, instCount, instances.data(), &instCount)) {
        instCount = 0;
      }
      for (size_t ii = 0; ii < instCount; ++ii) {
        // Skip hidden component instances and instances on invisible layers.
        if (!IsDrawingElementVisible(
                SUComponentInstanceToDrawingElement(instances[ii]))) {
          ++skippedHiddenInstances;
          continue;
        }
        SUComponentDefinitionRef def = SU_INVALID;
        if (SU_ERROR_NONE != SUComponentInstanceGetDefinition(instances[ii], &def))
          continue;
        if (SUIsInvalid(def))
          continue;
        if (isImageDefinition(def)) {
          ++skippedImageDefinitionInstances;
          continue;
        }

        SUEntitiesRef defEnts = SU_INVALID;
        if (SU_ERROR_NONE != SUComponentDefinitionGetEntities(def, &defEnts))
          continue;

        SUTransformation itrans;
        if (SU_ERROR_NONE != SUComponentInstanceGetTransform(instances[ii], &itrans))
          itrans = MakeIdentityTransform();

        SUTransformation combined;
        if (parentTrans) {
          SUTransformationMultiply(parentTrans, &itrans, &combined);
        } else {
          combined = itrans;
        }

        SUMaterialRef childInherited = inheritedMaterial;
        SUMaterialRef instanceMaterial = SU_INVALID;
        SUDrawingElementRef instanceElem =
            SUComponentInstanceToDrawingElement(instances[ii]);
        if (SU_ERROR_NONE ==
                SUDrawingElementGetMaterial(instanceElem, &instanceMaterial) &&
            !SUIsInvalid(instanceMaterial)) {
          childInherited = instanceMaterial;
        }
        collectRec(defEnts, &combined, childInherited, dedupeExistingFaces);
      }
    }
  };

  collectRec(entities, nullptr, SU_INVALID, false);
  auto collectAllDefinitions = [&]() {
    auto collectDefinitions = [&](auto getNumFn, auto getFn) {
      size_t defCount = 0;
      if (SU_ERROR_NONE != getNumFn(model, &defCount) || defCount == 0)
        return;
      std::vector<SUComponentDefinitionRef> defs(defCount);
      if (SU_ERROR_NONE != getFn(model, defCount, defs.data(), &defCount))
        return;
      for (size_t i = 0; i < defCount; ++i) {
        if (SUIsInvalid(defs[i]))
          continue;
        if (isImageDefinition(defs[i])) {
          ++skippedImageDefinitionInstances;
          continue;
        }
        SUEntitiesRef defEnts = SU_INVALID;
        if (SU_ERROR_NONE != SUComponentDefinitionGetEntities(defs[i], &defEnts))
          continue;
        collectRec(defEnts, nullptr, SU_INVALID, true);
      }
    };
    collectDefinitions(SUModelGetNumComponentDefinitions,
                       SUModelGetComponentDefinitions);
    collectDefinitions(SUModelGetNumGroupDefinitions, SUModelGetGroupDefinitions);
  };
  if (faceJobs.empty()) {
    const size_t beforeDefinitionSweep = faceJobs.size();
    collectAllDefinitions();
    if (faceJobs.size() > beforeDefinitionSweep) {
      fprintf(stderr,
              "LoadSkp: fallback definition sweep recovered %zu faces (traversal=%zu stats=%d)\n",
              faceJobs.size() - beforeDefinitionSweep, beforeDefinitionSweep,
              statFaceCount);
    }
  } else if (statFaceCount > 0 &&
             faceJobs.size() < static_cast<size_t>(statFaceCount)) {
    fprintf(stderr,
            "LoadSkp: traversal found %zu visible/instanced faces; model stats report %d total definition faces.\n",
            faceJobs.size(), statFaceCount);
  }
  if (skippedImageDefinitionInstances > 0) {
    fprintf(stderr, "LoadSkp: skipped %zu SketchUp image definition instances\n",
            skippedImageDefinitionInstances);
  }
  if (skippedHiddenFaces > 0 || skippedHiddenGroups > 0 ||
      skippedHiddenInstances > 0) {
    fprintf(stderr,
            "LoadSkp: skipped hidden entities: faces=%zu groups=%zu instances=%zu\n",
            skippedHiddenFaces, skippedHiddenGroups, skippedHiddenInstances);
  }

  const size_t faceCount = faceJobs.size();
  size_t processed = 0;
  int defaultMaterialIndex = -1;
  struct MeshAccumulator {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
  };
  struct CachedFaceMesh {
    std::vector<SUPoint3D> vertices;
    std::vector<SUVector3D> normals;
    std::vector<SUPoint3D> stqFront;
    std::vector<SUPoint3D> stqBack;
    std::vector<size_t> indices;
    bool haveFrontSTQ = false;
    bool haveBackSTQ = false;
    bool builtWithTextureWriter = false;
    bool valid = false;
  };
  struct FaceMeshLookup {
    const CachedFaceMesh *mesh = nullptr;
    bool usedTextureWriter = false;
  };
  std::unordered_map<int, MeshAccumulator> meshBatches;
  std::unordered_map<int64_t, CachedFaceMesh> basicFaceMeshCache;
  std::unordered_map<int64_t, CachedFaceMesh> texturedFaceMeshCache;
  if (statFaceCount > 0) {
    basicFaceMeshCache.reserve(static_cast<size_t>(statFaceCount));
    texturedFaceMeshCache.reserve(static_cast<size_t>(statFaceCount / 4 + 1));
  }
  CachedFaceMesh uncachedFaceMeshScratch;
  size_t meshHelperBuilds = 0;
  size_t meshHelperCacheHits = 0;

  auto getFacePersistentId = [](SUFaceRef face) -> int64_t {
    int64_t pid = 0;
    if (!SUIsInvalid(face)) {
      SUEntityGetPersistentID(SUFaceToEntity(face), &pid);
    }
    return pid;
  };

  auto populateFaceMesh = [&](SUFaceRef face, bool withTextureWriter,
                              CachedFaceMesh &outMesh) -> bool {
    outMesh = CachedFaceMesh{};

    SUMeshHelperRef meshRef = SU_INVALID;
    SUResult meshCreate = SU_ERROR_INVALID_INPUT;
    bool usedTextureWriter = withTextureWriter;
    if (withTextureWriter) {
      meshCreate = SUMeshHelperCreateWithTextureWriter(&meshRef, face, texWriter);
    }
    if (meshCreate != SU_ERROR_NONE) {
      meshCreate = SUMeshHelperCreate(&meshRef, face);
      usedTextureWriter = false;
    }
    if (meshCreate != SU_ERROR_NONE) {
      return false;
    }

    ++meshHelperBuilds;

    size_t numVerts = 0;
    if (SU_ERROR_NONE != SUMeshHelperGetNumVertices(meshRef, &numVerts) ||
        numVerts == 0) {
      SUMeshHelperRelease(&meshRef);
      return false;
    }

    outMesh.vertices.resize(numVerts);
    size_t vertsRetrieved = numVerts;
    if (SU_ERROR_NONE !=
            SUMeshHelperGetVertices(meshRef, vertsRetrieved, outMesh.vertices.data(),
                                    &vertsRetrieved) ||
        vertsRetrieved == 0) {
      SUMeshHelperRelease(&meshRef);
      return false;
    }
    if (vertsRetrieved != numVerts) {
      numVerts = vertsRetrieved;
      outMesh.vertices.resize(numVerts);
    }

    outMesh.stqFront.assign(numVerts, SUPoint3D{0.0, 0.0, 1.0});
    outMesh.stqBack.assign(numVerts, SUPoint3D{0.0, 0.0, 1.0});
    size_t stqFrontCount = numVerts;
    size_t stqBackCount = numVerts;
    outMesh.haveFrontSTQ =
        (SU_ERROR_NONE ==
             SUMeshHelperGetFrontSTQCoords(meshRef, stqFrontCount,
                                           outMesh.stqFront.data(),
                                           &stqFrontCount) &&
         stqFrontCount == numVerts);
    outMesh.haveBackSTQ =
        (SU_ERROR_NONE ==
             SUMeshHelperGetBackSTQCoords(meshRef, stqBackCount,
                                          outMesh.stqBack.data(), &stqBackCount) &&
         stqBackCount == numVerts);

    outMesh.normals.assign(numVerts, SUVector3D{0.0, 1.0, 0.0});
    size_t normalCount = numVerts;
    if (SU_ERROR_NONE !=
            SUMeshHelperGetNormals(meshRef, normalCount, outMesh.normals.data(),
                                   &normalCount) ||
        normalCount != numVerts) {
      std::fill(outMesh.normals.begin(), outMesh.normals.end(),
                SUVector3D{0.0, 1.0, 0.0});
    }

    size_t numTris = 0;
    if (SU_ERROR_NONE != SUMeshHelperGetNumTriangles(meshRef, &numTris) ||
        numTris == 0) {
      SUMeshHelperRelease(&meshRef);
      return false;
    }

    outMesh.indices.resize(numTris * 3);
    size_t numIndices = outMesh.indices.size();
    if (SU_ERROR_NONE !=
            SUMeshHelperGetVertexIndices(meshRef, numIndices, outMesh.indices.data(),
                                         &numIndices) ||
        numIndices < 3) {
      SUMeshHelperRelease(&meshRef);
      return false;
    }
    outMesh.indices.resize(numIndices);

    SUMeshHelperRelease(&meshRef);
    outMesh.builtWithTextureWriter = usedTextureWriter;
    outMesh.valid = true;
    return true;
  };

  auto getFaceMesh = [&](SUFaceRef face, bool wantTextureWriter) -> FaceMeshLookup {
    const int64_t facePid = getFacePersistentId(face);
    if (facePid == 0) {
      if (!populateFaceMesh(face, wantTextureWriter, uncachedFaceMeshScratch)) {
        return {};
      }
      return {&uncachedFaceMeshScratch,
              uncachedFaceMeshScratch.builtWithTextureWriter};
    }

    if (wantTextureWriter) {
      auto texturedIt = texturedFaceMeshCache.find(facePid);
      if (texturedIt != texturedFaceMeshCache.end()) {
        ++meshHelperCacheHits;
        return {&texturedIt->second, true};
      }

      CachedFaceMesh texturedMesh;
      if (populateFaceMesh(face, true, texturedMesh)) {
        if (texturedMesh.builtWithTextureWriter) {
          auto inserted =
              texturedFaceMeshCache.emplace(facePid, std::move(texturedMesh));
          return {&inserted.first->second, true};
        }
        auto inserted = basicFaceMeshCache.emplace(facePid, std::move(texturedMesh));
        return {&inserted.first->second, false};
      }
      wantTextureWriter = false;
    }

    auto texturedIt = texturedFaceMeshCache.find(facePid);
    if (texturedIt != texturedFaceMeshCache.end()) {
      ++meshHelperCacheHits;
      return {&texturedIt->second, false};
    }

    auto basicIt = basicFaceMeshCache.find(facePid);
    if (basicIt != basicFaceMeshCache.end()) {
      ++meshHelperCacheHits;
      return {&basicIt->second, false};
    }

    CachedFaceMesh basicMesh;
    if (!populateFaceMesh(face, false, basicMesh)) {
      return {};
    }
    auto inserted = basicFaceMeshCache.emplace(facePid, std::move(basicMesh));
    return {&inserted.first->second, false};
  };

  const size_t progressInterval =
      (std::max)(static_cast<size_t>(1), faceCount / 100);
  const std::string progressMsg = std::string("Processing SKP faces: ") + path;

  for (size_t fi = 0; fi < faceCount; ++fi) {
    SUFaceRef face = faceJobs[fi].face;
    const SUTransformation &faceTransform = faceJobs[fi].transform;

    const bool useTextureWriterForFace =
        textureWriterValid && outTextures &&
        FaceMayNeedTextureWriter(face, faceJobs[fi].inheritedMaterial);
    const FaceMeshLookup faceMeshLookup =
        getFaceMesh(face, useTextureWriterForFace);
    if (!faceMeshLookup.mesh || !faceMeshLookup.mesh->valid) {
      continue;
    }
    const CachedFaceMesh &faceMesh = *faceMeshLookup.mesh;
    const size_t numVerts = faceMesh.vertices.size();
    if (numVerts == 0 || faceMesh.indices.empty()) {
      continue;
    }

    // Compute normal matrix once per face (used for both vertex normals and
    // the back-side detection heuristic).
    bool isId = true;
    SUTransformationIsIdentity(&faceTransform, &isId);
    double normalMat[9] = {};
    const bool haveNormalMat = !isId && ComputeNormalMatrix(faceTransform, normalMat);
    bool isMirrored = false;
    if (!isId) {
      const double a = faceTransform.values[0], d = faceTransform.values[1], g = faceTransform.values[2];
      const double b = faceTransform.values[4], e = faceTransform.values[5], h = faceTransform.values[6];
      const double c = faceTransform.values[8], f = faceTransform.values[9], i = faceTransform.values[10];
      const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
      isMirrored = (det < 0.0);
    }

    const SUPoint3D *srcVerts = faceMesh.vertices.data();
    const SUVector3D *srcNormals = faceMesh.normals.data();

    const std::vector<size_t> &suIndices = faceMesh.indices;

    const bool useBackSide = ShouldUseBackSideMaterial(
        face, srcVerts, numVerts, suIndices);


    int materialIndex = -1;
    SUMaterialRef skMat =
        ResolveFaceMaterial(face, useBackSide, faceJobs[fi].inheritedMaterial);
    const bool hasMaterial = !SUIsInvalid(skMat);
    if (hasMaterial) {
      SUEntityRef matEntity = SUMaterialToEntity(skMat);
      int64_t pid = 0;
      SUEntityGetPersistentID(matEntity, &pid);
      const auto existingMaterial = materialMap.find(pid);
      if (existingMaterial != materialMap.end()) {
        materialIndex = existingMaterial->second;
      } else {
        Material mtl;

        SUStringRef name = SU_INVALID;
        SUStringCreate(&name);
        SUMaterialGetName(skMat, &name);
        const std::string nm = SUStringToStdString(name);
        SUStringRelease(&name);
        if (!nm.empty()) {
          strncpy_s(mtl.name, nm.c_str(), _TRUNCATE);
        }

        SUColor col = {255, 255, 255, 255};
        SUMaterialGetColor(skMat, &col);
        mtl.diffuseColor[0] = col.red / 255.0f;
        mtl.diffuseColor[1] = col.green / 255.0f;
        mtl.diffuseColor[2] = col.blue / 255.0f;
        mtl.diffuseColor[3] = 1.0f;

        bool useOpacity = false;
        if (SU_ERROR_NONE != SUMaterialGetUseOpacity(skMat, &useOpacity)) {
          useOpacity = false;
        }
        double opacity = 1.0;
        if (useOpacity && SU_ERROR_NONE == SUMaterialGetOpacity(skMat, &opacity)) {
          mtl.diffuseColor[3] = Clamp01(opacity);
        }

        mtl.metalness = 0.0f;
        mtl.roughness = 0.92f;
        mtl.ior = 1.5f;
        mtl.reflectionIor = mtl.ior;
        mtl.specularWeight = 1.0f;
        mtl.doubleSided = true;
        mtl.workflow = Material::kWorkflowMetalRoughness;
        mtl.schemaVersion = Material::kSchemaVersionCoronaArchviz;
        mtl.materialClass = Material::kMaterialClassGeneric;

        SUMaterialType materialType = SUMaterialType_Colored;
        if (SU_ERROR_NONE != SUMaterialGetType(skMat, &materialType)) {
          materialType = SUMaterialType_Colored;
        }

        auto loadDirectTexture = [&](SUTextureRef textureRef, bool colorized,
                                     int &dstSlot, bool *outAlphaTexture,
                                     float *outAmount = nullptr) {
          if (outAlphaTexture) {
            *outAlphaTexture = false;
          }
          if (SUIsInvalid(textureRef) || !outTextures) {
            return;
          }

          int64_t texturePid = 0;
          SUEntityRef textureEntity = SUTextureToEntity(textureRef);
          if (SU_ERROR_NONE != SUEntityGetPersistentID(textureEntity, &texturePid)) {
            texturePid = 0;
          }

          if (texturePid != 0) {
            const auto cached = directTextureMap.find(texturePid);
            if (cached != directTextureMap.end()) {
              dstSlot = cached->second;
              if (outAmount) {
                *outAmount = 1.0f;
              }
              if (outAlphaTexture) {
                const TextureAlphaInfo alphaInfo = AnalyzeTextureAlpha(
                    (*outTextures)[static_cast<size_t>(dstSlot)]);
                *outAlphaTexture = alphaInfo.hasNonOpaqueAlpha;
              }
              return;
            }
          }

          Texture importedTexture;
          if (!LoadTextureFromSketchUpTexture(textureRef, colorized, importedTexture)) {
            return;
          }

          dstSlot = static_cast<int>(outTextures->size());
          const TextureAlphaInfo alphaInfo = AnalyzeTextureAlpha(importedTexture);
          outTextures->push_back(std::move(importedTexture));
          if (texturePid != 0) {
            directTextureMap[texturePid] = dstSlot;
          }
          if (outAmount) {
            *outAmount = 1.0f;
          }
          if (outAlphaTexture) {
            *outAlphaTexture = alphaInfo.hasNonOpaqueAlpha;
          }
        };

        bool diffuseHasAlphaTexture = false;
        if (materialType == SUMaterialType_Textured ||
            materialType == SUMaterialType_ColorizedTexture) {
          long texId = 0;
          if (faceMeshLookup.usedTextureWriter) {
            SUResult texResult = SUTextureWriterGetTextureIdForFace(
                texWriter, face, !useBackSide, &texId);
            if (texResult != SU_ERROR_NONE || texId == 0) {
              texResult = SUTextureWriterGetTextureIdForFace(
                  texWriter, face, useBackSide, &texId);
            }
            (void)texResult;
          }

          if (outTextures && texId != 0) {
            const auto cachedTex = writerTextureMap.find(texId);
            if (cachedTex != writerTextureMap.end()) {
              mtl.diffuseTexture = cachedTex->second;
              const TextureAlphaInfo alphaInfo = AnalyzeTextureAlpha(
                  (*outTextures)[static_cast<size_t>(mtl.diffuseTexture)]);
              diffuseHasAlphaTexture = alphaInfo.hasNonOpaqueAlpha;
            } else {
              Texture tex;
              if (LoadTextureFromWriterTextureId(texWriter, texId, tex)) {
                mtl.diffuseTexture = static_cast<int>(outTextures->size());
                const TextureAlphaInfo alphaInfo = AnalyzeTextureAlpha(tex);
                diffuseHasAlphaTexture = alphaInfo.hasNonOpaqueAlpha;
                outTextures->push_back(std::move(tex));
                writerTextureMap[texId] = mtl.diffuseTexture;
              }
            }
          }

          if (mtl.diffuseTexture < 0) {
            SUTextureRef matTexture = SU_INVALID;
            if (SU_ERROR_NONE == SUMaterialGetTexture(skMat, &matTexture) &&
                !SUIsInvalid(matTexture)) {
              loadDirectTexture(matTexture, true, mtl.diffuseTexture,
                                &diffuseHasAlphaTexture);
            }
          }
        }

        SUMaterialWorkflow workflow = SUMaterialWorkflow_Classic;
        const bool pbrMetallicRoughness =
            SU_ERROR_NONE == SUMaterialGetWorkflow(skMat, &workflow) &&
            workflow == SUMaterialWorkflow_PBRMetallicRoughness;
        if (pbrMetallicRoughness) {
          double metallicFactor = 0.0;
          if (SU_ERROR_NONE == SUMaterialGetMetallicFactor(skMat, &metallicFactor)) {
            mtl.metalness = Clamp01(metallicFactor);
            if (mtl.metalness > 0.5f) {
              mtl.materialClass = Material::kMaterialClassMetal;
            }
          }

          double roughnessFactor = 1.0;
          if (SU_ERROR_NONE == SUMaterialGetRoughnessFactor(skMat, &roughnessFactor)) {
            mtl.roughness = Clamp01(roughnessFactor);
          }

          SUTextureRef metallicTexture = SU_INVALID;
          if (SU_ERROR_NONE == SUMaterialGetMetallicTexture(skMat, &metallicTexture) &&
              !SUIsInvalid(metallicTexture)) {
            loadDirectTexture(metallicTexture, false, mtl.metalnessTexture, nullptr);
          }

          SUTextureRef roughnessTexture = SU_INVALID;
          if (SU_ERROR_NONE ==
                  SUMaterialGetRoughnessTexture(skMat, &roughnessTexture) &&
              !SUIsInvalid(roughnessTexture)) {
            loadDirectTexture(roughnessTexture, false, mtl.roughnessGlossTexture,
                              nullptr);
            mtl.invertRoughnessTexture = false;
          }

          SUTextureRef normalTexture = SU_INVALID;
          if (SU_ERROR_NONE == SUMaterialGetNormalTexture(skMat, &normalTexture) &&
              !SUIsInvalid(normalTexture)) {
            loadDirectTexture(normalTexture, false, mtl.normalTexture, nullptr);
            double normalScale = 1.0;
            if (SU_ERROR_NONE == SUMaterialGetNormalScale(skMat, &normalScale)) {
              mtl.normalTextureAmount =
                  (std::max)(0.0f, static_cast<float>(normalScale));
            }
          }

          SUTextureRef aoTexture = SU_INVALID;
          if (SU_ERROR_NONE == SUMaterialGetAOTexture(skMat, &aoTexture) &&
              !SUIsInvalid(aoTexture)) {
            loadDirectTexture(aoTexture, false, mtl.occlusionTexture, nullptr);
            double aoStrength = 1.0;
            if (SU_ERROR_NONE == SUMaterialGetAOStrength(skMat, &aoStrength)) {
              mtl.occlusionTextureAmount = Clamp01(aoStrength);
            }
          }
        }

        if (mtl.diffuseColor[3] < 0.999f) {
          mtl.alphaMode = "BLEND";
          mtl.thinWalled = 1.0f;
          // SketchUp scalar opacity is an authoring/display control, not a
          // physical absorption coefficient.  In DXR, imported transparent
          // panes should behave like energy-conserving thin architectural
          // glass instead of dropping the non-opaque portion into black.
          const float transmission = 1.0f;
          mtl.transmissionWeight =
              (std::max)(mtl.transmissionWeight, transmission);
          mtl.transmissionColor[0] = mtl.diffuseColor[0];
          mtl.transmissionColor[1] = mtl.diffuseColor[1];
          mtl.transmissionColor[2] = mtl.diffuseColor[2];
          mtl.materialClass = Material::kMaterialClassGlass;
          if (!pbrMetallicRoughness) {
            mtl.roughness = (std::min)(mtl.roughness, 0.03f);
          }
        } else if (diffuseHasAlphaTexture && outTextures &&
                   mtl.diffuseTexture >= 0 &&
                   static_cast<size_t>(mtl.diffuseTexture) < outTextures->size()) {
          const TextureAlphaInfo alphaInfo =
              AnalyzeTextureAlpha((*outTextures)[static_cast<size_t>(mtl.diffuseTexture)]);
          mtl.alphaMode = alphaInfo.hasSmoothAlpha ? "BLEND" : "MASK";
        }

        if (outMaterials) {
          materialIndex = static_cast<int>(outMaterials->size());
          outMaterials->push_back(mtl);
        } else {
          materialIndex = static_cast<int>(materialMap.size());
        }
        materialMap[pid] = materialIndex;
      }
    } else if (outMaterials) {
      if (defaultMaterialIndex < 0) {
        Material mtl;
        defaultMaterialIndex = static_cast<int>(outMaterials->size());
        outMaterials->push_back(mtl);
      }
      materialIndex = defaultMaterialIndex;
    }

    auto &batch = meshBatches[materialIndex];
    const uint32_t baseVertex = static_cast<uint32_t>(batch.vertices.size());

    for (size_t vi = 0; vi < numVerts; ++vi) {
      Vertex v{};
      SUPoint3D p = srcVerts[vi];
      SUVector3D n = srcNormals[vi];

      if (!isId) {
        SUPoint3DTransform(&faceTransform, &p);
        if (haveNormalMat) {
          TransformNormal3x3(normalMat, n);
        } else {
          SUVector3DTransform(&faceTransform, &n);
        }
      }

      ConvertSketchUpPointToEngine(p, v.pos);
      if (rootTranslation) {
        v.pos[0] += rootTranslation[0];
        v.pos[1] += rootTranslation[1];
        v.pos[2] += rootTranslation[2];
      }
      ConvertSketchUpVectorToEngine(n, v.normal);
      if (useBackSide) {
        v.normal[0] = -v.normal[0];
        v.normal[1] = -v.normal[1];
        v.normal[2] = -v.normal[2];
      }
      Normalize3(v.normal);
      v.tangent[0] = 1.0f;
      v.tangent[1] = 0.0f;
      v.tangent[2] = 0.0f;
      v.tangent[3] = useBackSide ? -1.0f : 1.0f;
      const SUPoint3D &uvq =
          (useBackSide && faceMesh.haveBackSTQ)
              ? faceMesh.stqBack[vi]
              : (faceMesh.haveFrontSTQ ? faceMesh.stqFront[vi]
                                        : faceMesh.stqBack[vi]);
      const double q = (std::abs(uvq.z) > 1e-12) ? uvq.z : 1.0;
      v.uv[0] = static_cast<float>(uvq.x / q);
      v.uv[1] = static_cast<float>(uvq.y / q);
      batch.vertices.push_back(v);
    }

    const bool flipWinding = (useBackSide != isMirrored);
    if (flipWinding) {
      for (size_t k = 0; k + 2 < suIndices.size(); k += 3) {
        batch.indices.push_back(baseVertex + static_cast<uint32_t>(suIndices[k + 0]));
        batch.indices.push_back(baseVertex + static_cast<uint32_t>(suIndices[k + 2]));
        batch.indices.push_back(baseVertex + static_cast<uint32_t>(suIndices[k + 1]));
      }
    } else {
      for (size_t k = 0; k < suIndices.size(); ++k) {
        batch.indices.push_back(baseVertex + static_cast<uint32_t>(suIndices[k]));
      }
    }

    ++processed;
    if (s_progressCb &&
        (processed % progressInterval == 0 || processed == faceCount)) {
      s_progressCb(static_cast<float>(processed) / static_cast<float>(faceCount),
                   progressMsg);
    }
  }

  std::vector<int> sortedMaterialKeys;
  sortedMaterialKeys.reserve(meshBatches.size());
  for (const auto &kv : meshBatches) {
    sortedMaterialKeys.push_back(kv.first);
  }
  std::sort(sortedMaterialKeys.begin(), sortedMaterialKeys.end());

  for (int materialKey : sortedMaterialKeys) {
    auto batchIt = meshBatches.find(materialKey);
    if (batchIt == meshBatches.end())
      continue;

    MeshAccumulator &batch = batchIt->second;
    if (batch.vertices.empty() || batch.indices.empty())
      continue;

    ComputeTangents(batch.vertices, batch.indices);
    GpuMesh gm = LoadMeshFromMemory(batch.vertices, batch.indices);
    gm.materialIndex = materialKey;
    gm.materialSlot = materialKey;
    outMeshes.push_back(std::move(gm));
  }

  fprintf(stderr,
          "LoadSkp: faces=%zu meshes_generated=%zu mesh_helper_builds=%zu cache_hits=%zu\n",
          faceCount, outMeshes.size(), meshHelperBuilds, meshHelperCacheHits);
  for (size_t mi = 0; mi < outMeshes.size(); ++mi) {
    fprintf(stderr, "LoadSkp: mesh[%zu] verts=%u indices=%u material=%d\n", mi,
            outMeshes[mi].vertexCount, outMeshes[mi].indexCount,
            outMeshes[mi].materialIndex);
  }

  if (textureWriterValid)
    SUTextureWriterRelease(&texWriter);
  SUModelRelease(&model);

  if (outMeshes.empty()) {
    fprintf(stderr,
            "LoadSkp: no meshes were generated from '%s' - reporting failure\n",
            path.c_str());
    if (s_progressCb)
      s_progressCb(1.0f, std::string("SKP import failed (no meshes): ") + path);
    return false;
  }

  if (s_progressCb)
    s_progressCb(1.0f, std::string("SKP import finished: ") + path);
  return true;
}

#else // USE_SKETCHUP_SDK

bool LoadSkp(const std::string &path, std::vector<GpuMesh> &outMeshes,
             std::vector<Material> *outMaterials,
             std::vector<Texture> *outTextures, const float *rootTranslation,
             std::vector<ImportedSceneNode> *outSceneNodes) {
  (void)path;
  (void)outMeshes;
  (void)outMaterials;
  (void)outTextures;
  (void)rootTranslation;
  (void)outSceneNodes;
  fprintf(stderr,
          "LoadSkp: SketchUp SDK support not compiled in (USE_SKETCHUP_SDK=OFF).\n");
  return false;
}

#endif // USE_SKETCHUP_SDK

} // namespace Asset
