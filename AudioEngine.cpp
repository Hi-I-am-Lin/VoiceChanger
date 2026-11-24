// initguid.h must come first so PKEY_Device_FriendlyName is defined in this file.
#include <initguid.h>
#include "AudioEngine.h"

#include <avrt.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <cwctype>

using Microsoft::WRL::ComPtr;

namespace
{
    // Both streams use this format. AUTOCONVERTPCM makes Windows convert
    // to and from each device's real mix format (rate and channel count).
    constexpr DWORD SAMPLE_RATE = 48000;

    constexpr REFERENCE_TIME CAPTURE_BUFFER_DURATION = 500'000;  // 50 ms
    constexpr REFERENCE_TIME RENDER_BUFFER_DURATION = 1'000'000; // 100 ms

    // Audio queued for playback. Larger is safer against glitches but adds latency.
    constexpr UINT32 TARGET_RENDER_FRAMES = SAMPLE_RATE * 30 / 1000;
    // If the output clock runs slower than the microphone, the queue grows; drop audio above this.
    constexpr UINT32 MAX_RENDER_FRAMES = SAMPLE_RATE * 80 / 1000;

    // Fade in/out when the push-to-talk key changes, to avoid clicks.
    constexpr float FADE_SECONDS = 0.005f;

    constexpr DWORD STREAM_FLAGS =
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    WAVEFORMATEXTENSIBLE MakeMonoFloatFormat()
    {
        WAVEFORMATEXTENSIBLE format = {};
        format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        format.Format.nChannels = 1;
        format.Format.nSamplesPerSec = SAMPLE_RATE;
        format.Format.wBitsPerSample = 32;
        format.Format.nBlockAlign = sizeof(float);
        format.Format.nAvgBytesPerSec = SAMPLE_RATE * sizeof(float);
        format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        format.Samples.wValidBitsPerSample = 32;
        format.dwChannelMask = SPEAKER_FRONT_CENTER;
        format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        return format;
    }

    std::wstring GetFriendlyName(IMMDevice *device)
    {
        std::wstring name = L"(unknown device)";
        ComPtr<IPropertyStore> store;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)))
        {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal)
            {
                name = value.pwszVal;
            }
            PropVariantClear(&value);
        }
        return name;
    }

    bool ContainsNoCase(std::wstring haystack, std::wstring needle)
    {
        auto lower = [](std::wstring &s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](wchar_t c)
                           { return static_cast<wchar_t>(std::towlower(c)); });
        };
        lower(haystack);
        lower(needle);
        return haystack.find(needle) != std::wstring::npos;
    }

    // Returns S_FALSE when no active playback device matches.
    HRESULT FindRenderDeviceByName(IMMDeviceEnumerator *enumerator,
                                   const wchar_t *nameHint, IMMDevice **result)
    {
        *result = nullptr;
        if (!nameHint || !*nameHint)
        {
            return S_FALSE;
        }

        ComPtr<IMMDeviceCollection> devices;
        HRESULT hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices);
        if (FAILED(hr))
        {
            return hr;
        }

        UINT count = 0;
        hr = devices->GetCount(&count);
        if (FAILED(hr))
        {
            return hr;
        }

        for (UINT i = 0; i < count; ++i)
        {
            ComPtr<IMMDevice> device;
            if (SUCCEEDED(devices->Item(i, &device)) && ContainsNoCase(GetFriendlyName(device.Get()), nameHint))
            {
                *result = device.Detach();
                return S_OK;
            }
        }
        return S_FALSE;
    }
}

AudioEngine::~AudioEngine()
{
    Stop();
    if (captureEvent_)
        CloseHandle(captureEvent_);
    if (stopEvent_)
        CloseHandle(stopEvent_);
}

bool AudioEngine::Fail(const wchar_t *operation, HRESULT hr)
{
    wchar_t text[256] = {};
    swprintf_s(text, L"%ls failed: HRESULT 0x%08lX", operation, static_cast<unsigned long>(hr));
    errorMessage_ = text;
    return false;
}

