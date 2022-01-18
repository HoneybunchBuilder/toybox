# Toybox

A simple test application in C for Windows, Android and Others. 

## Building

### Windows
Make sure to have the following available on your path:
* ninja
* cmake 3.20
* clang
* dxc [via the Vulkan SDK](https://vulkan.lunarg.com/)

You will need the VS2019 build tools, a Windows 10 Kit install and LLVM for Windows installed.

This project relies on semantics provided by clang/gcc because I was lazy and didn't want to write out SSE/NEON intrinsics for some basic math.

### Android
Make sure the following environment variables are set properly
* `ANDROID_NDK_HOME`
* `ANDROID_HOME`
* `JAVA_HOME`

Android Studio is not used for the build process but the Android SDK, NDK and a Java 8 installation needs to be available. If these are sourced from your Android Studio install there should be no problems.

### Vcpkg
Make sure to bootstrap vcpkg with `./vcpkg/boostrap-vcpkg.bat`

Install the necessary dependencies with `./install_deps.bat` this will
invoke vcpkg for you for the `x64-windows` and `x64-windows-static` targets. 
Add android manually if you want it.

For the following triplets:

#### Known Working Vcpkg Triples
* x64-windows
* x64-windows-static
* arm64-android

#### Should-be-working vcpkg triplets
* x64-android

### VSCode

It's recommended that to build you use the tasks provided by the vscode workspace.

Simply run a task like `Configure x64 Windows` to generate the cmake project.

Then just run `Build x64 Windows Debug` to build the Windows project.

Additionally for Android you'll want to run the Install and Package tasks to get an installable APK. 

Feel free inspect the tasks and just run them from your terminal if you want.