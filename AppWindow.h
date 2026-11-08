#pragma once

#include <windows.h>

class AudioEngine;
HWND CreateAppWindow(HINSTANCE hInstance, AudioEngine& engine);
