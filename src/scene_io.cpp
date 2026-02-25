#include "scene_io.h"
#include "camera.h"
#include "dxr_renderer.h"
#include "ibl_manager.h"
#include "scene.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>

// Globals from main.cpp
extern std::vector<Asset::Material> g_loadedMaterials;
extern std::vector<Asset::GpuMesh> g_loadedMeshes;
extern std::vector<Asset::Texture> g_loadedTextures;
extern RenderMode g_currentRenderMode;
extern float g_camYaw;
extern float g_camPitch;
extern bool g_cloudRenderingEnabled;
extern float g_camSpeed;
extern float g_mouseSensitivity;
extern bool g_drawGrid;
#include "dx12_context.h"
#include "streamline_manager.h"

// use DX12Context::g_streamline
#include "clouds.h"
extern CloudManager g_cloudManager;

namespace fs = std::filesystem;
using json = nlohmann::json;

// --- Base64 Utilities ---
static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                        "abcdefghijklmnopqrstuvwxyz"
                                        "0123456789+/";

static std::string Base64Encode(unsigned char const *bytes_to_encode,
                                size_t in_len) {
  std::string ret;
  int i = 0;
  int j = 0;
  unsigned char char_array_3[3];
  unsigned char char_array_4[4];

  while (in_len--) {
    char_array_3[i++] = *(bytes_to_encode++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] =
          ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] =
          ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for (i = 0; i < 4; i++)
        ret += base64_chars[char_array_4[i]];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 3; j++)
      char_array_3[j] = '\0';

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] =
        ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] =
        ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

    for (j = 0; (j < i + 1); j++)
      ret += base64_chars[char_array_4[j]];

    while ((i++ < 3))
      ret += '=';
  }

  return ret;
}

static std::vector<unsigned char>
Base64Decode(std::string const &encoded_string) {
  size_t in_len = encoded_string.size();
  int i = 0;
  int j = 0;
  int in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  std::vector<unsigned char> ret;

  while (in_len-- && (encoded_string[in_] != '=') &&
         (isalnum(encoded_string[in_]) || (encoded_string[in_] == '+') ||
          (encoded_string[in_] == '/'))) {
    char_array_4[i++] = encoded_string[in_];
    in_++;
    if (i == 4) {
      for (i = 0; i < 4; i++)
        char_array_4[i] = (unsigned char)base64_chars.find(char_array_4[i]);

      char_array_3[0] =
          (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] =
          ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

      for (i = 0; (i < 3); i++)
        ret.push_back(char_array_3[i]);
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 4; j++)
      char_array_4[j] = 0;

    for (j = 0; j < 4; j++)
      char_array_4[j] = (unsigned char)base64_chars.find(char_array_4[j]);

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] =
        ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

    for (j = 0; (j < i - 1); j++)
      ret.push_back(char_array_3[j]);
  }

  return ret;
}

