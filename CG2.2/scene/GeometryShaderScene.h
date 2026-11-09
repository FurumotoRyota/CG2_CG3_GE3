#pragma once
#include <memory>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "scene/Scene.h"
#include "camera/Camera.h"
#include "camera/DebugCamera.h"
#include "3d/GeometryPipeline.h"
#include "base/TextureManager.h"

class Model;

/// <summary>
/// ジオメトリシェーダーのデモ（メニュー：シーン > ジオメトリシェーダー）
///  80_00 / 80_01：三角形を受け取って、爆発・縮小・回転などの加工をして出力する
///  80_02 / 80_03：点を受け取って、三角形 / テクスチャ付き四角形（ビルボード）を作る
/// </summary>
class GeometryShaderScene : public Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;
    void Finalize() override;

private:
    enum class Demo { ModelProcessing, PointsToShapes, Samples };

    void ScanResources();
    void BuildPoints();             // 点の頂点バッファを作り直す
    void BuildTestTriangle();       // 「三角形でのテスト」用の三角形1枚の頂点バッファ
    void UpdateCameraControl();     // 右クリックホールド=WASD / ホイールクリック=向き / ホイール=ズーム

    std::unique_ptr<Camera> camera_;
    DebugCamera* debugCamera_ = nullptr;
    bool cameraActive_ = false;
    bool middleActive_ = false;

    GeometryPipeline pipeline_;
    Microsoft::WRL::ComPtr<ID3D12Resource> paramsResource_;
    GeoParams* paramsData_ = nullptr;

    // 点の頂点バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> pointResource_;
    D3D12_VERTEX_BUFFER_VIEW pointView_{};
    int pointCount_ = 300;
    int builtPointCount_ = 0;

    // 三角形1枚（DrawInstanced(3,1,0,0)で描く）。Model の VertexData と同じ並び
    struct TestVertex
    {
        float position[4];
        float texcoord[2];
        float normal[3];
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> triangleResource_;
    D3D12_VERTEX_BUFFER_VIEW triangleView_{};

    Model* currentModel_ = nullptr;                                 // 表示中のモデル（Updateで読み込む）
    TextureManager::TextureHandle currentTexture_ = 0;              // 選択中のテクスチャ

    // 画面の状態
    Demo demo_ = Demo::ModelProcessing;
    float time_ = 0.0f;

    // モデル加工
    std::vector<std::string> modelNames_;
    std::vector<const char*> modelNamesCStr_;
    int modelIndex_ = 0;
    int processMode_ = 1;       // 0:そのまま 1:爆発 2:縮小 3:回転
    float amount_ = 0.5f;
    bool animate_ = true;
    bool autoRotate_ = true;
    float modelScale_ = 1.0f;
    float modelColor_[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    // 点から図形
    int shape_ = 1;             // 0:三角形 1:テクスチャ付き四角形
    float pointSize_ = 1.0f;
    float pointColor_[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    // スライドのサンプル（点・線・複製など）
    int sampleIndex_ = 5;           // GeometryPipeline のサンプル番号
    bool sampleOnModel_ = false;    // false:三角形1枚 true:モデル
    float sampleOffset_ = 1.5f;     // 複製をずらす距離
    float sampleUvScale_ = 2.0f;    // UVの倍率
    float sampleColor_[4] = { 1.0f, 0.9f, 0.2f, 1.0f };
    int sampleTextureIndex_ = 0;

    // テクスチャ
    std::vector<std::string> textureNames_;
    std::vector<const char*> textureNamesCStr_;
    int textureIndex_ = 0;
    bool useModelTexture_ = true; // モデル加工のとき、モデル自身のテクスチャを使う
};
