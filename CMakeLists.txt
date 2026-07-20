cmake_minimum_required(VERSION 3.16)

project(GUI_for_openEB LANGUAGES CXX VERSION 1.9.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(GUI_FOR_OPENEB_BUILD_TESTS "Build algorithm unit tests" ON)

# GCC 15+ no longer implicitly includes <cstdint>; add it globally so that
# uint8_t / uint16_t etc. used throughout the OpenEB headers stay available.
# See doc/compile.md for details.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "15")
    add_compile_options("-include;cstdint")
endif()

if(APPLE)
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
endif()

# --- External dependencies -------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Svg)
find_package(MetavisionSDK 5.2.0 REQUIRED COMPONENTS base core stream)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs calib3d videoio)

# Qt MOC / UIC / RCC auto-processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# --- Testing ---------------------------------------------------------------
# enable_testing() must be called BEFORE add_subdirectory(gui) so that the
# gui/tests gtest_discover_tests() calls (design §3.11) register with CTest.
enable_testing()

add_subdirectory(algo)
add_subdirectory(gui)

if(GUI_FOR_OPENEB_BUILD_TESTS)
    add_subdirectory(algo/tests)
endif()
