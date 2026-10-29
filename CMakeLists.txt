cmake_minimum_required(VERSION 3.24)
project(revc_fpv VERSION 0.1.0 LANGUAGES C CXX)

if(NOT APPLE)
  message(FATAL_ERROR "reVC-fpv targets the macOS (reVC) version of GTA Vice City.")
endif()

# The game executable is arm64-only, so the plugin must be too.
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(LIBFPV_DIR "$ENV{HOME}/Projects/libfpv" CACHE PATH "libfpv source checkout (for its headers)")
option(REVC_FPV_KAMIKAZE "Local development builds only: libfpv's warhead (needs a libfpv built with it)" OFF)
if(NOT EXISTS "${LIBFPV_DIR}/include/libfpv.h")
  message(FATAL_ERROR "libfpv.h not found in ${LIBFPV_DIR}/include. Pass -DLIBFPV_DIR=/path/to/libfpv.")
endif()

# Only GLFW's headers are needed; SOURCE_SUBDIR points at a folder without a
# CMakeLists.txt so the library itself isn't built.
include(FetchContent)
FetchContent_Declare(glfw
  URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
  URL_HASH SHA256=c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR include)
FetchContent_MakeAvailable(glfw)

# The plugin uses the game's own GLFW: its imports must name the game's
# libglfw.3.dylib, so the references resolve to the copy the game already
# loaded (and dyld interposing applies to the game's calls). A stub with the
# same install name stands in for it at link time; it is never shipped.
add_library(glfw_stub SHARED cmake/glfw_stub.c)
set_target_properties(glfw_stub PROPERTIES
  OUTPUT_NAME glfw.3
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/stub"
  INSTALL_NAME_DIR "@loader_path/../Frameworks"
  BUILD_WITH_INSTALL_NAME_DIR ON
  MACOSX_RPATH OFF)
target_link_options(glfw_stub PRIVATE -Wl,-compatibility_version,3.0.0 -Wl,-current_version,3.4.0)

add_library(revc_fpv SHARED
  src/audio.cpp
  src/autotest.cpp
  src/drone.cpp
  src/effects.cpp
  src/fpvapi.cpp
  src/hook.cpp
  src/input.cpp
  src/log.cpp
  src/model.cpp
  src/paths.cpp
  src/pilot.cpp
  src/plugin.cpp
  src/render.cpp
  src/sfx.cpp
  src/standin.cpp
  src/vc.cpp
  src/view.cpp
  src/world.cpp)
if(REVC_FPV_KAMIKAZE)
  target_sources(revc_fpv PRIVATE src/kamikaze.cpp)
  target_compile_definitions(revc_fpv PRIVATE LIBFPV_KAMIKAZE)
endif()

target_include_directories(revc_fpv PRIVATE
  src
  "${LIBFPV_DIR}/include"
  "${LIBFPV_DIR}/docs/renderers"
  "${glfw_SOURCE_DIR}/include")
# libfpv.dylib is loaded at runtime (src/fpvapi.cpp); the header only declares.
target_compile_definitions(revc_fpv PRIVATE
  REVC_FPV_VERSION="${PROJECT_VERSION}"
  GL_SILENCE_DEPRECATION
  GLFW_INCLUDE_NONE)
target_compile_options(revc_fpv PRIVATE -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(revc_fpv PRIVATE glfw_stub
  "-framework OpenGL" "-framework AudioToolbox" "-framework CoreAudio")
set_target_properties(revc_fpv PROPERTIES
  PREFIX ""
  OUTPUT_NAME reVC-fpv
  SUFFIX ".dylib"
  MACOSX_RPATH OFF
  BUILD_WITH_INSTALL_NAME_DIR ON
  INSTALL_NAME_DIR "@loader_path")

# Release check (tools/check_libfpv.cpp): a libfpv.dylib has every function the
# plugin loads. Not part of the default build.
add_executable(check_libfpv EXCLUDE_FROM_ALL tools/check_libfpv.cpp)
target_include_directories(check_libfpv PRIVATE src "${LIBFPV_DIR}/include")
