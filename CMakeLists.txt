cmake_minimum_required(VERSION 3.16)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

if(APPLE)
  if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    # ARM Macs start at OS 11.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum OS X deployment version")
  else()
    # SDL2 requires at least this version for everything to work properly.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9" CACHE STRING "Minimum OS X deployment version")
  endif()
endif()

project(pd C CXX)

include(TargetArch)
include(FindPkgConfig)
include(AssetUtils)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# On first build set the build type to RelWithDebInfo if it's not already set
if (NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt)
  if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
  endif()
endif()

# Options

set(ROMID "ntsc-final" CACHE STRING "Which ROM to build an executable for: ntsc-final, pal-final or jpn-final")
option(FORCE_CROSSCOMPILE "Force cross compiling" NO)

# Determine target arch

target_architecture(TARGET_ARCH)
if(TARGET_ARCH STREQUAL "i386")
  # for historical reasons
  set(TARGET_ARCH "i686")
endif()

# TODO
set(TARGET_IS_BIG_ENDIAN FALSE)

if((TARGET_ARCH MATCHES "unknown") OR (TARGET_ARCH STREQUAL ""))
  message(FATAL_ERROR "Unknown target arch")
elseif(TARGET_ARCH MATCHES "64")
  set(TARGET_IS_64BIT TRUE)
else()
  set(TARGET_IS_64BIT FALSE)
endif()

message(STATUS "Target arch: ${TARGET_ARCH}")

# Determine target OS and platform identifier ($arch-$os)

if(WIN32)
  # Windows
  set(TARGET_OS "windows")
elseif(APPLE)
  # OSX
  set(TARGET_OS "osx")
elseif(NINTENDO_SWITCH)
  # Switch
  set(TARGET_OS "nswitch")
elseif(ANDROID)
  # Android
  set(TARGET_OS "android")
elseif(UNIX)
  # Generic *nix; assume Linux
  set(TARGET_OS "linux")
else()
  # ???
  message(FATAL_ERROR "Unknown target OS or compiler")
endif()

set(TARGET_PLATFORM "${TARGET_ARCH}-${TARGET_OS}")

message(STATUS "Target platform: ${TARGET_PLATFORM}")

# Calculate version info and binary name

set(VERSION_IS_PAL 0)

if(ROMID STREQUAL "ntsc-final")
  set(VERSION 2)
  set(BIN_REGION "")
elseif(ROMID STREQUAL "pal-final")
  set(VERSION 4)
  set(BIN_REGION ".pal")
  set(VERSION_IS_PAL 1)
elseif(ROMID STREQUAL "jpn-final")
  set(VERSION 5)
  set(BIN_REGION ".jpn")
else()
  message(FATAL_ERROR "Unknown ROM ID: ${ROMID}")
endif()

