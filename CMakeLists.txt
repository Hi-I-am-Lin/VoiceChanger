cmake_minimum_required(VERSION 3.20)
project(VoiceChanger LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "VoiceChanger requires Windows and the Windows SDK (WASAPI).")
endif()

add_executable(VoiceChanger WIN32
    main.cpp
    AppWindow.cpp
    AppWindow.h
    AudioCapture.cpp
    AudioCapture.h
)
target_compile_features(VoiceChanger PRIVATE cxx_std_17)
set_target_properties(VoiceChanger PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(VoiceChanger PRIVATE UNICODE _UNICODE)
target_link_libraries(VoiceChanger PRIVATE ole32 user32 gdi32)

if(MSVC)
    target_compile_options(VoiceChanger PRIVATE /W3 /permissive- /sdl /utf-8)
endif()
