#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <shellapi.h>

struct AudioState
{
    IAudioClient* audioClient = nullptr;
    bool isRecording = false;
};

// Forward declaration of the window procedure
LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam
);


int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    PWSTR pCmdLine,
    int nCmdShow)
{
    // =========================================================
    // COM INITIALIZATION
    // =========================================================

    // Initialize COM for the current thread.
    HRESULT hr = CoInitializeEx(
        nullptr,
        COINIT_MULTITHREADED
    );

    // Check whether COM initialization failed.
    if (FAILED(hr))
    {
        return 0;
    }


    // =========================================================
    // AUDIO DEVICE ENUMERATOR
    // =========================================================

    // Pointer to the COM object used to find audio devices.
    IMMDeviceEnumerator* pEnumerator = nullptr;

    // Create an MMDeviceEnumerator COM object and obtain
    // its IMMDeviceEnumerator interface.
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&pEnumerator
    );

    if (FAILED(hr))
    {
        CoUninitialize();
        return 0;
    }


    // =========================================================
    // GET DEFAULT MICROPHONE
    // =========================================================

    // Pointer that will receive the default capture device.
    IMMDevice* firstMicrophone = nullptr;

    // Find the default audio capture device (microphone).
    hr = pEnumerator->GetDefaultAudioEndpoint(
        eCapture,          // Request an input/capture device.
        eConsole,          // Use the default console audio device.
        &firstMicrophone   // Store the resulting device pointer here.
    );

    if (FAILED(hr))
    {
        pEnumerator->Release();
        CoUninitialize();
        return 0;
    }

    // Pointer to the IAudioClient interface.
    // It is initialized to nullptr because the interface has not been obtained yet.
    IAudioClient* pAudioClient = nullptr;

    // Activate the IAudioClient interface on the selected microphone endpoint.
    // If successful, pAudioClient will point to the audio client object.
    hr = firstMicrophone->Activate(
        __uuidof(IAudioClient), // Interface that we want to obtain
        CLSCTX_ALL,             // Allow the COM object to run in any valid context
        nullptr,                // No additional activation parameters
        reinterpret_cast<void**>(&pAudioClient)
        // Receives the IAudioClient interface pointer
    );

    if (FAILED(hr))
    {
        firstMicrophone->Release();
        pEnumerator->Release();
        CoUninitialize();
        return 1;
    }

    // Pointer used to store the audio engine's default mixing format.
    // The format contains information such as sample rate, channel count,
    // and bits per sample.
    WAVEFORMATEX* pWaveFormat = nullptr;

    // Get the default audio format used by the Windows audio engine
    // when the microphone is operating in shared mode.
    hr = pAudioClient->GetMixFormat(&pWaveFormat);

    if (FAILED(hr))
    {
        pAudioClient->Release();
        firstMicrophone->Release();
        pEnumerator->Release();
        CoUninitialize();
        return 1;
    }

    // Requested buffer duration.
    // REFERENCE_TIME uses units of 100 nanoseconds.
    // 10,000,000 ¡Á 100 nanoseconds = 1 second.
    REFERENCE_TIME bufferDuration = 10'000'000;

    // Initialize the microphone's audio stream.
    hr = pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED, // Share the microphone with other applications
        0,                        // Do not enable additional stream flags
        bufferDuration,           // Requested buffer duration: 1 second
        0,                        // Must be 0 when using shared mode
        pWaveFormat,              // Use the audio engine's default mixing format
        nullptr                   // Use the default audio session
    );

    CoTaskMemFree(pWaveFormat);
    pWaveFormat = nullptr;

    if (FAILED(hr))
    {
        pAudioClient->Release();
        firstMicrophone->Release();
        pEnumerator->Release();
        CoUninitialize();
        return 1;
    }

    IAudioCaptureClient* pCaptureClient = nullptr;

    hr = pAudioClient->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(&pCaptureClient)
    );

    if (FAILED(hr))
    {
        pAudioClient->Release();
        firstMicrophone->Release();
        pEnumerator->Release();
        CoUninitialize();
        return 1;
    }

    // =========================================================
    // WINDOW CLASS
    // =========================================================

    // Define the name of our window class.
    const wchar_t CLASS_NAME[] = L"Window Class";

    // Create and zero-initialize a WNDCLASS structure.
    WNDCLASS wc = { };

    // Set the function that will process window messages.
    wc.lpfnWndProc = WindowProc;

    // Associate this window class with this application instance.
    wc.hInstance = hInstance;

    // Set the name of the window class.
    wc.lpszClassName = CLASS_NAME;

    // Register the window class with Windows.
    RegisterClass(&wc);

    AudioState audioState;
    audioState.audioClient = pAudioClient;

    // =========================================================
    // CREATE WINDOW
    // =========================================================

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

    // Check whether window creation failed.
    if (hwnd == nullptr)
    {
        pCaptureClient->Release();
        pAudioClient->Release();
        firstMicrophone->Release();
        pEnumerator->Release();
        CoUninitialize();

        return 1;
    }

    // Make the window visible.
    ShowWindow(hwnd, nCmdShow);


    // =========================================================
    // MESSAGE LOOP
    // =========================================================

    MSG msg = { };

    // Keep receiving and processing Windows messages
    // until WM_QUIT is received.
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }


    // =========================================================
    // CLEANUP
    // =========================================================
    pCaptureClient->Release();
    pAudioClient->Release();
    firstMicrophone->Release();
    pEnumerator->Release();
    CoUninitialize();

    return 0;
}


