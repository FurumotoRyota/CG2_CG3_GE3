#pragma once
#include "scene/Scene.h"
#include "editor/EditorLayout.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

/// <summary>
/// Feature list / change log scene (debug menu: Scene > 更新履歴).
/// To add a new entry, put a new block at the TOP of kEntries below.
/// </summary>
class ChangelogScene : public Scene
{
public:
    void Initialize() override {}
    void Update() override {}
    void Draw() override {}

    void DrawImGui() override
    {
#ifdef USE_IMGUI
        EditorLayout::Begin("Hierarchy", EditorLayout::Panel::Hierarchy);
        ImGui::Text("シーン: 更新履歴");
        ImGui::Separator();
        ImGui::TextWrapped("現在使える機能の一覧です。新しい機能を足したら下のリストの先頭に追記していきます。");
        ImGui::End();

        EditorLayout::Begin("Inspector", EditorLayout::Panel::Inspector);
        for (size_t i = 0; i < sizeof(kEntries) / sizeof(kEntries[0]); ++i) {
            const Entry& e = kEntries[i];
            if (ImGui::CollapsingHeader(e.title, i == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(e.body);
                ImGui::PopTextWrapPos();
            }
        }
        ImGui::End();
#endif // USE_IMGUI
    }

private:
    struct Entry
    {
        const char* title;
        const char* body;
    };

    // Newest first
    static constexpr Entry kEntries[] = {
        { "最新: ライティング強化",
          "- 鏡面反射: Phong / Blinn-Phong (オブジェクトのインスペクター。光沢で鋭さを調整)\n"
          "- 非均一スケールでも法線が正しく向く (法線を逆転置行列で変換)\n"
          "- ポイントライト / スポットライト (ライトを選択して「強さ」を上げると点灯。距離の減衰・円錐の角度を調整可)" },
        { "最新: ドラッグ&ドロップ / 動画",
          "- エクスプローラーからドロップ: 画像(スプライト) / .obj(+.mtl・テクスチャ) / 動画(.mp4 .m4v .mov .wmv)\n"
          "- 動画は元の場所から再生 (コピーしない)。音つき。インスペクターで再生/一時停止/最初から/ループ/音量\n"
          "- 動画: ドロップ=3Dの板(カメラで見え方が変わる) / Shift+ドロップ=2Dスプライト\n"
          "- 全オブジェクト・スプライトの位置マーカー (画面外は端に矢印)" },
        { "最新: カメラ操作の整理",
          "- ホイールクリック+ドラッグ: カメラの向きを変える\n"
          "- 右ボタン長押し: WASD/Space/Shiftで移動 (向きは変わらない)\n"
          "- ホイール回転: ズーム\n"
          "- 更新履歴シーンを追加" },
        { "エディタ・UI",
          "- UIの日本語化 (ヒエラルキー / インスペクター / ビューポート)\n"
          "- ビューポートでのオブジェクト選択・ギズモ操作\n"
          "- シーン切り替え (メニュー: Scene)\n"
          "- FPS表示 (固定60FPS)" },
        { "パーティクル",
          "- エミッター: 炎 / 爆発 / 花火 / 雪 / キラキラ / 虹色\n"
          "- 発生方式: 連続 / バースト\n"
          "- 形状: ぼかした丸 / 丸 / 四角\n"
          "- ブレンド: なし / 通常 / 加算 / 減算 / 乗算 / スクリーン\n"
          "- テクスチャ指定 (複数テクスチャ対応)\n"
          "- 花火の火花テクスチャを別に指定可能" },
        { "描画",
          "- OBJ読み込み / マルチメッシュ / マテリアル\n"
          "- ライティング切り替え\n"
          "- スプライト描画 (矩形切り出し対応)" },
        { "サウンド・入力",
          "- WAV / MP3 (Media Foundation) 読み込み、BGMループ再生\n"
          "- Mキー / ゲームパッドBボタンでBGM ON/OFF\n"
          "- キーボード + ゲームパッド入力" },
        { "エンジン構成",
          "- EngineLib (静的ライブラリ) をCG2.2とGameの2つのexeで共有\n"
          "- main.cppは最小限 / 出力・中間ファイルは build フォルダ\n"
          "- ReleaseではImGuiを除外 / GitHub ActionsでDebug・Development・Releaseをビルド" },
    };
};
