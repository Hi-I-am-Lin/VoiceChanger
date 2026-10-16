#include "AppWindow.h"
#include "AudioCapture.h"

namespace
{
int RunApplication(HINSTANCE hInstance, int nCmdShow)
{
    AudioState audioState;
    const int initializationResult = InitializeAudio(audioState);
    if (initializationResult != -1)
    {
        return initializationResult;
    }

    HWND hwnd = CreateAppWindow(hInstance, audioState);
    if (!hwnd)
    {
        return 1;
    }
    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    BOOL messageResult = 0;
    while ((messageResult = GetMessageW(&msg, nullptr, 0, 0)) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return messageResult == -1 ? 1 : 0;
}
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        return 0;
    }

    // Audio resources are destroyed before COM is uninitialized.
    const int result = RunApplication(hInstance, nCmdShow);
    CoUninitialize();
    return result;
}
