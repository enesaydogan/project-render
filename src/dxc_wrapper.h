#pragma once

#include <d3d12.h>
#include <cstdio>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem> // used for precompiled shader check
#include <windows.h>
#include <wrl.h>

// Try to include dxcapi.h, usually in Windows SDK.
// If this fails, we'd need to provide the full interface definition.
#include <dxcapi.h>

using Microsoft::WRL::ComPtr;

// Typedef for DxcCreateInstance
typedef HRESULT(__stdcall *DxcCreateInstanceProc)(REFCLSID rclsid, REFIID riid,
                                                  LPVOID *ppv);

class DxcHelper {
public:
  DxcHelper() {
    // Load dxcompiler.dll
    m_dxcDll = LoadLibraryW(L"dxcompiler.dll");
    if (!m_dxcDll) {
      // Also try explicit path from SDK if needed, but usually it's in System32
      // or alongside app
      throw std::runtime_error("Failed to load dxcompiler.dll. Ensure it is in "
                               "the executable directory or PATH.");
    }

    m_createInstance =
        (DxcCreateInstanceProc)GetProcAddress(m_dxcDll, "DxcCreateInstance");
    if (!m_createInstance) {
      throw std::runtime_error(
          "Failed to locate DxcCreateInstance in dxcompiler.dll");
    }

    // Initialize Utils
    if (FAILED(m_createInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_utils)))) {
      throw std::runtime_error("Failed to create DXC Utils");
    }

    // Initialize Compiler
    if (FAILED(
            m_createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_compiler)))) {
      throw std::runtime_error("Failed to create DXC Compiler");
    }

    // Initialize Default Include Handler
    if (FAILED(m_utils->CreateDefaultIncludeHandler(&m_includeHandler))) {
      throw std::runtime_error("Failed to create Default Include Handler");
    }
  }

  ~DxcHelper() {
    if (m_dxcDll) {
      FreeLibrary(m_dxcDll);
    }
  }

  // Compile shader to BLOB
  ComPtr<IDxcBlob> Compile(const std::wstring &filename,
                           const std::wstring &entryPoint,
                           const std::wstring &profiles,
                           const std::vector<std::wstring> &defines = {}) {
    // Prefer precompiled bytecode and only compile source as fallback.
    // Runtime deployment expects blobs next to the executable in ./shaders.
    std::filesystem::path requestedPath(filename);
    std::wstring base = requestedPath.stem().wstring();
    if (base.empty()) {
      base = filename;
    }
    if (ComPtr<IDxcBlobEncoding> blobEnc = TryLoadPrecompiledBlob(
            m_utils.Get(), requestedPath, base, entryPoint, profiles)) {
      // IDxcBlobEncoding inherits IDxcBlob.
      return blobEnc;
    }

    // Fall back to source compilation.
    const std::filesystem::path sourcePath = ResolveShaderSourcePath(requestedPath);
    const std::wstring sourceFile = sourcePath.wstring();
    const std::string filenameUtf8 = WStringToUtf8(filename);
    const std::string entryUtf8 =
        entryPoint.empty() ? std::string("<library>") : WStringToUtf8(entryPoint);
    const std::string profileUtf8 = WStringToUtf8(profiles);

    std::string defineList;
    for (size_t i = 0; i < defines.size(); ++i) {
      if (i > 0) {
        defineList += ",";
      }
      defineList += WStringToUtf8(defines[i]);
    }
    if (defineList.empty()) {
      defineList = "<none>";
    }

    const std::string shaderTag = "file=\"" + filenameUtf8 + "\" entry=\"" +
                                  entryUtf8 + "\" profile=\"" + profileUtf8 +
                                  "\" defines=\"" + defineList + "\"";
    LogShaderCompileMessage("ShaderCompile START: " + shaderTag);

    std::vector<LPCWSTR> args;

    // Basic arguments
    args.push_back(sourceFile.c_str());
    if (!entryPoint.empty()) {
      args.push_back(L"-E");
      args.push_back(entryPoint.c_str());
    }
    args.push_back(L"-T");
    args.push_back(profiles.c_str());

    // Debug flags?
#ifdef _DEBUG
    args.push_back(L"-Zi");
    args.push_back(L"-Qembed_debug");
    args.push_back(L"-Od");
#else
    // Optional for Nsight in Release/optimized builds:
    // set NSIGHT_SHADER_DEBUG=1 to embed shader debug symbols.
    wchar_t nsightDebugEnv[16] = {};
    DWORD nsightDebugLen = GetEnvironmentVariableW(
        L"NSIGHT_SHADER_DEBUG", nsightDebugEnv,
        static_cast<DWORD>(std::size(nsightDebugEnv)));
    if (nsightDebugLen > 0 && wcscmp(nsightDebugEnv, L"0") != 0) {
      args.push_back(L"-Zi");
      args.push_back(L"-Qembed_debug");
      // Keep optimization in Release while still embedding symbols.
      args.push_back(L"-Zss");
    }
#endif

    // Add defines
    for (const auto &def : defines) {
      args.push_back(L"-D");
      args.push_back(def.c_str());
    }

    // Load source
    ComPtr<IDxcBlobEncoding> pSource;
    if (FAILED(m_utils->LoadFile(sourceFile.c_str(), nullptr, &pSource))) {
      LogShaderCompileMessage("ShaderCompile FAIL: " + shaderTag +
                              " reason=\"Failed to load shader file\"");
      throw std::runtime_error("Failed to load shader file: " + filenameUtf8);
    }

    DxcBuffer sourceBuffer;
    sourceBuffer.Ptr = pSource->GetBufferPointer();
    sourceBuffer.Size = pSource->GetBufferSize();
    sourceBuffer.Encoding = DXC_CP_ACP; // Assume ANSI/UTF-8

    ComPtr<IDxcResult> pResults;

    HRESULT compileHr =
        m_compiler->Compile(&sourceBuffer, args.data(), (UINT32)args.size(),
                            m_includeHandler.Get(), IID_PPV_ARGS(&pResults));

    if (FAILED(compileHr) || !pResults) {
      LogShaderCompileMessage("ShaderCompile FAIL: " + shaderTag +
                              " compileHr=" + HrToHex(compileHr) +
                              " reason=\"DXC Compile call failed\"");
      throw std::runtime_error("Shader compilation invocation failed: " +
                               filenameUtf8);
    }

    ComPtr<IDxcBlobUtf8> pErrors;
    HRESULT errorOutputHr =
        pResults->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
    std::string compilerMessages;
    if (SUCCEEDED(errorOutputHr) && pErrors && pErrors->GetStringLength() > 0) {
      compilerMessages.assign((char *)pErrors->GetStringPointer(),
                              pErrors->GetStringLength());
    }

    HRESULT hrStatus = E_FAIL;
    HRESULT statusHr = pResults->GetStatus(&hrStatus);
    if (FAILED(statusHr)) {
      hrStatus = compileHr;
    }

    if (FAILED(hrStatus)) {
      std::string fail = "ShaderCompile FAIL: " + shaderTag +
                         " compileHr=" + HrToHex(compileHr) +
                         " statusHr=" + HrToHex(statusHr) +
                         " resultHr=" + HrToHex(hrStatus);
      if (!compilerMessages.empty()) {
        fail += "\n";
        fail += compilerMessages;
      }
      LogShaderCompileMessage(fail);
      throw std::runtime_error("Shader compilation failed: " + filenameUtf8 +
                               " [" + entryUtf8 + " / " + profileUtf8 + "]");
    }

    if (!compilerMessages.empty()) {
      LogShaderCompileMessage("ShaderCompile INFO: " + shaderTag +
                              " compilerMessages=\n" + compilerMessages);
    }

    ComPtr<IDxcBlob> pBlob;
    HRESULT blobHr = pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pBlob),
                                         nullptr);
    if (FAILED(blobHr) || !pBlob) {
      LogShaderCompileMessage("ShaderCompile FAIL: " + shaderTag +
                              " reason=\"Missing shader object blob\" blobHr=" +
                              HrToHex(blobHr));
      throw std::runtime_error("Shader compile output blob missing: " +
                               filenameUtf8);
    }

    LogShaderCompileMessage(
        "ShaderCompile OK: " + shaderTag +
        " bytecodeBytes=" + std::to_string((size_t)pBlob->GetBufferSize()));
    return pBlob;
  }

