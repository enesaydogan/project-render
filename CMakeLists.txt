cmake_minimum_required(VERSION 3.20)
project(project-render VERSION 0.1.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer vendored or already-populated FetchContent sources before attempting
# fresh downloads. This keeps clean configure working on machines that already
# have dependencies staged locally and reduces TLS/network-related failures.
function(project_render_prefer_fetchcontent_source dep_name candidate_dir)
  string(TOUPPER "${dep_name}" dep_name_upper)
  if(EXISTS "${candidate_dir}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_${dep_name_upper}
        "${candidate_dir}"
        CACHE PATH "Local source override for ${dep_name}" FORCE)
    message(STATUS "Using local ${dep_name} source: ${candidate_dir}")
  endif()
endfunction()

project_render_prefer_fetchcontent_source("imgui"      "${CMAKE_SOURCE_DIR}/build/_deps/imgui-src")
project_render_prefer_fetchcontent_source("assimp"     "${CMAKE_SOURCE_DIR}/build/_deps/assimp-src")
project_render_prefer_fetchcontent_source("shadermake" "${CMAKE_SOURCE_DIR}/build/_deps/shadermake-src")
project_render_prefer_fetchcontent_source("mathlib"    "${CMAKE_SOURCE_DIR}/build/_deps/mathlib-src")
project_render_prefer_fetchcontent_source("nri"        "${CMAKE_SOURCE_DIR}/build/_deps/nri-src")
project_render_prefer_fetchcontent_source("d3d12ma"    "${CMAKE_SOURCE_DIR}/build/_deps/d3d12ma-src")
project_render_prefer_fetchcontent_source("shadermake" "${CMAKE_SOURCE_DIR}/thirdparty/ShaderMake")
project_render_prefer_fetchcontent_source("mathlib"    "${CMAKE_SOURCE_DIR}/thirdparty/MathLib")
project_render_prefer_fetchcontent_source("nri"        "${CMAKE_SOURCE_DIR}/thirdparty/NRI")

if(WIN32)
  if(DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
    set(PROJECT_RENDER_WINDOWS_SDK_VERSION "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
  elseif(DEFINED ENV{WindowsSDKLibVersion})
    string(REGEX REPLACE "\\\\$" "" PROJECT_RENDER_WINDOWS_SDK_VERSION "$ENV{WindowsSDKLibVersion}")
  endif()

  get_filename_component(PROJECT_RENDER_WINDOWS_SDK_ROOT
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]"
    ABSOLUTE)

  set(PROJECT_RENDER_SDK_DXC "")
  set(PROJECT_RENDER_SDK_FXC "")

  if(PROJECT_RENDER_WINDOWS_SDK_ROOT AND PROJECT_RENDER_WINDOWS_SDK_VERSION)
    if(IS_DIRECTORY "${PROJECT_RENDER_WINDOWS_SDK_ROOT}/bin/${PROJECT_RENDER_WINDOWS_SDK_VERSION}.0")
      set(PROJECT_RENDER_WINDOWS_SDK_VERSION "${PROJECT_RENDER_WINDOWS_SDK_VERSION}.0")
    endif()

    set(PROJECT_RENDER_WINDOWS_SDK_BIN
      "${PROJECT_RENDER_WINDOWS_SDK_ROOT}/bin/${PROJECT_RENDER_WINDOWS_SDK_VERSION}/x64")
    find_program(PROJECT_RENDER_SDK_DXC NAMES dxc HINTS "${PROJECT_RENDER_WINDOWS_SDK_BIN}" NO_DEFAULT_PATH)
    find_program(PROJECT_RENDER_SDK_FXC NAMES fxc HINTS "${PROJECT_RENDER_WINDOWS_SDK_BIN}" NO_DEFAULT_PATH)
  endif()

  if(NOT PROJECT_RENDER_SDK_DXC)
    file(GLOB PROJECT_RENDER_WINDOWS_DXC_CANDIDATES
      "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxc.exe")
    if(PROJECT_RENDER_WINDOWS_DXC_CANDIDATES)
      list(SORT PROJECT_RENDER_WINDOWS_DXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
      list(GET PROJECT_RENDER_WINDOWS_DXC_CANDIDATES 0 PROJECT_RENDER_SDK_DXC)
    endif()
  endif()

  if(NOT PROJECT_RENDER_SDK_FXC)
    file(GLOB PROJECT_RENDER_WINDOWS_FXC_CANDIDATES
      "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe")
    if(PROJECT_RENDER_WINDOWS_FXC_CANDIDATES)
      list(SORT PROJECT_RENDER_WINDOWS_FXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
      list(GET PROJECT_RENDER_WINDOWS_FXC_CANDIDATES 0 PROJECT_RENDER_SDK_FXC)
    endif()
  endif()

  if(PROJECT_RENDER_SDK_DXC)
    set(SHADERMAKE_FIND_DXC OFF CACHE BOOL "" FORCE)
    set(SHADERMAKE_DXC_PATH "${PROJECT_RENDER_SDK_DXC}" CACHE FILEPATH "" FORCE)
    message(STATUS "Using DXC from Windows SDK for ShaderMake: ${PROJECT_RENDER_SDK_DXC}")
  endif()

  if(PROJECT_RENDER_SDK_FXC)
    set(SHADERMAKE_FIND_FXC OFF CACHE BOOL "" FORCE)
    set(SHADERMAKE_FXC_PATH "${PROJECT_RENDER_SDK_FXC}" CACHE FILEPATH "" FORCE)
    message(STATUS "Using FXC from Windows SDK for ShaderMake: ${PROJECT_RENDER_SDK_FXC}")
  endif()
endif()

# Qt UI integration option
option(USE_QT_UI "Enable Qt-based user interface" OFF)

if(USE_QT_UI)
  # attempt to locate Qt; if not found we disable the option gracefully
  find_package(Qt6 COMPONENTS Widgets)
  if(NOT Qt6_FOUND)
    message(WARNING "USE_QT_UI is ON but Qt6 not found. Disabling Qt UI.")
    set(USE_QT_UI OFF CACHE BOOL "Enable Qt-based user interface" FORCE)
  else()
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
    add_compile_definitions(USE_QT_UI=1)
    message(STATUS "Qt UI enabled")
  endif()
else()
  message(STATUS "Qt UI disabled (USE_QT_UI=OFF)")
endif()

add_executable(project-render src/main.cpp)

# when Qt is enabled, additional source files will be added later

# expose version information to the code as a preprocessor macro
# PROJECT_VERSION is automatically populated from project(... VERSION ...)
# no escape backslash required; CMake splits on whitespace
target_compile_definitions(project-render PRIVATE
    APP_VERSION="${PROJECT_VERSION}")
# Additional project sources
target_sources(project-render PRIVATE 
    src/input_handler.cpp
    src/dx12_context.cpp
    src/editor_ui.cpp
    src/camera.cpp
    src/dxr_renderer.cpp
    src/nrd_denoiser.cpp
    src/dxr_accumulation.cpp
    src/streamline_manager.cpp
    src/raster_renderer.cpp 
    src/file_import.cpp 
    src/scene.cpp 
    src/material_editor.cpp
    src/material/material_io.cpp
    src/material/material_livelink.cpp
    src/material/material_system.cpp
    src/light.cpp
    src/ibl_manager.cpp
    src/PragueSkyModel.cpp
    src/ImGuizmo.cpp
    src/oidn_denoiser.cpp
    src/clouds.cpp
    src/imgui_theme.cpp
    src/scene_io.cpp
    src/animation_sequence.cpp
    src/saved_views.cpp
    src/grass_manager.cpp
    src/livelink/livelink_types.cpp
    src/livelink/livelink_coordinator.cpp
    src/livelink/livelink_mock_provider.cpp
    src/livelink/livelink_pipe_provider.cpp
    src/livelink/livelink_scene_sync.cpp
    src/livelink/livelink_runtime.cpp
)

if(USE_QT_UI)
  target_sources(project-render PRIVATE
  src/qt/AnimationPanel.cpp
  src/qt/CameraPanel.cpp
  src/qt/EnvironmentPanel.cpp
      src/qt/MainWindow.cpp
      src/qt/DX12View.cpp
      src/qt/QtTheme.cpp
      src/qt/RenderPanel.cpp
      src/qt/RenderSettingsPanel.cpp
      src/qt/RenderModePanel.cpp
      src/qt/ScenePanel.cpp
      src/qt/LightsPanel.cpp
      src/qt/MaterialEditorPanel.cpp
      src/qt/ViewsPanel.cpp
      # additional panel widgets will be added here later
  )
  target_link_libraries(project-render PRIVATE Qt6::Widgets)
  target_include_directories(project-render PRIVATE ${CMAKE_SOURCE_DIR}/src/qt)

  if(WIN32)
    if(TARGET Qt6::qmake)
      get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
      get_filename_component(QT_WINDEPLOYQT_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
      set(QT_WINDEPLOYQT_EXECUTABLE "${QT_WINDEPLOYQT_DIR}/windeployqt.exe")
    else()
      get_filename_component(QT_INSTALL_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
      set(QT_WINDEPLOYQT_EXECUTABLE "${QT_INSTALL_PREFIX}/bin/windeployqt.exe")
    endif()

    if(EXISTS "${QT_WINDEPLOYQT_EXECUTABLE}")
      add_custom_command(TARGET project-render POST_BUILD
        COMMAND "${QT_WINDEPLOYQT_EXECUTABLE}" --no-translations --no-opengl-sw "$<TARGET_FILE:project-render>"
        COMMENT "Deploying Qt runtime..."
      )
      add_custom_command(TARGET project-render POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "$<TARGET_FILE_DIR:project-render>/networkinformation"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "$<TARGET_FILE_DIR:project-render>/tls"
        COMMENT "Removing unused Qt plugins (networkinformation, tls)"
      )
    else()
      message(WARNING "Qt UI is enabled but windeployqt.exe was not found. Qt runtime deployment will need to be handled manually.")
    endif()
  endif()
endif()

if (MSVC)
  target_compile_options(project-render PRIVATE /W4 /permissive-)
  # Enable strong optimization for Release builds and use AVX2 when available
  target_compile_options(project-render PRIVATE $<$<CONFIG:Release>:/Ox /Ob2 /Oi /Ot /Oy /GL /arch:AVX2>)
  target_link_options(project-render PRIVATE $<$<CONFIG:Release>:/LTCG>)
else()
  # Non-MSVC toolchains: enable high optimization and AVX2/FMA where supported
  add_compile_options(-O3 -march=native -mavx2 -mfma)
endif()

target_link_libraries(project-render PRIVATE d3d12 dxgi dxguid ole32 windowscodecs)

# make resources/ visible for includes (resource.h)
target_include_directories(project-render PRIVATE "${CMAKE_SOURCE_DIR}/resources")

# Link the D3D shader compiler for runtime shader compilation
target_link_libraries(project-render PRIVATE d3dcompiler)
target_link_libraries(project-render PRIVATE comctl32)
target_link_libraries(project-render PRIVATE Cabinet)  # Windows Compression API (LZMS) for .prs scene files

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
  # NRD Integration
  # Match engine-side NRD input packing:
  # - normals are stored directly in RGBA16F
  # - roughness is linear in alpha
  set(NRD_NORMAL_ENCODING "4" CACHE STRING "" FORCE)
  set(NRD_ROUGHNESS_ENCODING "1" CACHE STRING "" FORCE)
  set(NRD_NRI ON CACHE BOOL "" FORCE)
  set(NRD_EMBEDS_SPIRV_SHADERS OFF CACHE BOOL "" FORCE)
  set(NRD_EMBEDS_DXBC_SHADERS OFF CACHE BOOL "" FORCE)
  set(NRD_EMBEDS_DXIL_SHADERS ON CACHE BOOL "" FORCE)
  set(NRI_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
  set(NRI_ENABLE_D3D12_SUPPORT ON CACHE BOOL "" FORCE)
  set(NRI_ENABLE_D3D11_SUPPORT OFF CACHE BOOL "" FORCE)
  set(NRI_ENABLE_VK_SUPPORT OFF CACHE BOOL "" FORCE)
  set(NRI_ENABLE_VALIDATION_SUPPORT OFF CACHE BOOL "" FORCE)
  set(NRI_ENABLE_DEBUG_NAMES_AND_ANNOTATIONS ON CACHE BOOL "" FORCE)
  set(NRI_ENABLE_AGILITY_SDK_SUPPORT OFF CACHE BOOL "" FORCE)
  add_subdirectory(thirdparty/NRD)

  if(TARGET NRI_D3D12)
    target_compile_definitions(NRI_D3D12 PRIVATE
      D3D12_MS_DISPATCH_MAX_THREAD_GROUPS_PER_GRID=4194303
      D3D12_AS_TGSM_BYTES_MINIMUM_SUPPORT=32768
      D3D12_MS_TGSM_BYTES_MINIMUM_SUPPORT=28672
    )
  endif()
  
  target_link_libraries(project-render PRIVATE NRD NRDIntegration NRI)
  target_compile_definitions(project-render PRIVATE USE_NRD=1)

# Copy Streamline + NGX runtime files next to the executable.

# optional windows resources (icon + version info)
if (WIN32)
  # look for a manifest/icon file under resources/
  set(APP_ICON "${CMAKE_SOURCE_DIR}/resources/app.ico")
  set(APP_RC   "${CMAKE_SOURCE_DIR}/resources/app.rc")
  if(EXISTS ${APP_RC})
    target_sources(project-render PRIVATE ${APP_RC})
  endif()
endif()
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
  # sl.common.json is no longer copied; keep the file out of the build tree.
)

# Fetch and build Dear ImGui at configure time
include(FetchContent)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
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

# Place build outputs into dedicated folders to avoid spilling .lib/.exp into
# the top-level build directory. This keeps asset_loader.lib, imgui_static.lib,
# project-render.lib and project-render.exp out of the build root.
set_target_properties(project-render asset_loader imgui_static NRD NRI NRDIntegration PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/bin"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/lib"
  ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/lib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/lib"
  LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/lib"
  PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  PDB_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/lib"
  PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/lib"
  COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  COMPILE_PDB_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/lib"
  COMPILE_PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/lib"
)

# Instead of shipping plain text HLSL, compile shaders to bytecode
# files (.cso) at build time and distribute those.  The runtime loader
# (in dxc_wrapper.cpp) will prefer .cso blobs when available.
#
set(SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_BINARY_DIR "${CMAKE_BINARY_DIR}/shaders")
file(GLOB_RECURSE SHADER_ALL_INPUTS CONFIGURE_DEPENDS
    "${SHADER_SOURCE_DIR}/*.hlsl"
    "${SHADER_SOURCE_DIR}/*.hlsli"
)

# Locate DXC and build a handful of known entrypoint/profile combinations.
if(PROJECT_RENDER_SDK_DXC)
  set(DXC_COMPILER "${PROJECT_RENDER_SDK_DXC}")
else()
  find_program(DXC_COMPILER dxc HINTS ENV PATH)
endif()
if(DXC_COMPILER)
  message(STATUS "Found DXC compiler: ${DXC_COMPILER}")
  # Determine the directory containing the DXC executable so we can copy
  # runtime DLLs (dxcompiler.dll and dxil.dll) next to the executable.
  get_filename_component(DXC_DIR ${DXC_COMPILER} DIRECTORY)

  # Build compile list automatically from discovered entry points.
  # Format per entry: "relative_file|entry_point|profile".
  set(SHADER_COMPILE_LIST)
  file(GLOB_RECURSE SHADER_HLSL_FILES CONFIGURE_DEPENDS
      "${SHADER_SOURCE_DIR}/*.hlsl"
  )
  foreach(shader_path IN LISTS SHADER_HLSL_FILES)
    file(RELATIVE_PATH shader_rel "${SHADER_SOURCE_DIR}" "${shader_path}")
    file(READ "${shader_path}" shader_text)

    # DXR library translation unit.
    if(shader_rel STREQUAL "raytracing.hlsl")
      list(APPEND SHADER_COMPILE_LIST "${shader_rel}||lib_6_5")
    endif()

    string(REGEX MATCHALL "VSMain[A-Za-z0-9_]*[ \t\r\n]*\\(" vs_matches "${shader_text}")
    foreach(raw IN LISTS vs_matches)
      string(REGEX REPLACE "[ \t\r\n]*\\($" "" entry "${raw}")
      list(APPEND SHADER_COMPILE_LIST "${shader_rel}|${entry}|vs_6_0")
    endforeach()

    string(REGEX MATCHALL "PSMain[A-Za-z0-9_]*[ \t\r\n]*\\(" ps_matches "${shader_text}")
    foreach(raw IN LISTS ps_matches)
      string(REGEX REPLACE "[ \t\r\n]*\\($" "" entry "${raw}")
      list(APPEND SHADER_COMPILE_LIST "${shader_rel}|${entry}|ps_6_0")
    endforeach()

    string(REGEX MATCHALL "CSMain[A-Za-z0-9_]*[ \t\r\n]*\\(" cs_matches "${shader_text}")
    foreach(raw IN LISTS cs_matches)
      string(REGEX REPLACE "[ \t\r\n]*\\($" "" entry "${raw}")
      list(APPEND SHADER_COMPILE_LIST "${shader_rel}|${entry}|cs_6_5")
    endforeach()
  endforeach()
  # Ensure dedicated ReSTIR spatial pass is always compiled, even if
  # entry-point discovery misses it on some CMake regex edge cases.
  list(APPEND SHADER_COMPILE_LIST "dxr_tonemap_cs.hlsl|CSMain|cs_6_3")
  list(APPEND SHADER_COMPILE_LIST "restir_spatial_cs.hlsl|CSMain|cs_6_5")
  list(APPEND SHADER_COMPILE_LIST "restir_gi_spatial_cs.hlsl|CSMain|cs_6_5")
  # pbr_mesh's grass vertex path has occasionally been missed by regex-based
  # discovery in existing build trees; keep it explicit so Explorer launches do
  # not fall back to loading source HLSL from the current working directory.
  list(APPEND SHADER_COMPILE_LIST "pbr_mesh.hlsl|VSMainGrass|vs_6_0")
  list(APPEND SHADER_COMPILE_LIST "pbr_mesh.hlsl|VSMainGrassShadow|vs_6_0")
  list(REMOVE_DUPLICATES SHADER_COMPILE_LIST)

  set(COMPILED_SHADERS)
  foreach(entry IN LISTS SHADER_COMPILE_LIST)
    # convert pipe-separated string into a proper CMake list by swapping | for ;
    string(REPLACE "|" ";" parts "${entry}")
    if(NOT parts)
      message(WARNING "Empty shader list entry encountered, skipping")
      continue()
    endif()

    list(GET parts 0 file)
    list(GET parts 1 ep)
    list(GET parts 2 profile)

    set(src "${SHADER_SOURCE_DIR}/${file}")
    if(NOT EXISTS "${src}")
      message(WARNING "Shader source not found at ${src}; skipping compile.")
      continue()
    endif()

    get_filename_component(base "${file}" NAME_WE)
    set(out "${SHADER_BINARY_DIR}/${base}_${ep}_${profile}.cso")

    set(dxc_args -T ${profile})
    if(NOT ep STREQUAL "")
      list(APPEND dxc_args -E ${ep})
    endif()
    list(APPEND dxc_args -Fo "${out}" "${src}")

    set(entry_label "${ep}")
    if(entry_label STREQUAL "")
      set(entry_label "<library>")
    endif()

    add_custom_command(
      OUTPUT "${out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
      COMMAND ${DXC_COMPILER} ${dxc_args}
      DEPENDS "${src}" ${SHADER_ALL_INPUTS}
      COMMENT "Compiling shader ${file} [${entry_label},${profile}]"
    )
    list(APPEND COMPILED_SHADERS "${out}")
  endforeach()

  if(COMPILED_SHADERS)
    add_custom_target(compile_shaders ALL DEPENDS ${COMPILED_SHADERS})
    add_dependencies(project-render compile_shaders)
  endif()
else()
  message(WARNING "DXC compiler not found; shaders will be compiled at runtime")
endif()

# Copy only compiled shader bytecode to the runtime folder.
# Runtime expects blobs next to the executable under ./shaders.
if(DXC_COMPILER)
  add_custom_command(TARGET project-render POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E rm -rf
          "$<TARGET_FILE_DIR:project-render>/shaders"
      COMMAND ${CMAKE_COMMAND} -E rm -rf
          "$<TARGET_FILE_DIR:project-render>/shaders"
      COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:project-render>/shaders"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${SHADER_BINARY_DIR}"
          "$<TARGET_FILE_DIR:project-render>/shaders"
      # Copy DXC runtime DLLs (if present alongside the dxc executable)
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DXC_DIR}/dxcompiler.dll"
        "$<TARGET_FILE_DIR:project-render>/dxcompiler.dll"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DXC_DIR}/dxil.dll"
        "$<TARGET_FILE_DIR:project-render>/dxil.dll"
      COMMENT "Copying compiled shader blobs..."
  )
endif()

# Bring the assets directory along so models, textures, and
# other external data can be referenced by the executable from the
# build output without manual copying.
add_custom_command(TARGET project-render POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:project-render>/assets"
    COMMENT "Copying assets folder..."
)

# Build as a console app to allow output to be piped to the terminal.
# set_target_properties(project-render PROPERTIES WIN32_EXECUTABLE TRUE)

# Specify the entry point as WinMain to avoid linker errors
if (MSVC)
  target_link_options(project-render PRIVATE /ENTRY:"WinMainCRTStartup")
endif()

# ==============================================================================
# Installer and Component Setup (CPack)
# ==============================================================================

# Core Application
install(TARGETS project-render 
  RUNTIME DESTINATION bin 
  COMPONENT CoreApp
)

# Install dependent DLLs, shaders, and generated files in the bin folder
# This will catch Qt, NRD, OpenImageDenoise, shaders/, miniz.dll, assets, etc.
install(DIRECTORY "$<TARGET_FILE_DIR:project-render>/" 
  DESTINATION bin
  COMPONENT CoreApp
  PATTERN "*.pdb" EXCLUDE # Exclude debug symbols
  PATTERN "*.lib" EXCLUDE # Exclude static/import libraries
  PATTERN "*.exp" EXCLUDE # Exclude exports
)

# Optional Plugin: 3ds Max 2024 Live Link
if(EXISTS "${CMAKE_SOURCE_DIR}/build-max2024/Release/ProjectRenderLiveLink.dlu")
  install(FILES "${CMAKE_SOURCE_DIR}/build-max2024/Release/ProjectRenderLiveLink.dlu"
    DESTINATION plugins/max2024
    COMPONENT Max2024Plugin
  )
endif()

# Optional Plugin: 3ds Max 2025 Live Link
if(EXISTS "${CMAKE_SOURCE_DIR}/build-max2025/Release/ProjectRenderLiveLink.dlu")
  install(FILES "${CMAKE_SOURCE_DIR}/build-max2025/Release/ProjectRenderLiveLink.dlu"
    DESTINATION plugins/max2025
    COMPONENT Max2025Plugin
  )
endif()

# Optional Plugin: Archicad 28 Live Link
if(EXISTS "${CMAKE_SOURCE_DIR}/build-archicad28-v142/Release/ProjectRenderArchicad28LiveLink.apx")
  install(FILES "${CMAKE_SOURCE_DIR}/build-archicad28-v142/Release/ProjectRenderArchicad28LiveLink.apx"
    DESTINATION plugins/archicad28
    COMPONENT Archicad28Plugin
  )
endif()

# Configure CPack using NSIS generator
set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_NAME "Project Render")
set(CPACK_PACKAGE_VENDOR "Project Render Team")
set(CPACK_PACKAGE_VERSION_MAJOR "0")
set(CPACK_PACKAGE_VERSION_MINOR "1")
set(CPACK_PACKAGE_VERSION_PATCH "5")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Project Render")

# Explicitly limit the installer to only our components (ignore 3rd party components like assimp)
set(CPACK_COMPONENTS_ALL CoreApp Max2024Plugin Max2025Plugin Archicad28Plugin)

# Component display names and descriptions
set(CPACK_COMPONENT_COREAPP_DISPLAY_NAME "Project Render (Core Engine)")
set(CPACK_COMPONENT_COREAPP_REQUIRED ON)
set(CPACK_COMPONENT_COREAPP_DESCRIPTION "The main standalone rendering application and its required dependencies.")

set(CPACK_COMPONENT_MAX2024PLUGIN_DISPLAY_NAME "3ds Max 2024 Live Link")
set(CPACK_COMPONENT_MAX2024PLUGIN_DESCRIPTION "Installs the Live Link plugin for Autodesk 3ds Max 2024.")

set(CPACK_COMPONENT_MAX2025PLUGIN_DISPLAY_NAME "3ds Max 2025 Live Link")
set(CPACK_COMPONENT_MAX2025PLUGIN_DESCRIPTION "Installs the Live Link plugin for Autodesk 3ds Max 2025.")

set(CPACK_COMPONENT_ARCHICAD28PLUGIN_DISPLAY_NAME "Archicad 28 Live Link")
set(CPACK_COMPONENT_ARCHICAD28PLUGIN_DESCRIPTION "Installs the Live Link plugin for Graphisoft Archicad 28.")

# Explicitly tell CPack where NSIS is installed
set(CPACK_MAKENSIS_EXECUTABLE "C:/Program Files (x86)/NSIS/makensis.exe")

include(CPack)
