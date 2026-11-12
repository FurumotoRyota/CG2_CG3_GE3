# 更新履歴

エンジンを変更したら、ここに1行ずつ足していきます（新しい日付が上）。
書き方: 「追加」「変更」「修正」のどれかに分けて、何が変わったかを短く書く。

## 2026-11-12 チーム開発用に整理（Gameを空にして、機能をエンジンへ）

### 追加
- 起動演出「怜エンジン」を `startup/` に追加（どのゲームでも最初に流れる。終わったあとのシーンは `StartupScene::s_hooks.onFinish` でゲーム側が決める）
- 文字を自作の文字画像で出す `2d/BitmapFont.h`
- UI部品: `ui/UiRenderer.h`、`UiButton`、`UiGauge`（ダメージで遅れて減るバー）、`ScreenFade`（暗転・明転）、`UiToast`（お知らせ）
- ゲーム制作の道具: `math/Easing.h`、`math/MathUtil.h`、`math/Random.h`、`core/Timer.h`（Timer / Cooldown）、`core/StateMachine.h`、`core/SaveData.h`
- デバッグ機能: `debug/DebugDraw.h`（線・箱・球・矢印を画面に描く。F1でON/OFF）、`debug/LogPanel.h`（ログを画面で見る）
- `Logger::GetHistory()` / `Logger::ClearHistory()`（最近のログを覚える）
- `Framework::DrawOverlay()`（シーンの上に重ねて描く場所）
- `EditorLayout::DrawViewportRect()`（枠なしの四角）

### 変更
- `Framework::WindowTitle()` でウィンドウの題名をゲームごとに変えられる

### これまでにある機能（参考）
- DirectX12 描画（HDR・影・SSAO・ブルーム・トーンマップ）、シーン管理、入力（キーボード・ゲームパッド）、サウンド、パーティクル
- ポーズ画面 `core/PauseMenu.h`、自由カメラ `camera/FreeCamera.h`、画面の揺れ・ヒットストップ `camera/CameraEffects.h`、当たり判定 `math/Collision.h`
