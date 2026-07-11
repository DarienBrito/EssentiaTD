# SPDX-License-Identifier: AGPL-3.0-or-later
# Essentia CHOP Suite — builds 5 TD CHOP plugins (one DLL per operator type)
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
#        vendor/essentia/lib/       — essentia.lib (MSVC x64 static)
#
# Build:
#   Windows:
#     cd src
#     cmake -B build -G "Visual Studio 17 2022" -A x64
#     cmake --build build --config Release
#   macOS:
#     cd src
#     cmake -B build
#     cmake --build build --config Release
#
# Deploy:
#   Windows: Copy 5 .dll files to TD Plugins folder
#   macOS:   Copy 5 .plugin bundles to TD Plugins folder

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
    message(FATAL_ERROR
        "Essentia static library not found in ${ESSENTIA_LIB_DIR}\n"
        "Expected essentia.lib (Windows) or libessentia.a (macOS)")
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
    Shared/PCAProcessor.h
)

# Helper macro: define one CHOP DLL target
macro(add_chop_target TARGET_NAME)
    # Remaining args are the source files
    if(APPLE)
        add_library(${TARGET_NAME} MODULE ${ARGN} ${SHARED_HEADERS})
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

    # Must match the defines used when building the essentia static lib
    target_compile_definitions(${TARGET_NAME} PRIVATE
        ESSENTIA_STATIC
        ESSENTIA_NO_EIGEN
        _USE_MATH_DEFINES
    )

    target_link_libraries(${TARGET_NAME} PRIVATE "${ESSENTIA_LIB}")

    # -----------------------------------------------------------------
    # Platform-specific
    # -----------------------------------------------------------------
    if(WIN32)
        target_compile_definitions(${TARGET_NAME} PRIVATE
            WIN32 _WINDOWS NOMINMAX _HAS_STD_BYTE=0
        )
        set_target_properties(${TARGET_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug"
        )
    elseif(APPLE)
        set_target_properties(${TARGET_NAME} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION "plugin"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
        )
    endif()

    # -----------------------------------------------------------------
    # Compiler-specific
    # -----------------------------------------------------------------
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            /W2 /permissive-
            /wd4244 /wd4267 /wd4305 /wd4996
        )
        # Force all objects from essentia.lib to be linked — prevents the
        # linker from stripping algorithm registration statics (e.g.
        # RhythmExtractor2013) that aren't directly referenced by code.
        target_link_options(${TARGET_NAME} PRIVATE
            "/WHOLEARCHIVE:${ESSENTIA_LIB}"
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        target_compile_options(${TARGET_NAME} PRIVATE
            -Wall
            -Wno-conversion
            -Wno-sign-compare
            -Wno-unused-variable
            -Wno-deprecated-declarations
            $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-shorten-64-to-32>
        )
        # Force-load all objects so algorithm registration statics survive
        if(APPLE)
            target_link_options(${TARGET_NAME} PRIVATE
                -force_load ${ESSENTIA_LIB}
            )
        else()
            target_link_options(${TARGET_NAME} PRIVATE
                -Wl,--whole-archive ${ESSENTIA_LIB} -Wl,--no-whole-archive
            )
        endif()
    endif()

    # macOS: TD symbols are resolved at load time by the host application
    if(APPLE)
        target_link_options(${TARGET_NAME} PRIVATE
            -undefined dynamic_lookup
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
