# Building Essentia as a Static Library

This is the gating prerequisite — the plugin suite cannot compile until the Essentia static library exists (`essentia.lib` on Windows, `libessentia.a` on macOS).

## The authoritative build: `ci/essentia-CMakeLists.txt`

CI (and the recommended local build) does NOT use the options below — it overlays
`ci/essentia-CMakeLists.txt` onto a clone of upstream Essentia (pinned to the ref in
`.github/workflows/build.yml`) together with `ci/essentia_algorithms_reg.cpp`,
`ci/essentia_version.h` and `ci/essentia-patches/`. Local procedure (clone lives at
`_essentia_src/`, gitignored):

```bash
cp ci/essentia-CMakeLists.txt      _essentia_src/CMakeLists.txt
cp ci/essentia_algorithms_reg.cpp  _essentia_src/src/essentia_algorithms_reg.cpp
cp ci/essentia_version.h           _essentia_src/src/version.h
cp -r ci/essentia-patches/essentia/* _essentia_src/src/essentia/
cmake -S _essentia_src -B _essentia_src/build -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build   _essentia_src/build --config Release
cmake --install _essentia_src/build --config Release --prefix _essentia_src/install
# then copy install/include + install/lib over src/vendor/essentia/
```

Since 2026-07-20 this build enables the `Resample` algorithm (RhythmExtractor2013
requires 44.1 kHz; Rhythm batch resamples non-44.1k input). Its dependency
**libsamplerate 0.2.2** is pulled by `FetchContent` (pinned tag, network needed at
configure time) and produces a SECOND archive — `samplerate.lib` / `libsamplerate.a` —
installed next to the essentia archive. The plugin build (`src/CMakeLists.txt`) links it
plainly (no whole-archive; only the essentia archive carries factory registrars).
Restoring an excluded algorithm always needs BOTH the source un-excluded AND a
`Registrar<...>` entry in `ci/essentia_algorithms_reg.cpp`.

The options below are the older manual paths, kept for reference — they predate the
`ci/` build and do not produce `samplerate.lib`.

---

## Windows (MSVC x64)

## Option A: WAF with MSVC (upstream build system)

```bash
# Clone
git clone https://github.com/MTG/essentia.git
cd essentia

# Install Python + NumPy (WAF needs them)
pip install numpy

# Configure for MSVC — lightweight build, no extras
python waf configure --msvc_version="msvc 17.0" ^
    --lightweight=libav,yaml,chromaprint ^
    --with-static ^
    --build-static ^
    --prefix=build/install

# Build
python waf build
python waf install
```

The resulting files will be in `build/install/lib/essentia.lib` and `build/install/include/`.

**Known issues with WAF + MSVC:**
- WAF's MSVC detection can be flaky. If it fails, try Option B.
- Some algorithms pull in libav/yaml/chromaprint — the `--lightweight` flag disables them.

## Option B: Manual CMake Build (subset of algorithms)

If WAF doesn't cooperate, build only the algorithms we need with a custom CMakeLists.txt.

1. **Install Eigen** (header-only):
   ```bash
   # vcpkg
   vcpkg install eigen3:x64-windows
   # Or download from https://eigen.tuxfamily.org and set EIGEN3_INCLUDE_DIR
   ```

2. **Install FFTW3** (optional, Essentia can use KissFFT fallback):
   ```bash
   vcpkg install fftw3:x64-windows
   ```

