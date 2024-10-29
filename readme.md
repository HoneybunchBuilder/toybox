# Toybox

A personal game engine written in C for Windows, Linux, macOS, iOS and Android 

#### Main Branch Build Status

[![Windows](https://github.com/Honeybunch/toybox/actions/workflows/windows.yml/badge.svg)](https://github.com/Honeybunch/toybox/actions/workflows/windows.yml)
[![Linux](https://github.com/Honeybunch/toybox/actions/workflows/linux.yml/badge.svg)](https://github.com/Honeybunch/toybox/actions/workflows/linux.yml)
[![macOS](https://github.com/Honeybunch/toybox/actions/workflows/macos.yml/badge.svg)](https://github.com/Honeybunch/toybox/actions/workflows/macos.yml)
[![Android](https://github.com/Honeybunch/toybox/actions/workflows/android.yml/badge.svg)](https://github.com/Honeybunch/toybox/actions/workflows/android.yml)
[![iOS](https://github.com/Honeybunch/toybox/actions/workflows/ios.yml/badge.svg)](https://github.com/Honeybunch/toybox/actions/workflows/ios.yml)

## Building
This project builds with CMake and is expected to be used in [vcpkg](https://github.com/microsoft/vcpkg) manifest mode to be compiled.

All supported / tested build configurations can be found in the `CMakePresets.json` file. Aspirationally, any IDE that supports CMake Presets (VSCode, Visual Studio, CLion, etc.) is expected to work with as little manual configuration as possible.

Or if you just want to quickly build examples on the command line try:
```shell
cmake --preset x64-windows-ninja-llvm
cmake --build --preset release-x64-windows-ninja-llvm
```

### Pre-requisites

#### All Platforms
Make sure to have the following available on your PATH:
* ninja
* cmake (version 3.20+)
* clang (CI uses between versions 18-20; Anything after 17 should work)
* git
* xxd (version 2023-10-05+ has been tested)

You also need (but not on your PATH)
* vcpkg (latest version from git)

You will also need the following environment variables defined:
* `VCPKG_ROOT` - should point to your local vcpkg install

vcpkg will fetch a number of dependencies. See `vcpkg.json` for a complete list.

Notes:
 * You can install `xxd` on Windows via `scoop` by installing the `vim` package. Other packages like `busybox` provide an unsupported version of `xxd`
 * Shaders are [slang](https://shader-slang.com/) and thus are compiled by `slangc` which is retrieved by vcpkg
 * Tracy could be built locally by the `gui-tools` target but for now just install `tracy` from your package manager if you want to do profiling
 * Jolt physics comes with a viewer which can also be built from vcpkg. When more integration with Jolt is implemented this will probably be the way to access this tool.

#### Windows - Works
To use LLVM you will need the VS2022 build tools, a Windows 10/11 Kit install and LLVM for Windows installed.

#### Linux - Should Work
Any working clang toolchain should be able to build the project for any linux with only minimal build tools. 

First time vcpkg may complain if you have missing dependencies. Not listing them all here yet since it's different per distro and it changes as dependencies update

#### macOS - Blocked
You should only need the XCode developer command line tools installed and the MoltenVK Vulkan SDK if you want Validation Layers.

Toybox currently requires Vulkan 1.3 and MoltenVK only supports 1.2 so while macOS is compiled by CI it is not expected to actually run.

#### Android - WIP
Toybox can be built with the Android SDK and NDK but the ability to produce an actual runnable APK is still in development.

If you still feel like being brave:

You will need Java 11+

You will need the following installed from the android sdkmanager:
* `build-tools;31.0.0` (anything 30+ works; try latest)
* `ndk;26.2.11394342` (Older versions may fail to compile mimalloc & newer versions will fail to compile SDL3)
* `platform-tools` (Latest version should be fine)
* `platforms;android-33` (Hard requirement from SDL3)

The CMake scripts rely on these env vars being set properly. Through Android Studio or your own environment.
* `ANDROID_NDK_HOME`
* `ANDROID_HOME`
* `JAVA_HOME`

#### iOS - Blocked
As with macOS, iOS does not have a Vulkan 1.3 implementation yet. It's only built by CI to prove that the code can build.

### CLI Build
Check `CMakePresets.json` for the various supported configuration and build presets

Presets are organized along the following pattern: `<triplet>-<buildsystem>-<compiler>`

So an example for configuring and building the `x64-windows` triplet with `ninja` and `clang` would be:
* `cmake --preset x64-windows-ninja-llvm`
* `cmake --build --preset debug-x64-windows-ninja-llvm`

See the github actions page for build status and a quick overview of the supported and tested configurations

## Additional Notes
This project relies on some of clang's C language extensions because I was lazy and didn't want to write out SSE/NEON intrinsics for some basic math. See `src/tb_simd.h` & `src/tb_simd.c` for more details. This has balooned into a few other clang specific features being supported, such as Blocks. Only LLVM family compilers are expected to be able to compile Toybox for the foreseeable future.

For best results, use the latest version of vcpkg provided by your package manager or directly via Git. There are couple custom ports for SDL3 and SDL3_Mixer that I maintain. See `vcpkg-configuration.json` for how that's set up.

The `CMakePresets` for `x64-windows-ninja-llvm` and `x64-windows-static-ninja-llvm` has to specify `CMAKE_RC_COMPILER` as `llvm-rc` or else it may fail if run inside of a Visual Studio command prompt. CMake will default to using the msvc `rc` compiler and that will cause failures only in RelWithDebInfo / Release builds.