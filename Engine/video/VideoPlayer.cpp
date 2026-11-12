#include "VideoPlayer.h"

#include "../audio/Audio.h"
#include "../base/DirectXCommon.h"
#include "../base/SrvManager.h"

#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cassert>
#include <cstring>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr DXGI_FORMAT kVideoFormat = DXGI_FORMAT_B8G8R8X8_UNORM_SRGB; // X8: alpha is always 1
    constexpr int kTargetQueuedAudioBuffers = 8;
    constexpr int kMaxFramesPerUpdate = 8;

    std::wstring ToWide(const std::string& utf8)
    {
        const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        if (length <= 0) {
            return std::wstring();
        }
        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), length);
        wide.resize(static_cast<size_t>(length) - 1);
        return wide;
    }

    int64_t QueryCounter()
    {
        LARGE_INTEGER value{};
        QueryPerformanceCounter(&value);
        return value.QuadPart;
    }

    double CounterToSeconds(int64_t ticks)
    {
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        return static_cast<double>(ticks) / static_cast<double>(freq.QuadPart);
    }

    void SeekReaderToStart(IMFSourceReader* reader)
    {
        PROPVARIANT position;
        PropVariantInit(&position);
        position.vt = VT_I8;
        position.hVal.QuadPart = 0;
        reader->SetCurrentPosition(GUID_NULL, position);
        PropVariantClear(&position);
    }
}

VideoPlayer::~VideoPlayer()
{
    if (voice_) {
        voice_->Stop();
        voice_->DestroyVoice();
        voice_ = nullptr;
    }
    if (uploadBuffer_ && uploadMapped_) {
        uploadBuffer_->Unmap(0, nullptr);
        uploadMapped_ = nullptr;
    }
    if (srvAllocated_ && srvManager_) {
        srvManager_->Free(srvIndex_);
    }
}

bool VideoPlayer::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, Audio* audio, const std::string& path)
{
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    audio_ = audio;

    const std::wstring widePath = ToWide(path);

    //--------------------------------------------------
    // Video reader (RGB32 output; the reader converts the color format for us)
    //--------------------------------------------------
    ComPtr<IMFAttributes> attributes;
    MFCreateAttributes(attributes.GetAddressOf(), 1);
    attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    HRESULT hr = MFCreateSourceReaderFromURL(widePath.c_str(), attributes.Get(), videoReader_.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    videoReader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    videoReader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);

    ComPtr<IMFMediaType> videoType;
    MFCreateMediaType(videoType.GetAddressOf());
    videoType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    videoType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = videoReader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, videoType.Get());
    if (FAILED(hr)) {
        return false; // no video stream or no decoder (e.g. H.265 without the codec)
    }

    ComPtr<IMFMediaType> outType;
    hr = videoReader_->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), outType.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    MFGetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, &width_, &height_);
    if (width_ == 0 || height_ == 0) {
        return false;
    }
    defaultStride_ = static_cast<int32_t>(MFGetAttributeUINT32(outType.Get(), MF_MT_DEFAULT_STRIDE, 0));
    if (defaultStride_ == 0) {
        defaultStride_ = static_cast<int32_t>(width_) * 4;
    }

    PROPVARIANT durationValue;
    PropVariantInit(&durationValue);
    if (SUCCEEDED(videoReader_->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &durationValue))) {
        duration_ = static_cast<double>(durationValue.uhVal.QuadPart) / 10000000.0;
    }
    PropVariantClear(&durationValue);

    //--------------------------------------------------
    // Texture + upload buffer + SRV
    //--------------------------------------------------
    DirectX::TexMetadata metadata{};
    metadata.width = width_;
    metadata.height = height_;
    metadata.depth = 1;
    metadata.arraySize = 1;
    metadata.mipLevels = 1;
    metadata.format = kVideoFormat;
    metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
    texture_ = dxCommon_->CreateTextureResource(metadata);
    if (!texture_) {
        return false;
    }
    textureState_ = D3D12_RESOURCE_STATE_COPY_DEST;

    uploadRowPitch_ = (width_ * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    uploadBuffer_ = dxCommon_->CreateBufferResource(static_cast<size_t>(uploadRowPitch_) * height_);
    uploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&uploadMapped_));

    srvIndex_ = srvManager_->Allocate();
    srvAllocated_ = true;
    srvManager_->CreateSrvForTexture2D(srvIndex_, texture_.Get(), kVideoFormat, 1);

    // Show a black frame until the first video frame arrives (also moves the texture to the read state)
    UploadFrame(std::vector<uint8_t>(static_cast<size_t>(width_) * height_ * 4, 0));

    //--------------------------------------------------
    // Audio reader (optional: the file may have no audio)
    //--------------------------------------------------
    hr = MFCreateSourceReaderFromURL(widePath.c_str(), nullptr, audioReader_.GetAddressOf());
    if (SUCCEEDED(hr)) {
        audioReader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        hr = audioReader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);

        ComPtr<IMFMediaType> pcmType;
        if (SUCCEEDED(hr)) {
            MFCreateMediaType(pcmType.GetAddressOf());
            pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            hr = audioReader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, pcmType.Get());
        }
        if (SUCCEEDED(hr)) {
            ComPtr<IMFMediaType> audioOutType;
            hr = audioReader_->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), audioOutType.GetAddressOf());
            if (SUCCEEDED(hr)) {
                WAVEFORMATEX* waveFormat = nullptr;
                UINT32 waveFormatSize = 0;
                hr = MFCreateWaveFormatExFromMFMediaType(audioOutType.Get(), &waveFormat, &waveFormatSize);
                if (SUCCEEDED(hr)) {
                    audioFormat_ = *waveFormat;
                    CoTaskMemFree(waveFormat);
                    hasAudio_ = SUCCEEDED(audio_->GetXAudio2()->CreateSourceVoice(&voice_, &audioFormat_));
                }
            }
        }
        if (!hasAudio_) {
            audioReader_.Reset();
        }
    }

    clock_ = 0.0;
    playing_ = false;
    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE VideoPlayer::GetSrvHandle() const
{
    return srvManager_->GetGPUDescriptorHandle(srvIndex_);
}

