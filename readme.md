# Toybox

A simple test application in C for Windows, Linux, macOS, iOS and Android 

[![CMake](https://github.com/Honeybunch/toybox/actions/workflows/build.yml/badge.svg)](https://github.com/Honeybunch/toybox/actions/workflows/build.yml)

## Building
This project builds with CMake and relies heavily on a deep integration with [vcpkg](https://github.com/microsoft/vcpkg) for dependency management

All dependencies will be built and installed by vcpkg when you configure the project

All supported / tested build configurations can be found in the `CMakePresets.json` file which should allow just about any IDE that supports it (VSCode, Visual Studio, CLion, etc.) to build all configurations supported by your host OS.

### Pre-requisites

#### All Platforms
Make sure to have the following available on your path:
* ninja
* cmake 3.20+
* clang or gcc (lowest tested are llvm 10 and gcc 9)
* dxc [via the Vulkan SDK](https://vulkan.lunarg.com/)
* vcpkg

You will also need the following environment variables defined:
* `VCPKG_ROOT` - should point to your local vcpkg install

#### Windows
For LLVM you will need the VS2022 build tools, a Windows 10/11 Kit install and LLVM for Windows installed.

For Mingw you will just need a `gcc` install (tested with distributions from chocolatey and scoop)

#### Android
You will need the following installed from the android sdkmanager:
* `build-tools;31.0.0` (anything 30+ works; try latest)
* `ndk;23.0.7599858` (24.0.8215888 also works)
* `platform-tools;31.0.3` (Newer should work too)
* `platforms;android-29` (Hard requirement)

Make sure the following environment variables are set properly
* `ANDROID_NDK_HOME`
* `ANDROID_HOME`
* `JAVA_HOME`

Android Studio is not used for the build process but the Android SDK, NDK and a Java 11 installation needs to be available. If these are sourced from your Android Studio install there should be no problems.

#### Linux
For DXC to work properly you may need libncurses5 installed. You can install that on:

Ubuntu with: `sudo apt install -y libncurses5`

#### Macos / iOS
You should only need the xcode developer command line tools installed.

These are somewhat untested. Builds for macOS do work and even package properly but iOS builds have not been setup to properly package .ipa files nor has it been tested on any devices.

### CLI Build
Check `CMakePresets.json` for the various supported configuration and build presets

Presets are organized along the following pattern: `<triplet>-<buildsystem>-<compiler>`

So an example for configuring and building the `x64-windows` triplet with `ninja` and `clang` would be:
* `cmake --preset x64-windows-ninja-llvm`
* `cmake --build --preset debug-x64-windows-ninja-llvm`

See the github actions page for build status and a quick overview of the supported and tested configurations

## Additional Notes
This project relies on semantics provided by clang/gcc because I was lazy and didn't want to write out SSE/NEON intrinsics for some basic math. See `src/simd.h` & `src/simd.c` for more details.

A custom vcpkg registry can be foujnd in `vcpkg-configuration.json`. It is necessary for some extra patches to `mimalloc`, `volk` and `tracy`. I am working to get these upstreamed.

The `CMakePresets` for `x64-windows-ninja-llvm` and `x64-windows-static-ninja-llvm` has to specify `CMAKE_RC_COMPILER` as `llvm-rc` or else it may fail if run inside of a Visual Studio command prompt. CMake will default to using the msvc `rc` compiler and that will cause failures only in RelWithDebInfo / Release builds.