# git commit hash
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE VERSION_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# git branch name
execute_process(
  COMMAND git rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE VERSION_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(VERSION_TARGET "${TARGET_PLATFORM}")
set(VERSION_ARCH "${TARGET_ARCH}")
set(VERSION_ROMID "${ROMID}")
set(VERSION_BUILD "${CMAKE_BUILD_TYPE}")

if((VERSION_BRANCH STREQUAL "") OR (VERSION_BRANCH STREQUAL "HEAD"))
  set(VERSION_BRANCH "port-custom")
endif()

message(STATUS "Version: ${VERSION_ROMID}, hash ${VERSION_HASH}, branch ${VERSION_BRANCH}")

configure_file("${CMAKE_SOURCE_DIR}/port/include/versioninfo.h.in" "${CMAKE_BINARY_DIR}/port/include/versioninfo.h" @ONLY)

set(ASSET_DIR "src/assets/${ROMID}")
set(GENERATED_DIR "src/generated/${ROMID}")

set(ENV{ROMID} ${ROMID})

# Build folder postfix

set(BUILD_ID "${VERSION_ROMID}-${VERSION_TARGET}")

# Compiler defs

add_definitions(
  -DMATCHING=0
  -DPIRACYCHECKS=0
  -DROM_SIZE=32
  -DAVOID_UB=1
  -D_LANGUAGE_C=1
  -DPAL=${VERSION_IS_PAL}
  -DVERSION=${VERSION}
)

if(TARGET_IS_64BIT)
  add_definitions(-DPLATFORM_64BIT=1)
endif()

if(ANDROID)
  add_definitions(-DANDROID=1)
endif()

# Compiler flags

# Include directories
include_directories("${CMAKE_SOURCE_DIR}/include")
include_directories("${CMAKE_SOURCE_DIR}/include/PR")
include_directories("${CMAKE_SOURCE_DIR}/src/include")
include_directories("${CMAKE_BINARY_DIR}/src/generated/${ROMID}")
include_directories("${CMAKE_SOURCE_DIR}/src/lib/ultra/audio")
include_directories("${CMAKE_SOURCE_DIR}/port/include")
include_directories("${CMAKE_BINARY_DIR}/port/include")
include_directories("${CMAKE_BINARY_DIR}/port/vr")
include_directories("${CMAKE_SOURCE_DIR}/openxr/include/openxr")
include_directories("${CMAKE_BINARY_DIR}/port/vr/imgui")


# Set Release optimization to -Og until I fix the -O2 issues
if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
  add_compile_options(-Og)
endif()

# Common CFLAGS   
add_compile_options(
  -fno-inline-functions
  -fno-strict-aliasing
  -funsigned-char
  -fwrapv
  -include "versioninfo.h"
)

# Common warning flags
add_compile_options(
  -Wall
  -Wno-address
  -Wno-int-in-bool-context
  -Wno-misleading-indentation
  -Wno-missing-braces
  -Wno-multichar
  -Wno-tautological-compare
  -Wno-unused-but-set-variable
  -Wno-unused-value
  -Wno-unused-variable
  -fdiagnostics-color=always
)

# C specific flags
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-pointer-sign>)

# C++ specific flags
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-function>)

# Platform-specific flags
if (APPLE)
  # Make it look for libs in the executable folder manually since CMake's rpath system is wack
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rpath @executable_path")
elseif(ANDROID)
  # Android NDK enforces format-security as error, but our langGet() calls are safe
  add_compile_options(-Wno-format-security)
elseif(NOT ANDROID)
  # This warning is not recognized by XCode/Clang for some reason, and also not by Android NDK clang.
  add_compile_options(-Wno-format-truncation)
endif()

# Dependencies

set(LIBS )

# SDL2
if(ANDROID)
  # For Android, include dependencies from android.cmake
  include(cmake/android.cmake)
  list(APPEND LIBS SDL2::SDL2)
else()
  find_package(SDL2 REQUIRED)
  if(NINTENDO_SWITCH)
    if(NOT SDL2_LIBRARY OR NOT SDL2_INCLUDE_DIR)
      # sometimes FindSDL2 just wants us to suffer and does not set these
      set(SDL2_LIBRARY "SDL2")
      set(SDL2_INCLUDE_DIR "${DEVKITPRO}/portlibs/switch/include/SDL2")
    endif()
  endif()
  include_directories(SYSTEM "${SDL2_INCLUDE_DIR}")
  list(APPEND LIBS "${SDL2_LIBRARY}")
endif()

# ZLib
find_package(ZLIB REQUIRED)
if(ZLIB_INCLUDE_DIR)
  include_directories(SYSTEM "${ZLIB_INCLUDE_DIR}")
endif()
list(APPEND LIBS "${ZLIB_LIBRARY}")

# GL
if(NOT GL_LIBRARY)
  if(WIN32)
    set(GL_LIBRARY opengl32)
  elseif(NINTENDO_SWITCH)
    set(GL_LIBRARY EGL glapi drm_nouveau)
  elseif(ANDROID)
    set(GL_LIBRARY GLESv3 EGL)
  elseif(APPLE)
    set(GL_LIBRARY "-framework OpenGL")
  else()
    set(GL_LIBRARY GL)
  endif()
endif()
list(APPEND LIBS "${GL_LIBRARY}")

# Platform-specific extra libs
if(NOT EXTRA_LIBRARIES)
  if(WIN32)
    set(EXTRA_LIBRARIES dbghelp -static-libstdc++)
  elseif(NINTENDO_SWITCH)
    set(EXTRA_LIBRARIES stdc++ pthread)
  elseif(ANDROID)
    set(EXTRA_LIBRARIES log android)
  else()
    set(EXTRA_LIBRARIES stdc++ m dl)
  endif()
endif()
list(APPEND LIBS "${EXTRA_LIBRARIES}")