void VideoPlayer::SetVolume(float volume)
{
    volume_ = volume;
    if (voice_) {
        voice_->SetVolume(volume_);
    }
}

void VideoPlayer::Play()
{
    if (finished_) {
        Restart();
    }
    if (playing_) {
        return;
    }
    playing_ = true;
    lastCounter_ = QueryCounter();
    if (voice_) {
        voice_->Start();
    }
}

void VideoPlayer::Pause()
{
    playing_ = false;
    if (voice_) {
        voice_->Stop(); // keeps the queued data, so Play() continues from the same place
    }
}

void VideoPlayer::Restart()
{
    SeekToStart();
    finished_ = false;
    lastCounter_ = QueryCounter();
}

void VideoPlayer::ResetAudioVoice()
{
    if (!voice_) {
        return;
    }
    voice_->Stop();
    voice_->FlushSourceBuffers();
    queuedAudio_.clear();
    if (playing_) {
        voice_->Start();
    }
}

void VideoPlayer::SeekToStart()
{
    SeekReaderToStart(videoReader_.Get());
    if (audioReader_) {
        SeekReaderToStart(audioReader_.Get());
    }
    ResetAudioVoice();
    clock_ = 0.0;
    haveNextFrame_ = false;
    videoEnded_ = false;
    audioEnded_ = false;
}

bool VideoPlayer::ReadNextFrame()
{
    if (videoEnded_) {
        return false;
    }

    while (true) {
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        HRESULT hr = videoReader_->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, &timestamp, sample.GetAddressOf());
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            videoEnded_ = true;
            return false;
        }
        if (!sample) {
            continue; // stream tick etc.
        }

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()))) {
            continue;
        }

        nextFrame_.resize(static_cast<size_t>(width_) * height_ * 4);
        const size_t rowBytes = static_cast<size_t>(width_) * 4;

        ComPtr<IMF2DBuffer> buffer2D;
        if (SUCCEEDED(buffer.As(&buffer2D))) {
            BYTE* scanline0 = nullptr;
            LONG pitch = 0;
            if (FAILED(buffer2D->Lock2D(&scanline0, &pitch))) {
                continue;
            }
            // scanline0 is the top row; pitch may be negative for bottom-up buffers
            for (uint32_t y = 0; y < height_; ++y) {
                std::memcpy(nextFrame_.data() + y * rowBytes, scanline0 + static_cast<ptrdiff_t>(y) * pitch, rowBytes);
            }
            buffer2D->Unlock2D();
        }
        else {
            BYTE* data = nullptr;
            DWORD length = 0;
            if (FAILED(buffer->Lock(&data, nullptr, &length))) {
                continue;
            }
            const int32_t stride = defaultStride_;
            const size_t absStride = static_cast<size_t>(stride < 0 ? -stride : stride);
            for (uint32_t y = 0; y < height_; ++y) {
                const uint32_t srcRow = (stride < 0) ? (height_ - 1 - y) : y; // negative stride = bottom-up
                std::memcpy(nextFrame_.data() + y * rowBytes, data + srcRow * absStride, rowBytes);
            }
            buffer->Unlock();
        }

        nextFrameTime_ = static_cast<double>(timestamp) / 10000000.0;
        haveNextFrame_ = true;
        return true;
    }
}

