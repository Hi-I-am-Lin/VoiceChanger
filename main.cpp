#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cstring>
#include <string>
#include <new>
#include <cwchar>

#pragma comment(lib, "Ole32.lib")
#include <vector>

constexpr UINT_PTR CAPTURE_TIMER_ID = 1;
constexpr UINT CAPTURE_TIMER_INTERVAL_MS = 20;
constexpr size_t MAX_RECORDING_SECONDS = 30;

struct AudioState
{
    IAudioClient* audioClient = nullptr;
    IAudioCaptureClient* captureClient = nullptr;

    bool isRecording = false;

    WORD blockAlign = 0;
    BYTE silenceByte = 0;
    size_t maxRecordingBytes = 0;
    UINT32 discontinuityFlags = 0;
    std::vector<BYTE> recordedAudio;
};

// Read all currently available packets. S_FALSE means the storage limit was reached.
HRESULT ReadAvailableAudio(AudioState* state);
void UpdateCaptureTitle(HWND hwnd, const AudioState* state, const wchar_t* status);
void ShowCaptureError(HWND hwnd, const wchar_t* operation, HRESULT hr);
void StopCapture(HWND hwnd, AudioState* state);

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

    WORD blockAlign = pWaveFormat->nBlockAlign;
    size_t maxRecordingBytes =
        static_cast<size_t>(pWaveFormat->nAvgBytesPerSec) * MAX_RECORDING_SECONDS;

    // Unsigned 8-bit PCM uses 128 as silence; signed PCM and float use zero.
    BYTE silenceByte = pWaveFormat->wBitsPerSample == 8 ? 128 : 0;

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
        pCaptureClient->Release();
        pAudioClient->Release();
        firstMicrophone->Release();
        pEnumerator->Release();
        CoUninitialize();
        return 1;
    }

    AudioState audioState;
    audioState.audioClient = pAudioClient;
    audioState.captureClient = pCaptureClient;
    audioState.blockAlign = blockAlign;
    audioState.silenceByte = silenceByte;
    audioState.maxRecordingBytes = maxRecordingBytes;


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
    BOOL messageResult = 0;
    while ((messageResult = GetMessageW(&msg, nullptr, 0, 0)) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }


    // =========================================================
    // CLEANUP
    // =========================================================
    if (audioState.isRecording)
    {
        pAudioClient->Stop();
    }
    pCaptureClient->Release();
    pAudioClient->Release();
    firstMicrophone->Release();
    pEnumerator->Release();
    CoUninitialize();

    return messageResult == -1 ? 1 : 0;
}

// Copies Windows-owned packets into our own vector.
// All calls are made on the window thread in this learning prototype.
HRESULT ReadAvailableAudio(AudioState* state)
{
    if (!state || !state->captureClient || state->blockAlign == 0)
    {
        return E_INVALIDARG;
    }

    UINT32 packetFrames = 0;
    HRESULT hr = state->captureClient->GetNextPacketSize(&packetFrames);
    if (FAILED(hr))
    {
        return hr;
    }

    while (packetFrames > 0)
    {
        BYTE* pData = nullptr;
        UINT32 numFrames = 0;
        DWORD flags = 0;

        hr = state->captureClient->GetBuffer(
            &pData, &numFrames, &flags, nullptr, nullptr
        );
        if (FAILED(hr))
        {
            return hr;
        }
        if (hr == AUDCLNT_S_BUFFER_EMPTY || numFrames == 0)
        {
            return S_OK;
        }

        const size_t byteCount =
            static_cast<size_t>(numFrames) * state->blockAlign;
        const size_t oldSize = state->recordedAudio.size();
        const size_t remaining = state->maxRecordingBytes - oldSize;
        const size_t bytesToCopy = byteCount < remaining ? byteCount : remaining;

        // Resize before copying; ReleaseBuffer must still run if allocation fails.
        HRESULT copyResult = S_OK;
        try
        {
            state->recordedAudio.resize(oldSize + bytesToCopy);
            if (bytesToCopy > 0)
            {
                BYTE* destination = state->recordedAudio.data() + oldSize;
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                {
                    std::memset(destination, state->silenceByte, bytesToCopy);
                }
                else if (pData)
                {
                    std::memcpy(destination, pData, bytesToCopy);
                }
                else
                {
                    state->recordedAudio.resize(oldSize);
                    copyResult = E_POINTER;
                }
            }
        }
        catch (const std::bad_alloc&)
        {
            copyResult = E_OUTOFMEMORY;
        }

        if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
        {
            ++state->discontinuityFlags;
        }

        // Consume the whole packet, including any excess beyond the 30-second cap.
        // Do not use pData after this call.
        HRESULT releaseResult = state->captureClient->ReleaseBuffer(numFrames);
        if (FAILED(releaseResult))
        {
            return releaseResult;
        }
        if (FAILED(copyResult))
        {
            return copyResult;
        }
        if (state->recordedAudio.size() >= state->maxRecordingBytes)
        {
            return S_FALSE;
        }

        hr = state->captureClient->GetNextPacketSize(&packetFrames);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    return S_OK;
}

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