set(TARGET StreamEmber.Overlay.Host)

SET_CEF_TARGET_OUT_DIR()

add_executable(${TARGET} WIN32 host_main.cpp)
SET_EXECUTABLE_TARGET_PROPERTIES(${TARGET})
target_compile_options(${TARGET} PRIVATE /WX-)
target_include_directories(${TARGET} PRIVATE ${CEF_ROOT})
add_dependencies(${TARGET} libcef_dll_wrapper)
target_link_libraries(${TARGET} PRIVATE libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})

# Embeds StreamEmber.Overlay.Host.exe.manifest + compatibility.manifest (Windows 10 support declaration)
ADD_WINDOWS_MANIFEST("${CMAKE_CURRENT_SOURCE_DIR}" "${TARGET}" "exe")
