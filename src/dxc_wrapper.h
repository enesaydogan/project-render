#pragma once

#include <d3d12.h>
#include <cstdio>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
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
    args.push_back(filename.c_str());
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
    if (FAILED(m_utils->LoadFile(filename.c_str(), nullptr, &pSource))) {
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

  static void LogShaderCompileMessage(const std::string &message) {
    std::string line = message;
    if (line.empty() || line.back() != '\n') {
      line.push_back('\n');
    }

    OutputDebugStringA(line.c_str());
    fprintf(stderr, "%s", line.c_str());

    FILE *logFile = nullptr;
    if (fopen_s(&logFile, "error.log", "a") == 0 && logFile) {
      fprintf(logFile, "%s", line.c_str());
      fclose(logFile);
    }
  }

  HMODULE m_dxcDll = nullptr;
  DxcCreateInstanceProc m_createInstance = nullptr;

  ComPtr<IDxcUtils> m_utils;
  ComPtr<IDxcCompiler3> m_compiler;
  ComPtr<IDxcIncludeHandler> m_includeHandler;
};