# Sources

file(GLOB_RECURSE SRC_PORT "${CMAKE_SOURCE_DIR}/port/*.c")
file(GLOB_RECURSE SRC_PORT_CXX "${CMAKE_SOURCE_DIR}/port/*.cpp")

file(GLOB_RECURSE SRC_GAME "${CMAKE_SOURCE_DIR}/src/game/*.c")

file(GLOB SRC_LIB_MP3 "${CMAKE_SOURCE_DIR}/src/lib/mp3/*.c")
file(GLOB SRC_LIB_NAUDIO "${CMAKE_SOURCE_DIR}/src/lib/naudio/*.c")
file(GLOB SRC_LIB_ULTRA_AUDIO "${CMAKE_SOURCE_DIR}/src/lib/ultra/audio/*.c")
file(GLOB SRC_LIB_GU "${CMAKE_SOURCE_DIR}/src/lib/ultra/gu/*.c")
set(SRC_LIB
  src/lib/ultra/io/vimodentsclan1.c
  src/lib/ultra/io/vimodepallan1.c
  src/lib/ultra/io/vimodempallan1.c
  src/lib/ultra/io/vitbl.c
  src/lib/ailist.c
  src/lib/anim.c
  src/lib/args.c
  src/lib/audiodma.c
  src/lib/audiomgr.c
  src/lib/base.c
  src/lib/collision.c
  src/lib/debughud.c
  src/lib/dma.c
  src/lib/fault.c
  src/lib/joy.c
  src/lib/lib_2f490_c.c
  src/lib/lib_2fc60.c
  src/lib/lib_3e3e0.c
  src/lib/lib_04f60nb.c
  src/lib/lib_17ce0.c
  src/lib/lib_034d0.c
  src/lib/lib_39c80.c
  src/lib/lib_15850.c
  src/lib/mema.c
  src/lib/memp.c
  src/lib/model.c
  src/lib/modelasm_c.c
  src/lib/mp3.c
  src/lib/mtx_c.c
  src/lib/mtx.c
  src/lib/music.c
  src/lib/path.c
  src/lib/profile.c
  src/lib/rdp.c
  src/lib/rmon.c
  src/lib/rng_c.c
  src/lib/rzip_c.c
  src/lib/snd.c
  src/lib/speaker.c
  src/lib/varsinit.c
  src/lib/vi.c
  src/textureconfig.c
)
list(PREPEND SRC_LIB "${CMAKE_SOURCE_DIR}")

set(SRC
  ${SRC_GAME}
  ${SRC_PORT}
  ${SRC_PORT_CXX}
  ${SRC_LIB_MP3}
  ${SRC_LIB_NAUDIO}
  ${SRC_LIB_ULTRA_AUDIO}
  ${SRC_LIB_GU}
  ${SRC_LIB}
)

if(WIN32)
  list(APPEND SRC "${CMAKE_SOURCE_DIR}/dist/windows/icon.rc")
endif()

# Asset files

