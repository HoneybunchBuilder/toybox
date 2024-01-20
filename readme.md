# Toybox

A personal game engine written in C for Windows, Linux, macOS, iOS and Android 

#### Main Branch Build Status

[![Windows](https://github.com/Honeybunch/toybox/actions/workflows/windows.yml/badge.svg)](https://github.com/Honeybunch/toybox/actions/workflows/windows.yml)
[![Linux](https://github.com/Honeybunch/toybox/actions/workflows/linux.yml/badge.svg)](https://github.com/Honeybunch/toybox/actions/workflows/linux.yml)
[![macOS](https://github.com/Honeybunch/toybox/actions/workflows/macos.yml/badge.svg)](https://github.com/Honeybunch/toybox/actions/workflows/macos.yml)
[![Android](https://github.com/Honeybunch/toybox/actions/workflows/android.yml/badge.svg)](https://github.com/Honeybunch/toybox/actions/workflows/android.yml)
[![iOS](https://github.com/Honeybunch/toybox/actions/workflows/ios.yml/badge.svg)](https://github.com/Honeybunch/toybox/actions/workflows/ios.yml)

## Building
This project builds with CMake and relies heavily on a deep integration with [vcpkg](https://github.com/microsoft/vcpkg) for dependency management

All dependencies will be built and installed by vcpkg when you configure the project

All supported / tested build configurations can be found in the `CMakePresets.json` file which should allow just about any IDE that supports it (VSCode, Visual Studio, CLion, etc.) to build all configurations supported by your host OS.

### Pre-requisites

#### All Platforms
Make sure to have the following available on your path:
* ninja
* cmake 3.20+
* clang (lowest tested is llvm 10)
* dxc [via the Vulkan SDK](https://vulkan.lunarg.com/)
* vcpkg - latest version from git; more details below

You will also need the following environment variables defined:
* `VCPKG_ROOT` - should point to your local vcpkg install

#### Windows
To use LLVM you will need the VS2022 build tools, a Windows 10/11 Kit install and LLVM for Windows installed.

To build the dependency `ktx` you will need to have bash installed. The bash from git works just fine. If you have git installed from somewhere non-standard like `scoop` or `choco` you will also need it to be available from `C:\Program Files\Git` or else the `FindBash` module of `ktx` will not function properly in vcpkg's environment. I've tried passing bash through via `VCPKG_KEEP_ENV_VARS` to no avail.

#### Android
NOT PRIORITY
Android's vulkan device story is pretty lame these days and it doesn't look like it's getting any shiny new features that I care about such as mesh shaders. So while CI builds android to verify that the code can build properly, the tools to package apks and produce a proper application have been stripped as they had rotted. A downstream project can get an Android build made through gradle without too much trouble just make sure to include the java from the SDL3 version you are using. You'll have to go get that manually.

If you still feel like being brave:

You will need the following installed from the android sdkmanager:
* `build-tools;31.0.0` (anything 30+ works; try latest)
* `ndk;23.0.7599858` (24.0.8215888 also works)
* `platform-tools;31.0.3` (Newer should work too)
* `platforms;android-29` (Hard requirement)

The CMake scripts rely on these env vars being set properly. Through Android Studio or your own environment.
* `ANDROID_NDK_HOME`
* `ANDROID_HOME`
* `JAVA_HOME`

#### Linux
For DXC to work properly you may need libncurses5 installed. You can install that on:

Ubuntu with: `sudo apt install -y libncurses5`

#### macOS
You should only need the xcode developer command line tools installed and the MoltenVK Vulkan SDK (primarily for `dxc`).
Builds for macOS made by CI but they aren't regularly tested.

#### iOS
iOS is similar to Android in that mobile is not generally a desired platform so while CI does build for iOS it remains untested and unsupported

### CLI Build
Check `CMakePresets.json` for the various supported configuration and build presets

Presets are organized along the following pattern: `<triplet>-<buildsystem>-<compiler>`

So an example for configuring and building the `x64-windows` triplet with `ninja` and `clang` would be:
* `cmake --preset x64-windows-ninja-llvm`
* `cmake --build --preset debug-x64-windows-ninja-llvm`

See the github actions page for build status and a quick overview of the supported and tested configurations

## Additional Notes
This project relies on semantics provided by clang because I was lazy and didn't want to write out SSE/NEON intrinsics for some basic math. See `src/simd.h` & `src/simd.c` for more details.

For best results, use the latest version of vcpkg. I have had to contribute a variety of changes upstream (latest change is as-of Dec 3rd 2022) so using a version sourced from your package manager may not be new enough. Yet. In the near future you should be able to use any 2023+ version of vcpkg and it should work out of the box with this project across all platforms.

The `CMakePresets` for `x64-windows-ninja-llvm` and `x64-windows-static-ninja-llvm` has to specify `CMAKE_RC_COMPILER` as `llvm-rc` or else it may fail if run inside of a Visual Studio command prompt. CMake will default to using the msvc `rc` compiler and that will cause failures only in RelWithDebInfo / Release builds.