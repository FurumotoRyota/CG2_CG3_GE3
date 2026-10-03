#pragma once
#include <Windows.h>
#include <wrl/client.h>
#include <cstdint>

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800 // DirectInput 8.0 以降を使用することを指定, dinput.h より先に書くこと
#endif
#include <dinput.h>

#include "../base/WinApp.h"

/// <summary>
/// キーボードとゲームパッド（DirectInput）の入力を管理する
/// 毎フレーム Update() を1回呼ぶこと
/// </summary>
class Input
{
public:
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    void Initialize(WinApp* winApp);
    void Update();
    void Finalize();

    //--------------------------------------------------
    // キーボード（keyNumber は DIK_xxx）
    //--------------------------------------------------
    bool PushKey(BYTE keyNumber) const;           // 押している
    bool ReleaseKey(BYTE keyNumber) const;        // 押していない
    bool TriggerKey(BYTE keyNumber) const;        // 押した瞬間
    bool ReleaseTriggerKey(BYTE keyNumber) const; // 離した瞬間

    // DebugCameraなど、配列をそのまま受け取る既存コード用
    const BYTE* GetKeys() const { return key_; }
    const BYTE* GetPrevKeys() const { return prevKey_; }

    //--------------------------------------------------
    // ゲームパッド
    // buttonIndex は DIJOYSTATE2::rgbButtons の添字（コントローラーにより並びが異なる場合がある）
    //--------------------------------------------------
    bool HasGamepad() const { return hasGamepad_; }
    const DIJOYSTATE2& GetJoyState() const { return joyState_; }
    const DIJOYSTATE2& GetPrevJoyState() const { return prevJoyState_; }

    bool PushGamepadButton(int buttonIndex) const;
    bool TriggerGamepadButton(int buttonIndex) const;

    // 十字キー(POV)が指定方向に入った瞬間か
    // directionCentidegrees: 0=上, 9000=右, 18000=下, 27000=左（±45度まで許容）
    bool TriggerPov(DWORD directionCentidegrees) const;

private:
    ComPtr<IDirectInput8> directInput_;
    ComPtr<IDirectInputDevice8> keyboard_;
    ComPtr<IDirectInputDevice8> joystick_; // 未接続ならnullptr

    BYTE key_[256] = {};
    BYTE prevKey_[256] = {};

    DIJOYSTATE2 joyState_{};
    DIJOYSTATE2 prevJoyState_{};
    bool hasGamepad_ = false;
};
