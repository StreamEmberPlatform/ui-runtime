# RDR2 (DirectX 12) backend. Needs no ScriptHookRDR2 SDK: it hooks DXGI/D3D12 itself with MinHook.
set(TARGET StreamEmber.Overlay.RDR2)

# MinHook (BSD-2) 1.3.4, kept in vendor/minhook (no download)
set(MINHOOK_DIR "${CMAKE_SOURCE_DIR}/vendor/minhook" CACHE PATH "MinHook source tree (include/, src/)")
if(NOT EXISTS "${MINHOOK_DIR}/include/MinHook.h")
  message(FATAL_ERROR "MinHook not found in ${MINHOOK_DIR}")
endif()

add_library(seo_minhook STATIC
  "${MINHOOK_DIR}/src/buffer.c"
  "${MINHOOK_DIR}/src/hook.c"
  "${MINHOOK_DIR}/src/trampoline.c"
  "${MINHOOK_DIR}/src/hde/hde64.c"
)
set_target_properties(seo_minhook PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_include_directories(seo_minhook PUBLIC "${MINHOOK_DIR}/include")

add_library(${TARGET} SHARED backend.cpp)
set_target_properties(${TARGET} PROPERTIES
  SUFFIX ".asi"
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
target_compile_options(${TARGET} PRIVATE /W4 /utf-8 /wd4100)
se_add_version_resource(${TARGET} "StreamEmber Overlay for RDR2 (Direct3D 12)")
target_link_libraries(${TARGET} PRIVATE StreamEmber.Overlay.BackendCommon seo_minhook d3d12 d3d11)
