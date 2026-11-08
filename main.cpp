#include "AppWindow.h"
#include "AudioEngine.h"

namespace
{
    // Playback device to send the changed voice to. VB-Audio Virtual Cable names it
    // "CABLE Input (VB-Audio Virtual Cable)". Without it, the default speakers are used.
    constexpr wchar_t OUTPUT_DEVICE_HINT[] = L"CABLE Input";

    void ShowError(const std::wstring& message)
    {
        MessageBoxW(nullptr, message.c_str(), L"VoiceChanger", MB_OK | MB_ICONERROR);
    }

    int RunApplication(HINSTANCE hInstance, int nCmdShow)
    {
        AudioEngine engine;
        if (!engine.Initialize(OUTPUT_DEVICE_HINT))
        {
            ShowError(engine.ErrorMessage());
            return 1;
        }

        HWND hwnd = CreateAppWindow(hInstance, engine);
        if (!hwnd)
        {
            return 1;
        }

        if (!engine.Start())
        {
            ShowError(engine.ErrorMessage());
            DestroyWindow(hwnd);
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

        engine.Stop();
        return messageResult == -1 ? 1 : 0;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        return 1;
    }

    // Audio resources are destroyed before COM is uninitialized.
    const int result = RunApplication(hInstance, nCmdShow);
    CoUninitialize();
    return result;
}
