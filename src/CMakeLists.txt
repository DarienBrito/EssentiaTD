# SPDX-License-Identifier: AGPL-3.0-or-later
# Essentia CHOP Suite — builds 5 TD CHOP plugins (one DLL per operator type)
#   EssentiaSpectrumCHOP, EssentiaSpectralCHOP, EssentiaTonalCHOP,
#   EssentiaRhythmCHOP, EssentiaLoudnessCHOP
#
# Prerequisites:
#   1. Copy CHOP_CPlusPlusBase.h and CPlusPlus_Common.h from the TouchDesigner
#      C++ SDK samples into this directory (src/).
#   2. Place pre-compiled Essentia static lib + headers under vendor/essentia/:
#        vendor/essentia/include/   — Essentia headers (essentia/, essentia/standard/, ...)
#        vendor/essentia/lib/       — essentia.lib (MSVC x64 static)
#
# Build:
#   cd src
#   cmake -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
#
# Deploy:
#   Copy the 5 resulting .dll files to your TD Plugins folder or next to .toe

cmake_minimum_required(VERSION 3.20)
project(EssentiaCHOPSuite LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
# Essentia static lib (and its transitive deps)
find_library(ESSENTIA_LIB essentia PATHS "${ESSENTIA_LIB_DIR}" NO_DEFAULT_PATH)
if(NOT ESSENTIA_LIB)
    message(FATAL_ERROR "essentia.lib not found in ${ESSENTIA_LIB_DIR}")
endif()

# Shared sources included in every target
set(SHARED_HEADERS
    CHOP_CPlusPlusBase.h
    CPlusPlus_Common.h
    Shared/RingBuffer.h
    Shared/Utils.h
    Shared/EssentiaInit.h
)

# Helper macro: define one CHOP DLL target
macro(add_chop_target TARGET_NAME)
    # Remaining args are the source files
    add_library(${TARGET_NAME} SHARED ${ARGN} ${SHARED_HEADERS})

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

    # Must match the defines used when building essentia.lib
    target_compile_definitions(${TARGET_NAME} PRIVATE
        ESSENTIA_STATIC
        ESSENTIA_NO_EIGEN
        _USE_MATH_DEFINES
        NOMINMAX
        _HAS_STD_BYTE=0
    )

    target_link_libraries(${TARGET_NAME} PRIVATE "${ESSENTIA_LIB}")

    # Windows-specific
    if(WIN32)
        target_compile_definitions(${TARGET_NAME} PRIVATE WIN32 _WINDOWS)
        set_target_properties(${TARGET_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug"
        )
    endif()

    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            /W2 /permissive-
            /wd4244 /wd4267 /wd4305 /wd4996
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
# 2. EssentiaSpectralCHOP
# ---------------------------------------------------------------------------
add_chop_target(EssentiaSpectralCHOP
    EssentiaSpectralCHOP.h
    EssentiaSpectralCHOP.cpp
    Parameters_Spectral.h
    Parameters_Spectral.cpp
)

# ---------------------------------------------------------------------------
# 3. EssentiaTonalCHOP
# ---------------------------------------------------------------------------
add_chop_target(EssentiaTonalCHOP
    EssentiaTonalCHOP.h
    EssentiaTonalCHOP.cpp
    Parameters_Tonal.h
    Parameters_Tonal.cpp
)

# ---------------------------------------------------------------------------
# 4. EssentiaRhythmCHOP
# ---------------------------------------------------------------------------
add_chop_target(EssentiaRhythmCHOP
    EssentiaRhythmCHOP.h
    EssentiaRhythmCHOP.cpp
    Parameters_Rhythm.h
    Parameters_Rhythm.cpp
)

# ---------------------------------------------------------------------------
# 5. EssentiaLoudnessCHOP
# ---------------------------------------------------------------------------
add_chop_target(EssentiaLoudnessCHOP
    EssentiaLoudnessCHOP.h
    EssentiaLoudnessCHOP.cpp
    Parameters_Loudness.h
    Parameters_Loudness.cpp
)
