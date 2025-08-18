cmake_minimum_required(VERSION 3.20)
project(MirrorBladeBridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# -----------------------------
# RED4ext SDK include directory
# -----------------------------
if(NOT DEFINED RED4EXT_SDK)
  if(DEFINED ENV{RED4EXT_SDK})
    set(RED4EXT_SDK $ENV{RED4EXT_SDK})
  else()
    message(FATAL_ERROR "Please set RED4EXT_SDK to the path of RED4ext.SDK (e.g., -DRED4EXT_SDK=C:/dev/RED4ext.SDK)")
  endif()
endif()

# -----------------------------
# Optional: FidelityFX FSR2
# -----------------------------
option(WITH_FSR2 "Build with FidelityFX FSR2 support" OFF)
set(FSR2_ROOT "" CACHE PATH "Root directory containing FSR2 headers/libs (optional)")

# -----------------------------
# Sources
# -----------------------------
set(SRC_FILES
    src/main.cpp
    src/Bridge.cpp
    src/MBState.cpp
    src/OpsHelpers.cpp
    src/MirrorBladeBridge.cpp
    src/Upscaler_FSR2.cpp
    src/RenderHook.cpp
    src/Plugin.cpp
    src/MirrorBladeOps.cpp
    src/MBIPC.cpp
    src/MBFeatures.cpp
    src/MirrorBladeInstance.cpp
    src/TGDKOps.cpp
    src/MBLog.cpp
    src/MBConfig.cpp
    src/TGDKFigure8Fold.cpp
)

add_library(MirrorBladeBridge SHARED ${SRC_FILES})


# -----------------------------
# Includes
# -----------------------------
target_include_directories(MirrorBladeBridge
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${RED4EXT_SDK}/include
)

if(WITH_FSR2)
  if(FSR2_ROOT)
    target_include_directories(MirrorBladeBridge PRIVATE ${FSR2_ROOT}/include)
    # Example lib search; adjust names/paths to your FSR2 package:
    find_library(FSR2_LIB NAMES ffx_fsr2_api_x64 ffx_fsr2_api PATHS ${FSR2_ROOT}/lib ${FSR2_ROOT}/lib/x64)
    if(FSR2_LIB)
      target_link_libraries(MirrorBladeBridge PRIVATE ${FSR2_LIB})
      target_compile_definitions(MirrorBladeBridge PRIVATE WITH_FSR2=1)
    else()
      message(WARNING "WITH_FSR2=ON but FSR2 lib not found in FSR2_ROOT; building without FSR2.")
    endif()
  else()
    message(WARNING "WITH_FSR2=ON but FSR2_ROOT not set; building without FSR2.")
  endif()
endif()

# -----------------------------
# Win32 + MSVC settings
# -----------------------------
target_compile_definitions(MirrorBladeBridge PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

if(MSVC)
  target_compile_options(MirrorBladeBridge PRIVATE
    /permissive- /Zc:__cplusplus /EHsc /bigobj /MP /utf-8
  )
endif()

# -----------------------------
# Link libraries
# -----------------------------
# advapi32: ConvertStringSecurityDescriptorToSecurityDescriptorW (MBIPC)
# ws2_32  : sockets (if used)
# d3d12/dxgi: upscaler backend, device/queue types
target_link_libraries(MirrorBladeBridge PRIVATE
  advapi32
  ws2_32
  d3d12
  dxgi
)

# -----------------------------
# PCH handling for MBTypeReg.cpp
# If your project uses CMake PCH, this skips it.
# If your project uses MSVC /Yu globally, this also forces /Y- for this TU.
# -----------------------------
set_source_files_properties(src/MBTypeReg.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
if(MSVC)
  set_source_files_properties(src/MBTypeReg.cpp PROPERTIES COMPILE_OPTIONS "/Y-")
endif()

# -----------------------------
# Output name
# -----------------------------
set_target_properties(MirrorBladeBridge PROPERTIES OUTPUT_NAME "MirrorBladeBridge")

# -----------------------------
# (Optional) tidy source groups in IDEs
# -----------------------------
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})