bool AudioEngine::Initialize(const wchar_t *outputNameHint)
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&enumerator));
    if (FAILED(hr))
        return Fail(L"Create device enumerator", hr);

    // ---- Input: the real microphone ----
    ComPtr<IMMDevice> input;
    hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &input);
    if (FAILED(hr))
        return Fail(L"Find default microphone", hr);
    inputName_ = GetFriendlyName(input.Get());

    // Capturing the cable we write into would create a feedback loop.
    if (ContainsNoCase(inputName_, L"VB-Audio") || ContainsNoCase(inputName_, L"CABLE Output"))
    {
        errorMessage_ = L"The default Windows microphone is the virtual cable (" + inputName_ + L").\nSet your real microphone as the default recording device, "
                                                                                                L"and select CABLE Output only inside the chat app.";
        return false;
    }

    // ---- Output: the virtual cable, or speakers for testing ----
    ComPtr<IMMDevice> output;
    hr = FindRenderDeviceByName(enumerator.Get(), outputNameHint, &output);
    if (FAILED(hr))
        return Fail(L"Enumerate playback devices", hr);
    usingNamedOutput_ = (hr == S_OK);
    if (!usingNamedOutput_)
    {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &output);
        if (FAILED(hr))
            return Fail(L"Find default playback device", hr);
    }
    outputName_ = GetFriendlyName(output.Get());

    WAVEFORMATEXTENSIBLE format = MakeMonoFloatFormat();
    const WAVEFORMATEX *wfx = &format.Format;

    // ---- Capture stream, woken by an event each device period (~10 ms) ----
    hr = input->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &captureAudioClient_);
    if (FAILED(hr))
        return Fail(L"Activate microphone", hr);

    hr = captureAudioClient_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                         STREAM_FLAGS | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                         CAPTURE_BUFFER_DURATION, 0, wfx, nullptr);
    if (FAILED(hr))
        return Fail(L"Initialize microphone stream", hr);

    captureEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!captureEvent_ || !stopEvent_)
    {
        return Fail(L"Create events", HRESULT_FROM_WIN32(GetLastError()));
    }

    hr = captureAudioClient_->SetEventHandle(captureEvent_);
    if (FAILED(hr))
        return Fail(L"Set microphone event", hr);

    hr = captureAudioClient_->GetService(IID_PPV_ARGS(&captureClient_));
    if (FAILED(hr))
        return Fail(L"Get capture client", hr);

    // ---- Render stream, filled from the audio thread after every capture packet ----
    hr = output->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &renderAudioClient_);
    if (FAILED(hr))
        return Fail(L"Activate output device", hr);

    hr = renderAudioClient_->Initialize(AUDCLNT_SHAREMODE_SHARED, STREAM_FLAGS,
                                        RENDER_BUFFER_DURATION, 0, wfx, nullptr);
    if (FAILED(hr))
        return Fail(L"Initialize output stream", hr);

    hr = renderAudioClient_->GetBufferSize(&renderBufferFrames_);
    if (FAILED(hr))
        return Fail(L"Get output buffer size", hr);

    hr = renderAudioClient_->GetService(IID_PPV_ARGS(&renderClient_));
    if (FAILED(hr))
        return Fail(L"Get render client", hr);

    shifter_.Prepare(SAMPLE_RATE);
    // Reserve now so the audio thread never allocates. Packets are smaller than the capture buffer.
    scratch_.reserve(SAMPLE_RATE);
    return true;
}

bool AudioEngine::Start()
{
    if (running_.load())
        return true;
    if (!captureAudioClient_ || !renderAudioClient_)
    {
        errorMessage_ = L"Audio engine is not initialized.";
        return false;
    }

    threadError_.store(S_OK);
    ResetEvent(stopEvent_);

    // Queue a little silence so playback does not starve before the first packet.
    HRESULT hr = WriteSilence(TARGET_RENDER_FRAMES);
    if (FAILED(hr))
        return Fail(L"Prefill output", hr);

    hr = renderAudioClient_->Start();
    if (FAILED(hr))
        return Fail(L"Start output", hr);

    hr = captureAudioClient_->Start();
    if (FAILED(hr))
    {
        renderAudioClient_->Stop();
        return Fail(L"Start microphone", hr);
    }

    running_.store(true);
    thread_ = std::thread(&AudioEngine::AudioThreadMain, this);
    return true;
}

void AudioEngine::Stop()
{
    if (thread_.joinable())
    {
        SetEvent(stopEvent_);
        thread_.join();
    }
    if (captureAudioClient_)
    {
        captureAudioClient_->Stop();
        captureAudioClient_->Reset();
    }
    if (renderAudioClient_)
    {
        renderAudioClient_->Stop();
        renderAudioClient_->Reset();
    }
    running_.store(false);
    talking_.store(false);
}

