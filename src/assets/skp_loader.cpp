#include "asset_loader.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <stdio.h>

#ifdef USE_SKETCHUP_SDK
// SketchUp C API headers
#include <SketchUpAPI/initialize.h>
#include <SketchUpAPI/model/model.h>
#include <SketchUpAPI/model/entities.h>
#include <SketchUpAPI/model/entity.h>
#include <SketchUpAPI/model/entity_list.h>
#include <SketchUpAPI/model/entity_list_iterator.h>
#include <SketchUpAPI/model/drawing_element.h>
#include <SketchUpAPI/model/face.h>
#include <SketchUpAPI/model/mesh_helper.h>
#include <SketchUpAPI/model/material.h>
#include <SketchUpAPI/model/image_rep.h>
#include <SketchUpAPI/model/texture_writer.h>
#include <SketchUpAPI/model/texture.h>
#include <SketchUpAPI/model/uv_helper.h>
#include <SketchUpAPI/model/component_instance.h>
#include <SketchUpAPI/model/component_definition.h>
#include <SketchUpAPI/model/group.h>
#include <SketchUpAPI/geometry/transformation.h>
#include <SketchUpAPI/geometry/point3d.h>
#include <SketchUpAPI/geometry/vector3d.h>
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
static inline void ConvertSketchUpPointToEngine(const SUPoint3D &p, float out[3]) {
  constexpr double kInchToMeter = 0.0254;
  // Keep X, move Z->Y, and map Y->-Z to preserve handedness.
  out[0] = static_cast<float>(p.x * kInchToMeter);
  out[1] = static_cast<float>(p.z * kInchToMeter);
  out[2] = static_cast<float>(-p.y * kInchToMeter);
}