namespace SceneIO {

bool SaveScene(const std::string &path) {
  try {
    json j;

    // 1. Camera
    j["camera"]["pos"] = {g_cameraData.pos[0], g_cameraData.pos[1],
                          g_cameraData.pos[2]};
    j["camera"]["forward"] = {g_cameraData.forward[0], g_cameraData.forward[1],
                              g_cameraData.forward[2]};
    j["camera"]["up"] = {g_cameraData.up[0], g_cameraData.up[1],
                         g_cameraData.up[2]};
    j["camera"]["fov"] = g_cameraData.fov;
    j["camera"]["intensity"] = g_cameraData.intensity;
    j["camera"]["maxSPP"] = g_cameraData.maxSPP;
    j["camera"]["maxSpecularBounces"] = g_cameraData.maxSpecularBounces;
    j["camera"]["maxRefractiveBounces"] = g_cameraData.maxRefractiveBounces;
    j["camera"]["maxGIBounces"] = g_cameraData.maxGIBounces;
    j["camera"]["yaw"] = g_camYaw;
    j["camera"]["pitch"] = g_camPitch;
    j["camera"]["useAdaptiveSampling"] = g_cameraData.useAdaptiveSampling;
    j["camera"]["noiseThreshold"] = g_cameraData.noiseThreshold;
    j["camera"]["debugVisualizationMode"] = g_cameraData.debugVisualizationMode;

    // 2. Settings
    j["settings"]["cloudRendering"] = g_cloudRenderingEnabled;
    j["settings"]["camSpeed"] = g_camSpeed;
    j["settings"]["mouseSensitivity"] = g_mouseSensitivity;
    j["settings"]["drawGrid"] = g_drawGrid;

    // Cloud Parameters
    auto &cp = g_cloudManager.GetParams();
    j["clouds"]["density"] = cp.density;
    j["clouds"]["absorption"] = cp.absorption;
    j["clouds"]["coverage"] = cp.coverage;
    j["clouds"]["scattering"] = cp.scattering;
    j["clouds"]["steps"] = cp.steps;
    j["clouds"]["sunIntensity"] = cp.sunIntensity;
    j["clouds"]["cloudTop"] = cp.cloudTop;
    j["clouds"]["cloudBottom"] = cp.cloudBottom;
    j["clouds"]["windSpeed"] = cp.windSpeed;
    j["clouds"]["baseScale"] = cp.baseScale;
    j["clouds"]["detailScale"] = cp.detailScale;
    j["clouds"]["coverageScale"] = cp.coverageScale;
    j["clouds"]["coverageVariation"] = cp.coverageVariation;
    j["clouds"]["erosion"] = cp.erosion;
    j["clouds"]["warpStrength"] = cp.warpStrength;
    j["clouds"]["shapePower"] = cp.shapePower;
    j["clouds"]["powderStrength"] = cp.powderStrength;
    j["clouds"]["shadowSteps"] = cp.shadowSteps;
    j["clouds"]["shadowStepSize"] = cp.shadowStepSize;
    j["clouds"]["shadowLod"] = cp.shadowLod;
    j["clouds"]["maxSteps"] = cp.maxSteps;
    j["clouds"]["verticalStepMeters"] = cp.verticalStepMeters;
    j["clouds"]["shadowEvery"] = cp.shadowEvery;
    j["clouds"]["shadowDensityThreshold"] = cp.shadowDensityThreshold;

    // Streamline / DLSS
    j["streamline"]["enabled"] = DX12Context::g_streamline.IsEnabled();
    j["streamline"]["mode"] = (int)DX12Context::g_streamline.GetMode();
    j["streamline"]["quality"] = (int)DX12Context::g_streamline.GetQuality();

    // Global Scene Lighting
    j["lighting"]["lightDir"] = {
        g_cameraData.lightDir[0], g_cameraData.lightDir[1],
        g_cameraData.lightDir[2], g_cameraData.lightDir[3]};
    j["lighting"]["lightColor"] = {
        g_cameraData.lightColor[0], g_cameraData.lightColor[1],
        g_cameraData.lightColor[2], g_cameraData.lightColor[3]};
    j["lighting"]["ambientColor"] = {
        g_cameraData.ambientColor[0], g_cameraData.ambientColor[1],
        g_cameraData.ambientColor[2], g_cameraData.ambientColor[3]};

    // 3. Render Mode
    j["renderMode"] =
        (g_currentRenderMode == RenderMode::DXR) ? "DXR" : "Raster";

    // 4. IBL Settings
    auto &ibl = IBLManager::Get();
    j["ibl"]["source"] = (ibl.GetIBLSource() == IBLManager::IBLSource::File)
                             ? "File"
                             : "PragueSkyModel";
    j["ibl"]["visibility"] = ibl.GetSkyVisibility();
    j["ibl"]["albedo"] = ibl.GetSkyAlbedo();
    j["ibl"]["solarElevation"] = ibl.GetSolarAltitude();
    j["ibl"]["solarAzimuth"] = ibl.GetSolarAzimuth();
    j["ibl"]["altitude"] = ibl.GetObserverAltitude();
    j["ibl"]["skyIntensity"] = ibl.GetSkyIntensity();
    j["ibl"]["sunIntensity"] = ibl.GetSunIntensity();
    j["ibl"]["sunSize"] = ibl.GetSunSize();

    // 5. Materials (Flat list of all materials)
    for (const auto &mat : g_loadedMaterials) {
      json m;
      m["name"] = std::string(mat.name);
      m["diffuseColor"] = {mat.diffuseColor[0], mat.diffuseColor[1],
                           mat.diffuseColor[2], mat.diffuseColor[3]};
      m["reflectionColor"] = {mat.reflectionColor[0], mat.reflectionColor[1],
                              mat.reflectionColor[2], mat.reflectionColor[3]};
      m["reflectionGlossiness"] = mat.reflectionGlossiness;
      m["metalness"] = mat.metalness;
      m["refractionColor"] = {mat.refractionColor[0], mat.refractionColor[1],
                              mat.refractionColor[2], mat.refractionColor[3]};
      m["refractionGlossiness"] = mat.refractionGlossiness;
      m["ior"] = mat.ior;
      m["emissiveColor"] = {mat.emissiveColor[0], mat.emissiveColor[1],
                            mat.emissiveColor[2], mat.emissiveColor[3]};
      m["emissiveIntensity"] = mat.emissiveIntensity;

      m["clearcoat"] = mat.clearcoat;
      m["clearcoatRoughness"] = mat.clearcoatRoughness;
      m["thinWalled"] = mat.thinWalled;
      m["translucency"] = mat.translucency;
      m["uvScale"] = {mat.uvScale[0], mat.uvScale[1]};
      m["uvOffset"] = {mat.uvOffset[0], mat.uvOffset[1]};

      m["triPlanarEnabled"] = mat.triPlanarEnabled;
      m["triPlanarScale"] = mat.triPlanarScale;
      m["triPlanarSharpness"] = mat.triPlanarSharpness;
      m["triPlanarNormalStrength"] = mat.triPlanarNormalStrength;

      // Note: We save indices but they are fragile. In a better system
      // we would save texture paths/names.
      m["diffuseTexture"] = mat.diffuseTexture;
      m["reflectionTexture"] = mat.reflectionTexture;
      m["normalTexture"] = mat.normalTexture;
      m["emissiveTexture"] = mat.emissiveTexture;
      m["metalRoughTexture"] = mat.metalRoughTexture;

      j["materials"].push_back(m);
    }

    // 6. Nodes
    for (const auto &node : Scene::GetNodes()) {
      json n;
      n["name"] = node.name;
      n["sourcePath"] = node.sourcePath;
      n["visible"] = node.visible;
      n["meshIndices"] = node.meshIndices;
      std::vector<float> transform(16);
      for (int i = 0; i < 16; ++i)
        transform[i] = node.transform[i];
      n["transform"] = transform;
      j["nodes"].push_back(n);
    }

    // 7. Embedded Assets
    for (size_t i = 0; i < g_loadedMeshes.size(); ++i) {
      const auto &mesh = g_loadedMeshes[i];
      json m;
      m["index"] = i;
      m["materialIndex"] = mesh.materialIndex;
      m["vertexCount"] = mesh.vertexCount;
      m["indexCount"] = mesh.indexCount;
      m["vertices"] = Base64Encode(
          reinterpret_cast<const unsigned char *>(mesh.cpuVertices.data()),
          mesh.cpuVertices.size() * sizeof(Asset::Vertex));
      m["indices"] = Base64Encode(
          reinterpret_cast<const unsigned char *>(mesh.cpuIndices.data()),
          mesh.cpuIndices.size() * sizeof(uint32_t));
      j["embeddedAssets"]["meshes"].push_back(m);
    }

    for (size_t i = 0; i < g_loadedTextures.size(); ++i) {
      const auto &tex = g_loadedTextures[i];
      json t;
      t["index"] = i;
      t["width"] = tex.width;
      t["height"] = tex.height;
      t["format"] = (int)tex.format;
      if (!tex.cpuData.empty()) {
        t["data"] = Base64Encode(tex.cpuData.data(), tex.cpuData.size());
      } else {
        t["data"] = ""; // Missing CPU data but keep index slot
      }
      j["embeddedAssets"]["textures"].push_back(t);
    }

    std::ofstream file(path);
    if (!file.is_open())
      return false;
    file << j.dump(4);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "SaveScene exception: " << e.what() << std::endl;
    return false;
  }
}

bool LoadScene(const std::string &path) {
  try {
    std::ifstream file(path);
    if (!file.is_open())
      return false;
    json j;
    file >> j;

    // 1. Reset Scene
    Scene::ResetScene();

    // 1.5 Embedded Assets (Load these FIRST so nodes can reference them)
    bool hasEmbedded = false;
    size_t embeddedMeshCount = 0;
    if (j.contains("embeddedAssets")) {
      hasEmbedded = true;
      auto &ea = j["embeddedAssets"];
      if (ea.contains("textures")) {
        fprintf(stderr, "LoadScene: decoding %zu embedded textures\n",
                ea["textures"].size());
        for (const auto &t : ea["textures"]) {
          size_t idx = t.value("index", g_loadedTextures.size());
          if (idx >= g_loadedTextures.size()) {
            g_loadedTextures.resize(idx + 1);
          }
          std::string b64 = t.value("data", "");
          if (!b64.empty()) {
            std::vector<unsigned char> data = Base64Decode(b64);
            Asset::Texture tex = Asset::LoadTextureFromMemory(
                data.data(), t["width"], t["height"], (DXGI_FORMAT)t["format"]);
            g_loadedTextures[idx] = std::move(tex);
          } else {
            // Keep slot empty if no data
          }
        }
        // Register these just-loaded embedded textures
        Scene::RegisterTextures(g_loadedTextures);
      }
      if (ea.contains("meshes")) {
        fprintf(stderr, "LoadScene: decoding %zu embedded meshes\n",
                ea["meshes"].size());
        for (const auto &m : ea["meshes"]) {
          std::vector<unsigned char> vData = Base64Decode(m["vertices"]);
          std::vector<unsigned char> iData = Base64Decode(m["indices"]);

          std::vector<Asset::Vertex> vertices(m["vertexCount"].get<size_t>());
          if (vData.size() == vertices.size() * sizeof(Asset::Vertex)) {
            memcpy(vertices.data(), vData.data(), vData.size());
          }

          std::vector<uint32_t> indices(m["indexCount"].get<size_t>());
          if (iData.size() == indices.size() * sizeof(uint32_t)) {
            memcpy(indices.data(), iData.data(), iData.size());
          }

          Asset::GpuMesh mesh = Asset::LoadMeshFromMemory(vertices, indices);
          mesh.materialIndex = m.value("materialIndex", -1);
          g_loadedMeshes.push_back(mesh);
        }
        embeddedMeshCount = g_loadedMeshes.size();
      }
    }

    // 2. Camera
    if (j.contains("camera")) {
      auto c = j["camera"];
      g_cameraData.pos[0] = c["pos"][0];
      g_cameraData.pos[1] = c["pos"][1];
      g_cameraData.pos[2] = c["pos"][2];
      if (c.contains("forward")) {
        g_cameraData.forward[0] = c["forward"][0];
        g_cameraData.forward[1] = c["forward"][1];
        g_cameraData.forward[2] = c["forward"][2];
      }
      if (c.contains("up")) {
        g_cameraData.up[0] = c["up"][0];
        g_cameraData.up[1] = c["up"][1];
        g_cameraData.up[2] = c["up"][2];
      }
        g_cameraData.fov = c.value("fov", 60.0f);
        g_cameraData.intensity = c.value("intensity", g_cameraData.intensity);
        g_cameraData.intensity =
          (std::clamp)(g_cameraData.intensity, 1e-5f, 10.0f);
      g_cameraData.maxSPP = c.value("maxSPP", 1024.0f);
      g_cameraData.maxSpecularBounces = c.value("maxSpecularBounces", 3.0f);
      g_cameraData.maxRefractiveBounces = c.value("maxRefractiveBounces", 3.0f);
      g_cameraData.maxGIBounces = c.value("maxGIBounces", 2.0f);

      // Restore Yaw/Pitch for consistent mouse-look
      g_camYaw = c.value("yaw", g_camYaw);
      g_camPitch = c.value("pitch", g_camPitch);
      g_cameraData.useAdaptiveSampling = c.value("useAdaptiveSampling", 0.0f);
      g_cameraData.noiseThreshold = c.value("noiseThreshold", 0.05f);
      g_cameraData.debugVisualizationMode =
          c.value("debugVisualizationMode", 0.0f);
    }

    // 2.5 Settings
    if (j.contains("settings")) {
      auto s = j["settings"];
      g_cloudRenderingEnabled = s.value("cloudRendering", true);
      g_camSpeed = s.value("camSpeed", g_camSpeed);
      g_mouseSensitivity = s.value("mouseSensitivity", g_mouseSensitivity);
      g_drawGrid = s.value("drawGrid", g_drawGrid);
    }

    if (j.contains("clouds")) {
      auto c = j["clouds"];
      auto &cp = g_cloudManager.GetParams();
      cp.density = c.value("density", cp.density);
      cp.absorption = c.value("absorption", cp.absorption);
      cp.coverage = c.value("coverage", cp.coverage);
      cp.scattering = c.value("scattering", cp.scattering);
      cp.steps = c.value("steps", cp.steps);
      cp.sunIntensity = c.value("sunIntensity", cp.sunIntensity);
      cp.cloudTop = c.value("cloudTop", cp.cloudTop);
      cp.cloudBottom = c.value("cloudBottom", cp.cloudBottom);
      cp.windSpeed = c.value("windSpeed", cp.windSpeed);
      cp.baseScale = c.value("baseScale", cp.baseScale);
      cp.detailScale = c.value("detailScale", cp.detailScale);
      cp.coverageScale = c.value("coverageScale", cp.coverageScale);
      cp.coverageVariation = c.value("coverageVariation", cp.coverageVariation);
      cp.erosion = c.value("erosion", cp.erosion);
      cp.warpStrength = c.value("warpStrength", cp.warpStrength);
      cp.shapePower = c.value("shapePower", cp.shapePower);
      cp.powderStrength = c.value("powderStrength", cp.powderStrength);
      cp.shadowSteps = c.value("shadowSteps", cp.shadowSteps);
      cp.shadowStepSize = c.value("shadowStepSize", cp.shadowStepSize);
      cp.shadowLod = c.value("shadowLod", cp.shadowLod);
      cp.maxSteps = c.value("maxSteps", cp.maxSteps);
      cp.verticalStepMeters =
          c.value("verticalStepMeters", cp.verticalStepMeters);
      cp.shadowEvery = c.value("shadowEvery", cp.shadowEvery);
      cp.shadowDensityThreshold =
          c.value("shadowDensityThreshold", cp.shadowDensityThreshold);
    }

    if (j.contains("streamline")) {
      auto sl = j["streamline"];
      DX12Context::g_streamline.SetEnabled(sl.value("enabled", true));
      DX12Context::g_streamline.SetMode((StreamlineManager::Mode)sl.value(
          "mode", (int)StreamlineManager::Mode::Off));
      DX12Context::g_streamline.SetQuality((StreamlineManager::Quality)sl.value(
          "quality", (int)StreamlineManager::Quality::Balanced));
    }

    if (j.contains("lighting")) {
      auto l = j["lighting"];
      for (int i = 0; i < 4; ++i) {
        g_cameraData.lightDir[i] = l["lightDir"][i];
        g_cameraData.lightColor[i] = l["lightColor"][i];
        g_cameraData.ambientColor[i] = l["ambientColor"][i];
      }
    }

    // 3. Render Mode
    if (j.contains("renderMode")) {
      g_currentRenderMode =
          (j["renderMode"] == "DXR") ? RenderMode::DXR : RenderMode::Raster;
    }

    // 4. IBL Settings
    if (j.contains("ibl")) {
      auto &ibl = IBLManager::Get();
      auto i = j["ibl"];
      ibl.SetIBLSource((i["source"] == "File")
                           ? IBLManager::IBLSource::File
                           : IBLManager::IBLSource::PragueSkyModel);
      ibl.SetSkyVisibility(i.value("visibility", 30.0f));
      ibl.SetSkyAlbedo(i.value("albedo", 0.5f));
      ibl.SetSolarAltitude(i.value("solarElevation", 0.5f));
      ibl.SetSolarAzimuth(i.value("solarAzimuth", 0.0f));
      ibl.SetObserverAltitude(i.value("altitude", 200.0f));
      ibl.SetSkyIntensity(i.value("skyIntensity", 1.0f));
      ibl.SetSunIntensity(i.value("sunIntensity", 1.0f));
      ibl.SetSunSize(i.value("sunSize", 2.0f));
      ibl.UpdateSkyModel();
    }

    // 5. Nodes and Asset Re-loading
    if (j.contains("nodes")) {
      fprintf(stderr, "LoadScene: processing %zu nodes\n", j["nodes"].size());
      for (const auto &n : j["nodes"]) {
        std::string sourcePath = n.value("sourcePath", "");
        fprintf(stderr, "LoadScene: Node '%s' sourcePath='%s'\n",
                n.value("name", "unknown").c_str(), sourcePath.c_str());

        // If we have embedded assets AND meshIndices, we can reconstruct the
        // node directly
        if (hasEmbedded && n.contains("meshIndices")) {
          Scene::Node node;
          node.name = n.value("name", "EmbeddedNode");
          node.sourcePath = sourcePath;
          node.visible = n.value("visible", true);
          node.meshIndices = n["meshIndices"].get<std::vector<size_t>>();
          if (n.contains("transform")) {
            for (int i = 0; i < 16; ++i)
              node.transform[i] = n["transform"][i];
          }
          auto &sceneNodes =
              const_cast<std::vector<Scene::Node> &>(Scene::GetNodes());
          sceneNodes.push_back(node);
          continue;
        }

        if (sourcePath.empty()) {
          // Could be a synthetic mesh or plane
          if (n.value("name", "") == "Ground Plane") {
            fprintf(stderr, "LoadScene: Re-adding default plane\n");
            Scene::AddDefaultPlane(0.0f);

            auto &nodes =
                const_cast<std::vector<Scene::Node> &>(Scene::GetNodes());
            if (!nodes.empty()) {
              auto &node = nodes.back();
              node.visible = n.value("visible", true);
              if (n.contains("transform")) {
                for (int i = 0; i < 16; ++i)
                  node.transform[i] = n["transform"][i];
              }
            }
          }
          continue;
        }

        if (fs::exists(sourcePath)) {
          fprintf(stderr, "LoadScene: importing %s\n", sourcePath.c_str());
          if (Scene::ImportModel(sourcePath)) {
            // After import, the last node added corresponds to this asset
            auto &nodes =
                const_cast<std::vector<Scene::Node> &>(Scene::GetNodes());
            if (!nodes.empty()) {
              auto &node = nodes.back();
              node.name = n.value("name", node.name);
              node.visible = n.value("visible", true);
              if (n.contains("transform")) {
                for (int i = 0; i < 16; ++i)
                  node.transform[i] = n["transform"][i];
              }
              fprintf(
                  stderr,
                  "LoadScene: Successfully imported %s and updated node '%s'\n",
                  sourcePath.c_str(), node.name.c_str());
            }
          } else {
            fprintf(stderr, "LoadScene: Failed to import model %s\n",
                    sourcePath.c_str());
          }
        } else {
          fprintf(stderr, "LoadScene: Asset not found: %s\n",
                  sourcePath.c_str());
        }
      }
    }

    // 6. Restore/Load Materials and build mapping
    std::vector<int> jsonToEngineMat;
    if (j.contains("materials")) {
      auto savedMats = j["materials"];
      fprintf(stderr, "LoadScene: Restoring/Loading %zu materials\n",
              savedMats.size());
      jsonToEngineMat.resize(savedMats.size(), -1);

      for (size_t jIndex = 0; jIndex < savedMats.size(); ++jIndex) {
        const auto &sm = savedMats[jIndex];
        std::string name = sm.value("name", "Material");

        int foundIdx = -1;
        // Search all existing materials by name
        for (size_t mIdx = 0; mIdx < g_loadedMaterials.size(); ++mIdx) {
          if (std::string(g_loadedMaterials[mIdx].name) == name) {
            foundIdx = (int)mIdx;
            break;
          }
        }

        if (foundIdx == -1) {
          fprintf(stderr, "LoadScene: Creating new material '%s'\n",
                  name.c_str());
          Asset::Material newMat;
          strncpy_s(newMat.name, name.c_str(), sizeof(newMat.name) - 1);
          g_loadedMaterials.push_back(newMat);
          foundIdx = (int)g_loadedMaterials.size() - 1;
        }

        jsonToEngineMat[jIndex] = foundIdx;
        auto &mat = g_loadedMaterials[foundIdx];
        fprintf(stderr,
                "LoadScene: Applying properties to material '%s' (engine index "
                "%d)\n",
                mat.name, foundIdx);

        if (sm.contains("diffuseColor"))
          for (int k = 0; k < 4; k++)
            mat.diffuseColor[k] = sm["diffuseColor"][k];
        if (sm.contains("reflectionColor"))
          for (int k = 0; k < 4; k++)
            mat.reflectionColor[k] = sm["reflectionColor"][k];
        mat.reflectionGlossiness =
            sm.value("reflectionGlossiness", mat.reflectionGlossiness);
        mat.metalness = sm.value("metalness", mat.metalness);
        if (sm.contains("refractionColor"))
          for (int k = 0; k < 4; k++)
            mat.refractionColor[k] = sm["refractionColor"][k];
        mat.refractionGlossiness =
            sm.value("refractionGlossiness", mat.refractionGlossiness);
        mat.ior = sm.value("ior", mat.ior);
        if (sm.contains("emissiveColor"))
          for (int k = 0; k < 4; k++)
            mat.emissiveColor[k] = sm["emissiveColor"][k];
        mat.emissiveIntensity =
            sm.value("emissiveIntensity", mat.emissiveIntensity);

        mat.clearcoat = sm.value("clearcoat", mat.clearcoat);
        mat.clearcoatRoughness =
            sm.value("clearcoatRoughness", mat.clearcoatRoughness);
        mat.thinWalled = sm.value("thinWalled", mat.thinWalled);
        mat.translucency = sm.value("translucency", mat.translucency);
        if (sm.contains("uvScale"))
          for (int k = 0; k < 2; k++)
            mat.uvScale[k] = sm["uvScale"][k];
        if (sm.contains("uvOffset"))
          for (int k = 0; k < 2; k++)
            mat.uvOffset[k] = sm["uvOffset"][k];

        mat.triPlanarEnabled =
            sm.value("triPlanarEnabled", mat.triPlanarEnabled);
        mat.triPlanarScale = sm.value("triPlanarScale", mat.triPlanarScale);
        mat.triPlanarSharpness =
            sm.value("triPlanarSharpness", mat.triPlanarSharpness);
        mat.triPlanarNormalStrength =
            sm.value("triPlanarNormalStrength", mat.triPlanarNormalStrength);

        if (sm.contains("diffuseTexture")) {
          int tidx = sm["diffuseTexture"];
          mat.diffuseTexture =
              (tidx >= 0 && tidx < (int)g_loadedTextures.size()) ? tidx : -1;
        }
        if (sm.contains("reflectionTexture")) {
          int tidx = sm["reflectionTexture"];
          mat.reflectionTexture =
              (tidx >= 0 && tidx < (int)g_loadedTextures.size()) ? tidx : -1;
        }
        if (sm.contains("normalTexture")) {
          int tidx = sm["normalTexture"];
          mat.normalTexture =
              (tidx >= 0 && tidx < (int)g_loadedTextures.size()) ? tidx : -1;
        }
        if (sm.contains("emissiveTexture")) {
          int tidx = sm["emissiveTexture"];
          mat.emissiveTexture =
              (tidx >= 0 && tidx < (int)g_loadedTextures.size()) ? tidx : -1;
        }
        if (sm.contains("metalRoughTexture")) {
          int tidx = sm["metalRoughTexture"];
          mat.metalRoughTexture =
              (tidx >= 0 && tidx < (int)g_loadedTextures.size()) ? tidx : -1;
        }
      }
    }

    // 7. Final fix-up for embedded meshes material indices
    if (hasEmbedded) {
      for (size_t i = 0; i < embeddedMeshCount; ++i) {
        int oldIdx = g_loadedMeshes[i].materialIndex;
        if (oldIdx >= 0 && oldIdx < (int)jsonToEngineMat.size()) {
          g_loadedMeshes[i].materialIndex = jsonToEngineMat[oldIdx];
        }
      }
    }

    if (hasEmbedded) {
      fprintf(
          stderr,
          "LoadScene: Rebuilding AS and DXR pipeline for embedded assets\n");
      Scene::RebuildAccelerationStructures();
      DxrRenderer::CreateRayTracingPipeline(0, 0);
    }

    UpdateCameraCB();
    DxrRenderer::ResetAccumulation();
    return true;
  } catch (const std::exception &e) {
    std::cerr << "LoadScene exception: " << e.what() << std::endl;
    return false;
  }
}

} // namespace SceneIO
