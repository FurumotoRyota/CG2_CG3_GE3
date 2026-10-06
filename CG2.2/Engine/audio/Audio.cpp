#include "Audio.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cassert>
#include <cstring>
#include <fstream>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")      // Media Foundation platform
#pragma comment(lib, "mfreadwrite.lib") // IMFSourceReader
#pragma comment(lib, "mfuuid.lib")      // MF GUIDs

namespace
{
    struct ChunkHeader
    {
        char id[4];   // チャンク毎のID
        int32_t size; // チャンクサイズ
    };

    struct RiffHeader
    {
        ChunkHeader chunk; // "RIFF"
        char type[4];      // "WAVE"
    };

    struct FormatChunk
    {
        ChunkHeader chunk; // "fmt "
        WAVEFORMATEX fmt;  // 波形フォーマット
    };
}

void Audio::Initialize()
{
    HRESULT hr = XAudio2Create(xAudio2_.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR);
    assert(SUCCEEDED(hr));

    hr = xAudio2_->CreateMasteringVoice(&masterVoice_);
    assert(SUCCEEDED(hr));

    // Media Foundation (used to decode MP3/AAC/etc. into PCM)
    hr = MFStartup(MF_VERSION);
    assert(SUCCEEDED(hr));
}

void Audio::Finalize()
{
    for (IXAudio2SourceVoice* voice : oneShotVoices_) {
        voice->Stop();
        voice->DestroyVoice();
    }
    oneShotVoices_.clear();

    for (auto& [handle, sound] : sounds_) {
        DestroyVoice(sound);
    }
    sounds_.clear();

    if (masterVoice_) {
        masterVoice_->DestroyVoice();
        masterVoice_ = nullptr;
    }
    xAudio2_.Reset();

    MFShutdown();
}

Audio::SoundHandle Audio::LoadWave(const std::string& filename)
{
    std::ifstream file(filename, std::ios_base::binary);
    assert(file.is_open());

    RiffHeader riff{};
    file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
    assert(std::strncmp(riff.chunk.id, "RIFF", 4) == 0);
    assert(std::strncmp(riff.type, "WAVE", 4) == 0);

    FormatChunk format{};
    bool formatFound = false;
    ChunkHeader dataChunkHeader{};
    bool dataFound = false;

    while (!dataFound) {
        ChunkHeader chunk{};
        file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk));
        assert(!file.eof());

        if (std::strncmp(chunk.id, "fmt ", 4) == 0) {
            format.chunk = chunk;
            assert(static_cast<size_t>(chunk.size) <= sizeof(format.fmt));
            file.read(reinterpret_cast<char*>(&format.fmt), chunk.size);
            formatFound = true;
        }
        else if (std::strncmp(chunk.id, "data", 4) == 0) {
            dataChunkHeader = chunk;
            dataFound = true;
        }
        else {
            // LIST/INFO/JUNKなど未対応チャンクは読み飛ばす（奇数サイズは1byteパディングがある）
            file.seekg(chunk.size + (chunk.size % 2), std::ios_base::cur);
        }
    }
    assert(formatFound);

    Sound sound;
    sound.wfex = format.fmt;
    sound.buffer.resize(dataChunkHeader.size);
    file.read(reinterpret_cast<char*>(sound.buffer.data()), dataChunkHeader.size);

    SoundHandle handle = nextHandle_++;
    sounds_.emplace(handle, std::move(sound));
    return handle;
}

Audio::SoundHandle Audio::LoadSound(const std::string& filename)
{
    // UTF-8 path -> wide string
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0);
    std::wstring widePath(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, widePath.data(), wideLength);

    // Open the file with a source reader (it picks the right decoder for the format)
    Microsoft::WRL::ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(widePath.c_str(), nullptr, reader.GetAddressOf());
    assert(SUCCEEDED(hr));

    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);

    // Ask the reader to output uncompressed PCM
    Microsoft::WRL::ComPtr<IMFMediaType> pcmType;
    hr = MFCreateMediaType(pcmType.GetAddressOf());
    assert(SUCCEEDED(hr));
    pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, pcmType.Get());
    assert(SUCCEEDED(hr));

    // Get the actual PCM format (sample rate, channels, bit depth)
    Microsoft::WRL::ComPtr<IMFMediaType> outType;
    hr = reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), outType.GetAddressOf());
    assert(SUCCEEDED(hr));

    WAVEFORMATEX* waveFormat = nullptr;
    UINT32 waveFormatSize = 0;
    hr = MFCreateWaveFormatExFromMFMediaType(outType.Get(), &waveFormat, &waveFormatSize);
    assert(SUCCEEDED(hr));

    Sound sound;
    sound.wfex = *waveFormat;
    CoTaskMemFree(waveFormat);

    // Read all samples and append the PCM data
    while (true) {
        DWORD flags = 0;
        Microsoft::WRL::ComPtr<IMFSample> sample;
        hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, sample.GetAddressOf());
        assert(SUCCEEDED(hr));

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            break;
        }
        if (!sample) {
            continue;
        }

        Microsoft::WRL::ComPtr<IMFMediaBuffer> mediaBuffer;
        hr = sample->ConvertToContiguousBuffer(mediaBuffer.GetAddressOf());
        assert(SUCCEEDED(hr));

        BYTE* data = nullptr;
        DWORD dataLength = 0;
        hr = mediaBuffer->Lock(&data, nullptr, &dataLength);
        assert(SUCCEEDED(hr));
        sound.buffer.insert(sound.buffer.end(), data, data + dataLength);
        mediaBuffer->Unlock();
    }

    SoundHandle handle = nextHandle_++;
    sounds_.emplace(handle, std::move(sound));
    return handle;
}

