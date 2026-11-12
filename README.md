# Divergence Engine チーム開発用

DirectX12 の自作エンジンで、3人で1つのゲームを作るためのリポジトリです。

## フォルダ構成

| フォルダ | 中身 |
| --- | --- |
| `Engine/` | エンジン本体（描画・入力・サウンドなど）。起動演出 `Engine/startup/` もここ。基本は触らず、変更するときは相談する |
| `externals/` | 外部ライブラリ（DirectXTex・assimp・ImGui など） |
| `Game/` | ゲーム本体。みんなが作業する場所 |
| `Game/resources/` | 画像・モデル・音などの素材 |

## 初回セットアップ

`Engine/` は、別のリポジトリ（エンジン）をサブモジュールとして取り込んでいます。

1. 次のコマンドで取ってくる（`--recurse-submodules` をつけると、`Engine/` の中身も一緒に取れる）
   ```
   git clone --recurse-submodules <このリポジトリのURL>
   ```
   すでに `git clone` 済みで `Engine/` が空のときは、`git submodule update --init --recursive`
2. 1回だけ、次を実行する（`git pull` のときに、`Engine/` も一緒に更新されるようになる）
   ```
   git config --global submodule.recurse true
   ```
3. `CG2.2.slnx` を Visual Studio で開く
4. スタートアップ プロジェクトを **Game** にする
5. 構成は **Development | x64** を選んでビルド・実行（Debug は重いので、普段は使わない）

## エンジンの更新

- エンジンは別のリポジトリにあり、**毎日、自動で「Engine を新しい版にする」プルリクエストが作られます**（変更されたコミットの一覧つき）
- そのプルリクエストでも、ビルドが自動で動きます。ビルドが通っていたらマージして、各自 `git pull` します
- エンジンの変更内容は、`Engine/CHANGELOG.md` で読めます
- **`Engine/` の中は、エンジン担当以外は直接変更しない**（変更したいときは、エンジン担当に頼む）
- エンジン担当が変更するときは、先に `Engine/` の中でコミットして push してから、ゲーム側でコミットする（順番を逆にすると、他の人のビルドが壊れます）
  ```
  cd Engine
  git add -A
  git commit -m "変更内容"
  git push
  cd ..
  git add Engine
  git commit -m "Engine を更新"
  git push
  ```

## 起動演出

エンジンの起動演出（スタートアップ演出）は `Engine/startup/` にあり、どのゲームでも最初に流れます。
演出が終わったあとのシーンは `Game/Game.cpp` の `StartupScene::s_hooks.onFinish` で決めます。
演出の素材は `Engine/resources/startup/` にあり、ビルド時に `Game/Engine/resources/` へコピーされます。

## エンジンの便利機能（`Engine/` にあり、`#include` するだけで使えます）

| ファイル | 中身 |
| --- | --- |
| `math/Easing.h` | 動きに緩急をつける（`Easing::OutBack(t)` など） |
| `math/MathUtil.h` | `Clamp` / `Lerp` / `Remap` / `Damp`（なめらかな追従）/ 角度の計算 |
| `math/Random.h` | 乱数（`rng.Int(1, 6)` / `rng.Chance(0.3f)` など） |
| `math/Collision.h` | 当たり判定（円・矩形・球・箱・線・レイ） |
| `core/Timer.h` | `Timer`（○秒測る）と `Cooldown`（再使用までの待ち時間） |
| `core/StateMachine.h` | 状態遷移（待機・移動・攻撃の切り替え） |
| `core/SaveData.h` | ハイスコアや設定の保存・読み込み |
| `camera/CameraEffects.h` | 画面の揺れ・ヒットストップ |
| `camera/FreeCamera.h` | 自由に動かせるカメラ |
| `core/PauseMenu.h` | ポーズ画面 |
| `ui/UiButton.h` | ボタン（マウスで押せる） |
| `ui/UiGauge.h` | 体力ゲージ（ダメージで赤いバーが遅れて減る） |
| `ui/ScreenFade.h` | 画面の暗転・明転（シーン切り替え用） |
| `ui/UiToast.h` | 「SAVED」のような、少しだけ出て消えるお知らせ |
| `ui/UiRenderer.h` | UI部品を画面に描く係（四角と文字）。スプライトで描くので Release でも表示される |
| `2d/BitmapFont.h` | 自作した文字画像から文字を出す部品（フォントデータは読み込まない） |
| `debug/DebugDraw.h` | 線・箱・球・矢印などを、画面の上に描く（当たり判定の確認用。F1キーでON/OFF） |
| `debug/LogPanel.h` | ログを画面（システムパネルの「ログ」タブ）で見る |

### デバッグ描画の使い方

```cpp
// 毎フレーム、いま使っているカメラを渡す（3Dのとき）
DebugDraw::Instance().SetCamera(camera);
// 当たり判定の箱・球・向きを描く
DebugDraw::Instance().Box3(enemyMin, enemyMax, DebugDraw::Red);
DebugDraw::Instance().Sphere3(bulletPos, 0.3f, DebugDraw::Yellow);
DebugDraw::Instance().Ray3(playerPos, aimDir, 10.0f, DebugDraw::Cyan);
```

呼んだ分は、そのフレームだけ描かれます。F1キーで表示のON/OFFを切り替えられます。

### 文字の出し方（自作の文字画像を使う）

1. Aseprite などで、**白い文字・背景透明**の PNG を自作する
   - 同じ大きさのマスを並べる（例: 1マス 16x32、16列 x 6行 = 画像 256 x 192）
   - 左上から右へ、行ごとに下へ、`スペース ! " # ... ~` の95文字をこの順に並べる
   - 日本語を使うときは、並べた文字をそのまま文字列で渡す（`BitmapFont::Initialize` の最後の引数）
2. 画像を `Game/resources/font.png` に置く
3. シーンで `font_.Initialize(...)` と `ui_.Initialize(...)` を呼ぶ（使い方は `BitmapFont.h` / `UiRenderer.h` の先頭のコメント）

文字画像は、必ず自分たちで作ること（上の「素材のルール」を参照）。


使い方は、各ファイルの先頭のコメントに書いてあります。

## 素材のルール（授業のレギュレーション）

ゲームに入れる素材は、次のルールを守ります。

- AI生成画像（加工・トレースも含む）は使わない
- 課題で配布された素材は使わない（自作に差し替える）
- チーム外の人（知人・同級生）から素材をもらわない。音源以外は、チームメンバーが自作する
- フリー素材は音源だけ使える。画像・フォントデータは使えない（音源も利用規約を読む。音源ファイルがそのまま入っているとNGな規約もある）
- フリーフォントのデータをプログラムに読み込ませない。文字は、自作した画像（フォントで作った文字画像）を読み込む

素材を追加するときは、これに当てはまらないか確認してください。

## ブランチのルール

- `main` には直接 push しない。必ずプルリクエストで取り込む
- 作業は機能ごとに `feature/名前` のブランチを切って行う（例: `feature/player`）
- 作業を始める前に `git pull` で最新にしておく
- 西部劇シューティングは `western-shooter` ブランチに残してある（参考用）

## 衝突しやすいファイル

- `Game/Game.vcxproj` : ファイルを追加すると全員が書き換える。衝突したら「両方の行を残す」で解決する
- `Game/Game.cpp` : シーンのメニューを足す場所。足す行だけ変えて、他は触らない
- `Engine/` : 変更するときは、先にチームに連絡する

## ファイルを追加するとき

Visual Studio の「追加」→「新しい項目」から作ると、`Game.vcxproj` に自動で登録されます。
フォルダに直接置いただけでは、ビルドに含まれません。
