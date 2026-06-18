cmake_minimum_required(VERSION 3.24)
project(Backdropper LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.md")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.md" BACKDROPPER_VERSION_MD)
string(REGEX MATCH "Current version:[ \t]*([0-9]+\\.[0-9]+\\.[0-9]+)" _version_match "${BACKDROPPER_VERSION_MD}")
set(BACKDROPPER_VERSION "${CMAKE_MATCH_1}")
if(NOT BACKDROPPER_VERSION)
  message(FATAL_ERROR "Could not read Current version from VERSION.md")
endif()

foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config}" config_upper)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib")
endforeach()

add_library(BackdropperThumb SHARED
  src/ThumbProvider/BackdropperThumb.cpp
  src/ThumbProvider/BackdropperThumb.def
  src/common/render.cpp
  src/common/settings.cpp
  src/common/thumbnail_cache.cpp
)
target_compile_definitions(BackdropperThumb PRIVATE UNICODE _UNICODE NOMINMAX _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)
target_include_directories(BackdropperThumb PRIVATE src/common)
target_link_libraries(BackdropperThumb PRIVATE windowscodecs shlwapi shell32 shcore ole32 runtimeobject gdi32 d2d1 d3d11 dxgi windowsapp)

add_executable(BackdropperSettings WIN32
  src/Settings/BackdropperSettings.cpp
  src/resources/Backdropper.rc
  src/common/settings.cpp
  src/common/thumbnail_cache.cpp
)
target_compile_definitions(BackdropperSettings PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_definitions(BackdropperSettings PRIVATE BACKDROPPER_VERSION="${BACKDROPPER_VERSION}")
target_include_directories(BackdropperSettings PRIVATE src/common src/resources)
target_link_libraries(BackdropperSettings PRIVATE comctl32 shell32 shlwapi ole32 gdiplus dwmapi comdlg32)
target_link_options(BackdropperSettings PRIVATE
  "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'"
)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT BackdropperSettings)

add_executable(RenderSmoke
  src/RenderSmoke/RenderSmoke.cpp
  src/common/render.cpp
)
target_compile_definitions(RenderSmoke PRIVATE UNICODE _UNICODE NOMINMAX _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)
target_include_directories(RenderSmoke PRIVATE src/common)
target_link_libraries(RenderSmoke PRIVATE windowscodecs ole32 shell32 shlwapi shcore runtimeobject gdi32 d2d1 d3d11 dxgi windowsapp)
