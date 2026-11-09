#pragma once
// 【見本A】ゲーム用クラスを別に作り、エンジンが呼ぶ形（設計案：まだエンジンには入っていない）
//
//  エンジンとゲームのやり取りは、この3つだけに絞る。
//    IGameLogic::Initialize(api)  … 再生を始めたとき1回
//    IGameLogic::Update(api)      … 再生中、毎フレーム1回
//    IGameLogic::Shutdown(api)    … 再生を止めたとき1回
//  ゲームのコードは GameApi（エンジンが渡す道具箱）だけを触る。
//
//  安全のための決まり:
//    ・オブジェクトを増やす・消す関数は、その場では何もせず「予約」する。エンジンが次のフレームの頭で実行する
//      （描画中に消してクラッシュする、をゲーム側が気にしなくてよい）
//    ・ゲームの状態（体力・スコアなど）は、ゲームのクラスだけが持つ。エンジンは持たない
#include <vector>

#include "input/GameInput.h"
#include "camera/CameraEffects.h"
#include "math/Vector2.h"

// エディタで置いたオブジェクト（または Spawn で出したもの）を指す番号札。消えたあとに使っても落ちない
struct ObjectRef
{
    int id = -1;
    bool operator==(const ObjectRef& o) const { return id == o.id; }
};

class GameApi
{
public:
    // ---- オブジェクト ----
    ObjectRef Find(const char* name) const;                       // 名前で1つ探す（なければ id=-1）
    std::vector<ObjectRef> FindAll(const char* name) const;       // 同じ名前をすべて
    ObjectRef Spawn(const char* name, const Vector2& pos);        // エディタで置いた「name」を複製して出す（次のフレームから見える）
    void Destroy(ObjectRef obj);                                  // 消す（予約。次のフレームの頭で消える）
    bool IsAlive(ObjectRef obj) const;
    Vector2 GetPosition(ObjectRef obj) const;                     // 地面の上の位置（x, z）
    void SetPosition(ObjectRef obj, const Vector2& pos);
    float GetRadius(ObjectRef obj) const;                         // 見た目の大きさから決めた判定用の半径

    // ---- 入力・時間・演出・判定 ----
    const GameInput& Input() const;                               // Move / Aim / Fire / Jump ...
    float Dt() const;                                             // 1フレームの進み（ヒットストップ中は0）
    CameraEffects& Fx();                                          // Fx().Shake(大きさ, 秒) / Fx().HitStop(フレーム)
    bool WallAt(int cellX, int cellZ) const;                      // マップの壁
    bool RaycastWalls(const Vector2& from, const Vector2& to, Vector2* hit = nullptr) const;
    void FollowCamera(ObjectRef target, float height);            // 真上から追うカメラ
};

class IGameLogic
{
public:
    virtual ~IGameLogic() = default;
    virtual void Initialize(GameApi& api) = 0;
    virtual void Update(GameApi& api) = 0;
    virtual void Shutdown(GameApi& api) {}
};
