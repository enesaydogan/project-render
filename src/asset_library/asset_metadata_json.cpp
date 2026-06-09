#include "asset_metadata_json.h"

namespace assetlib {

nlohmann::json MetadataToJson(const AssetMetadata &m) {
  nlohmann::json j;
  j["id"] = m.id.ToString();
  j["type"] = AssetTypeToString(m.type);
  j["displayName"] = m.displayName;
  j["virtualPath"] = m.virtualPath;
  j["sourcePath"] = m.sourcePath;
  j["sourceContentHash"] = m.sourceContentHash;
  j["sourceTimestamp"] = m.sourceTimestamp;
  j["cookerVersion"] = m.cookerVersion;
  j["cookedPayloadHash"] = m.cookedPayloadHash;
  j["cookState"] = CookStateToString(m.cookState);
  nlohmann::json deps = nlohmann::json::array();
  for (const auto &d : m.dependencies)
    deps.push_back(d.ToString());
  j["dependencies"] = std::move(deps);
  j["tags"] = m.tags;
  j["thumbnailRef"] = m.thumbnailRef;
  j["importSettings"] = m.importSettingsJson;
  j["license"] = m.license;
  j["attribution"] = m.attribution;
  return j;
}

bool MetadataFromJson(const nlohmann::json &j, AssetMetadata &m) {
  if (!j.is_object() || !j.contains("id"))
    return false;
  if (!AssetId::FromString(j.value("id", std::string()), m.id))
    return false;
  m.type = AssetTypeFromString(j.value("type", std::string("unknown")));
  m.displayName = j.value("displayName", std::string());
  m.virtualPath = j.value("virtualPath", std::string());
  m.sourcePath = j.value("sourcePath", std::string());
  m.sourceContentHash = j.value("sourceContentHash", uint64_t{0});
  m.sourceTimestamp = j.value("sourceTimestamp", int64_t{0});
  m.cookerVersion = j.value("cookerVersion", uint32_t{0});
  m.cookedPayloadHash = j.value("cookedPayloadHash", uint64_t{0});
  m.cookState = CookStateFromString(j.value("cookState", std::string()));
  if (j.contains("dependencies") && j["dependencies"].is_array()) {
    for (const auto &d : j["dependencies"]) {
      AssetId dep;
      if (d.is_string() && AssetId::FromString(d.get<std::string>(), dep))
        m.dependencies.push_back(dep);
    }
  }
  if (j.contains("tags") && j["tags"].is_array()) {
    for (const auto &t : j["tags"])
      if (t.is_string())
        m.tags.push_back(t.get<std::string>());
  }
  m.thumbnailRef = j.value("thumbnailRef", std::string());
  m.importSettingsJson = j.value("importSettings", std::string());
  m.license = j.value("license", std::string());
  m.attribution = j.value("attribution", std::string());
  return true;
}

} // namespace assetlib
