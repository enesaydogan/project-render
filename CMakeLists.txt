cmake_minimum_required(VERSION 3.20)
project(project-render LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(project-render src/main.cpp)
# Additional project sources
target_sources(project-render PRIVATE 
    src/camera.cpp 
    src/dxr_renderer.cpp 
    src/dxr_accumulation.cpp
  src/streamline_manager.cpp
    src/raster_renderer.cpp 
    src/file_import.cpp 
    src/scene.cpp 
    src/material_editor.cpp
    src/light.cpp
    src/ibl_manager.cpp
    src/PragueSkyModel.cpp
    src/ImGuizmo.cpp
    src/oidn_denoiser.cpp
)

if (MSVC)
  target_compile_options(project-render PRIVATE /W4 /permissive-)
  # Enable strong optimization for Release builds
  target_compile_options(project-render PRIVATE $<$<CONFIG:Release>:/Ox /Ob2 /Oi /Ot /Oy /GL>)
  target_link_options(project-render PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

target_link_libraries(project-render PRIVATE d3d12 dxgi)

# Link the D3D shader compiler for runtime shader compilation
target_link_libraries(project-render PRIVATE d3dcompiler)
target_link_libraries(project-render PRIVATE comctl32)

# Streamline (DLSS-SR + DLSS-RR) SDK headers (runtime is dynamically loaded)
set(STREAMLINE_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/Streamline")
target_include_directories(project-render PRIVATE "${STREAMLINE_ROOT}/include")

# Optional OpenImageDenoise support (GPU zero-copy requires building OIDN with
# a GPU backend and platform support for D3D12 shared resource import)
option(USE_OPENIMAGEDENOISE "Enable OpenImageDenoise support" OFF)
if (USE_OPENIMAGEDENOISE)
  # Point to the local pre-built binaries in thirdparty/oidn
  set(OIDN_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/oidn")
  set(OpenImageDenoise_DIR "${OIDN_ROOT}/lib/cmake/OpenImageDenoise-2.4.1")
  find_package(OpenImageDenoise CONFIG REQUIRED)

  target_link_libraries(project-render PRIVATE OpenImageDenoise)
  target_compile_definitions(project-render PRIVATE USE_OIDN=1)
  message(STATUS "OpenImageDenoise support enabled (using local binaries at ${OIDN_ROOT})")

  # Define OIDN DLLs to copy to the output directory
  set(OIDN_DLL_DIR "${OIDN_ROOT}/bin")
  set(OIDN_DLLS
    "${OIDN_DLL_DIR}/OpenImageDenoise.dll"
    "${OIDN_DLL_DIR}/OpenImageDenoise_core.dll"
    "${OIDN_DLL_DIR}/OpenImageDenoise_device_cpu.dll"
    "${OIDN_DLL_DIR}/OpenImageDenoise_device_cuda.dll"
    "${OIDN_DLL_DIR}/OpenImageDenoise_device_hip.dll"
    "${OIDN_DLL_DIR}/OpenImageDenoise_device_sycl.dll"
    "${OIDN_DLL_DIR}/sycl8.dll"
    "${OIDN_DLL_DIR}/tbb12.dll"
    "${OIDN_DLL_DIR}/tbbbind.dll"
    "${OIDN_DLL_DIR}/tbbbind_2_0.dll"
    "${OIDN_DLL_DIR}/tbbbind_2_5.dll"
    "${OIDN_DLL_DIR}/ur_adapter_level_zero.dll"
    "${OIDN_DLL_DIR}/ur_loader.dll"
    "${OIDN_DLL_DIR}/ur_win_proxy_loader.dll"
  )

  add_custom_command(TARGET project-render POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${OIDN_DLLS}
      "$<TARGET_FILE_DIR:project-render>/"
    COMMENT "Copying OpenImageDenoise runtime binaries..."
  )
else()
  message(STATUS "OpenImageDenoise support disabled (USE_OPENIMAGEDENOISE=OFF)")
endif()

# Copy Streamline + NGX runtime files next to the executable.
# NOTE: This project uses dynamic loading (LoadLibrary) so we don't link against
# any Streamline import libs, but the DLLs must be present at runtime.
set(STREAMLINE_BIN_DIR "${STREAMLINE_ROOT}/bin/x64")
set(STREAMLINE_DEV_BIN_DIR "${STREAMLINE_ROOT}/bin/x64/")
set(STREAMLINE_SCRIPTS_DIR "${STREAMLINE_ROOT}/scripts")

# DLSS needs NGX. NVIDIA provides "development" NGX DLLs that don't enforce the
# AppID whitelist check (typically with a watermark). For convenience, use those
# automatically in Debug builds.
set(STREAMLINE_RUNTIME_BIN_DIR "$<IF:$<CONFIG:Debug>,${STREAMLINE_DEV_BIN_DIR},${STREAMLINE_BIN_DIR}>")

add_custom_command(TARGET project-render POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STREAMLINE_RUNTIME_BIN_DIR}/sl.interposer.dll"
    "$<TARGET_FILE_DIR:project-render>/sl.interposer.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STREAMLINE_RUNTIME_BIN_DIR}/sl.common.dll"
    "$<TARGET_FILE_DIR:project-render>/sl.common.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STREAMLINE_RUNTIME_BIN_DIR}/sl.dlss.dll"
    "$<TARGET_FILE_DIR:project-render>/sl.dlss.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STREAMLINE_RUNTIME_BIN_DIR}/sl.dlss_d.dll"
    "$<TARGET_FILE_DIR:project-render>/sl.dlss_d.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STREAMLINE_RUNTIME_BIN_DIR}/nvngx_dlss.dll"
    "$<TARGET_FILE_DIR:project-render>/nvngx_dlss.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STREAMLINE_RUNTIME_BIN_DIR}/nvngx_dlssd.dll"
    "$<TARGET_FILE_DIR:project-render>/nvngx_dlssd.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STREAMLINE_RUNTIME_BIN_DIR}/nvngx_dlss.license.txt"
    "$<TARGET_FILE_DIR:project-render>/nvngx_dlss.license.txt"
  # Do NOT ship Streamline's default interposer JSON next to the executable.
  # It overrides Preferences and (in the SDK-provided file) forces showConsole
  # and logPath=C:/NGXLogs. Remove any stale copy from previous builds.
  COMMAND ${CMAKE_COMMAND} -E rm -f
    "$<TARGET_FILE_DIR:project-render>/sl.interposer.json"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STREAMLINE_SCRIPTS_DIR}/sl.common.json"
    "$<TARGET_FILE_DIR:project-render>/sl.common.json"
)

# Fetch and build Dear ImGui at configure time
include(FetchContent)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.89.8
)

# Fetch and build Assimp
FetchContent_Declare(
  assimp
  GIT_REPOSITORY https://github.com/assimp/assimp.git
  GIT_TAG v5.3.1
)

set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(imgui assimp)

# ImGui sources (core + backend for Win32 + DX12)
set(IMGUI_SOURCES
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)

add_library(imgui_static STATIC ${IMGUI_SOURCES})
# Propagate ImGui include dirs to consumers
target_include_directories(imgui_static PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_static PRIVATE d3d12 d3dcompiler)

target_link_libraries(project-render PRIVATE imgui_static)

# Asset loader static library (stub implementation present)
add_library(asset_loader STATIC 
    src/assets/asset_loader.cpp 
    src/assets/asset_loader.h
    src/assets/tinygltf_impl.cpp
)
target_include_directories(asset_loader PUBLIC src/assets)

# VCPKG integration (since packages were installed into the local repos/vcpkg)
set(VCPKG_ROOT "${CMAKE_SOURCE_DIR}/repos/vcpkg")
set(VCPKG_INSTALLED_DIR "${VCPKG_ROOT}/installed/x64-windows")
target_include_directories(asset_loader PUBLIC 
    "${VCPKG_INSTALLED_DIR}/include"
    "${VCPKG_INSTALLED_DIR}/include/miniz"
)
target_link_libraries(asset_loader PRIVATE 
    debug "${VCPKG_INSTALLED_DIR}/debug/lib/tinyexr.lib"
    optimized "${VCPKG_INSTALLED_DIR}/lib/tinyexr.lib"
    debug "${VCPKG_INSTALLED_DIR}/debug/lib/miniz.lib"
    optimized "${VCPKG_INSTALLED_DIR}/lib/miniz.lib"
    assimp::assimp
)

# Copy vcpkg DLLs (e.g., miniz.dll) to the output directory
set(VCPKG_RUNTIME_BIN_DIR "$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/debug/bin,${VCPKG_INSTALLED_DIR}/bin>")
add_custom_command(TARGET project-render POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${VCPKG_RUNTIME_BIN_DIR}/miniz.dll"
    "$<TARGET_FILE_DIR:project-render>/miniz.dll"
  COMMENT "Copying vcpkg runtime binaries (miniz.dll)..."
)

# Optionally enable tinygltf support (install via vcpkg: `vcpkg install tinygltf`)
option(USE_TINYGLTF "Enable tinygltf integration for glTF import (requires tinygltf headers)" ON)
if (USE_TINYGLTF)
  # Try to locate tiny_gltf.h on the system include paths first
  find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h)

  # If not found, try common vcpkg install locations (respect VCPKG_ROOT if set)
  if (NOT TINYGLTF_INCLUDE_DIR)
    if(DEFINED ENV{VCPKG_ROOT})
      find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h PATHS $ENV{VCPKG_ROOT}/installed/x64-windows/include NO_DEFAULT_PATH)
      if(NOT TINYGLTF_INCLUDE_DIR)
        find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h PATHS $ENV{VCPKG_ROOT}/installed/${CMAKE_HOST_SYSTEM_PROCESSOR}-windows/include NO_DEFAULT_PATH)
      endif()
    endif()
  endif()

  if (TINYGLTF_INCLUDE_DIR)
    message(STATUS "Found tinygltf include dir: ${TINYGLTF_INCLUDE_DIR}")
    target_include_directories(asset_loader PUBLIC ${TINYGLTF_INCLUDE_DIR})
    target_compile_definitions(asset_loader PUBLIC USE_TINYGLTF=1)
  else()
    message(WARNING "USE_TINYGLTF enabled but tiny_gltf.h not found. Build will use stub loader.")
  endif()
endif()
target_link_libraries(project-render PRIVATE asset_loader)

# Build as a console app to allow output to be piped to the terminal.
# set_target_properties(project-render PROPERTIES WIN32_EXECUTABLE TRUE)

# Specify the entry point as WinMain to avoid linker errors
if (MSVC)
  target_link_options(project-render PRIVATE /ENTRY:"WinMainCRTStartup")
endif()
