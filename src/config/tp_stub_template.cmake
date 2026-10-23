# SPDX-License-Identifier: MIT
# Copyright (c) Kirikiri SDL2 Developers

cmake_minimum_required(VERSION 3.12)
if(DEFINED CMAKE_SYSTEM_NAME AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "The Kirikiri SDL2 plugin SDK supports only macOS (Darwin).")
endif()
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 10.14)
endif()
project(tp_stub_krkrsdl2 LANGUAGES NONE)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "The Kirikiri SDL2 plugin SDK supports only macOS (Darwin).")
endif()
enable_language(CXX)

add_library(tp_stub STATIC tp_stub.cpp)
set_target_properties(tp_stub PROPERTIES CXX_STANDARD 11 POSITION_INDEPENDENT_CODE TRUE)
target_include_directories(tp_stub PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
