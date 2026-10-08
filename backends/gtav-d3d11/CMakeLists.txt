set(TARGET StreamEmber.Overlay.GTAV)

if(NOT EXISTS "${SHV_SDK_DIR}/inc/main.h" OR NOT EXISTS "${SHV_SDK_DIR}/lib/ScriptHookV.lib")
  message(FATAL_ERROR "ScriptHookV SDK not found in ${SHV_SDK_DIR} (set -DSHV_SDK_DIR=...)")
endif()

add_library(${TARGET} SHARED backend.cpp)
# The backend does not link CEF: it loads StreamEmber.Overlay.dll at runtime, so it uses its own flags.
set_target_properties(${TARGET} PROPERTIES
  SUFFIX ".asi"
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
target_compile_options(${TARGET} PRIVATE /W4 /utf-8 /wd4100)
se_add_version_resource(${TARGET} "StreamEmber Overlay for GTA V (Direct3D 11)")
target_include_directories(${TARGET} PRIVATE "${SHV_SDK_DIR}/inc")
target_link_libraries(${TARGET} PRIVATE StreamEmber.Overlay.BackendCommon "${SHV_SDK_DIR}/lib/ScriptHookV.lib")
