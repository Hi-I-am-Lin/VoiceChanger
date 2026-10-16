#pragma once

#include <windows.h>
#include <audioclient.h>
#include <vector>

struct AudioState
{
    AudioState() = default;
    ~AudioState();
    AudioState(const AudioState&) = delete;
    AudioState& operator=(const AudioState&) = delete;
    IAudioClient* audioClient = nullptr;
    IAudioCaptureClient* captureClient = nullptr;

    bool isRecording = false;

    WORD blockAlign = 0;
    BYTE silenceByte = 0;
    size_t maxRecordingBytes = 0;
    UINT32 discontinuityFlags = 0;
    std::vector<BYTE> recordedAudio;
};

// Returns the original process exit code on initialization failure, or -1 on success.
int InitializeAudio(AudioState& state);

// S_FALSE indicates that the 30-second storage limit was reached.
HRESULT ReadAvailableAudio(AudioState* state);
