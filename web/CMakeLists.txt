# Web build: reVC (re3, miami branch) + librw + this mod + libfpv, compiled
# together into one WebAssembly module with Emscripten.
#
#   web/setup.sh                       # patched reVC source in build/re3
#   emcmake cmake -S web -B build/web
#   cmake --build build/web -j
cmake_minimum_required(VERSION 3.24)
project(revc_fpv_web C CXX)

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Configure with emcmake (Emscripten).")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(MOD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(RE3_DIR "${MOD_DIR}/build/re3" CACHE PATH "reVC source with web/re3-web.patch applied (web/setup.sh)")
set(LIBFPV_DIR "$ENV{HOME}/Projects/libfpv" CACHE PATH "libfpv source checkout")
if(NOT EXISTS "${RE3_DIR}/src/core/config.h")
  message(FATAL_ERROR "No reVC source in ${RE3_DIR}: run web/setup.sh first.")
endif()

set(RE3_DEFINES_MOD NDEBUG LIBRW CMAKE_NO_AUTOLINK AUDIO_OAL CMAKE_BUILD USE_OUR_VERSIONING FPV_HOOKS)

# --- librw (GL3 backend on WebGL 2, GLFW) ---------------------------------------
set(RW_DIR "${RE3_DIR}/vendor/librw")
file(GLOB RW_SOURCES "${RW_DIR}/src/*.cpp" "${RW_DIR}/src/gl/*.cpp" "${RW_DIR}/src/d3d/*.cpp" "${RW_DIR}/src/ps2/*.cpp" "${RW_DIR}/src/gl/glad/glad.c"
     "${RW_DIR}/src/lodepng/lodepng.cpp")
