#pragma once

#include <windows.h>

struct AudioState;
HWND CreateAppWindow(HINSTANCE hInstance, AudioState& audioState);
