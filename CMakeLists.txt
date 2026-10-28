cmake_minimum_required(VERSION 3.21)

project(Spotlight
    VERSION 0.3.0
    DESCRIPTION "SPOTLIGHT! - all-in-one creative production studio"
    HOMEPAGE_URL "https://github.com/NyteRyzer/spotlight-"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(SPOTLIGHT_BUILD_TESTS "Build unit tests" ON)
option(SPOTLIGHT_BUILD_TOOLS "Build developer tools (screenshot generator)" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Multimedia)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src/core)
add_subdirectory(src/media)
add_subdirectory(src/ui)
add_subdirectory(src/app)

if(SPOTLIGHT_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(SPOTLIGHT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/Packaging.cmake)
