# SPDX-License-Identifier: AGPL-3.0-or-later
# Essentia CHOP Suite — builds 5 TD CHOP plugins (one per operator type)
#   EssentiaSpectrumCHOP   — shared FFT provider
#   EssentiaSpectralCHOP   — unified real-time + batch spectral analysis
#   EssentiaTonalCHOP      — unified real-time + batch tonal analysis
#   EssentiaRhythmCHOP     — unified real-time + batch rhythm analysis
#   EssentiaLoudnessCHOP   — unified real-time + batch loudness analysis
#
# Prerequisites:
#   1. Copy CHOP_CPlusPlusBase.h and CPlusPlus_Common.h from the TouchDesigner
#      C++ SDK samples into this directory (src/).
#   2. Place pre-compiled Essentia static lib + headers under vendor/essentia/:
#        vendor/essentia/include/   — Essentia headers (essentia/, essentia/standard/, ...)
#        vendor/essentia/lib/       — essentia.lib  (Windows, MSVC x64 static)
#                              or     libessentia.a  (macOS, arm64 static)
#
# Build (Windows):
#   cd src
#   cmake -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
#
# Build (macOS):
#   cd src
#   cmake -B build
#   cmake --build build --config Release
#
# Deploy:
#   Windows — Copy the 5 .dll files to your TD Plugins folder
#   macOS   — Copy the 5 .plugin bundles to ~/Library/Application Support/Derivative/TouchDesigner/Plugins/

cmake_minimum_required(VERSION 3.20)
project(EssentiaCHOPSuite LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Platform — macOS toolchain settings
# ---------------------------------------------------------------------------
if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -stdlib=libc++")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS architectures")
    endif()
