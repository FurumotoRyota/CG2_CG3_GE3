#include "Audio.h"

#include <cassert>
#include <cstring>
#include <fstream>

#pragma comment(lib, "xaudio2.lib")

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
