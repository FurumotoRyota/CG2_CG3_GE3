#pragma once
#include <functional>
#include <string>
#include <vector>

class DirectXCommon;

/// <summary>
/// 読み込みを「段階」に分けて、1フレームに1段階ずつ進める仕組み（NOW LOADING 画面用）。
/// 重い読み込みを一気にやると画面が固まるので、小分けにして、その間に演出やロード表示を流せるようにする。
///
/// 使い方:
///   LoadQueue queue;
///   queue.Add("モデル", [&] { models->Load("a.obj"); });        // 段階を登録（登録順に実行）
///   queue.Add("テクスチャ", [&] { textures->Load("b.png"); });
///   // 毎フレーム（ロード画面の Update）:
///   if (!queue.IsDone()) { queue.Tick(dxCommon); }              // 1段階進める。最初の数フレームは絵を出すのを優先して待つ
///   // ロード画面の DrawImGui:
///   if (!queue.IsDone()) { queue.DrawNowLoading(screenW, screenH); }   // 右下に「NOW LOADING... 42%」
///   // 起動時に一気に読むなら:  queue.RunAll(dxCommon);
/// 各段階のあとに dxCommon->ExecuteAndWait() を呼ぶので、段階の中でテクスチャなどをアップロードしてよい。
/// </summary>
class LoadQueue
{
public:
    using Step = std::function<void()>;

    int waitFrames = 3; // 最初の何フレームかは読み込まずに待つ（ロード画面の絵を先に出すため）

    void Add(const std::string& label, Step step);
    void Clear();

    // 1フレームに1回呼ぶ。1段階進めて、全部終わったら true。dxCommon を渡すと、各段階のあとにアップロードを確定させる
    bool Tick(DirectXCommon* dxCommon = nullptr);
    // 待たずに全部実行する
    void RunAll(DirectXCommon* dxCommon = nullptr);

    bool IsEmpty() const { return steps_.empty(); }
    bool IsDone() const { return !steps_.empty() && next_ >= steps_.size(); } // 段階を登録する前は false
    float Progress() const; // 0〜1
    const std::string& CurrentLabel() const; // いま読み込んでいる（次にやる）段階の名前

    // 画面の右下に「NOW LOADING...」と進み具合を出す（ImGui使用時のみ。ゲーム画面の座標で指定）
    void DrawNowLoading(float screenW, float screenH, unsigned int color = 0xFF28283Cu);

private:
    struct Item
    {
        std::string label;
        Step step;
    };
    std::vector<Item> steps_;
    size_t next_ = 0;
    int waited_ = 0;
    int frames_ = 0; // ドットの動き用
};
