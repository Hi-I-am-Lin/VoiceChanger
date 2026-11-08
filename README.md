# VoiceChanger

A real-time voice changer built with C++ and Windows WASAPI. It reads the default microphone, shifts the pitch, and plays the result into a virtual audio cable. Chat or recording apps use the cable as their microphone, so they hear the changed voice while a push-to-talk key is held (silence otherwise).

```
Real microphone -> VoiceChanger (pitch shift, push-to-talk) -> "CABLE Input" (playback)
                                                               "CABLE Output" (recording) -> Discord / WeChat / QQ ...
```

## Setup

1. Install [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) (free) and reboot.
2. In Windows sound settings, keep your **real microphone** as the default recording device.
3. In the chat app, select **CABLE Output (VB-Audio Virtual Cable)** as the microphone.
4. Run VoiceChanger. The window shows which devices are in use.

If no device named "CABLE Input" exists, VoiceChanger plays to the default speakers instead (test mode). Use headphones then, or the speakers will feed back into the microphone.

## Controls

The push-to-talk key works while any app has focus. The other keys work only while the VoiceChanger window has focus.

- Hold **F8** (default) to talk. Press **F2** in the window, then any key, to change it.
- **Up / Down**: pitch +1 / -1 semitone (range -12 to +12). **0**: reset.

End-to-end latency is roughly 20 ms (pitch shifter) + 30 ms (output queue) + device buffers.

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

If the compiler or Windows SDK cannot be found, check the components listed above in Visual Studio Installer. Before running the application, ensure that a default microphone is available and desktop apps are allowed to access it (Settings > Privacy & security > Microphone).

## VS Code

1. Install the Microsoft **C/C++** and **CMake Tools** extensions.
2. Open the project folder, run **CMake: Select a Kit**, and select the installed MSVC toolchain.
3. Run **CMake: Configure**, followed by **CMake: Build**.
4. Launch `VoiceChanger` using the run or debug features in CMake Tools.

You can also run the build commands above in the VS Code terminal. When switching generators or toolchains, use a new build directory to avoid reusing an incompatible CMake cache.

## Project Files

- `main.cpp`: Application entry point, COM lifetime, and message loop.
- `AppWindow.h` / `AppWindow.cpp`: Status window, pitch keys, and push-to-talk key binding.
- `AudioEngine.h` / `AudioEngine.cpp`: Device selection, capture and render streams, and the real-time audio thread.
- `PitchShifter.h` / `PitchShifter.cpp`: Low-latency pitch shifter (two crossfaded delay-line taps).
- `CMakeLists.txt`: Compiler options, the Windows GUI executable configuration, and system library dependencies.
- `build/`: Local build directory, ignored by Git. CMake-generated Visual Studio project files also go here and do not need to be committed.

The project depends on Windows audio APIs and cannot run directly on Linux or macOS.
