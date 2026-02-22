cmake_minimum_required(VERSION 3.15)
include(FetchContent)


project(deep_compositor_demo VERSION 1.0)

# Set C++ Standard (C++17 required for modern features)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Fixes CMake FetchContent warning
cmake_policy(SET CMP0135 NEW)

# GoogleTest
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

FetchContent_MakeAvailable(googletest)


# Find required packages
find_package(OpenEXR REQUIRED)
find_package(Imath REQUIRED)
find_package(PNG QUIET)

# Option for PNG support
if(PNG_FOUND)
    add_definitions(-DHAS_PNG_SUPPORT)
    message(STATUS "PNG support enabled")
else()
    message(STATUS "PNG support disabled (libpng not found)")
endif()

include_directories(${CMAKE_SOURCE_DIR}/src)

# Core library (shared between main executable, test generator, and tests)
add_library(compositor_lib STATIC
    src/utils.cpp
    src/deep_image.cpp
    src/deep_reader.cpp
    src/deep_writer.cpp
    src/deep_compositor.cpp
    src/deep_volume.cpp
)

target_link_libraries(compositor_lib
    OpenEXR::OpenEXR
    Imath::Imath
)

if(PNG_FOUND)
    target_link_libraries(compositor_lib PNG::PNG)
endif()

target_compile_options(compositor_lib PRIVATE -Wall -Wextra -Wpedantic)

# Main executable
add_executable(deep_compositor src/main.cpp)
target_link_libraries(deep_compositor compositor_lib)

# Test image generator executable
add_executable(generate_test_images test_data/generate_test_images.cpp)
target_link_libraries(generate_test_images compositor_lib)

# Create output directory
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/output)

target_compile_options(deep_compositor PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(generate_test_images PRIVATE -Wall -Wextra -Wpedantic)

# Install targets
install(TARGETS deep_compositor generate_test_images
    RUNTIME DESTINATION bin
)


enable_testing()
include(GoogleTest)

# Unit tests
file(GLOB_RECURSE UNIT_TEST_SOURCES src/tests/unit/*.cpp)
if(UNIT_TEST_SOURCES)
    add_executable(unit_tests ${UNIT_TEST_SOURCES})
    target_link_libraries(unit_tests compositor_lib GTest::gtest_main)
    target_compile_options(unit_tests PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(unit_tests)
endif()

# Integration tests
file(GLOB_RECURSE INTEGRATION_TEST_SOURCES src/tests/integration/*.cpp)
if(INTEGRATION_TEST_SOURCES)
    add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})
    target_link_libraries(integration_tests compositor_lib GTest::gtest_main)
    target_compile_options(integration_tests PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(integration_tests)
endif()