void Audio::Unload(SoundHandle handle)
{
    auto it = sounds_.find(handle);
    if (it == sounds_.end()) {
        return;
    }
    DestroyVoice(it->second);
    sounds_.erase(it);
}

void Audio::Play(SoundHandle handle, bool loop)
{
    Sound& sound = GetSound(handle);
    DestroyVoice(sound);

    HRESULT hr = xAudio2_->CreateSourceVoice(&sound.voice, &sound.wfex);
    assert(SUCCEEDED(hr));

    XAUDIO2_BUFFER buffer{};
    buffer.pAudioData = sound.buffer.data();
    buffer.AudioBytes = static_cast<UINT32>(sound.buffer.size());
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

    hr = sound.voice->SubmitSourceBuffer(&buffer);
    assert(SUCCEEDED(hr));

    hr = sound.voice->Start();
    assert(SUCCEEDED(hr));

    sound.playing = true;
}

void Audio::PlayOneShot(SoundHandle handle, float volume)
{
    Sound& sound = GetSound(handle);

    IXAudio2SourceVoice* voice = nullptr;
    HRESULT hr = xAudio2_->CreateSourceVoice(&voice, &sound.wfex);
    assert(SUCCEEDED(hr));

    XAUDIO2_BUFFER buffer{};
    buffer.pAudioData = sound.buffer.data();
    buffer.AudioBytes = static_cast<UINT32>(sound.buffer.size());
    buffer.Flags = XAUDIO2_END_OF_STREAM;

    hr = voice->SubmitSourceBuffer(&buffer);
    assert(SUCCEEDED(hr));
    voice->SetVolume(volume);

    hr = voice->Start();
    assert(SUCCEEDED(hr));

    oneShotVoices_.push_back(voice);
}

void Audio::Update()
{
    // 再生し終わったワンショットを破棄する
    for (auto it = oneShotVoices_.begin(); it != oneShotVoices_.end();) {
        XAUDIO2_VOICE_STATE state{};
        (*it)->GetState(&state);
        if (state.BuffersQueued == 0) {
            (*it)->DestroyVoice();
            it = oneShotVoices_.erase(it);
        }
        else {
            ++it;
        }
    }
}

void Audio::Pause(SoundHandle handle)
{
    Sound& sound = GetSound(handle);
    if (sound.voice) {
        sound.voice->Stop(); // Stopはバッファ位置を保持する（一時停止）
    }
    sound.playing = false;
}

void Audio::Resume(SoundHandle handle)
{
    Sound& sound = GetSound(handle);
    if (sound.voice) {
        sound.voice->Start();
        sound.playing = true;
    }
}

void Audio::Stop(SoundHandle handle)
{
    Sound& sound = GetSound(handle);
    DestroyVoice(sound);
}

bool Audio::IsPlaying(SoundHandle handle) const
{
    const Sound& sound = GetSound(handle);
    if (!sound.voice || !sound.playing) {
        return false;
    }
    // ループなしで最後まで再生し終わっていたら再生中ではない
    XAUDIO2_VOICE_STATE state{};
    sound.voice->GetState(&state);
    return state.BuffersQueued > 0;
}

Audio::Sound& Audio::GetSound(SoundHandle handle)
{
    auto it = sounds_.find(handle);
    assert(it != sounds_.end());
    return it->second;
}

const Audio::Sound& Audio::GetSound(SoundHandle handle) const
{
    auto it = sounds_.find(handle);
    assert(it != sounds_.end());
    return it->second;
}

void Audio::DestroyVoice(Sound& sound)
{
    if (sound.voice) {
        sound.voice->Stop();
        sound.voice->DestroyVoice();
        sound.voice = nullptr;
    }
    sound.playing = false;
}
