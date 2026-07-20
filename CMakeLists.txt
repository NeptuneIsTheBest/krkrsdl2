# SPDX-License-Identifier: MIT
# Copyright (c) Kirikiri SDL2 Developers

cmake_minimum_required(VERSION 3.16)

# This project intentionally supports one target: macOS 13+ on Apple Silicon.
# Set the target defaults before project() so they are used by compiler checks.
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
	message(FATAL_ERROR "Kirikiri SDL3 can only be built on macOS.")
endif()

if(DEFINED CMAKE_SYSTEM_NAME AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	message(FATAL_ERROR "Kirikiri SDL3 only supports macOS; '${CMAKE_SYSTEM_NAME}' is not supported.")
endif()

if(IOS)
	message(FATAL_ERROR "iOS is not supported. Kirikiri SDL3 only supports macOS.")
endif()

if(DEFINED CMAKE_OSX_SYSROOT)
	string(TOLOWER "${CMAKE_OSX_SYSROOT}" KRKRSDL3_OSX_SYSROOT_LOWER)
	if(KRKRSDL3_OSX_SYSROOT_LOWER MATCHES "iphone|appletv|watch")
		message(FATAL_ERROR "Apple mobile platforms are not supported. Use the macOS SDK.")
	endif()
endif()

if(NOT DEFINED CMAKE_OSX_ARCHITECTURES OR CMAKE_OSX_ARCHITECTURES STREQUAL "")
	set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Supported macOS architecture" FORCE)
endif()
list(LENGTH CMAKE_OSX_ARCHITECTURES KRKRSDL3_ARCH_COUNT)
if(NOT KRKRSDL3_ARCH_COUNT EQUAL 1 OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
	message(FATAL_ERROR
		"Kirikiri SDL3 only supports arm64. x86_64 and Universal binaries are not supported "
		"(CMAKE_OSX_ARCHITECTURES='${CMAKE_OSX_ARCHITECTURES}').")
endif()

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
	set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum supported macOS version" FORCE)
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET MATCHES "^[0-9]+(\\.[0-9]+)*$")
	message(FATAL_ERROR "CMAKE_OSX_DEPLOYMENT_TARGET must be a macOS version number.")
endif()
if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "13.0")
	message(FATAL_ERROR
		"Kirikiri SDL3 requires macOS 13.0 or later "
		"(CMAKE_OSX_DEPLOYMENT_TARGET='${CMAKE_OSX_DEPLOYMENT_TARGET}').")
endif()

if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
	set(CMAKE_POLICY_VERSION_MINIMUM 3.16)
endif()

project(krkrsdl3 LANGUAGES C CXX)

