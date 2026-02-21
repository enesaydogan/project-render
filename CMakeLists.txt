cmake_minimum_required(VERSION 3.20)
project(project-render LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(project-render src/main.cpp)
# Additional project sources
target_sources(project-render PRIVATE 
    src/editor_ui.cpp
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
    src/clouds.cpp
    src/imgui_theme.cpp
    src/scene_io.cpp
)

if (MSVC)
  target_compile_options(project-render PRIVATE /W4 /permissive-)
  # Enable strong optimization for Release builds
  target_compile_options(project-render PRIVATE $<$<CONFIG:Release>:/Ox /Ob2 /Oi /Ot /Oy /GL>)
  target_link_options(project-render PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

target_link_libraries(project-render PRIVATE d3d12 dxgi dxguid ole32 windowscodecs)

# Link the D3D shader compiler for runtime shader compilation
target_link_libraries(project-render PRIVATE d3dcompiler)
target_link_libraries(project-render PRIVATE comctl32)

# Streamline (DLSS-SR + DLSS-RR) SDK headers (runtime is dynamically loaded)
set(STREAMLINE_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/Streamline")
# Agility SDK headers (includes d3dx12 helper library)
target_include_directories(project-render PRIVATE "${STREAMLINE_ROOT}/external/dx-agility-sdk-headers-1.714.0-preview/include")
target_include_directories(project-render PRIVATE "${STREAMLINE_ROOT}/include")
target_include_directories(project-render PRIVATE "${STREAMLINE_ROOT}/external/json/include")

# vcpkg roots (used by asset_loader and runtime copy steps)
set(VCPKG_ROOT "${CMAKE_SOURCE_DIR}/repos/vcpkg")
set(VCPKG_INSTALLED_DIR "${VCPKG_ROOT}/installed/x64-windows")


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
  GIT_TAG master
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
option(USE_SKETCHUP_SDK "Enable SketchUp SDK support for .skp import" OFF)
# If a bundled SDK exists in the repo, hint about it at configure time
if (EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/sketchup_sdk/headers")
  message(STATUS "Bundled SketchUp SDK detected at ${CMAKE_SOURCE_DIR}/thirdparty/sketchup_sdk (set -DUSE_SKETCHUP_SDK=ON to enable .skp import)")
endif()
add_library(asset_loader STATIC 
    src/assets/asset_loader.cpp 
    src/assets/asset_loader.h
    src/assets/tinygltf_impl.cpp
    src/assets/skp_loader.cpp
)
if (USE_SKETCHUP_SDK)
  # If you enable this option you may provide SKETCHUP_SDK_ROOT or set
  # SKETCHUP_SDK_INCLUDE in your environment. We also search the bundled
  # thirdparty/sketchup_sdk/headers folder as a fallback.
  find_path(
    SKETCHUP_SDK_INCLUDE
    NAMES SketchUpAPI/model/model.h SketchUpAPI/model.h
    PATHS $ENV{SKETCHUP_SDK_ROOT}/include ${CMAKE_SOURCE_DIR}/thirdparty/sketchup_sdk/headers ${CMAKE_SOURCE_DIR}/thirdparty/sketchup_sdk/include
    NO_DEFAULT_PATH
  )
  if (SKETCHUP_SDK_INCLUDE)
    message(STATUS "SketchUp SDK headers found: ${SKETCHUP_SDK_INCLUDE}")
    target_include_directories(asset_loader PUBLIC ${SKETCHUP_SDK_INCLUDE})
    target_compile_definitions(asset_loader PUBLIC USE_SKETCHUP_SDK=1)

    # Try to locate SketchUp SDK import library (bundled or system-installed)
    find_library(
      SKETCHUP_SDK_LIBRARY
      NAMES SketchUpAPI sketchup
      PATHS ${CMAKE_SOURCE_DIR}/thirdparty/sketchup_sdk/binaries/sketchup/x64 $ENV{SKETCHUP_SDK_ROOT}/lib
      NO_DEFAULT_PATH
    )
    if (SKETCHUP_SDK_LIBRARY)
      message(STATUS "SketchUp SDK library found: ${SKETCHUP_SDK_LIBRARY}")
      target_link_libraries(asset_loader PRIVATE ${SKETCHUP_SDK_LIBRARY})

      # Copy SketchUp runtime DLLs to output so SUInitialize() and preference
      # support can load them at runtime. Copy the API DLL and (if present)
      # the CommonPreferences helper DLL.
      add_custom_command(TARGET project-render POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/thirdparty/sketchup_sdk/binaries/sketchup/x64/SketchUpAPI.dll
          $<TARGET_FILE_DIR:project-render>
        COMMENT "Copying SketchUp SDK runtime DLL..."
      )

      if (EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/sketchup_sdk/binaries/sketchup/x64/SketchUpCommonPreferences.dll")
        add_custom_command(TARGET project-render POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/thirdparty/sketchup_sdk/binaries/sketchup/x64/SketchUpCommonPreferences.dll
            $<TARGET_FILE_DIR:project-render>
          COMMENT "Copying SketchUpCommonPreferences.dll to output..."
        )
      endif()
    else()
      message(WARNING "SketchUp SDK headers found but import library not located. Set SKETCHUP_SDK_ROOT or add the SDK lib path to avoid link errors.")
    endif()
  else()
    message(WARNING "USE_SKETCHUP_SDK=ON but SketchUp SDK headers not found. Falling back to stub.")
  endif()
endif()

target_include_directories(asset_loader PUBLIC src/assets)

# vcpkg integration (packages installed under repos/vcpkg)
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

# Workaround: ensure asset_loader objects are pulled into the final link on MSVC.
# This forces the linker to include all members from the static lib so symbols
# referenced from other object files are resolved.
if(MSVC)
  target_link_options(project-render PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:asset_loader>")
endif()

# Add the Prague Sky Model compute shader to the build
set(PRAGUE_SKY_SHADER "shaders/prague_sky_cs.hlsl")
if (EXISTS "${CMAKE_SOURCE_DIR}/${PRAGUE_SKY_SHADER}")
  source_group("Shaders" FILES ${PRAGUE_SKY_SHADER})

  # Ensure the shader is copied to the output directory
  add_custom_command(
      TARGET project-render POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${CMAKE_SOURCE_DIR}/${PRAGUE_SKY_SHADER}
              $<TARGET_FILE_DIR:project-render>
  )
else()
  message(WARNING "Prague sky shader not found at ${CMAKE_SOURCE_DIR}/${PRAGUE_SKY_SHADER} — skipping copy.")
endif()

# Build as a console app to allow output to be piped to the terminal.
# set_target_properties(project-render PROPERTIES WIN32_EXECUTABLE TRUE)

# Specify the entry point as WinMain to avoid linker errors
if (MSVC)
  target_link_options(project-render PRIVATE /ENTRY:"WinMainCRTStartup")
endif()
