set(TARGET StreamEmber.Overlay)

add_library(${TARGET} SHARED
  overlay_core.cpp
  overlay_log.cpp
  overlay_log.h
  ${SEO_INCLUDE_DIR}/se_overlay.h
)
SET_LIBRARY_TARGET_PROPERTIES(${TARGET})
se_add_version_resource(${TARGET} "StreamEmber Overlay core (CEF browser)")
# CEF's flags include /WX; keep warnings visible but not fatal for our own code.
target_compile_options(${TARGET} PRIVATE /WX-)
target_compile_definitions(${TARGET} PRIVATE SE_OVERLAY_CORE_BUILD)
target_include_directories(${TARGET} PRIVATE ${SEO_INCLUDE_DIR} ${CEF_ROOT})
add_dependencies(${TARGET} libcef_dll_wrapper)
target_link_libraries(${TARGET} PRIVATE libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})