void VideoPlayer::FeedAudio()
{
    if (!voice_ || !audioReader_) {
        return;
    }

    // Drop buffers that have been played
    XAUDIO2_VOICE_STATE state{};
    voice_->GetState(&state);
    while (queuedAudio_.size() > state.BuffersQueued) {
        queuedAudio_.pop_front();
    }

    while (!audioEnded_ && queuedAudio_.size() < kTargetQueuedAudioBuffers) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        HRESULT hr = audioReader_->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, sample.GetAddressOf());
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            audioEnded_ = true;
            break;
        }
        if (!sample) {
            continue;
        }

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()))) {
            continue;
        }
        BYTE* data = nullptr;
        DWORD length = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &length))) {
            continue;
        }
        queuedAudio_.emplace_back(data, data + length);
        buffer->Unlock();

        XAUDIO2_BUFFER audioBuffer{};
        audioBuffer.pAudioData = queuedAudio_.back().data();
        audioBuffer.AudioBytes = static_cast<UINT32>(queuedAudio_.back().size());
        voice_->SubmitSourceBuffer(&audioBuffer);
    }
}

void VideoPlayer::Update()
{
    if (!playing_) {
        return;
    }

    // Advance the clock (clamped so a long stall does not skip a huge part of the video)
    const int64_t now = QueryCounter();
    const double delta = (std::min)(CounterToSeconds(now - lastCounter_), 0.1);
    lastCounter_ = now;
    clock_ += delta;

    FeedAudio();

    // Show the newest frame whose time has come (older ones are skipped)
    bool shown = false;
    for (int i = 0; i < kMaxFramesPerUpdate; ++i) {
        if (!haveNextFrame_ && !ReadNextFrame()) {
            break;
        }
        if (nextFrameTime_ > clock_) {
            break;
        }
        haveNextFrame_ = false;
        shownFrame_.swap(nextFrame_);
        shown = true;
        if (nextFrameTime_ + 0.05 >= clock_ || i == kMaxFramesPerUpdate - 1) {
            break; // close enough to the clock: show this one now
        }
    }
    if (shown) {
        UploadFrame(shownFrame_);
    }

    // End of the video
    if (videoEnded_ && !haveNextFrame_) {
        if (loop_) {
            SeekToStart();
        }
        else {
            playing_ = false;
            finished_ = true;
            if (voice_) {
                voice_->Stop();
            }
        }
    }
}

void VideoPlayer::UploadFrame(const std::vector<uint8_t>& pixels)
{
    const size_t rowBytes = static_cast<size_t>(width_) * 4;
    for (uint32_t y = 0; y < height_; ++y) {
        std::memcpy(uploadMapped_ + static_cast<size_t>(y) * uploadRowPitch_, pixels.data() + y * rowBytes, rowBytes);
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    auto transition = [&](D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture_.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        commandList->ResourceBarrier(1, &barrier);
    };

    if (textureState_ != D3D12_RESOURCE_STATE_COPY_DEST) {
        transition(textureState_, D3D12_RESOURCE_STATE_COPY_DEST);
    }

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = texture_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = uploadBuffer_.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = 0;
    src.PlacedFootprint.Footprint.Format = kVideoFormat;
    src.PlacedFootprint.Footprint.Width = width_;
    src.PlacedFootprint.Footprint.Height = height_;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = uploadRowPitch_;

    commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    transition(D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
    textureState_ = D3D12_RESOURCE_STATE_GENERIC_READ;
}
