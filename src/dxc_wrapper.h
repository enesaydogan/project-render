#pragma once

#include <d3d12.h>
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
      throw std::runtime_error("Failed to load shader file");
    }

    DxcBuffer sourceBuffer;
    sourceBuffer.Ptr = pSource->GetBufferPointer();
    sourceBuffer.Size = pSource->GetBufferSize();
    sourceBuffer.Encoding = DXC_CP_ACP; // Assume ANSI/UTF-8

    ComPtr<IDxcResult> pResults;

    HRESULT compileHr =
        m_compiler->Compile(&sourceBuffer, args.data(), (UINT32)args.size(),
                            m_includeHandler.Get(), IID_PPV_ARGS(&pResults));

    (void)compileHr;

    ComPtr<IDxcBlobUtf8> pErrors;
    pResults->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
    if (pErrors && pErrors->GetStringLength() > 0) {
      OutputDebugStringA((char *)pErrors->GetStringPointer());
      std::cerr << "Shader compile errors: "
                << (char *)pErrors->GetStringPointer() << std::endl;
    }

    HRESULT hrStatus;
    pResults->GetStatus(&hrStatus);
    if (FAILED(hrStatus)) {
      throw std::runtime_error("Shader compilation failed");
    }

    ComPtr<IDxcBlob> pBlob;
    pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pBlob), nullptr);
    return pBlob;
  }

private:
  HMODULE m_dxcDll = nullptr;
  DxcCreateInstanceProc m_createInstance = nullptr;

  ComPtr<IDxcUtils> m_utils;
  ComPtr<IDxcCompiler3> m_compiler;
  ComPtr<IDxcIncludeHandler> m_includeHandler;
};
