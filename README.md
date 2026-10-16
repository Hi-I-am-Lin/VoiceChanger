# VoiceChanger

A voice changer project built with C++ and Windows WASAPI. It is currently a microphone capture prototype: hold Space while the window has focus to record, and release it to stop. Each recording is limited to 30 seconds. Audio is stored only in memory; voice effects and virtual microphone output are not yet implemented.

## Requirements

- Windows 10 or 11.
- CMake 3.20 or later.
- Visual Studio 2022 Build Tools or Visual Studio 2022 with the **Desktop development with C++** workload, including MSVC v143 and the Windows SDK. VS Code itself does not include a compiler.

The project uses CMake to manage builds. There is no need to commit `.sln`, `.vcxproj`, or `.vcxproj.filters` files, and no third-party audio libraries are required.

## Build from the Command Line

Run these commands from the project root. CMake will locate the installed Visual Studio 2022 toolchain:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\VoiceChanger.exe
```

For a debug build, replace `Release` with `Debug`. For a 32-bit build, use a separate build directory and replace `-A x64` with `-A Win32`.

If the compiler or Windows SDK cannot be found, check the components listed above in Visual Studio Installer. Before running the application, ensure that a default microphone is available and desktop apps are allowed to access it.

## VS Code

1. Install the Microsoft **C/C++** and **CMake Tools** extensions.
2. Open the project folder, run **CMake: Select a Kit**, and select the installed MSVC toolchain.
3. Run **CMake: Configure**, followed by **CMake: Build**.
4. Launch `VoiceChanger` using the run or debug features in CMake Tools.

You can also run the build commands above in the VS Code terminal. When switching generators or toolchains, use a new build directory to avoid reusing an incompatible CMake cache.

## Project Files

- `main.cpp`: Application entry point, COM lifetime, and message loop.
- `AppWindow.h` / `AppWindow.cpp`: Window creation, keyboard input, and capture status display.
- `AudioCapture.h` / `AudioCapture.cpp`: Microphone initialization, packet capture, and audio resource cleanup.
- `CMakeLists.txt`: Compiler options, the Windows GUI executable configuration, and system library dependencies.
- `build/`: Local build directory, ignored by Git. CMake-generated Visual Studio project files also go here and do not need to be committed.

The project depends on Windows audio APIs and cannot run directly on Linux or macOS.
