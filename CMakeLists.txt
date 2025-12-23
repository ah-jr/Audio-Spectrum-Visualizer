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

# Build options
option(BUILD_STANDALONE "Build standalone application" ON)
option(BUILD_VST3 "Build VST3 plugin" OFF)

# ============================================================================
# Core library (shared between standalone and VST)
# ============================================================================
set(CORE_SOURCES
    src/fft.cpp
)

set(CORE_HEADERS
    src/fft.hpp
    src/eq_processor.hpp
)

add_library(SpectrumCore STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_include_directories(SpectrumCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(WIN32)
    target_compile_definitions(SpectrumCore PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()

# ============================================================================
# Standalone Application
# ============================================================================
if(BUILD_STANDALONE)
    message(STATUS "Building standalone application...")
    
    include(FetchContent)
    
    # Fetch raylib
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
    
    set(STANDALONE_SOURCES
        src/main.cpp
        src/audio_analyzer.cpp
        src/spectrum_visualizer.cpp
        src/file_dialog.cpp
    )
    
    set(STANDALONE_HEADERS
        src/audio_analyzer.hpp
        src/spectrum_visualizer.hpp
        src/file_dialog.hpp
    )
    
    add_executable(AudioSpectrumVisualizer ${STANDALONE_SOURCES} ${STANDALONE_HEADERS})
    
    target_include_directories(AudioSpectrumVisualizer PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external
    )
    
    target_link_libraries(AudioSpectrumVisualizer PRIVATE 
        SpectrumCore
        raylib
    )
    
    if(WIN32)
        target_link_libraries(AudioSpectrumVisualizer PRIVATE winmm ole32)
        target_compile_definitions(AudioSpectrumVisualizer PRIVATE NOMINMAX)
    elseif(APPLE)
        target_link_libraries(AudioSpectrumVisualizer PRIVATE 
            "-framework CoreAudio" 
            "-framework AudioToolbox"
            "-framework CoreFoundation"
        )
    elseif(UNIX)
        target_link_libraries(AudioSpectrumVisualizer PRIVATE pthread m dl)
    endif()
endif()

# ============================================================================
# VST3 Plugin
# ============================================================================
if(BUILD_VST3)
    message(STATUS "Building VST3 plugin...")
    
    set(VST3_SDK_ROOT "" CACHE PATH "Path to VST3 SDK")
    
    # Check for VST3 SDK
    if(NOT VST3_SDK_ROOT)
        # Try common locations
        set(_vst3_search_paths
            "$ENV{VST3_SDK_ROOT}"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vst3sdk"
            "C:/SDKs/vst3sdk"
            "C:/VST_SDK/vst3sdk"
        )
        foreach(_path ${_vst3_search_paths})
            if(EXISTS "${_path}/CMakeLists.txt")
                set(VST3_SDK_ROOT "${_path}")
                break()
            endif()
        endforeach()
    endif()
    
    if(NOT VST3_SDK_ROOT OR NOT EXISTS "${VST3_SDK_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR 
            "VST3 SDK not found!\n"
            "Please download it and set VST3_SDK_ROOT:\n"
            "  1. git clone --recursive https://github.com/steinbergmedia/vst3sdk.git\n"
            "  2. cmake .. -DBUILD_VST3=ON -DVST3_SDK_ROOT=path/to/vst3sdk\n"
            "\n"
            "Or place it in: ${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk"
        )
    endif()
    
    message(STATUS "Using VST3 SDK: ${VST3_SDK_ROOT}")
    
    # Add VST3 SDK
    set(SMTG_ADD_VSTGUI OFF CACHE BOOL "" FORCE)  # We don't need VSTGUI
    set(SMTG_ADD_VST3_PLUGINS_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ADD_VST3_HOSTING_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)  # Skip validator
    set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)  # Skip symlink (needs Admin)
    
    if(NOT TARGET sdk)
        add_subdirectory(${VST3_SDK_ROOT} ${CMAKE_BINARY_DIR}/vst3sdk EXCLUDE_FROM_ALL)
    endif()
    
    set(VST_SOURCES
        vst/plugin_processor.cpp
        vst/plugin_controller.cpp
        vst/plugin_editor.cpp
        vst/plugin_entry.cpp
        vst/gl_renderer.cpp
    )
    
    set(VST_HEADERS
        vst/plugin_processor.hpp
        vst/plugin_controller.hpp
        vst/plugin_editor.hpp
        vst/plugin_ids.hpp
        vst/gl_renderer.hpp
        vst/shared_data.hpp
    )
    
    set(target SpectrumEQ)
    
    smtg_add_vst3plugin(${target} ${VST_SOURCES} ${VST_HEADERS})
    
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/vst
    )
    
    target_link_libraries(${target} PRIVATE
        SpectrumCore
        sdk
    )
    
    if(WIN32)
        target_compile_definitions(${target} PRIVATE NOMINMAX _USE_MATH_DEFINES)
        target_link_libraries(${target} PRIVATE opengl32 gdi32)
    elseif(APPLE)
        find_package(OpenGL REQUIRED)
        target_link_libraries(${target} PRIVATE OpenGL::GL)
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(${target} PRIVATE OpenGL::GL)
    endif()
    
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "com.spectrumeq.vst3"
        COMPANY_NAME "SpectrumEQ"
    )
endif()

# Copy sample audio file for testing (if exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/audio")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/audio" DESTINATION "${CMAKE_BINARY_DIR}")
endif()
