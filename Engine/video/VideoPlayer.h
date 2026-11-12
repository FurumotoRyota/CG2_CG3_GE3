#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <xaudio2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class DirectXCommon;
class SrvManager;
class Audio;

/// <summary>
/// Plays a video file (mp4 etc.) with Media Foundation.
/// - Video frames are decoded one at a time and uploaded to a texture every frame (no whole-file decoding).
/// - Audio is decoded in small pieces and streamed to XAudio2.
/// - The playback clock is the wall clock (it stops while paused). Late frames are skipped.
/// Use GetSrvHandle() with Sprite::SetExternalTexture() to show it.
/// </summary>
class VideoPlayer
{
public:
    VideoPlayer() = default;
    ~VideoPlayer();
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    // path: UTF-8. Returns false if the file cannot be opened/decoded.
    bool Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, Audio* audio, const std::string& path);

    // Call once per frame before drawing (records the texture upload into the current command list)
    void Update();

    void Play();    // also restarts from the beginning when the video has finished
    void Pause();
    void Restart(); // seek to the beginning (keeps the playing/paused state)

    void SetLoop(bool loop) { loop_ = loop; }
    bool IsLoop() const { return loop_; }
    bool IsPlaying() const { return playing_; }
    bool IsFinished() const { return finished_; }

    void SetVolume(float volume);
    float GetVolume() const { return volume_; }

    double GetTime() const { return clock_; }
    double GetDuration() const { return duration_; }
    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandle() const;

private:
    bool ReadNextFrame();  // decode the next video frame into nextFrame_ (false at the end of the file)
    void FeedAudio();      // keep a few audio buffers queued
    void UploadFrame(const std::vector<uint8_t>& pixels);
    void SeekToStart();
    void ResetAudioVoice();

    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    Audio* audio_ = nullptr;

    // Video
    Microsoft::WRL::ComPtr<IMFSourceReader> videoReader_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    int32_t defaultStride_ = 0;
    double duration_ = 0.0;
    std::vector<uint8_t> nextFrame_;  // decoded, waiting for its time (tightly packed BGRA, top-down)
    std::vector<uint8_t> shownFrame_; // the frame to upload now
    double nextFrameTime_ = 0.0;
    bool haveNextFrame_ = false;
    bool videoEnded_ = false;

    // Texture (default heap) + persistent upload buffer
    Microsoft::WRL::ComPtr<ID3D12Resource> texture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer_;
    uint8_t* uploadMapped_ = nullptr;
    uint32_t uploadRowPitch_ = 0;
    uint32_t srvIndex_ = 0;
    bool srvAllocated_ = false;
    D3D12_RESOURCE_STATES textureState_ = D3D12_RESOURCE_STATE_COPY_DEST;

    // Audio
    Microsoft::WRL::ComPtr<IMFSourceReader> audioReader_;
    WAVEFORMATEX audioFormat_{};
    IXAudio2SourceVoice* voice_ = nullptr;
    std::deque<std::vector<uint8_t>> queuedAudio_; // keeps submitted buffers alive until played
    bool audioEnded_ = false;
    bool hasAudio_ = false;
    float volume_ = 1.0f;

    // Clock
    double clock_ = 0.0;
    int64_t lastCounter_ = 0;
    bool playing_ = false;
    bool finished_ = false;
    bool loop_ = true;
};
