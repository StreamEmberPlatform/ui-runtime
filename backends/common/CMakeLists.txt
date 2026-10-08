# Game-independent part of the backends: Overlay.ini, core loading, window input hook, D3D11 renderer.
add_library(StreamEmber.Overlay.BackendCommon STATIC
  backend_base.cpp
  backend_common.h
  d3d11_renderer.cpp
  d3d11_renderer.h
  input.cpp
  input.h
)
set_target_properties(StreamEmber.Overlay.BackendCommon PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
target_compile_options(StreamEmber.Overlay.BackendCommon PRIVATE /W4 /utf-8 /wd4100)
target_compile_definitions(StreamEmber.Overlay.BackendCommon PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(StreamEmber.Overlay.BackendCommon PUBLIC ${SEO_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(StreamEmber.Overlay.BackendCommon PUBLIC d3d11 dxgi d3dcompiler gdi32)
