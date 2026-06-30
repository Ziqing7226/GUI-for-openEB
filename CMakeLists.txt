cmake_minimum_required(VERSION 3.16)

project(GUI_for_openEB LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# GCC 15+ no longer implicitly includes <cstdint>; add it globally so that
# uint8_t / uint16_t etc. used throughout the OpenEB headers stay available.
# See doc/compile.md for details.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "15")
    add_compile_options("-include;cstdint")
endif()

# --- External dependencies -------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
find_package(MetavisionSDK 5.2.0 REQUIRED COMPONENTS base core stream)
find_package(OpenCV REQUIRED)

# Qt MOC / UIC / RCC auto-processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_subdirectory(algo)
add_subdirectory(gui)