endif()

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
set(ESSENTIA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/essentia")
set(ESSENTIA_INCLUDE "${ESSENTIA_DIR}/include")
set(ESSENTIA_LIB_DIR "${ESSENTIA_DIR}/lib")

# Verify essentia headers exist
if(NOT EXISTS "${ESSENTIA_INCLUDE}/essentia/algorithmfactory.h")
    message(FATAL_ERROR
        "Essentia headers not found at ${ESSENTIA_INCLUDE}/essentia/.\n"
        "Place Essentia include tree under vendor/essentia/include/")
endif()

# Verify TD SDK headers exist
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/CHOP_CPlusPlusBase.h")
    message(FATAL_ERROR
        "TouchDesigner SDK headers not found.\n"
        "Copy CHOP_CPlusPlusBase.h and CPlusPlus_Common.h from the TD SDK into src/")
endif()

# ---------------------------------------------------------------------------
# Common settings for all targets
# ---------------------------------------------------------------------------
find_library(ESSENTIA_LIB essentia PATHS "${ESSENTIA_LIB_DIR}" NO_DEFAULT_PATH)
if(NOT ESSENTIA_LIB)
    message(FATAL_ERROR
        "Essentia static library not found in ${ESSENTIA_LIB_DIR}.\n"
        "Expected essentia.lib (Windows) or libessentia.a (macOS).")
endif()

# Output directories
if(APPLE)
    set(PLUGIN_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/plugin")
else()
    set(PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}")
endif()

# Shared sources included in every target
set(SHARED_HEADERS
    CHOP_CPlusPlusBase.h
    CPlusPlus_Common.h
    Shared/RingBuffer.h
    Shared/Utils.h
    Shared/EssentiaInit.h
    Shared/BatchCommon.h
    Shared/BatchFrameProcessor.h
    Shared/AsyncBatchRunner.h
    Shared/UnifiedCHOPBase.h
)

# Helper macro: define one CHOP plugin target
macro(add_chop_target TARGET_NAME)
    if(APPLE)
        add_library(${TARGET_NAME} MODULE ${ARGN} ${SHARED_HEADERS})
        set_target_properties(${TARGET_NAME} PROPERTIES
            PREFIX ""
            BUNDLE TRUE
            BUNDLE_EXTENSION "plugin"
        )
        foreach(_cfg LIBRARY_OUTPUT_DIRECTORY
                     LIBRARY_OUTPUT_DIRECTORY_DEBUG
                     LIBRARY_OUTPUT_DIRECTORY_RELEASE
                     LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO
                     LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL
                     RUNTIME_OUTPUT_DIRECTORY
                     RUNTIME_OUTPUT_DIRECTORY_DEBUG
                     RUNTIME_OUTPUT_DIRECTORY_RELEASE
                     RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
                     RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL)
            set_target_properties(${TARGET_NAME} PROPERTIES ${_cfg} "${PLUGIN_OUTPUT_DIR}")
        endforeach()
    else()
        add_library(${TARGET_NAME} SHARED ${ARGN} ${SHARED_HEADERS})
    endif()

    target_include_directories(${TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${ESSENTIA_INCLUDE}"
        "${ESSENTIA_INCLUDE}/essentia"
        "${ESSENTIA_INCLUDE}/essentia/streaming"
        "${ESSENTIA_INCLUDE}/essentia/streaming/algorithms"
        "${ESSENTIA_INCLUDE}/essentia/scheduler"
        "${ESSENTIA_INCLUDE}/essentia/utils"
        "${ESSENTIA_INCLUDE}/3rdparty"
        "${ESSENTIA_INCLUDE}/3rdparty/kiss_fft130"
        "${ESSENTIA_INCLUDE}/3rdparty/spline"
    )

    target_compile_definitions(${TARGET_NAME} PRIVATE
        ESSENTIA_STATIC
        ESSENTIA_NO_EIGEN
    )

    target_link_libraries(${TARGET_NAME} PRIVATE "${ESSENTIA_LIB}")

    # --- Windows / MSVC ---
    if(WIN32)
        target_compile_definitions(${TARGET_NAME} PRIVATE
            WIN32 _WINDOWS _USE_MATH_DEFINES NOMINMAX _HAS_STD_BYTE=0
        )
        set_target_properties(${TARGET_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PLUGIN_OUTPUT_DIR}/Release"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${PLUGIN_OUTPUT_DIR}/Debug"
        )
    endif()

    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            /W2 /permissive-
            /wd4244 /wd4267 /wd4305 /wd4996
        )
        target_link_options(${TARGET_NAME} PRIVATE
            "/WHOLEARCHIVE:${ESSENTIA_LIB}"
        )
    endif()

    # --- macOS / Apple Clang ---
    if(APPLE)
        target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wno-unused-variable)
        # Force-load the entire static archive so Essentia's self-registering
        # algorithm factories are not stripped by the linker.
        target_link_options(${TARGET_NAME} PRIVATE
            "-Wl,-force_load,${ESSENTIA_LIB}"
        )
        target_link_libraries(${TARGET_NAME} PRIVATE
            "-framework Accelerate"
        )
    endif()
endmacro()

# ---------------------------------------------------------------------------
# 1. EssentiaSpectrumCHOP
# ---------------------------------------------------------------------------
add_chop_target(EssentiaSpectrumCHOP
    EssentiaSpectrumCHOP.h
    EssentiaSpectrumCHOP.cpp
    Parameters_Spectrum.h
    Parameters_Spectrum.cpp
)

# ---------------------------------------------------------------------------
# 2. EssentiaSpectralCHOP (unified real-time + batch)
# ---------------------------------------------------------------------------
add_chop_target(EssentiaSpectralCHOP
    EssentiaSpectralCHOP.h
    EssentiaSpectralCHOP.cpp
    Parameters_Spectral.h
    Parameters_Spectral.cpp
)

# ---------------------------------------------------------------------------
# 3. EssentiaTonalCHOP (unified real-time + batch)
# ---------------------------------------------------------------------------
add_chop_target(EssentiaTonalCHOP
    EssentiaTonalCHOP.h
    EssentiaTonalCHOP.cpp
    Parameters_Tonal.h
    Parameters_Tonal.cpp
)

# ---------------------------------------------------------------------------
# 4. EssentiaRhythmCHOP (unified real-time + batch)
# ---------------------------------------------------------------------------
add_chop_target(EssentiaRhythmCHOP
    EssentiaRhythmCHOP.h
    EssentiaRhythmCHOP.cpp
    Parameters_Rhythm.h
    Parameters_Rhythm.cpp
)

# ---------------------------------------------------------------------------
# 5. EssentiaLoudnessCHOP (unified real-time + batch)
# ---------------------------------------------------------------------------
add_chop_target(EssentiaLoudnessCHOP
    EssentiaLoudnessCHOP.h
    EssentiaLoudnessCHOP.cpp
    Parameters_Loudness.h
    Parameters_Loudness.cpp
)
