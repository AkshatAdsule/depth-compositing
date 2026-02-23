cmake_minimum_required(VERSION 3.15)

project(deep_compositor_demo VERSION 1.0)

# Set C++ Standard (C++17 required for modern features)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Fixes CMake FetchContent warning
cmake_policy(SET CMP0135 NEW)

# Find required packages
find_package(OpenEXR REQUIRED)
find_package(Imath REQUIRED)
find_package(PNG QUIET)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GUI_DEPS REQUIRED gtkmm-4.0 libadwaita-1)

# Option for PNG support
if(PNG_FOUND)
    add_definitions(-DHAS_PNG_SUPPORT)
    message(STATUS "PNG support enabled")
else()
    message(STATUS "PNG support disabled (libpng not found)")
endif()

# include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src ${GUI_DEPS_INCLUDE_DIRS})
link_directories(${GUI_DEPS_LIBRARY_DIRS})

# Main compositor sources
set(COMPOSITOR_SOURCES
    src/main.cpp
    src/utils.cpp
    src/deep_image.cpp
    src/deep_reader.cpp
    src/deep_writer.cpp
    src/deep_compositor.cpp
    src/deep_volume.cpp
)

# Main executable
add_executable(deep_compositor ${COMPOSITOR_SOURCES})

target_link_libraries(deep_compositor
    OpenEXR::OpenEXR
    Imath::Imath
)

if(PNG_FOUND)
    target_link_libraries(deep_compositor PNG::PNG)
endif()


# GUI executable
add_executable(deep_gui src/gui_main.cpp)
target_compile_options(deep_gui PRIVATE ${GUI_DEPS_CFLAGS_OTHER})
target_link_libraries(deep_gui 
    PRIVATE 
    ${GUI_DEPS_LIBRARIES}
)


# Test image generator executable
add_executable(generate_test_images
    test_data/generate_test_images.cpp
    src/utils.cpp
    src/deep_image.cpp
    src/deep_writer.cpp
)

target_link_libraries(generate_test_images
    OpenEXR::OpenEXR
    Imath::Imath
)

if(PNG_FOUND)
    target_link_libraries(generate_test_images PNG::PNG)
endif()

# Create output directory
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/output)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set(COMMON_FLAGS -Wall -Wextra -Wpedantic)
    target_compile_options(deep_compositor PRIVATE ${COMMON_FLAGS})
    target_compile_options(deep_gui PRIVATE ${COMMON_FLAGS})
    target_compile_options(generate_test_images PRIVATE ${COMMON_FLAGS})
endif()

# Install targets
install(TARGETS deep_compositor generate_test_images
    RUNTIME DESTINATION bin
)


# Create a dedicated test driver executable
add_executable(tester src/tester.cpp)

# 2. Add the source files required by the tester logic
target_sources(tester PRIVATE 
    src/utils.cpp
    src/deep_image.cpp
    src/deep_writer.cpp
)

# 3. Link the necessary OpenEXR and Imath libraries
target_link_libraries(tester
    PRIVATE
    OpenEXR::OpenEXR
    Imath::Imath
)

# 4. (Optional) Add PNG support if detected
if(PNG_FOUND)
    target_link_libraries(tester PRIVATE PNG::PNG)
endif()

# 5. Set the output location to your project root or build folder
set_target_properties(tester PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)