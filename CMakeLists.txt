# StreamEmber Overlay (ui-runtime) — CEF based HTML overlay for single-player game mods.
#
# Targets:
#   StreamEmber.Overlay        core DLL (CEF browser process side, C ABI in include/se_overlay.h)
#   StreamEmber.Overlay.Host   CEF sub-process exe (renderer + JS bridge)
#   StreamEmber.Overlay.GTAV   GTA V backend (.asi, D3D11, ScriptHookV present callback)
#   StreamEmber.Overlay.RDR2   RDR2 backend (.asi, D3D12 via D3D11On12, own DXGI hooks with MinHook)
#
# Normally driven by build.ps1, which downloads CEF into third_party/cef first and passes -DSE_VERSION.
cmake_minimum_required(VERSION 3.21)
project(StreamEmberOverlay LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Only x64 is supported (configure with -A x64).")
endif()

set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef" CACHE PATH "CEF binary distribution (minimal)")
if(NOT EXISTS "${CEF_ROOT}/cmake/FindCEF.cmake")
  message(FATAL_ERROR "CEF not found in ${CEF_ROOT}. Run build.ps1 (it downloads CEF) or set -DCEF_ROOT=...")
endif()
set(SHV_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ScriptHookV_SDK" CACHE PATH
    "ScriptHookV SDK folder containing inc/main.h and lib/ScriptHookV.lib")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(StreamEmberVersion)

# Static CRT everywhere (CEF's wrapper is built with /MT); avoids mixing /MD and /MT.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# No Chromium sandbox: the browser process lives inside the game.
set(USE_SANDBOX OFF CACHE BOOL "" FORCE)

list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

# CEF's C++ wrapper (static library built from the distribution's sources)
add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)
# The minimal distribution has no Debug\ folder; libcef.lib is only an import library, so Debug builds use it too.
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_RELEASE}" "${CEF_LIB_RELEASE}")

set(SEO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_subdirectory(core)
add_subdirectory(host)
add_subdirectory(backends/common)
add_subdirectory(backends/gtav-d3d11)
add_subdirectory(backends/rdr2-d3d12)

PRINT_CEF_CONFIG()
