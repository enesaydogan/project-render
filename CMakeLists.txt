cmake_minimum_required(VERSION 3.20)
project(project-render LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(project-render src/main.cpp)
# Additional project sources
target_sources(project-render PRIVATE src/camera.cpp src/dxr_renderer.cpp src/raster_renderer.cpp src/file_import.cpp src/scene.cpp)

if (MSVC)
  target_compile_options(project-render PRIVATE /W4 /permissive-)
endif()

target_link_libraries(project-render PRIVATE d3d12 dxgi)

# Link the D3D shader compiler for runtime shader compilation
target_link_libraries(project-render PRIVATE d3dcompiler)
target_link_libraries(project-render PRIVATE comctl32)

# Fetch and build Dear ImGui at configure time
include(FetchContent)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.89.8
)
FetchContent_MakeAvailable(imgui)

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
add_library(asset_loader STATIC src/assets/asset_loader.cpp src/assets/asset_loader.h)
target_include_directories(asset_loader PUBLIC src/assets)

# Optionally enable tinygltf support (install via vcpkg: `vcpkg install tinygltf`)
option(USE_TINYGLTF "Enable tinygltf integration for glTF import (requires tinygltf headers)" OFF)
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
    # Compile the tinygltf implementation unit into the asset_loader library so
    # that the header-only library's symbols are instantiated exactly once.
    target_sources(asset_loader PRIVATE src/assets/tinygltf_impl.cpp)
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