if(NOT APPLE OR NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	message(FATAL_ERROR "Kirikiri SDL3 only supports macOS.")
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.13")
	cmake_policy(SET CMP0077 NEW)
endif()

# Disable install rules while third-party projects are configured.
if(NOT DEFINED _DISABLE_INSTALLATION)
	set(_DISABLE_INSTALLATION FALSE)
	macro(install)
		if(NOT _DISABLE_INSTALLATION)
			_install(${ARGN})
		endif()
	endmacro()
endif()

option(OPTION_ENABLE_ASYNC_IMAGE_LOAD "Enable asynchronous image loading." ON)
option(OPTION_ENABLE_CANVAS "Enable the Canvas interface. Requires OpenGL ES at runtime." OFF)
option(OPTION_ENABLE_CASEINSENSITIVITY "Enable case-insensitive storage paths." ON)
option(OPTION_ENABLE_EXTERNAL_PLUGINS "Enable loading external plugins through Plugins.link." ON)
option(OPTION_BUILD_MACOS_BUNDLE "Build a macOS application bundle instead of a command-line executable." ON)
option(KRKRSDL3_BUILD_ENABLE_WARNINGS "Enable project compiler warnings." OFF)
option(KRKRSDL3_ENABLE_PRECOMPILED_HEADERS "Enable precompiled headers." ON)

if(NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(NOT DEFINED KRKRSDL3_REPO_DIR)
	set(KRKRSDL3_REPO_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

set(MACOS_RESOURCES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/macos"
	CACHE FILEPATH "Path to the macOS application resources.")
set(EMBED_DATA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/data"
	CACHE FILEPATH "Data file or directory embedded in the application bundle.")

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(
	-fno-delete-null-pointer-checks
	KRKRSDL3_COMPILER_SUPPORTS_NO_DELETE_NULL_POINTER_CHECKS
)

add_compile_options(-Wno-declaration-after-statement)

file(STRINGS src/config/src_list/kirikirisdl3/sources.txt KIRIKIRISDL3_SRC)
file(STRINGS src/config/src_list/kirikirisdl3/sources_sse2.txt KIRIKIRISDL3_SSE2_SRC)
file(STRINGS src/config/src_list/kirikirisdl3/includes.txt KIRIKIRISDL3_INCLUDES)

set(KRKRSDL3COMMON_SRC
	${KIRIKIRISDL3_SRC}
	${KIRIKIRISDL3_SSE2_SRC}
)
set(KRKRSDL3COMMON_INCLUDES ${KIRIKIRISDL3_INCLUDES})

# AppleClang 21 predefines TARGET_OS_MAC. These legacy C libraries interpret
# that symbol as classic Mac OS and select headers which no longer exist.
# Undefine it only for those sources; SDL and Cocoa must retain the definition.
set(KRKRSDL3_LEGACY_TARGET_OS_MAC_SRC ${KIRIKIRISDL3_SRC})
list(FILTER KRKRSDL3_LEGACY_TARGET_OS_MAC_SRC INCLUDE REGEX
	"^(external/krkrz/external/freetype/.*\\.c|external/krkrz/external/lpng/.*\\.c|external/zlib/.*\\.c)$")
set_source_files_properties(${KRKRSDL3_LEGACY_TARGET_OS_MAC_SRC}
	PROPERTIES
		COMPILE_OPTIONS "-UTARGET_OS_MAC;-include;${CMAKE_CURRENT_SOURCE_DIR}/src/config/LegacyMacOSCompatibility.h"
		SKIP_PRECOMPILE_HEADERS ON
)

set(KRKRSDL3OPUS_SRC ${KRKRSDL3COMMON_SRC})
list(FILTER KRKRSDL3OPUS_SRC INCLUDE REGEX "^external/krkrz/external/opus")
set_source_files_properties(${KRKRSDL3OPUS_SRC} PROPERTIES
	INCLUDE_DIRECTORIES
	"${CMAKE_CURRENT_SOURCE_DIR}/external/krkrz/external/opus/silk;${CMAKE_CURRENT_SOURCE_DIR}/external/krkrz/external/opus/silk/fixed;${CMAKE_CURRENT_SOURCE_DIR}/external/krkrz/external/opus/silk/float")

# The krkrz SSE2 sources share a header that also exposes unused AVX helpers.
# Preload an SSE-only view so those sources never parse the AVX declarations.
set(KRKRSDL3_SSE2_X86SIMDUTIL_SRC
	external/krkrz/visual/gl/ResampleImageSSE2.cpp
	external/krkrz/visual/gl/adjust_color_sse2.cpp
	external/krkrz/visual/gl/boxblur_sse2.cpp
)
set_source_files_properties(${KRKRSDL3_SSE2_X86SIMDUTIL_SRC} PROPERTIES
	COMPILE_OPTIONS
	"-include;${CMAKE_CURRENT_SOURCE_DIR}/src/config/SSE2OnlyX86SIMDUtil.h")

set(KRKRSDL3_DEFINITIONS
	TVP_COMPILING_KRKRSDL3
	SDL_FUNCTION_POINTER_IS_VOID_POINTER=1
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

set(KRKRSDL3_CFLAGS)
if(KRKRSDL3_BUILD_ENABLE_WARNINGS)
	list(APPEND KRKRSDL3_CFLAGS -Wall)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	list(APPEND KRKRSDL3_CFLAGS -Wno-everything)
else()
	list(APPEND KRKRSDL3_CFLAGS -w)
endif()

set(KRKRSDL3_CXXFLAGS
	${KRKRSDL3_CFLAGS}
	-Wno-non-virtual-dtor
)

if(OPTION_ENABLE_ASYNC_IMAGE_LOAD)
	list(APPEND KRKRSDL3_DEFINITIONS KRKRSDL3_ENABLE_ASYNC_IMAGE_LOAD)
endif()
if(OPTION_ENABLE_CASEINSENSITIVITY)
	list(APPEND KRKRSDL3_DEFINITIONS KRKRZ_CASE_INSENSITIVE)
endif()
if(OPTION_ENABLE_EXTERNAL_PLUGINS)
	list(APPEND KRKRSDL3_DEFINITIONS KRKRSDL3_ENABLE_PLUGINS)
endif()
if(KRKRSDL3_COMPILER_SUPPORTS_NO_DELETE_NULL_POINTER_CHECKS)
	list(APPEND KRKRSDL3_CXXFLAGS -fno-delete-null-pointer-checks)
endif()

if(OPTION_ENABLE_CANVAS)
	list(APPEND KRKRSDL3_DEFINITIONS KRKRZ_ENABLE_CANVAS)
	list(APPEND KRKRSDL3COMMON_SRC
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
		src/core/visual/sdl3/opengl/OpenGLScreenSDL3.cpp
	)
	list(APPEND KRKRSDL3COMMON_INCLUDES
		src/core/visual/sdl3/opengl
		external/krkrz/external/angle/include
		external/krkrz/external/glm
	)
endif()

set(SDL_SHARED OFF CACHE BOOL "Build SDL as a static library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build SDL as a static library" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Disable SDL tests" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Disable SDL test support library" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "Disable SDL installation" FORCE)
add_subdirectory(external/SDL)
get_target_property(SDL3_INCLUDE_DIRS SDL3-static INCLUDE_DIRECTORIES)
set(SDL3_LIBRARIES SDL3::SDL3-static)

set(XNASONG OFF CACHE BOOL "Disable FAudio XNA Song support" FORCE)
set(BUILD_SDL3 ON CACHE BOOL "Build FAudio against SDL3" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static third-party libraries" FORCE)
set(_DISABLE_INSTALLATION_OLD ${_DISABLE_INSTALLATION})
set(_DISABLE_INSTALLATION TRUE)
add_subdirectory(external/FAudio)
set(_DISABLE_INSTALLATION ${_DISABLE_INSTALLATION_OLD})

list(APPEND KRKRSDL3_DEFINITIONS TVP_FAUDIO_IMPLEMENT)

function(krkrsdl3_configure_target target)
	set_target_properties(${target} PROPERTIES
		C_STANDARD 11
		C_STANDARD_REQUIRED YES
		CXX_STANDARD 14
		CXX_STANDARD_REQUIRED YES
	)
	target_compile_definitions(${target} PUBLIC ${KRKRSDL3_DEFINITIONS})
	target_compile_options(${target} PUBLIC
		$<$<COMPILE_LANGUAGE:C>:${KRKRSDL3_CFLAGS}>
		$<$<COMPILE_LANGUAGE:CXX>:${KRKRSDL3_CXXFLAGS}>
	)
	if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.16" AND KRKRSDL3_ENABLE_PRECOMPILED_HEADERS)
		target_precompile_headers(${target} PRIVATE
			"${CMAKE_CURRENT_SOURCE_DIR}/src/core/sdl3/SDLPrecompiledHeader.h")
	endif()
endfunction()

add_library(krkrsdl3common STATIC)
target_sources(krkrsdl3common PRIVATE ${KRKRSDL3COMMON_SRC})
target_include_directories(krkrsdl3common PUBLIC ${KRKRSDL3COMMON_INCLUDES})
find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
target_link_libraries(krkrsdl3common PUBLIC
	SDL3::SDL3-static
	FAudio-static
	${AUDIOTOOLBOX_FRAMEWORK}
)
krkrsdl3_configure_target(krkrsdl3common)

add_executable(krkrsdl3 src/core/sdl3/SDLEntrypoint.cpp)
set_target_properties(krkrsdl3 PROPERTIES OUTPUT_NAME "krkrsdl3")
target_link_libraries(krkrsdl3 PRIVATE krkrsdl3common)
krkrsdl3_configure_target(krkrsdl3)

if(OPTION_BUILD_MACOS_BUNDLE)
	set_target_properties(krkrsdl3 PROPERTIES
		BUNDLE TRUE
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_BUNDLE_NAME "Kirikiri SDL3"
		MACOSX_BUNDLE_BUNDLE_VERSION "0.0.0"
		MACOSX_BUNDLE_COPYRIGHT "(KIRIKIRI core) (C) W.Dee and contributors All Rights Reserved. This software is based in part on the work of Independent JPEG Group."
		MACOSX_BUNDLE_GUI_IDENTIFIER pw.uyjulian.krkrsdl3
		MACOSX_BUNDLE_ICON_FILE "krkrsdl3.icns"
		MACOSX_BUNDLE_LONG_VERSION_STRING "Development version"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "dev"
		XCODE_ATTRIBUTE_SKIP_INSTALL NO
		XCODE_ATTRIBUTE_INSTALL_PATH "/Applications"
	)
	set_property(SOURCE
		"${EMBED_DATA_PATH}"
		"${MACOS_RESOURCES_ROOT}/krkrsdl3.icns"
		PROPERTY MACOSX_PACKAGE_LOCATION "Resources"
	)
	target_sources(krkrsdl3 PRIVATE
		"${EMBED_DATA_PATH}"
		"${MACOS_RESOURCES_ROOT}/krkrsdl3.icns"
	)
endif()

get_directory_property(HAS_PARENT_SCOPE PARENT_DIRECTORY)
if(HAS_PARENT_SCOPE)
	set(KRKRSDL3_NAME krkrsdl3 PARENT_SCOPE)
	set(KRKRSDL3_REPO_DIR "${KRKRSDL3_REPO_DIR}" PARENT_SCOPE)
endif()