3. **Create a CMakeLists.txt** in the Essentia source root:

   ```cmake
   cmake_minimum_required(VERSION 3.20)
   project(EssentiaStatic LANGUAGES CXX)
   set(CMAKE_CXX_STANDARD 17)

   # Collect all .cpp files from src/algorithms/ that we need
   file(GLOB_RECURSE ESSENTIA_SRC
       src/essentia/*.cpp
       src/algorithms/spectral/*.cpp
       src/algorithms/tonal/*.cpp
       src/algorithms/rhythm/*.cpp
       src/algorithms/temporal/*.cpp
       src/algorithms/standard/*.cpp
       src/algorithms/stats/*.cpp
   )

   # Exclude problematic files (streaming-only, external deps)
   list(FILTER ESSENTIA_SRC EXCLUDE REGEX ".*streaming.*")
   list(FILTER ESSENTIA_SRC EXCLUDE REGEX ".*gaia.*")
   list(FILTER ESSENTIA_SRC EXCLUDE REGEX ".*vamp.*")
   list(FILTER ESSENTIA_SRC EXCLUDE REGEX ".*nnls.*")

   add_library(essentia STATIC ${ESSENTIA_SRC})

   target_include_directories(essentia PUBLIC
       src/
       src/3rdparty/
   )

   # Eigen
   find_package(Eigen3 REQUIRED)
   target_link_libraries(essentia PUBLIC Eigen3::Eigen)

   # FFTW3 (optional)
   find_library(FFTW3_LIB fftw3f)
   if(FFTW3_LIB)
       target_link_libraries(essentia PUBLIC ${FFTW3_LIB})
       target_compile_definitions(essentia PUBLIC HAVE_FFTW)
   endif()

   # Disable optional deps
   target_compile_definitions(essentia PUBLIC
       ESSENTIA_EXPORTS
       _USE_MATH_DEFINES
   )

   install(TARGETS essentia ARCHIVE DESTINATION lib)
   install(DIRECTORY src/essentia/ DESTINATION include/essentia FILES_MATCHING PATTERN "*.h")
   ```

   Build:
   ```bash
   cd essentia
   cmake -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   cmake --install build --prefix install
   ```

4. **Copy outputs** to the plugin project:
   ```
   install/lib/essentia.lib  →  src/vendor/essentia/lib/
   install/include/essentia/ →  src/vendor/essentia/include/essentia/
   ```

## Verification

Create a minimal test:

```cpp
#include <essentia/algorithmfactory.h>
#include <iostream>

int main() {
    essentia::init();
    auto* algo = essentia::standard::AlgorithmFactory::create("Windowing",
        "type", std::string("hann"), "size", 1024);
    std::cout << "Essentia OK: " << algo->name() << std::endl;
    delete algo;
    essentia::shutdown();
    return 0;
}
```

Compile and link against `essentia.lib`. If it runs and prints "Essentia OK: Windowing", the library is ready.

---

## macOS

### Option A: WAF (upstream build system)

```bash
# Clone
git clone https://github.com/MTG/essentia.git
cd essentia

# Install Python + NumPy (WAF needs them)
pip install numpy

# Configure — lightweight build, no extras
python3 waf configure \
    --lightweight=libav,yaml,chromaprint \
    --with-static \
    --build-static \
    --prefix=build/install

# Build
python3 waf build
python3 waf install
```

The resulting files will be in `build/install/lib/libessentia.a` and `build/install/include/`.

### Option B: Manual CMake Build

Use the same CMakeLists.txt from the Windows section above, but with the macOS generator:

```bash
cd essentia
cmake -B build
cmake --build build --config Release
cmake --install build --prefix install
```

### Copy outputs to the plugin project

```
install/lib/libessentia.a     →  src/vendor/essentia/lib/
install/include/essentia/     →  src/vendor/essentia/include/essentia/
```

---

## TouchDesigner SDK Headers

Copy from your TouchDesigner installation:

**Windows:**
```
C:/Program Files/Derivative/TouchDesigner/Samples/CPlusPlus/
    CHOP_CPlusPlusBase.h
    CPlusPlus_Common.h
```

**macOS:**
```
/Applications/TouchDesigner.app/Contents/Resources/Samples/CPlusPlus/
    CHOP_CPlusPlusBase.h
    CPlusPlus_Common.h
```

Into `src/` alongside the plugin source files.
