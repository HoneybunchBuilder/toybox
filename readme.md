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
Make sure to have the following available on your path:
* ninja
* cmake 3.20+
* clang (17 is used by CI. Some older versions of clang don't support `_Float16`)
* dxc [via the Vulkan SDK](https://vulkan.lunarg.com/)
* vcpkg - latest version from git; more details below

You will also need the following environment variables defined:
* `VCPKG_ROOT` - should point to your local vcpkg install

#### Windows - Works
To use LLVM you will need the VS2022 build tools, a Windows 10/11 Kit install and LLVM for Windows installed.

#### Linux - Should Work
For DXC to work properly you may need libncurses5 installed. You can install that on:

Ubuntu with: `sudo apt install -y libncurses5`

#### macOS - Blocked
You should only need the XCode developer command line tools installed and the MoltenVK Vulkan SDK (primarily for `dxc`).

Toybox currently requires Vulkan 1.3 and MoltenVK only supports 1.2 so while macOS is compiled by CI it is not expected to actually run.

#### Android - WIP
Toybox can be built with the Android SDK and NDK but the ability to produce an actual runnable APK is still in development.

If you still feel like being brave:

You will need the following installed from the android sdkmanager:
* `build-tools;31.0.0` (anything 30+ works; try latest)
* `ndk;26.2.11394342` (Older versions may fail to compile mimalloc)
* `platform-tools;31.0.3` (Newer should work too)
* `platforms;android-31` (Hard requirement from SDL3)

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
This project relies on some of clang's C language extensions because I was lazy and didn't want to write out SSE/NEON intrinsics for some basic math. See `src/simd.h` & `src/simd.c` for more details. This has balooned into a few other clang specific features being supported, such as Blocks. Only LLVM family compilers are expected to be able to compile Toybox for the foreseeable future.

For best results, use the latest version of vcpkg provided by your package manager or directly via Git. There are couple custom ports for SDL3 and SDL3_Mixer that I maintain. See `vcpkg-configuration.json` for how that's set up.

The `CMakePresets` for `x64-windows-ninja-llvm` and `x64-windows-static-ninja-llvm` has to specify `CMAKE_RC_COMPILER` as `llvm-rc` or else it may fail if run inside of a Visual Studio command prompt. CMake will default to using the msvc `rc` compiler and that will cause failures only in RelWithDebInfo / Release builds.