add_library(librw STATIC ${RW_SOURCES})
target_include_directories(librw PUBLIC "${RW_DIR}")
target_compile_definitions(librw PUBLIC RW_GL3 PRIVATE LODEPNG_NO_COMPILE_CPP NDEBUG)
target_compile_options(librw PRIVATE -Wno-everything --use-port=contrib.glfw3)
set_source_files_properties("${RW_DIR}/src/gl/gl3device.cpp" PROPERTIES
  COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/glfw_rename.h")
set_target_properties(librw PROPERTIES CXX_STANDARD 11)

# --- reVC ---------------------------------------------------------------------------
file(GLOB_RECURSE RE3_SOURCES "${RE3_DIR}/src/*.cpp")
list(FILTER RE3_SOURCES EXCLUDE REGEX "/src/skel/win/")
set(RE3_INCLUDES)
file(GLOB_RECURSE RE3_HEADERS "${RE3_DIR}/src/*.h")
foreach(h ${RE3_HEADERS})
  get_filename_component(d "${h}" DIRECTORY)
  list(APPEND RE3_INCLUDES "${d}")
endforeach()
list(REMOVE_DUPLICATES RE3_INCLUDES)

# --- libfpv (static, docs/WEB.md) ---------------------------------------------------
set(LIBFPV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBFPV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${LIBFPV_DIR}" libfpv EXCLUDE_FROM_ALL)

# --- the mod: src/ (shared with macOS) and its web pieces -----------------------------
set(MOD_SOURCES
  "${MOD_DIR}/src/drone.cpp" "${MOD_DIR}/src/effects.cpp" "${MOD_DIR}/src/input.cpp" "${MOD_DIR}/src/log.cpp"
  "${MOD_DIR}/src/model.cpp" "${MOD_DIR}/src/pilot.cpp" "${MOD_DIR}/src/plugin.cpp" "${MOD_DIR}/src/render.cpp"
  "${MOD_DIR}/src/sfx.cpp" "${MOD_DIR}/src/standin.cpp" "${MOD_DIR}/src/view.cpp" "${MOD_DIR}/src/world.cpp"
  src/audio_web.cpp src/fpvapi_web.cpp src/hook_web.cpp src/mission.cpp src/paths_web.cpp src/vc_web.cpp)
add_library(revc_fpv_mod OBJECT ${MOD_SOURCES})
target_include_directories(revc_fpv_mod PRIVATE "${MOD_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${LIBFPV_DIR}/include" "${LIBFPV_DIR}/docs/renderers" "${RE3_DIR}" ${RE3_INCLUDES} "${RW_DIR}")
target_compile_definitions(revc_fpv_mod PRIVATE ${RE3_DEFINES_MOD} REVC_FPV_VERSION="web" GLFW_INCLUDE_NONE RW_GL3)
target_compile_options(revc_fpv_mod PRIVATE --use-port=contrib.glfw3 -Wall -Wno-unused-parameter
  "$<$<CONFIG:Release>:-O3>")
set_target_properties(revc_fpv_mod PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# Emscripten's OpenAL has no EFX; reVC includes the header and finds the
# functions missing at run time.
foreach(h efx.h efx-presets.h)
  file(READ "${RE3_DIR}/vendor/openal-soft/include/AL/${h}" text)
  string(REPLACE "#include \"alc.h\"" "#include <AL/alc.h>" text "${text}")
  string(REPLACE "#include \"al.h\"" "#include <AL/al.h>" text "${text}")
  file(WRITE "${CMAKE_BINARY_DIR}/compat/AL/${h}"
       "#include <AL/al.h>\n#ifndef AL_APIENTRY\n#define AL_APIENTRY\n#endif\n#ifndef ALC_APIENTRY\n#define ALC_APIENTRY\n#endif\n${text}")
endforeach()

add_executable(revc ${RE3_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/src/GitSHA1.cpp")
target_include_directories(revc PRIVATE "${RE3_DIR}" ${RE3_INCLUDES} "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/compat")
set(RE3_DEFINES NDEBUG LIBRW CMAKE_NO_AUTOLINK AUDIO_OAL CMAKE_BUILD USE_OUR_VERSIONING FPV_HOOKS)
target_compile_definitions(revc PRIVATE ${RE3_DEFINES})
# The game's GLFW calls go through the mod (src/plugin.cpp), as dyld interposing does on macOS.
set(GLFW_RENAME "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/glfw_rename.h")
set_source_files_properties("${RE3_DIR}/src/skel/glfw/glfw.cpp" PROPERTIES COMPILE_OPTIONS "${GLFW_RENAME}")
target_compile_options(revc PRIVATE -Wno-everything --use-port=contrib.glfw3 -sUSE_MPG123=1)
set_target_properties(revc PROPERTIES CXX_STANDARD 11 OUTPUT_NAME index SUFFIX ".html"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
  LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
target_link_libraries(revc PRIVATE librw revc_fpv_mod libfpv::fpv)
target_link_options(revc PRIVATE
  --use-port=contrib.glfw3 -sUSE_MPG123=1 -lopenal -lidbfs.js
  -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sGL_ENABLE_GET_PROC_ADDRESS=1
  -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=1024MB -sSTACK_SIZE=4MB
  -sFORCE_FILESYSTEM=1 -sEXIT_RUNTIME=0
  -sEXPORTED_RUNTIME_METHODS=FS,callMain,addRunDependency,removeRunDependency,HEAPU8,UTF8ToString
  --shell-file "${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
  "$<$<CONFIG:Release>:-O3>")
option(REVC_WEB_ASAN "AddressSanitizer (debugging memory errors)" OFF)
if(REVC_WEB_ASAN)
  foreach(t librw revc revc_fpv_mod)
    target_compile_options(${t} PRIVATE -fsanitize=address)
  endforeach()
  target_link_options(revc PRIVATE -fsanitize=address -sINITIAL_MEMORY=2GB -sASAN_SHADOW_SIZE=268435456)
endif()
option(REVC_WEB_DEBUG "Function names in stack traces and Emscripten's run-time checks" OFF)
if(REVC_WEB_DEBUG)
  target_link_options(revc PRIVATE --profiling-funcs -O1 "-Wl,--Map=${CMAKE_BINARY_DIR}/revc.map")
  target_compile_options(revc PRIVATE -fno-inline)
  target_compile_options(revc_fpv_mod PRIVATE -fno-inline)
endif()
