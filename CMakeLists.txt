# Copyright (c) 2025, Sascha Willems
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
include(FetchContent)
set(NAME ModernVkTriangle)
project(${NAME})

include_directories(external)
include_directories(external/vulkan)
include_directories(external/glm)
include_directories(external/volk)
include_directories(external/vma)
include_directories(external/tinyobj)
include_directories(external/ktx/include)
include_directories(external/ktx/other_include)

FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.0.1
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM)
FetchContent_MakeAvailable(SFML)

OPTION(USE_D2D_WSI "Build the project using Direct to Display swapchain" OFF)
OPTION(USE_WAYLAND_WSI "Build the project using Wayland swapchain" OFF)

set(KTX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/ktx)
set(KTX_SOURCES
    ${KTX_DIR}/lib/texture.c
    ${KTX_DIR}/lib/hashlist.c
    ${KTX_DIR}/lib/checkheader.c
    ${KTX_DIR}/lib/swap.c
    ${KTX_DIR}/lib/memstream.c
    ${KTX_DIR}/lib/filestream.c
    ${KTX_DIR}/lib/vkloader.c)

add_library(ktx STATIC ${KTX_SOURCES})

OPTION(USE_D2D_WSI "Build the project using Direct to Display swapchain" OFF)
OPTION(USE_WAYLAND_WSI "Build the project using Wayland swapchain" OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNOMINMAX -D_USE_MATH_DEFINES")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")

add_executable(${NAME} src/main.cpp)
add_definitions(-D_CRT_SECURE_NO_WARNINGS -DVK_NO_PROTOTYPES)
target_compile_features(${NAME} PRIVATE cxx_std_20)
target_link_libraries(${NAME} PRIVATE SFML::Graphics ktx $ENV{VULKAN_SDK}/Lib/slang.lib)