private:
  static std::filesystem::path GetExecutableDirectory() {
    wchar_t modulePath[MAX_PATH] = {};
    DWORD len =
        GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (len == 0 || len >= std::size(modulePath)) {
      return {};
    }
    return std::filesystem::path(modulePath).parent_path();
  }

  static void AppendUniquePath(std::vector<std::filesystem::path> &paths,
                               const std::filesystem::path &candidate) {
    if (candidate.empty()) {
      return;
    }
    const std::filesystem::path normalized = candidate.lexically_normal();
    for (const auto &existing : paths) {
      if (existing == normalized) {
        return;
      }
    }
    paths.push_back(normalized);
  }

  static bool ShaderDirNameEquals(const std::filesystem::path &segment,
                                  const wchar_t *name) {
    if (segment.empty() || !name) {
      return false;
    }
    return _wcsicmp(segment.wstring().c_str(), name) == 0;
  }

  static std::filesystem::path StripLeadingShaderDirectory(
      const std::filesystem::path &path) {
    if (path.empty() || path.is_absolute()) {
      return path;
    }

    auto it = path.begin();
    if (it == path.end()) {
      return path;
    }

    if (!ShaderDirNameEquals(*it, L"shaders") &&
        !ShaderDirNameEquals(*it, L"shader")) {
      return path;
    }

    ++it;
    std::filesystem::path stripped;
    for (; it != path.end(); ++it) {
      stripped /= *it;
    }
    return stripped;
  }

  static std::vector<std::filesystem::path>
  BuildBlobSearchDirectories(const std::filesystem::path &requestedPath) {
    std::vector<std::filesystem::path> dirs;
    const std::filesystem::path exeDir = GetExecutableDirectory();
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    const std::filesystem::path relNoShaderPrefix =
        StripLeadingShaderDirectory(requestedPath);

    // Preferred deployment location: <exe>/shaders
    AppendUniquePath(dirs, exeDir / L"shaders");
    AppendUniquePath(dirs, cwd / L"shaders");
    // Compatibility fallback: <exe>/shader
    AppendUniquePath(dirs, exeDir / L"shader");
    AppendUniquePath(dirs, cwd / L"shader");

    if (requestedPath.has_parent_path()) {
      if (requestedPath.is_absolute()) {
        AppendUniquePath(dirs, requestedPath.parent_path());
      } else {
        AppendUniquePath(dirs, exeDir / requestedPath.parent_path());
        AppendUniquePath(dirs, cwd / requestedPath.parent_path());
      }
    }

    if (relNoShaderPrefix.has_parent_path()) {
      AppendUniquePath(dirs, exeDir / L"shaders" / relNoShaderPrefix.parent_path());
      AppendUniquePath(dirs, cwd / L"shaders" / relNoShaderPrefix.parent_path());
      AppendUniquePath(dirs, exeDir / L"shader" / relNoShaderPrefix.parent_path());
      AppendUniquePath(dirs, cwd / L"shader" / relNoShaderPrefix.parent_path());
    }

    return dirs;
  }

  static bool MatchesBlobPrefix(const std::wstring &fileName,
                                const std::wstring &prefix) {
    if (fileName.size() <= prefix.size() + 4) {
      return false;
    }
    if (fileName.rfind(prefix, 0) != 0) {
      return false;
    }
    if (fileName.size() < 4 ||
        _wcsicmp(fileName.substr(fileName.size() - 4).c_str(), L".cso") != 0) {
      return false;
    }
    return true;
  }

  static int ParseProfileScore(const std::wstring &profile) {
    // Expected forms: cs_6_3, ps_6_0, lib_6_5, etc.
    size_t stageSep = profile.find(L'_');
    if (stageSep == std::wstring::npos) {
      return -1;
    }
    size_t minorSep = profile.find(L'_', stageSep + 1);
    if (minorSep == std::wstring::npos) {
      return -1;
    }
    try {
      const int major = std::stoi(profile.substr(stageSep + 1, minorSep - stageSep - 1));
      const int minor = std::stoi(profile.substr(minorSep + 1));
      return major * 100 + minor;
    } catch (...) {
      return -1;
    }
  }

  static std::filesystem::path FindClosestProfileBlob(
      const std::vector<std::filesystem::path> &dirs,
      const std::wstring &prefix,
      const std::wstring &requestedProfile) {
    std::filesystem::path bestPath;
    int bestScore = -1;
    const int requestedScore = ParseProfileScore(requestedProfile);

    for (const auto &dir : dirs) {
      std::error_code ec;
      if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        continue;
      }

      for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file()) {
          continue;
        }
        const std::wstring name = entry.path().filename().wstring();
        if (!MatchesBlobPrefix(name, prefix)) {
          continue;
        }

        const size_t profileStart = prefix.size();
        const size_t profileLen = name.size() - profileStart - 4; // strip ".cso"
        const std::wstring candidateProfile = name.substr(profileStart, profileLen);
        int score = ParseProfileScore(candidateProfile);
        if (score < 0) {
          score = 0;
        }

        // Prefer equal/newer profile versions, fall back to highest available.
        if (requestedScore >= 0 && score < requestedScore) {
          score -= 1000;
        }

        if (score > bestScore) {
          bestScore = score;
          bestPath = entry.path();
        }
      }
    }

    return bestPath;
  }

  static ComPtr<IDxcBlobEncoding> TryLoadPrecompiledBlob(
      IDxcUtils *utils, const std::filesystem::path &requestedPath,
      const std::wstring &base, const std::wstring &entryPoint,
      const std::wstring &profiles) {
    if (!utils || base.empty()) {
      return {};
    }

    const std::wstring prefix = base + L"_" + entryPoint + L"_";
    const std::wstring exactName = prefix + profiles + L".cso";
    const auto dirs = BuildBlobSearchDirectories(requestedPath);

    for (const auto &dir : dirs) {
      const std::filesystem::path csoPath = dir / exactName;
      std::error_code ec;
      if (!std::filesystem::exists(csoPath, ec)) {
        continue;
      }
      ComPtr<IDxcBlobEncoding> blobEnc;
      if (SUCCEEDED(utils->LoadFile(csoPath.wstring().c_str(), nullptr, &blobEnc))) {
        return blobEnc;
      }
    }

    // If exact profile is missing (e.g., asking for cs_6_3 when only cs_6_5
    // was packaged), load the closest available profile for the same shader.
    const std::filesystem::path altPath =
        FindClosestProfileBlob(dirs, prefix, profiles);
    if (!altPath.empty()) {
      ComPtr<IDxcBlobEncoding> blobEnc;
      if (SUCCEEDED(utils->LoadFile(altPath.wstring().c_str(), nullptr, &blobEnc))) {
        return blobEnc;
      }
    }

    return {};
  }

  static std::filesystem::path
  ResolveShaderSourcePath(const std::filesystem::path &requestedPath) {
    if (requestedPath.empty()) {
      return requestedPath;
    }

    std::error_code ec;
    if (std::filesystem::exists(requestedPath, ec)) {
      return requestedPath;
    }

    const std::filesystem::path exeDir = GetExecutableDirectory();
    const std::filesystem::path relNoShaderPrefix =
        StripLeadingShaderDirectory(requestedPath);

    std::vector<std::filesystem::path> candidates;
    AppendUniquePath(candidates, exeDir / requestedPath);
    AppendUniquePath(candidates, exeDir / L"shaders" / relNoShaderPrefix);
    AppendUniquePath(candidates, exeDir / L"shader" / relNoShaderPrefix);
    AppendUniquePath(candidates, std::filesystem::current_path(ec) / requestedPath);
    AppendUniquePath(candidates,
                     std::filesystem::current_path(ec) / L"shaders" / relNoShaderPrefix);
    AppendUniquePath(candidates,
                     std::filesystem::current_path(ec) / L"shader" / relNoShaderPrefix);

    for (const auto &candidate : candidates) {
      if (std::filesystem::exists(candidate, ec)) {
        return candidate;
      }
    }
    return requestedPath;
  }

  static std::string WStringToUtf8(const std::wstring &text) {
    if (text.empty()) {
      return {};
    }

    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                         (int)text.size(), nullptr, 0, nullptr,
                                         nullptr);
    if (sizeNeeded <= 0) {
      return "<wide-char-convert-failed>";
    }

    std::string out((size_t)sizeNeeded, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0],
                        sizeNeeded, nullptr, nullptr);
    return out;
  }

  static std::string HrToHex(HRESULT hr) {
    char buf[32] = {};
    sprintf_s(buf, "0x%08X", (unsigned)hr);
    return buf;
  }

  // Logging is disabled because shaders are precompiled; runtime compilation
  // messages are no longer needed and clutter the console.
  static void LogShaderCompileMessage(const std::string &message) {
    (void)message;
  }

  HMODULE m_dxcDll = nullptr;
  DxcCreateInstanceProc m_createInstance = nullptr;

  ComPtr<IDxcUtils> m_utils;
  ComPtr<IDxcCompiler3> m_compiler;
  ComPtr<IDxcIncludeHandler> m_includeHandler;
};
