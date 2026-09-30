#pragma once

class WinApp;
class DirectXCommon;
class Input;
class Audio;
class SrvManager;
class TextureManager;
class ModelManager;
class Object3dCommon;
class SpriteCommon;
class ParticleSystem;
class SceneManager;

/// <summary>
/// シーンが使うエンジン機能への参照をまとめたもの（Frameworkが所有・作成する）
/// </summary>
struct SceneContext
{
    WinApp* winApp = nullptr;
    DirectXCommon* dxCommon = nullptr;
    Input* input = nullptr;
    Audio* audio = nullptr;
    SrvManager* srvManager = nullptr;
    TextureManager* textureManager = nullptr;
    ModelManager* modelManager = nullptr;
    Object3dCommon* object3dCommon = nullptr;
    SpriteCommon* spriteCommon = nullptr;
    ParticleSystem* particleSystem = nullptr;
    SceneManager* sceneManager = nullptr;
};
