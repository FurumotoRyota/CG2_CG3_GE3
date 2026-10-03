#include "Input.h"
#include "../base/Logger.h"

#include <cassert>
#include <cstring>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

namespace
{
    // 接続されているゲームコントローラーを列挙し、最初の1台のGUIDを取得する
    BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE* instance, VOID* context)
    {
        GUID* foundGuid = reinterpret_cast<GUID*>(context);
        *foundGuid = instance->guidInstance;
        return DIENUM_STOP; // 最初に見つかった1台だけを使う
    }

    // 各軸の値の範囲を-32768～32767に設定する
    BOOL CALLBACK EnumAxesCallback(const DIDEVICEOBJECTINSTANCE* instance, VOID* context)
    {
        IDirectInputDevice8* joystick = reinterpret_cast<IDirectInputDevice8*>(context);

        DIPROPRANGE propRange{};
        propRange.diph.dwSize = sizeof(DIPROPRANGE);
        propRange.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        propRange.diph.dwHow = DIPH_BYID;
        propRange.diph.dwObj = instance->dwType;
        propRange.lMin = -32768;
        propRange.lMax = 32767;

        joystick->SetProperty(DIPROP_RANGE, &propRange.diph);
        return DIENUM_CONTINUE;
    }

    // POVハットの値(1/100度単位。0xFFFFFFFFは未入力)が指定方向とみなせるか判定する
    // （±45度まで許容し、4方向・8方向どちらのPOVにも対応）
    bool PovMatchesDirection(DWORD pov, DWORD directionCentidegrees)
    {
        if (pov == 0xFFFFFFFF) {
            return false;
        }
        DWORD diff = (pov > directionCentidegrees) ? (pov - directionCentidegrees) : (directionCentidegrees - pov);
        if (diff > 18000) {
            diff = 36000 - diff;
        }
        return diff <= 4500;
    }
}

void Input::Initialize(WinApp* winApp)
{
    assert(winApp);

    HRESULT hr = DirectInput8Create(
        winApp->GetHInstance(),
        DIRECTINPUT_VERSION,
        IID_IDirectInput8,
        reinterpret_cast<void**>(directInput_.GetAddressOf()),
        nullptr);
    assert(SUCCEEDED(hr));

    // キーボード
    hr = directInput_->CreateDevice(GUID_SysKeyboard, keyboard_.GetAddressOf(), nullptr);
    assert(SUCCEEDED(hr));

    hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(hr));

    hr = keyboard_->SetCooperativeLevel(winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
    assert(SUCCEEDED(hr));

    // ゲームパッド（接続されていない場合は joystick_ == nullptr のままにして、以降のポーリングをスキップする）
    GUID joystickGuid = GUID_NULL;
    directInput_->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, &joystickGuid, DIEDFL_ATTACHEDONLY);

    if (joystickGuid != GUID_NULL) {
        HRESULT joyHr = directInput_->CreateDevice(joystickGuid, joystick_.GetAddressOf(), nullptr);
        if (SUCCEEDED(joyHr)) {
            joyHr = joystick_->SetDataFormat(&c_dfDIJoystick2);
            assert(SUCCEEDED(joyHr));

            joyHr = joystick_->SetCooperativeLevel(winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
            assert(SUCCEEDED(joyHr));

            joystick_->EnumObjects(EnumAxesCallback, joystick_.Get(), DIDFT_AXIS);

            Logger::Log("GamePad connected");
        }
        else {
            joystick_.Reset();
            Logger::Log("GamePad found but failed to create device");
        }
    }
    else {
        Logger::Log("GamePad not found (keyboard only)");
    }
}

void Input::Update()
{
    // キーボード
    std::memcpy(prevKey_, key_, sizeof(key_));
    keyboard_->Acquire();
    keyboard_->GetDeviceState(sizeof(key_), key_);

    // ゲームパッド（接続されている場合のみ）
    prevJoyState_ = joyState_;
    hasGamepad_ = false;
    if (joystick_) {
        joystick_->Poll();
        HRESULT joyHr = joystick_->GetDeviceState(sizeof(DIJOYSTATE2), &joyState_);
        if (FAILED(joyHr)) {
            // フォーカスが外れる等でデバイスをロストした場合は再取得を試みる
            joystick_->Acquire();
            joyHr = joystick_->GetDeviceState(sizeof(DIJOYSTATE2), &joyState_);
        }
        hasGamepad_ = SUCCEEDED(joyHr);
    }
}

void Input::Finalize()
{
    if (keyboard_) {
        keyboard_->Unacquire();
        keyboard_.Reset();
    }
    if (joystick_) {
        joystick_->Unacquire();
        joystick_.Reset();
    }
    directInput_.Reset();
}

bool Input::PushKey(BYTE keyNumber) const
{
    return (key_[keyNumber] & 0x80) != 0;
}

bool Input::ReleaseKey(BYTE keyNumber) const
{
    return (key_[keyNumber] & 0x80) == 0;
}

bool Input::TriggerKey(BYTE keyNumber) const
{
    return (key_[keyNumber] & 0x80) != 0 && (prevKey_[keyNumber] & 0x80) == 0;
}

bool Input::ReleaseTriggerKey(BYTE keyNumber) const
{
    return (key_[keyNumber] & 0x80) == 0 && (prevKey_[keyNumber] & 0x80) != 0;
}

bool Input::PushGamepadButton(int buttonIndex) const
{
    return hasGamepad_ && (joyState_.rgbButtons[buttonIndex] & 0x80) != 0;
}

bool Input::TriggerGamepadButton(int buttonIndex) const
{
    return hasGamepad_
        && (joyState_.rgbButtons[buttonIndex] & 0x80) != 0
        && (prevJoyState_.rgbButtons[buttonIndex] & 0x80) == 0;
}

bool Input::TriggerPov(DWORD directionCentidegrees) const
{
    if (!hasGamepad_) {
        return false;
    }
    return PovMatchesDirection(joyState_.rgdwPOV[0], directionCentidegrees)
        && !PovMatchesDirection(prevJoyState_.rgdwPOV[0], directionCentidegrees);
}