generate_asset_headers("lang/" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mklang" "en" JSON_HEADERS)
generate_asset_headers("pads/" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mkpads" "" JSON_HEADERS)
generate_asset_headers("tiles/" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mktiles" "" JSON_HEADERS)
generate_asset_headers("animations.json" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mkanims" "" JSON_HEADERS)
generate_asset_headers("sequences.json" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mksequences" "" JSON_HEADERS)
generate_asset_headers("textures.json" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mktextures" "" JSON_HEADERS)

add_custom_target(pd_headers ALL DEPENDS ${JSON_HEADERS})

# Game target

set(BIN_NAME "pd${BIN_REGION}.${TARGET_ARCH}")
if(ANDROID)
  add_library(pd SHARED ${SRC})
else()
  add_executable(pd ${SRC})
endif()
add_dependencies(pd pd_headers)
target_link_libraries(pd ${LIBS})
if(NOT ANDROID)
  set_target_properties(pd PROPERTIES OUTPUT_NAME "${BIN_NAME}")
endif()
if(WIN32)
  set_target_properties(pd PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Additional commands for specific platforms.

if(NINTENDO_SWITCH)
  add_custom_command(TARGET pd POST_BUILD
    COMMAND nacptool --create "Perfect Dark" "fgsfds and contributors" "${VERSION_ROMID}/${VERSION_BRANCH}/${VERSION_HASH}" "${BIN_NAME}.nacp"
    COMMAND aarch64-none-elf-strip -o ${CMAKE_BINARY_DIR}/${BIN_NAME}.stripped.elf ${CMAKE_BINARY_DIR}/${BIN_NAME}.elf
    COMMAND elf2nro ${CMAKE_BINARY_DIR}/${BIN_NAME}.stripped.elf ${CMAKE_BINARY_DIR}/${BIN_NAME}.nro --icon=${CMAKE_SOURCE_DIR}/dist/nswitch/icon.jpg --nacp=${BIN_NAME}.nacp
  )
endif()

# ---------------------------------------------------------------
# OpenXR
# Android & Windows : loader pré-compilé fourni dans le repo
# Autres (Linux, etc.) : find_package système
# ---------------------------------------------------------------
if(ANDROID)
  message(STATUS "OpenXR: using bundled Android loader")
  add_compile_definitions(XR_USE_PLATFORM_ANDROID=1)
  include_directories(${CMAKE_SOURCE_DIR}/OpenXR/include)
  add_library(openxr_loader SHARED IMPORTED)
  set_target_properties(openxr_loader PROPERTIES
    IMPORTED_LOCATION
    ${CMAKE_SOURCE_DIR}/OpenXR/Libs/Android/arm64-v8a/Debug/libopenxr_loader.so
  )

elseif(WIN32)
  message(STATUS "OpenXR: using bundled Windows loader")
  add_compile_definitions(XR_USE_PLATFORM_WIN32=1)
  include_directories(${CMAKE_SOURCE_DIR}/OpenXR/include)
  add_library(openxr_loader SHARED IMPORTED)

  if(TARGET_IS_64BIT)
    # x64 — loader 64-bit
    set_target_properties(openxr_loader PROPERTIES
      IMPORTED_LOCATION  ${CMAKE_SOURCE_DIR}/OpenXR/Libs/Windows/x64/openxr_loader.dll
      IMPORTED_IMPLIB    ${CMAKE_SOURCE_DIR}/OpenXR/Libs/Windows/x64/openxr_loader.lib
    )
    message(STATUS "OpenXR: Windows x64 loader selected")
  else()
    # x86 (i686) — loader 32-bit
    set_target_properties(openxr_loader PROPERTIES
      IMPORTED_LOCATION  ${CMAKE_SOURCE_DIR}/OpenXR/Libs/Windows/x32/openxr_loader.dll
      IMPORTED_IMPLIB    ${CMAKE_SOURCE_DIR}/OpenXR/Libs/Windows/x32/openxr_loader.lib
    )
    message(STATUS "OpenXR: Windows x86 (32-bit) loader selected")
  endif()

else()
  # Linux, macOS, etc. — find_package système
  message(STATUS "OpenXR: using system package")
  find_package(OpenXR REQUIRED)
  if(OpenXR_FOUND)
    list(APPEND LIBS OpenXR::openxr_loader)
  else()
    list(APPEND LIBS "openxr_loader")
  endif()
endif()


if(WIN32)
    target_link_libraries(pd urlmon)
endif()
if(ANDROID OR WIN32)
  target_link_libraries(pd openxr_loader)
endif()
if(ANDROID)
  target_link_libraries(pd android log)
endif()

if(WIN32)
  if(TARGET_IS_64BIT)
    set(OPENXR_DLL "${CMAKE_SOURCE_DIR}/OpenXR/Libs/Windows/x64/openxr_loader.dll")
  else()
    set(OPENXR_DLL "${CMAKE_SOURCE_DIR}/OpenXR/Libs/Windows/x32/openxr_loader.dll")
  endif()

  add_custom_command(TARGET pd POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${OPENXR_DLL}"
      "$<TARGET_FILE_DIR:pd>/openxr_loader.dll"
    COMMENT "Copying openxr_loader.dll to output directory"
  )
endif()

if(WIN32)
    set(LOGO_IMG "${CMAKE_SOURCE_DIR}/logo.bmp")
  add_custom_command(TARGET pd POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${LOGO_IMG}"
      "$<TARGET_FILE_DIR:pd>/logo.bmp"
    COMMENT "Copying logo.bmp to output directory"
  )
endif()
# ---------------------------------------------------------------