void AudioEngine::AudioThreadMain()
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Ask Windows to schedule this thread as real-time audio work.
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    HANDLE waitHandles[2] = {stopEvent_, captureEvent_};
    for (;;)
    {
        // The timeout only matters if the device stops signalling.
        const DWORD wait = WaitForMultipleObjects(2, waitHandles, FALSE, 1000);
        if (wait == WAIT_OBJECT_0)
        {
            break;
        }
        if (wait != WAIT_OBJECT_0 + 1 && wait != WAIT_TIMEOUT)
        {
            threadError_.store(HRESULT_FROM_WIN32(GetLastError()));
            break;
        }

        const HRESULT hr = ProcessCapturedAudio();
        if (FAILED(hr))
        {
            // For example AUDCLNT_E_DEVICE_INVALIDATED when a device is unplugged.
            threadError_.store(hr);
            break;
        }
    }

    talking_.store(false);
    running_.store(false);
    if (mmcss)
        AvRevertMmThreadCharacteristics(mmcss);
    if (SUCCEEDED(comResult))
        CoUninitialize();
}

HRESULT AudioEngine::ProcessCapturedAudio()
{
    UINT32 packetFrames = 0;
    HRESULT hr = captureClient_->GetNextPacketSize(&packetFrames);

    while (SUCCEEDED(hr) && packetFrames > 0)
    {
        BYTE *data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        hr = captureClient_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (FAILED(hr))
            return hr;
        if (hr == AUDCLNT_S_BUFFER_EMPTY || frames == 0)
            return S_OK;

        if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
        {
            // The thread was late and Windows overwrote some microphone data.
            discontinuities_.fetch_add(1);
        }

        const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data;
        ProcessBlock(silent ? nullptr : reinterpret_cast<const float *>(data), frames);

        // Do not use `data` after this call.
        hr = captureClient_->ReleaseBuffer(frames);
        if (FAILED(hr))
            return hr;

        hr = WriteToRender(scratch_.data(), frames);
        if (FAILED(hr))
            return hr;

        hr = captureClient_->GetNextPacketSize(&packetFrames);
    }
    return hr;
}

void AudioEngine::ProcessBlock(const float *input, UINT32 frames)
{
    scratch_.resize(frames); // Within reserved capacity, so no allocation.
    shifter_.SetSemitones(semitones_.load());

    // GetAsyncKeyState sees the key even when another app has keyboard focus.
    const bool held = (GetAsyncKeyState(pushToTalkKey_.load()) & 0x8000) != 0;
    talking_.store(held);

    const float target = held ? 1.0f : 0.0f;
    const float step = 1.0f / (FADE_SECONDS * SAMPLE_RATE);

    for (UINT32 i = 0; i < frames; ++i)
    {
        // Keep the shifter running while muted so it has history when the key is pressed.
        const float shifted = shifter_.Process(input ? input[i] : 0.0f);

        if (gain_ < target)
            gain_ = std::min(target, gain_ + step);
        else if (gain_ > target)
            gain_ = std::max(target, gain_ - step);

        scratch_[i] = shifted * gain_;
    }
}

HRESULT AudioEngine::WriteSilence(UINT32 frames)
{
    BYTE *buffer = nullptr;
    HRESULT hr = renderClient_->GetBuffer(frames, &buffer);
    if (FAILED(hr))
        return hr;
    return renderClient_->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT);
}

HRESULT AudioEngine::WriteToRender(const float *samples, UINT32 frames)
{
    UINT32 padding = 0;
    HRESULT hr = renderAudioClient_->GetCurrentPadding(&padding);
    if (FAILED(hr))
        return hr;

    if (padding == 0)
    {
        // Playback ran dry (the output clock is faster, or we were late). Rebuild the cushion.
        underruns_.fetch_add(1);
        hr = WriteSilence(TARGET_RENDER_FRAMES);
        if (FAILED(hr))
            return hr;
        padding = TARGET_RENDER_FRAMES;
    }

    if (padding >= MAX_RENDER_FRAMES)
    {
        // Too much audio is queued; drop this block to keep latency bounded.
        droppedBlocks_.fetch_add(1);
        return S_OK;
    }

    const UINT32 frameCount = std::min(frames, renderBufferFrames_ - padding);
    if (frameCount == 0)
        return S_OK;

    BYTE *buffer = nullptr;
    hr = renderClient_->GetBuffer(frameCount, &buffer);
    if (FAILED(hr))
        return hr;
    std::memcpy(buffer, samples, frameCount * sizeof(float));
    return renderClient_->ReleaseBuffer(frameCount, 0);
}
