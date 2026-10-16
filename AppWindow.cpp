#include "AppWindow.h"
#include "AudioCapture.h"

#include <string>
#include <new>
#include <cwchar>

namespace
{
constexpr UINT_PTR CAPTURE_TIMER_ID = 1;
constexpr UINT CAPTURE_TIMER_INTERVAL_MS = 20;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void UpdateCaptureTitle(HWND hwnd, const AudioState* state, const wchar_t* status)
{
    const std::wstring title = std::wstring(status)
        + L" | Bytes: " + std::to_wstring(state->recordedAudio.size())
        + L" | Discontinuity flags: " + std::to_wstring(state->discontinuityFlags);
    SetWindowTextW(hwnd, title.c_str());
}

void ShowCaptureError(HWND hwnd, const wchar_t* operation, HRESULT hr)
{
    wchar_t errorText[160] = {};
    swprintf_s(errorText, 160, L"%ls failed: HRESULT 0x%08lX",
        operation, static_cast<unsigned long>(hr));
    SetWindowTextW(hwnd, errorText);
}

void StopCapture(HWND hwnd, AudioState* state)
{
    if (!state || !state->isRecording)
    {
        return;
    }

    HRESULT hr = state->audioClient->Stop();
    if (FAILED(hr))
    {
        ShowCaptureError(hwnd, L"Stop", hr);
        return;
    }

    KillTimer(hwnd, CAPTURE_TIMER_ID);
    state->isRecording = false;

    // Stop freezes capture. Read the final pending packets before returning.
    hr = ReadAvailableAudio(state);
    if (FAILED(hr))
    {
        ShowCaptureError(hwnd, L"Final read", hr);
        return;
    }

    UpdateCaptureTitle(hwnd, state,
        hr == S_FALSE ? L"30-second limit reached" : L"Stopped - hold Space for a new recording");
}


LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_NCCREATE)
    {
        auto* createInfo = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* state = static_cast<AudioState*>(createInfo->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    auto* state = reinterpret_cast<AudioState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA)
        );

    switch (uMsg)
    {
    case WM_KEYDOWN:
        if (wParam == VK_SPACE && state)
        {
            // Ignore auto-repeat, including after the 30-second recording limit.
            if (state->isRecording || (lParam & (static_cast<LPARAM>(1) << 30)))
            {
                return 0;
            }

            // Reset pending Windows data before starting a fresh recording.
            HRESULT hr = state->audioClient->Reset();
            if (FAILED(hr))
            {
                ShowCaptureError(hwnd, L"Reset", hr);
                return 0;
            }

            // Each new press replaces the previous recording.
            state->recordedAudio.clear();
            state->discontinuityFlags = 0;
            try
            {
                // Allocate before capture starts to avoid growth during each packet.
                state->recordedAudio.reserve(state->maxRecordingBytes);
            }
            catch (const std::bad_alloc&)
            {
                ShowCaptureError(hwnd, L"Reserve recording storage", E_OUTOFMEMORY);
                return 0;
            }

            // A timer posts WM_TIMER messages; it is not a precise audio clock.
            if (SetTimer(hwnd, CAPTURE_TIMER_ID,
                CAPTURE_TIMER_INTERVAL_MS, nullptr) == 0)
            {
                SetWindowTextW(hwnd, L"Failed to create capture timer");
                return 0;
            }

            hr = state->audioClient->Start();
            if (FAILED(hr))
            {
                KillTimer(hwnd, CAPTURE_TIMER_ID);
                ShowCaptureError(hwnd, L"Start", hr);
                return 0;
            }

            state->isRecording = true;
            UpdateCaptureTitle(hwnd, state, L"Capturing");
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == CAPTURE_TIMER_ID)
        {
            if (state && state->isRecording)
            {
                HRESULT hr = ReadAvailableAudio(state);
                if (FAILED(hr))
                {
                    // Keep the error visible. Restart the app if Stop also fails.
                    KillTimer(hwnd, CAPTURE_TIMER_ID);
                    HRESULT stopResult = state->audioClient->Stop();
                    if (SUCCEEDED(stopResult))
                    {
                        state->isRecording = false;
                    }
                    ShowCaptureError(hwnd, L"Read audio", hr);
                }
                else if (hr == S_FALSE)
                {
                    StopCapture(hwnd, state);
                }
                else
                {
                    UpdateCaptureTitle(hwnd, state, L"Capturing");
                }
            }
            return 0;
        }
        break;

    case WM_KEYUP:
        if (wParam == VK_SPACE && state)
        {
            StopCapture(hwnd, state);
            return 0;
        }
        break;

    case WM_KILLFOCUS:
        // Preserve the original hold-to-record behavior for this prototype.
        StopCapture(hwnd, state);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, CAPTURE_TIMER_ID);
        if (state && state->isRecording)
        {
            if (SUCCEEDED(state->audioClient->Stop()))
            {
                state->isRecording = false;
            }
        }
        PostQuitMessage(0);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        FillRect(hdc, &ps.rcPaint, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        EndPaint(hwnd, &ps);
    }
    return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

}

HWND CreateAppWindow(HINSTANCE hInstance, AudioState& audioState)
{
    // Define the name of our window class.
    const wchar_t CLASS_NAME[] = L"Window Class";

    // Create and zero-initialize a WNDCLASS structure.
    WNDCLASSW wc = { };

    // Set the function that will process window messages.
    wc.lpfnWndProc = WindowProc;

    // Associate this window class with this application instance.
    wc.hInstance = hInstance;

    // Set the name of the window class.
    wc.lpszClassName = CLASS_NAME;

    // Register the window class with Windows.
    if (!RegisterClassW(&wc))
    {
        return nullptr;
    }

    // Create an actual window based on the registered window class.
    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Hold Space to capture",
        WS_OVERLAPPEDWINDOW,

        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,

        NULL,
        NULL,
        hInstance,
        &audioState
    );

    return hwnd;
}
