#include "AudioCapture.h"

#include <mmdeviceapi.h>
#include <cstring>
#include <new>

namespace
{
constexpr size_t MAX_RECORDING_SECONDS = 30;
}

AudioState::~AudioState()
{
    if (isRecording && audioClient)
    {
        audioClient->Stop();
    }
    if (captureClient) captureClient->Release();
    if (audioClient) audioClient->Release();
}

int InitializeAudio(AudioState& state)
{
    HRESULT hr = S_OK;

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
        return 1;
    }

    // Requested buffer duration.
    // REFERENCE_TIME uses units of 100 nanoseconds.
    // 10,000,000 times 100 nanoseconds = 1 second.
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
        return 1;
    }


    state.audioClient = pAudioClient;
    state.captureClient = pCaptureClient;
    state.blockAlign = blockAlign;
    state.silenceByte = silenceByte;
    state.maxRecordingBytes = maxRecordingBytes;
    firstMicrophone->Release();
    pEnumerator->Release();
    return -1;
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

