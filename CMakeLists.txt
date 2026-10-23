# SPDX-License-Identifier: MIT
# Copyright (c) Kirikiri SDL2 Developers

cmake_minimum_required(VERSION 3.12)

# Reject explicit cross-platform targets before CMake probes their SDKs.
if(DEFINED CMAKE_SYSTEM_NAME AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "Kirikiri SDL2 supports only macOS (Darwin).")
endif()

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 10.14)
endif()
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.12)
endif()

project(krkrsdl2 LANGUAGES NONE)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "Kirikiri SDL2 supports only macOS (Darwin).")
endif()
enable_language(C CXX)

# Disable install for subprojects
if(NOT DEFINED _DISABLE_INSTALLATION)
set(_DISABLE_INSTALLATION FALSE)
macro(install)
if (NOT _DISABLE_INSTALLATION)
_install(${ARGN})
endif()
endmacro()
endif()

if (${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.13")
cmake_policy(SET CMP0077 NEW)
endif()
option(OPTION_ENABLE_ASYNC_IMAGE_LOAD "Enable async image load." ON)
option(OPTION_ENABLE_CANVAS "Enable Canvas interface. Requires OpenGL ES or ANGLE at runtime." OFF)
option(OPTION_ENABLE_CASEINSENSITIVITY "Enable case insensitivity. May malfunction if directories in the path are not listable." ON)
option(OPTION_ENABLE_SDL_THREADS "Use SDL threads instead of std::thread." OFF)
option(OPTION_ENABLE_EXTERNAL_PLUGINS "Enable external plugins. This will make Plugins.link load a plugin instead of a no-op." ON)
option(OPTION_BUILD_MACOS_BUNDLE "Build an application bundle for macOS." ON)
option(KRKRSDL2_ENABLE_PRECOMPILED_HEADERS "Use precompiled headers when supported by CMake." ON)
option(KRKRSDL2_BUILD_ENABLE_WARNINGS "Enable warnings when building the project." OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type." FORCE)
endif()

if(NOT DEFINED KRKRSDL2_REPO_DIR)
    set(KRKRSDL2_REPO_DIR ${CMAKE_CURRENT_SOURCE_DIR})
endif()

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-fno-delete-null-pointer-checks KRKRSDL2_COMPILER_SUPPORTS_NO_DELETE_NULL_POINTER_CHECKS)

set(MACOS_RESOURCES_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/macos CACHE FILEPATH "The path to the resources for macOS.")
set(EMBED_DATA_PATH ${CMAKE_CURRENT_SOURCE_DIR}/data CACHE FILEPATH "The path to the data file or directory.")
add_compile_options(-Wno-declaration-after-statement)

set(KRKRSDL2_SRC "")
set(KRKRSDL2_INCLUDES "")
set(KRKRSDL2COMMON_SRC "")
set(KRKRSDL2COMMON_INCLUDES "")

file(STRINGS src/config/src_list/kirikirisdl2/sources.txt KIRIKIRISDL2_SRC)
file(STRINGS src/config/src_list/kirikirisdl2/sources_sse2.txt KIRIKIRISDL2_SSE2_SRC)
file(STRINGS src/config/src_list/kirikirisdl2/sources_avx2.txt KIRIKIRISDL2_AVX2_SRC)
file(STRINGS src/config/src_list/kirikirisdl2/includes.txt KIRIKIRISDL2_INCLUDES)

list(APPEND KRKRSDL2COMMON_SRC ${KIRIKIRISDL2_SRC})
list(APPEND KRKRSDL2COMMON_SRC ${KIRIKIRISDL2_SSE2_SRC})
list(APPEND KRKRSDL2COMMON_SRC ${KIRIKIRISDL2_AVX2_SRC})
list(APPEND KRKRSDL2COMMON_INCLUDES ${KIRIKIRISDL2_INCLUDES})

set(KRKRSDL2OPUS_SRC "")
list(APPEND KRKRSDL2OPUS_SRC ${KRKRSDL2COMMON_SRC})
list(FILTER KRKRSDL2OPUS_SRC INCLUDE REGEX "^external/krkrz/external/opus")
set_source_files_properties(${KRKRSDL2OPUS_SRC} PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/external/krkrz/external/opus/silk;${CMAKE_CURRENT_SOURCE_DIR}/external/krkrz/external/opus/silk/fixed;${CMAKE_CURRENT_SOURCE_DIR}/external/krkrz/external/opus/silk/float")

list(APPEND KRKRSDL2COMMON_SRC src/support/roundevenf.c)

# Reuse the project's zlib rather than FreeType's bundled legacy copy,
# whose Byte typedef conflicts with the modern macOS SDK headers.
set_property(SOURCE external/krkrz/external/freetype/src/gzip/ftgzip.c
    APPEND PROPERTY COMPILE_DEFINITIONS FT_CONFIG_OPTION_SYSTEM_ZLIB)

list(APPEND KRKRSDL2_DEFINITIONS
	TVP_COMPILING_KRKRSDL2
	TVP_OPUS_DECODER_IMPLEMENT
	TVP_LOG_TO_COMMANDLINE_CONSOLE
	HAVE_CONFIG_H
	FT2_BUILD_LIBRARY
	USE_ALLOCA
	OPUS_BUILD
	HAVE_LRINTF
	HAVE_LRINT
	FLOAT_APPROX
	PNG_ARM_NEON_OPT=0
)

if(KRKRSDL2_BUILD_ENABLE_WARNINGS)
list(APPEND KRKRSDL2_CFLAGS
	-Wall
)
else()
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
list(APPEND KRKRSDL2_CFLAGS
	-Wno-everything
)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
list(APPEND KRKRSDL2_CFLAGS
	-w
)
endif()
endif()

list(APPEND KRKRSDL2_CXXFLAGS ${KRKRSDL2_CFLAGS})

list(APPEND KRKRSDL2_CXXFLAGS
	-Wno-non-virtual-dtor
)

if(OPTION_ENABLE_ASYNC_IMAGE_LOAD)
list(APPEND KRKRSDL2_DEFINITIONS
	KRKRSDL2_ENABLE_ASYNC_IMAGE_LOAD
)
endif()

if(OPTION_ENABLE_CANVAS)
list(APPEND KRKRSDL2_DEFINITIONS
	KRKRZ_ENABLE_CANVAS
)
list(APPEND KRKRSDL2COMMON_SRC
	external/krkrz/visual/opengl/CanvasIntf.cpp
	external/krkrz/visual/opengl/DrawCycleTimer.cpp
	external/krkrz/visual/opengl/GLFrameBufferObject.cpp
	external/krkrz/visual/opengl/GLShaderUtil.cpp
	external/krkrz/visual/opengl/Matrix32Intf.cpp
	external/krkrz/visual/opengl/Matrix44Intf.cpp
	external/krkrz/visual/opengl/OffscreenIntf.cpp
	external/krkrz/visual/opengl/ShaderProgramIntf.cpp
	external/krkrz/visual/opengl/TextureIntf.cpp
	external/krkrz/visual/opengl/VertexBinderIntf.cpp
	external/krkrz/visual/opengl/VertexBufferIntf.cpp
	src/core/visual/sdl2/opengl/OpenGLScreenSDL2.cpp
)
list(APPEND KRKRSDL2COMMON_INCLUDES
	src/core/visual/sdl2/opengl
	external/krkrz/external/angle/include
	external/krkrz/external/glm
)
endif()

if(OPTION_ENABLE_CASEINSENSITIVITY)
list(APPEND KRKRSDL2_DEFINITIONS
	KRKRZ_CASE_INSENSITIVE
)
endif()

if(OPTION_ENABLE_SDL_THREADS)
list(APPEND KRKRSDL2_DEFINITIONS
	KRKRZ_USE_SDL_THREADS
)
endif()

if(OPTION_ENABLE_EXTERNAL_PLUGINS)
list(APPEND KRKRSDL2_DEFINITIONS
	KRKRSDL2_ENABLE_PLUGINS
)
endif()

if(KRKRSDL2_COMPILER_SUPPORTS_NO_DELETE_NULL_POINTER_CHECKS)
list(APPEND KRKRSDL2_CXXFLAGS
	-fno-delete-null-pointer-checks
)
endif()

set(SDL_SHARED_ENABLED_BY_DEFAULT OFF)
add_subdirectory(external/SDL)
get_target_property(SDL2_INCLUDE_DIRS SDL2-static INCLUDE_DIRECTORIES)
set(SDL2_LIBRARIES SDL2-static)
list(APPEND KRKRSDL2_LIBS SDL2-static)

set(XNASONG OFF CACHE BOOL "Intentionally disabled for FAudio: enable XNA_Song.c" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Intentionally disabled for FAudio: Build shared libraries" FORCE)
set(_DISABLE_INSTALLATION_OLD ${_DISABLE_INSTALLATION})
set(_DISABLE_INSTALLATION TRUE)
add_subdirectory(external/FAudio)
set(_DISABLE_INSTALLATION ${_DISABLE_INSTALLATION_OLD})
list(APPEND KRKRSDL2_LIBS FAudio)
list(APPEND KRKRSDL2_DEFINITIONS TVP_FAUDIO_IMPLEMENT)

set(KRKRSDL2COMMON_NAME krkrsdl2common)

add_library(${KRKRSDL2COMMON_NAME} STATIC)
set_target_properties(${KRKRSDL2COMMON_NAME} PROPERTIES C_STANDARD 11)
set_target_properties(${KRKRSDL2COMMON_NAME} PROPERTIES CXX_STANDARD 14)
target_sources(${KRKRSDL2COMMON_NAME} PRIVATE ${KRKRSDL2COMMON_SRC})
target_compile_definitions(${KRKRSDL2COMMON_NAME} PUBLIC ${KRKRSDL2_DEFINITIONS})
target_compile_options(${KRKRSDL2COMMON_NAME} PUBLIC $<$<COMPILE_LANGUAGE:C>:${KRKRSDL2_CFLAGS}>)
target_compile_options(${KRKRSDL2COMMON_NAME} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:${KRKRSDL2_CXXFLAGS}>)
target_include_directories(${KRKRSDL2COMMON_NAME} PUBLIC ${KRKRSDL2COMMON_INCLUDES})
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.13.0")
target_link_options(${KRKRSDL2COMMON_NAME} PUBLIC ${KRKRSDL2_LDFLAGS})
endif()
target_link_libraries(${KRKRSDL2COMMON_NAME} ${KRKRSDL2_LIBS})
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.16.0")
if(KRKRSDL2_ENABLE_PRECOMPILED_HEADERS)
target_precompile_headers(${KRKRSDL2COMMON_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core/sdl2/SDLPrecompiledHeader.h)
endif()
endif()

list(APPEND KRKRSDL2_LIBS ${KRKRSDL2COMMON_NAME})
list(APPEND KRKRSDL2_SRC src/core/sdl2/SDLEntrypoint.cpp)
set(KRKRSDL2_NAME krkrsdl2)
add_executable(${KRKRSDL2_NAME})
set_target_properties(${KRKRSDL2_NAME} PROPERTIES C_STANDARD 11)
set_target_properties(${KRKRSDL2_NAME} PROPERTIES CXX_STANDARD 14)
target_sources(${KRKRSDL2_NAME} PRIVATE ${KRKRSDL2_SRC})
target_compile_definitions(${KRKRSDL2_NAME} PUBLIC ${KRKRSDL2_DEFINITIONS})
target_compile_options(${KRKRSDL2_NAME} PUBLIC $<$<COMPILE_LANGUAGE:C>:${KRKRSDL2_CFLAGS}>)
target_compile_options(${KRKRSDL2_NAME} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:${KRKRSDL2_CXXFLAGS}>)
target_include_directories(${KRKRSDL2_NAME} PUBLIC ${KRKRSDL2_INCLUDES})
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.13.0")
target_link_options(${KRKRSDL2_NAME} PUBLIC ${KRKRSDL2_LDFLAGS})
endif()
target_link_libraries(${KRKRSDL2_NAME} ${KRKRSDL2_LIBS})
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.16.0")
if(KRKRSDL2_ENABLE_PRECOMPILED_HEADERS)
target_precompile_headers(${KRKRSDL2_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core/sdl2/SDLPrecompiledHeader.h)
endif()
endif()

if(OPTION_BUILD_MACOS_BUNDLE)
set_target_properties(${KRKRSDL2_NAME} PROPERTIES
	BUNDLE TRUE
	MACOSX_BUNDLE TRUE
	MACOSX_BUNDLE_BUNDLE_NAME "Kirikiri SDL2"
	MACOSX_BUNDLE_BUNDLE_VERSION "0.0.0"
	MACOSX_BUNDLE_COPYRIGHT "(KIRIKIRI core) (C) W.Dee and contributors All Rights Reserved. This software is based in part on the work of Independent JPEG Group. For details: Run this program with '-about' option."
	MACOSX_BUNDLE_GUI_IDENTIFIER pw.uyjulian.krkrsdl2
	MACOSX_BUNDLE_ICON_FILE "krkrsdl2.icns"
	MACOSX_BUNDLE_LONG_VERSION_STRING "Development version"
	MACOSX_BUNDLE_SHORT_VERSION_STRING "dev"
	XCODE_ATTRIBUTE_SKIP_INSTALL NO
	XCODE_ATTRIBUTE_INSTALL_PATH "/Applications"
)
set_property(
	SOURCE ${EMBED_DATA_PATH} ${MACOS_RESOURCES_ROOT}/krkrsdl2.icns
	PROPERTY MACOSX_PACKAGE_LOCATION "Resources"
)
target_sources(${KRKRSDL2_NAME} PRIVATE 
	${EMBED_DATA_PATH}
	${MACOS_RESOURCES_ROOT}/krkrsdl2.icns
)
endif()

get_directory_property(HAS_PARENT_SCOPE PARENT_DIRECTORY)
if(HAS_PARENT_SCOPE)
# Share certain variables to projects that include this one
set(KRKRSDL2_NAME ${KRKRSDL2_NAME} PARENT_SCOPE)
set(KRKRSDL2_REPO_DIR ${KRKRSDL2_REPO_DIR} PARENT_SCOPE)
endif()
