#pragma once

#include "PitchShifter.h"

#include <windows.h>
#include <audioclient.h>
#include <wrl/client.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

// Streams microphone -> pitch shifter -> output device on a dedicated audio thread.
//
// The output device should be the *playback* side of a virtual audio cable
// (for example "CABLE Input" from VB-Audio). Other apps then select the cable's
// *recording* side ("CABLE Output") as their microphone and hear the changed voice.
class AudioEngine
{
public:
    AudioEngine() = default;
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Opens the default microphone and the first playback device whose name
    // contains `outputNameHint`. Falls back to the default playback device.
    bool Initialize(const wchar_t* outputNameHint);
    bool Start();
    void Stop();

    void SetPushToTalkKey(int virtualKey) { pushToTalkKey_.store(virtualKey); }
    int PushToTalkKey() const { return pushToTalkKey_.load(); }
    void SetSemitones(float semitones) { semitones_.store(semitones); }
    float Semitones() const { return semitones_.load(); }

    bool IsTalking() const { return talking_.load(); }
    bool IsRunning() const { return running_.load(); }
    HRESULT ThreadError() const { return threadError_.load(); }
    UINT32 Underruns() const { return underruns_.load(); }
    UINT32 DroppedBlocks() const { return droppedBlocks_.load(); }
    UINT32 Discontinuities() const { return discontinuities_.load(); }

    const std::wstring& InputName() const { return inputName_; }
    const std::wstring& OutputName() const { return outputName_; }
    bool UsingNamedOutput() const { return usingNamedOutput_; }
    const std::wstring& ErrorMessage() const { return errorMessage_; }

private:
    void AudioThreadMain();
    HRESULT ProcessCapturedAudio();
    void ProcessBlock(const float* input, UINT32 frames);
    HRESULT WriteToRender(const float* samples, UINT32 frames);
    HRESULT WriteSilence(UINT32 frames);
    bool Fail(const wchar_t* operation, HRESULT hr);

    Microsoft::WRL::ComPtr<IAudioClient> captureAudioClient_;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient_;
    Microsoft::WRL::ComPtr<IAudioClient> renderAudioClient_;
    Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient_;
    HANDLE captureEvent_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    std::thread thread_;
    UINT32 renderBufferFrames_ = 0;

    // Owned by the audio thread while running.
    PitchShifter shifter_;
    std::vector<float> scratch_;
    float gain_ = 0.0f;

    std::atomic<float> semitones_{ 0.0f };
    std::atomic<int> pushToTalkKey_{ VK_F8 };
    std::atomic<bool> talking_{ false };
    std::atomic<bool> running_{ false };
    std::atomic<HRESULT> threadError_{ S_OK };
    std::atomic<UINT32> underruns_{ 0 };
    std::atomic<UINT32> droppedBlocks_{ 0 };
    std::atomic<UINT32> discontinuities_{ 0 };

    std::wstring inputName_;
    std::wstring outputName_;
    bool usingNamedOutput_ = false;
    std::wstring errorMessage_;
};