// Handle window messages
// Including keyboard input and window closing
LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{

    if (uMsg == WM_NCCREATE)
    {
        CREATESTRUCTW* createInfo =
            reinterpret_cast<CREATESTRUCTW*>(lParam);

        AudioState* state =
            static_cast<AudioState*>(createInfo->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state)
        );

        return TRUE;
    }

    AudioState* state = reinterpret_cast<AudioState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA)
        );

    switch (uMsg)
    {
    case WM_KEYDOWN:
    {
        if (wParam == VK_SPACE && state)
        {
            if (!state->isRecording)
            {
                HRESULT hr = state->audioClient->Start();

                if (SUCCEEDED(hr))
                {
                    state->isRecording = true;
                    SetWindowTextW(hwnd, L"Capturing...");
                }
                else
                {
                    SetWindowTextW(hwnd, L"Failed to start capture");
                }
            }

            return 0;
        }

        break;
    }

    case WM_KEYUP:
    {
        if (wParam == VK_SPACE && state)
        {
            if (state->isRecording)
            {
                HRESULT hr = state->audioClient->Stop();

                if (SUCCEEDED(hr))
                {
                    state->isRecording = false;
                    SetWindowTextW(hwnd, L"Hold Space to capture");
                }
                else
                {
                    SetWindowTextW(hwnd, L"Failed to stop capture");
                }
            }

            return 0;
        }

        break;
    }

    case WM_KILLFOCUS:
    {
        if (state && state->isRecording)
        {
            HRESULT hr = state->audioClient->Stop();

            if (SUCCEEDED(hr))
            {
                state->isRecording = false;
                SetWindowTextW(hwnd, L"Hold Space to capture");
            }
        }

        return 0;
    }

    case WM_DESTROY:
    {
        if (state && state->isRecording)
        {
            state->audioClient->Stop();
            state->isRecording = false;
        }

        PostQuitMessage(0);
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;

        // Begin painting the window.
        HDC hdc = BeginPaint(hwnd, &ps);

        // Fill the window background.
        FillRect(
            hdc,
            &ps.rcPaint,
            (HBRUSH)(COLOR_WINDOW + 1)
        );

        // Finish painting.
        EndPaint(hwnd, &ps);
    }

    return 0;
    }

    // Let Windows handle all messages that we did not process.
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}