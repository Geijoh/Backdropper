cmake_minimum_required(VERSION 3.24)
project(Backdropper LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.md")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.md" BACKDROPPER_VERSION_LINE REGEX "^Current version:")
string(REGEX REPLACE "^Current version:[ \t]*([0-9]+\\.[0-9]+\\.[0-9]+).*$" "\\1" BACKDROPPER_VERSION "${BACKDROPPER_VERSION_LINE}")
if(NOT BACKDROPPER_VERSION)
  message(FATAL_ERROR "Could not read Current version from VERSION.md")
endif()
string(REPLACE "." ";" BACKDROPPER_VERSION_PARTS "${BACKDROPPER_VERSION}")
list(GET BACKDROPPER_VERSION_PARTS 0 BACKDROPPER_VERSION_MAJOR)
list(GET BACKDROPPER_VERSION_PARTS 1 BACKDROPPER_VERSION_MINOR)
list(GET BACKDROPPER_VERSION_PARTS 2 BACKDROPPER_VERSION_PATCH)
message(STATUS "Backdropper version: ${BACKDROPPER_VERSION}")

function(add_backdropper_version_resource target description original_filename file_type)
  set(BACKDROPPER_FILE_DESCRIPTION "${description}")
  set(BACKDROPPER_INTERNAL_NAME "${target}")
  set(BACKDROPPER_ORIGINAL_FILENAME "${original_filename}")
  set(BACKDROPPER_FILE_TYPE "${file_type}")
  set(version_rc "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}-version.rc")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/BackdropperVersion.rc.in"
    "${version_rc}"
    @ONLY
  )
  target_sources(${target} PRIVATE "${version_rc}")
endfunction()

set_source_files_properties(third_party/md4c/md4c.c PROPERTIES LANGUAGE CXX)

foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config}" config_upper)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib")
endforeach()

add_library(BackdropperThumb SHARED
  src/ThumbProvider/BackdropperThumb.cpp
  src/ThumbProvider/BackdropperThumb.def
  src/common/format_support.cpp
  src/common/render.cpp
  src/common/settings.cpp
  src/common/thumbnail_cache.cpp
)
target_compile_definitions(BackdropperThumb PRIVATE UNICODE _UNICODE NOMINMAX _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)
target_include_directories(BackdropperThumb PRIVATE src/common)
target_link_libraries(BackdropperThumb PRIVATE windowscodecs shlwapi shell32 shcore ole32 runtimeobject gdi32 d2d1 d3d11 dxgi windowsapp)
add_backdropper_version_resource(BackdropperThumb "Backdropper thumbnail provider" "BackdropperThumb.dll" VFT_DLL)

add_executable(BackdropperSettings WIN32
  src/Settings/BackdropperSettings.cpp
  src/resources/Backdropper.rc
  src/common/format_support.cpp
  src/common/settings.cpp
  src/common/thumbnail_cache.cpp
  third_party/md4c/md4c.c
)
target_compile_definitions(BackdropperSettings PRIVATE UNICODE _UNICODE NOMINMAX MD4C_USE_UTF16)
target_compile_definitions(BackdropperSettings PRIVATE BACKDROPPER_VERSION="${BACKDROPPER_VERSION}")
target_include_directories(BackdropperSettings PRIVATE src/common src/resources third_party/md4c)
target_link_libraries(BackdropperSettings PRIVATE comctl32 shell32 shlwapi ole32 oleaut32 oleacc uiautomationcore gdiplus dwmapi comdlg32 urlmon)
add_backdropper_version_resource(BackdropperSettings "Backdropper settings" "BackdropperSettings.exe" VFT_APP)
target_link_options(BackdropperSettings PRIVATE
  "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'"
)
add_custom_command(TARGET BackdropperSettings PRE_BUILD
  COMMAND powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/stop-build-app.ps1"
    "$<TARGET_FILE:BackdropperSettings>"
  VERBATIM
)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT BackdropperSettings)

add_executable(BackdropperUpdater
  src/Updater/BackdropperUpdater.cpp
  src/common/thumbnail_cache.cpp
)
target_compile_definitions(BackdropperUpdater PRIVATE UNICODE _UNICODE NOMINMAX)
target_include_directories(BackdropperUpdater PRIVATE src/common)
target_link_libraries(BackdropperUpdater PRIVATE shell32 ole32 urlmon wintrust crypt32)
add_backdropper_version_resource(BackdropperUpdater "Backdropper updater" "BackdropperUpdater.exe" VFT_APP)
add_dependencies(BackdropperSettings BackdropperUpdater)
add_custom_command(TARGET BackdropperSettings POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/PRIVACY.md"
    "$<TARGET_FILE_DIR:BackdropperSettings>/PRIVACY.md"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    "$<TARGET_FILE_DIR:BackdropperSettings>/THIRD_PARTY_NOTICES.md"
)

add_executable(RenderSmoke
  src/RenderSmoke/RenderSmoke.cpp
  src/common/format_support.cpp
  src/common/render.cpp
)
target_compile_definitions(RenderSmoke PRIVATE UNICODE _UNICODE NOMINMAX _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)
target_include_directories(RenderSmoke PRIVATE src/common)
target_link_libraries(RenderSmoke PRIVATE windowscodecs ole32 shell32 shlwapi shcore runtimeobject gdi32 d2d1 d3d11 dxgi windowsapp)
add_backdropper_version_resource(RenderSmoke "Backdropper renderer smoke test" "RenderSmoke.exe" VFT_APP)