static inline void ConvertSketchUpVectorToEngine(const SUVector3D &v, float out[3]) {
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

static bool ComputeTriangleNormal(const std::vector<SUPoint3D> &verts,
                                  const std::vector<size_t> &indices,
                                  SUVector3D &outNormal) {
  if (indices.size() < 3 || verts.empty())
    return false;
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    const size_t i0 = indices[i + 0];
    const size_t i1 = indices[i + 1];
    const size_t i2 = indices[i + 2];
    if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
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
                                      const SUTransformation &faceTransform,
                                      const std::vector<SUPoint3D> &verts,
                                      const std::vector<size_t> &indices) {
  SUVector3D triNormal = {};
  if (!ComputeTriangleNormal(verts, indices, triNormal))
    return false;

  SUVector3D faceNormal = {};
  if (SU_ERROR_NONE != SUFaceGetNormal(face, &faceNormal))
    return false;
  SUVector3DTransform(&faceTransform, &faceNormal);
  if (!NormalizeSUVector(faceNormal))
    return false;

  // If tessellated winding opposes face-front normal, this mesh represents the
  // back side and should use back-side material/UVs.
  return DotVec(triNormal, faceNormal) < 0.0;
}

static bool LoadTextureFromWriterTextureId(SUTextureWriterRef writer, long textureId,
                                           Asset::Texture &outTex) {
  SUImageRepRef image = SU_INVALID;
  if (SU_ERROR_NONE != SUImageRepCreate(&image))
    return false;

  bool ok = false;
  if (SU_ERROR_NONE == SUTextureWriterGetImageRep(writer, textureId, &image)) {
    if (SU_ERROR_NONE == SUImageRepConvertTo32BitsPerPixel(image)) {
      size_t width = 0, height = 0;
      if (SU_ERROR_NONE == SUImageRepGetPixelDimensions(image, &width, &height) &&
          width > 0 && height > 0) {
        std::vector<SUColor> colors(width * height);
        if (SU_ERROR_NONE == SUImageRepGetDataAsColors(image, colors.data())) {
          std::vector<uint8_t> rgba(width * height * 4);
          for (size_t i = 0; i < colors.size(); ++i) {
            rgba[i * 4 + 0] = colors[i].red;
            rgba[i * 4 + 1] = colors[i].green;
            rgba[i * 4 + 2] = colors[i].blue;
            rgba[i * 4 + 3] = colors[i].alpha;
          }
          Asset::Texture tex = Asset::LoadTextureFromMemory(
              rgba.data(), (int)width, (int)height, DXGI_FORMAT_R8G8B8A8_UNORM);
          if (tex.resource) {
            outTex = std::move(tex);
            ok = true;
          }
        }
      }
    }
  }

  SUImageRepRelease(&image);
  return ok;
}

static SUMaterialRef ResolveFaceMaterial(SUFaceRef face, bool preferBackSide,
                                         SUMaterialRef inheritedMaterial) {
  SUMaterialRef front = SU_INVALID;
  SUMaterialRef back = SU_INVALID;
  const bool hasFront =
      (SU_ERROR_NONE == SUFaceGetFrontMaterial(face, &front) && !SUIsInvalid(front));
  const bool hasBack =
      (SU_ERROR_NONE == SUFaceGetBackMaterial(face, &back) && !SUIsInvalid(back));

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

bool LoadSkp(const std::string &path, std::vector<GpuMesh> &outMeshes,
             std::vector<Material> *outMaterials,
             std::vector<Texture> *outTextures, const float *rootTranslation) {
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
  fprintf(stderr, "LoadSkp: applying unit/axis conversion (inches->meters, Z-up->Y-up)\n");
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

  // Texture writer (used for extracting mapped textures)
  SUTextureWriterRef texWriter = SU_INVALID;
  bool textureWriterValid =
      (SU_ERROR_NONE == SUTextureWriterCreate(&texWriter) && !SUIsInvalid(texWriter));
  if (!textureWriterValid) {
    fprintf(stderr, "LoadSkp: texture writer unavailable; continuing without texture extraction.\n");
  }

  // Map material persistent-id -> engine material index
  std::map<int64_t, int> materialMap;
  std::map<int64_t, bool> materialHasTexture;

  SUEntitiesRef entities = SU_INVALID;
  if (SU_ERROR_NONE != SUModelGetEntities(model, &entities) || SUIsInvalid(entities)) {
    fprintf(stderr, "LoadSkp: failed to get model entities.\n");
    if (textureWriterValid)
      SUTextureWriterRelease(&texWriter);
    SUModelRelease(&model);
    return false;
  }

  // Collect faces recursively from top-level entities, groups and component
  // instances. Each face carries the accumulated transform and inherited
  // material (for SketchUp's group/component material painting behavior).
  struct FaceJob {
    SUFaceRef face = SU_INVALID;
    SUTransformation transform = MakeIdentityTransform();
    SUMaterialRef inheritedMaterial = SU_INVALID;
  };
  std::vector<FaceJob> faceJobs;

  std::function<void(SUEntitiesRef, const SUTransformation *, SUMaterialRef)> collectRec;
  collectRec = [&](SUEntitiesRef ents, const SUTransformation *parentTrans,
                   SUMaterialRef inheritedMaterial) {
    if (SUIsInvalid(ents))
      return;

    // Faces in this entity container
    size_t localFaceCount = 0;
    if (SU_ERROR_NONE != SUEntitiesGetFaces(ents, 0, nullptr, &localFaceCount))
      localFaceCount = 0;
    if (localFaceCount > 0) {
      std::vector<SUFaceRef> localFaces(localFaceCount);
      if (SU_ERROR_NONE !=
          SUEntitiesGetFaces(ents, localFaceCount, localFaces.data(), &localFaceCount)) {
        localFaceCount = 0;
      }
      for (size_t i = 0; i < localFaceCount; ++i) {
        FaceJob job;
        job.face = localFaces[i];
        job.transform = parentTrans ? *parentTrans : MakeIdentityTransform();
        job.inheritedMaterial = inheritedMaterial;
        faceJobs.emplace_back(job);
      }
    } else {
      // Fallback for files where SUEntitiesGetFaces returns none but the generic
      // entity list still contains face refs.
      SUEntityListRef faceList = SU_INVALID;
      if (SU_ERROR_NONE == SUEntityListCreate(&faceList)) {
        if (SU_ERROR_NONE == SUEntitiesEntityListFill(ents, SURefType_Face, faceList)) {
          size_t faceListCount = 0;
          if (SU_ERROR_NONE == SUEntityListSize(faceList, &faceListCount) && faceListCount > 0) {
            SUEntityListIteratorRef it = SU_INVALID;
            if (SU_ERROR_NONE == SUEntityListIteratorCreate(&it)) {
              if (SU_ERROR_NONE == SUEntityListBegin(faceList, &it)) {
                bool inRange = false;
                while (SU_ERROR_NONE == SUEntityListIteratorIsInRange(it, &inRange) && inRange) {
                  SUEntityRef ent = SU_INVALID;
                  if (SU_ERROR_NONE == SUEntityListIteratorGetEntity(it, &ent)) {
                    SUFaceRef faceRef = SUFaceFromEntity(ent);
                    if (!SUIsInvalid(faceRef)) {
                      FaceJob job;
                      job.face = faceRef;
                      job.transform = parentTrans ? *parentTrans : MakeIdentityTransform();
                      job.inheritedMaterial = inheritedMaterial;
                      faceJobs.emplace_back(job);
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

    // Recurse into groups
    size_t groupCount = 0;
    if (SU_ERROR_NONE != SUEntitiesGetGroups(ents, 0, nullptr, &groupCount))
      groupCount = 0;
    if (groupCount > 0) {
      std::vector<SUGroupRef> groups(groupCount);
      if (SU_ERROR_NONE != SUEntitiesGetGroups(ents, groupCount, groups.data(), &groupCount))
        groupCount = 0;
      for (size_t gi = 0; gi < groupCount; ++gi) {
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
        collectRec(groupEnts, &combined, childInherited);
      }
    }

    // Recurse into component instances (follow instance -> definition -> entities)
    size_t instCount = 0;
    if (SU_ERROR_NONE != SUEntitiesGetInstances(ents, 0, nullptr, &instCount))
      instCount = 0;
    if (instCount > 0) {
      std::vector<SUComponentInstanceRef> instances(instCount);
      if (SU_ERROR_NONE !=
          SUEntitiesGetInstances(ents, instCount, instances.data(), &instCount)) {
        instCount = 0;
      }
      for (size_t ii = 0; ii < instCount; ++ii) {
        SUComponentDefinitionRef def = SU_INVALID;
        if (SU_ERROR_NONE != SUComponentInstanceGetDefinition(instances[ii], &def))
          continue;
        if (SUIsInvalid(def))
          continue;
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
        collectRec(defEnts, &combined, childInherited);
      }
    }
  };

  collectRec(entities, nullptr, SU_INVALID);
  if (faceJobs.empty()) {
    // Fallback path: some files keep geometry in definitions not reachable from
    // top-level entities in a way SUEntitiesGetInstances exposes.
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
        SUEntitiesRef defEnts = SU_INVALID;
        if (SU_ERROR_NONE != SUComponentDefinitionGetEntities(defs[i], &defEnts))
          continue;
        collectRec(defEnts, nullptr, SU_INVALID);
      }
    };
    collectDefinitions(SUModelGetNumComponentDefinitions, SUModelGetComponentDefinitions);
    collectDefinitions(SUModelGetNumGroupDefinitions, SUModelGetGroupDefinitions);
    collectDefinitions(SUModelGetNumImageDefinitions, SUModelGetImageDefinitions);
  }

  size_t faceCount = faceJobs.size();
  size_t processed = 0;
  std::vector<bool> faceUsesBackSide(faceCount, false);
  int defaultMaterialIndex = -1;
  struct MeshAccumulator {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
  };
  std::map<int, MeshAccumulator> meshBatches;

  for (size_t fi = 0; fi < faceCount; ++fi) {
    SUFaceRef face = faceJobs[fi].face;
    const SUTransformation &faceTransform = faceJobs[fi].transform;

    // Create a tessellated mesh helper for the face (gives triangles + STQ)
    SUMeshHelperRef meshRef = SU_INVALID;
    SUResult meshCreate = SU_ERROR_INVALID_INPUT;
    if (textureWriterValid)
      meshCreate = SUMeshHelperCreateWithTextureWriter(&meshRef, face, texWriter);
    if (meshCreate != SU_ERROR_NONE)
      meshCreate = SUMeshHelperCreate(&meshRef, face);
    if (meshCreate != SU_ERROR_NONE) {
      continue;
    }

    // vertices
    size_t numVerts = 0;
    if (SU_ERROR_NONE != SUMeshHelperGetNumVertices(meshRef, &numVerts) || numVerts == 0) {
      SUMeshHelperRelease(&meshRef);
      continue;
    }
    std::vector<SUPoint3D> suVerts(numVerts);
    size_t vertsRetrieved = numVerts;
    if (SU_ERROR_NONE !=
        SUMeshHelperGetVertices(meshRef, vertsRetrieved, suVerts.data(), &vertsRetrieved) ||
        vertsRetrieved == 0) {
      SUMeshHelperRelease(&meshRef);
      continue;
    }
    if (vertsRetrieved != numVerts) {
      numVerts = vertsRetrieved;
      suVerts.resize(numVerts);
    }

    // STQ (front/back)
    std::vector<SUPoint3D> stqFront(numVerts, SUPoint3D{0.0, 0.0, 1.0});
    std::vector<SUPoint3D> stqBack(numVerts, SUPoint3D{0.0, 0.0, 1.0});
    size_t stqFrontCount = numVerts;
    size_t stqBackCount = numVerts;
    const bool haveFrontSTQ =
        (SU_ERROR_NONE == SUMeshHelperGetFrontSTQCoords(meshRef, stqFrontCount,
                                                        stqFront.data(), &stqFrontCount) &&
         stqFrontCount == numVerts);
    const bool haveBackSTQ =
        (SU_ERROR_NONE == SUMeshHelperGetBackSTQCoords(meshRef, stqBackCount,
                                                       stqBack.data(), &stqBackCount) &&
         stqBackCount == numVerts);

    // normals
    std::vector<SUVector3D> normals(numVerts, SUVector3D{0.0, 1.0, 0.0});
    size_t normalCount = numVerts;
    if (SU_ERROR_NONE ==
            SUMeshHelperGetNormals(meshRef, normalCount, normals.data(), &normalCount) &&
        normalCount == numVerts) {
      // retrieved successfully
    } else {
      std::fill(normals.begin(), normals.end(), SUVector3D{0.0, 1.0, 0.0});
    }

    // Apply instance/group transform to vertices & normals if not identity
    bool isId = true;
    SUTransformationIsIdentity(&faceTransform, &isId);
    if (!isId) {
      for (size_t vi = 0; vi < numVerts; ++vi) {
        SUPoint3DTransform(&faceTransform, &suVerts[vi]);
        SUVector3DTransform(&faceTransform, &normals[vi]);
      }
    }

    // indices
    size_t numTris = 0;
    if (SU_ERROR_NONE != SUMeshHelperGetNumTriangles(meshRef, &numTris) || numTris == 0) {
      SUMeshHelperRelease(&meshRef);
      continue;
    }
    std::vector<size_t> suIndices(numTris * 3);
    size_t numIndices = suIndices.size();
    if (SU_ERROR_NONE !=
        SUMeshHelperGetVertexIndices(meshRef, numIndices, suIndices.data(), &numIndices) ||
        numIndices < 3) {
      SUMeshHelperRelease(&meshRef);
      continue;
    }
    suIndices.resize(numIndices);
    const bool useBackSide =
        ShouldUseBackSideMaterial(face, faceTransform, suVerts, suIndices);
    faceUsesBackSide[fi] = useBackSide;

    // Build engine vertices & indices
    std::vector<Vertex> vertices;
    vertices.reserve(numVerts);
    for (size_t vi = 0; vi < numVerts; ++vi) {
      Vertex v{};
      ConvertSketchUpPointToEngine(suVerts[vi], v.pos);
      if (rootTranslation) {
        v.pos[0] += rootTranslation[0];
        v.pos[1] += rootTranslation[1];
        v.pos[2] += rootTranslation[2];
      }
      ConvertSketchUpVectorToEngine(normals[vi], v.normal);
      if (useBackSide) {
        v.normal[0] = -v.normal[0];
        v.normal[1] = -v.normal[1];
        v.normal[2] = -v.normal[2];
      }
      Normalize3(v.normal);
      // tangents not provided by SDK — set default
      v.tangent[0] = 1.0f;
      v.tangent[1] = 0.0f;
      v.tangent[2] = 0.0f;
      v.tangent[3] = useBackSide ? -1.0f : 1.0f;
      // UV from STQ
      const SUPoint3D &uvq =
          (useBackSide && haveBackSTQ)
              ? stqBack[vi]
              : (haveFrontSTQ ? stqFront[vi] : stqBack[vi]);
      const double q = (std::abs(uvq.z) > 1e-12) ? uvq.z : 1.0;
      v.uv[0] = (float)(uvq.x / q);
      v.uv[1] = (float)(uvq.y / q);
      vertices.push_back(v);
    }

    std::vector<uint32_t> indices;
    indices.reserve(suIndices.size());
    if (useBackSide) {
      for (size_t k = 0; k + 2 < suIndices.size(); k += 3) {
        const uint32_t i0 = static_cast<uint32_t>(suIndices[k + 0]);
        const uint32_t i1 = static_cast<uint32_t>(suIndices[k + 1]);
        const uint32_t i2 = static_cast<uint32_t>(suIndices[k + 2]);
        indices.push_back(i0);
        indices.push_back(i2);
        indices.push_back(i1);
      }
    } else {
      for (size_t k = 0; k < suIndices.size(); ++k) {
        indices.push_back(static_cast<uint32_t>(suIndices[k]));
      }
    }

    // Material for this face
    int materialIndex = -1;
    SUMaterialRef skMat =
        ResolveFaceMaterial(face, useBackSide, faceJobs[fi].inheritedMaterial);
    const bool hasMaterial = !SUIsInvalid(skMat);
    if (hasMaterial) {
      // Get persistent id for material entity to de-duplicate
      SUEntityRef matEntity = SUMaterialToEntity(skMat);
      int64_t pid = 0;
      SUEntityGetPersistentID(matEntity, &pid);
      auto it = materialMap.find(pid);
      if (it != materialMap.end()) {
        materialIndex = it->second;
      } else {
        // Create new Material
        Material mtl;
        // Name
        SUStringRef name = SU_INVALID;
        SUStringCreate(&name);
        SUMaterialGetName(skMat, &name);
        std::string nm = SUStringToStdString(name);
        SUStringRelease(&name);
        if (!nm.empty())
          strncpy_s(mtl.name, nm.c_str(), _TRUNCATE);

        // Color
        SUColor col = {255, 255, 255, 255};
        SUMaterialGetColor(skMat, &col);
        mtl.diffuseColor[0] = col.red / 255.0f;
        mtl.diffuseColor[1] = col.green / 255.0f;
        mtl.diffuseColor[2] = col.blue / 255.0f;
        mtl.diffuseColor[3] = 1.0f;
        double opacity = 1.0;
        if (SU_ERROR_NONE == SUMaterialGetOpacity(skMat, &opacity)) {
          if (opacity < 0.0)
            opacity = 0.0;
          if (opacity > 1.0)
            opacity = 1.0;
          mtl.diffuseColor[3] = (float)opacity;
        }
        // SketchUp materials are generally non-metallic and mostly matte in the
        // viewport. Use a conservative default to better match expected look.
        mtl.metalness = 0.0f;
        mtl.reflectionGlossiness = 0.0f;
        mtl.doubleSided = true;

        // Add to list
        if (outMaterials) {
          materialIndex = (int)outMaterials->size();
          outMaterials->push_back(mtl);
        } else {
          // No materials array provided: still assign an index in local map
          materialIndex = (int)materialMap.size();
        }
        materialMap[pid] = materialIndex;
        SUTextureRef matTexture = SU_INVALID;
        const bool hasTexture =
            (SU_ERROR_NONE == SUMaterialGetTexture(skMat, &matTexture) &&
             !SUIsInvalid(matTexture));
        materialHasTexture[pid] = hasTexture;
      }
    } else {
      // No material: create default white material if requested
      if (outMaterials) {
        if (defaultMaterialIndex < 0) {
          Material mtl;
          defaultMaterialIndex = (int)outMaterials->size();
          outMaterials->push_back(mtl);
        }
        materialIndex = defaultMaterialIndex;
      }
    }

    // Batch per material to avoid creating thousands of tiny meshes/buffers.
    auto &batch = meshBatches[materialIndex];
    const uint32_t baseVertex = static_cast<uint32_t>(batch.vertices.size());
    batch.vertices.insert(batch.vertices.end(), vertices.begin(), vertices.end());
    batch.indices.reserve(batch.indices.size() + indices.size());
    for (uint32_t idx : indices) {
      batch.indices.push_back(baseVertex + idx);
    }

    SUMeshHelperRelease(&meshRef);

    // Progress
    ++processed;
    if (s_progressCb)
      s_progressCb((float)processed / (float)faceCount,
                    std::string("Processing faces...") + path);
  }

  if (textureWriterValid) {
    // Extract textures directly from SketchUp API image reps to avoid slow
    // disk round-trips (WriteAllTextures + reloading files).
    if (outMaterials && outTextures) {
      std::map<long, int> textureIdToIndex;
      for (size_t fi = 0; fi < faceCount; ++fi) {
        SUFaceRef face = faceJobs[fi].face;
        const bool preferBackSide = faceUsesBackSide[fi];
        long texId = 0;
        SUResult texResult = SUTextureWriterGetTextureIdForFace(
            texWriter, face, !preferBackSide, &texId);
        if (texResult != SU_ERROR_NONE || texId == 0) {
          texResult = SUTextureWriterGetTextureIdForFace(
              texWriter, face, preferBackSide, &texId);
        }
        if (texId == 0)
          continue;

        int texIndex = -1;
        auto cachedTex = textureIdToIndex.find(texId);
        if (cachedTex != textureIdToIndex.end()) {
          texIndex = cachedTex->second;
        } else {
          Asset::Texture tex;
          if (!LoadTextureFromWriterTextureId(texWriter, texId, tex))
            continue;

          texIndex = (int)outTextures->size();
          outTextures->push_back(std::move(tex));
          textureIdToIndex[texId] = texIndex;
        }

        SUMaterialRef skMat = ResolveFaceMaterial(
            face, preferBackSide, faceJobs[fi].inheritedMaterial);
        if (SUIsInvalid(skMat))
          continue;
        SUEntityRef matEntity = SUMaterialToEntity(skMat);
        int64_t pid = 0;
        SUEntityGetPersistentID(matEntity, &pid);
        auto texIt = materialHasTexture.find(pid);
        if (texIt != materialHasTexture.end() && !texIt->second)
          continue;
        auto it = materialMap.find(pid);
        if (it != materialMap.end()) {
          int mIdx = it->second;
          if (mIdx >= 0 && (size_t)mIdx < outMaterials->size() &&
              (*outMaterials)[mIdx].diffuseTexture < 0)
            (*outMaterials)[mIdx].diffuseTexture = texIndex;
        }
      }
    }
  }

  // Create GPU meshes from merged batches.
  for (auto &kv : meshBatches) {
    if (kv.second.vertices.empty() || kv.second.indices.empty())
      continue;
    GpuMesh gm = LoadMeshFromMemory(kv.second.vertices, kv.second.indices);
    gm.materialIndex = kv.first;
    outMeshes.push_back(std::move(gm));
  }

  // Diagnostic summary for debugging imports
  fprintf(stderr, "LoadSkp: faces=%zu meshes_generated=%zu\n", faceCount, outMeshes.size());
  for (size_t mi = 0; mi < outMeshes.size(); ++mi) {
    fprintf(stderr, "LoadSkp: mesh[%zu] verts=%u indices=%u material=%d\n",
            mi, outMeshes[mi].vertexCount, outMeshes[mi].indexCount, outMeshes[mi].materialIndex);
  }

  if (textureWriterValid)
    SUTextureWriterRelease(&texWriter);
  SUModelRelease(&model);

  // Treat an import that produces zero meshes as a failure so UI won't show
  // "Loaded" when nothing was created.
  if (outMeshes.empty()) {
    fprintf(stderr, "LoadSkp: no meshes were generated from '%s' — reporting failure\n", path.c_str());
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
             std::vector<Texture> *outTextures, const float *rootTranslation) {
  (void)path; (void)outMeshes; (void)outMaterials; (void)outTextures; (void)rootTranslation;
  fprintf(stderr, "LoadSkp: SketchUp SDK support not compiled in (USE_SKETCHUP_SDK=OFF).\n");
  return false;
}

#endif // USE_SKETCHUP_SDK

} // namespace Asset
