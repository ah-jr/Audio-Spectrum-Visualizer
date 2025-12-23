cmake_minimum_required(VERSION 3.16)

# Fix for older CMake compatibility in dependencies
if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

project(AudioSpectrumVisualizer VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Fetch raylib (using 5.5 which has updated CMake requirements)
include(FetchContent)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(raylib)

# Download miniaudio header if not present
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/miniaudio.h")
    message(STATUS "Downloading miniaudio.h...")
    file(DOWNLOAD 
        "https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/miniaudio.h"
        SHOW_PROGRESS
    )
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/fft.cpp
    src/audio_analyzer.cpp
    src/spectrum_visualizer.cpp
    src/file_dialog.cpp
)

set(HEADERS
    src/fft.hpp
    src/audio_analyzer.hpp
    src/spectrum_visualizer.hpp
    src/file_dialog.hpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

# Platform-specific settings
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm ole32)
elseif(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        "-framework CoreAudio" 
        "-framework AudioToolbox"
        "-framework CoreFoundation"
    )
elseif(UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread m dl)
endif()

# Copy sample audio file for testing (if exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/audio")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/audio" DESTINATION "${CMAKE_BINARY_DIR}")
endif()

