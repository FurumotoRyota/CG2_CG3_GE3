#pragma once
#include <xaudio2.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/// <summary>
/// XAudio2による音声再生
/// LoadWave で読み込んで得たハンドルで Play / Pause / Resume / Stop を行う
/// （1つのハンドルにつきボイスは1つ。BGMのような使い方を想定。SEの多重再生は今後拡張する）
/// </summary>
class Audio
{
public:
    using SoundHandle = uint32_t;
    static constexpr SoundHandle kInvalidHandle = 0xFFFFFFFF;

    void Initialize();
    void Finalize();

    // 毎フレーム1回呼ぶ。再生し終わったワンショットのボイスを片付ける
    void Update();

    // WAVファイルを読み込む。"fmt "/"data"以外のチャンク(LIST等)は読み飛ばす
    SoundHandle LoadWave(const std::string& filename);
    // Media Foundationで読み込む。WAVに加えてMP3/AAC(m4a)など圧縮フォーマットも使える
    // 読み込み時に再生用のPCMへ全て展開するので、再生は LoadWave と同じ Play / PlayOneShot でできる
    SoundHandle LoadSound(const std::string& filename);
    void Unload(SoundHandle handle);

    // 最初から再生する（再生中なら作り直す）
    void Play(SoundHandle handle, bool loop = false);
    // SE向け：同じハンドルを重ねて何回でも同時再生できる（止める必要なし。再生後は自動で破棄される）
    // ※再生中のワンショットがある間は、そのハンドルを Unload しないこと
    void PlayOneShot(SoundHandle handle, float volume = 1.0f);
    // 一時停止（Resumeで続きから再生）
    void Pause(SoundHandle handle);
    void Resume(SoundHandle handle);
    // 停止してボイスを破棄する
    void Stop(SoundHandle handle);

    bool IsPlaying(SoundHandle handle) const;

private:
    struct Sound
    {
        WAVEFORMATEX wfex{};
        std::vector<BYTE> buffer;
        IXAudio2SourceVoice* voice = nullptr;
        bool playing = false;
    };

    Sound& GetSound(SoundHandle handle);
    const Sound& GetSound(SoundHandle handle) const;
    static void DestroyVoice(Sound& sound);

    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice* masterVoice_ = nullptr;

    std::unordered_map<SoundHandle, Sound> sounds_;
    std::vector<IXAudio2SourceVoice*> oneShotVoices_;
    SoundHandle nextHandle_ = 0;
};
