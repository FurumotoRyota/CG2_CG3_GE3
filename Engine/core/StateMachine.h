#pragma once
// 状態遷移（ステートマシン）。「待機」「移動」「攻撃」など、状態ごとの動きを切り替える（ヘッダーだけで使える）
//
//   enum class EnemyState { Idle, Chase, Attack };
//   StateMachine<EnemyState> fsm_;
//
//   // 初期化で、状態ごとに「入ったとき・毎フレーム・出るとき」の処理を登録する
//   fsm_.Add(EnemyState::Idle, {
//       [this] { /* 入ったとき */ },
//       [this](float dt) { if (PlayerIsNear()) fsm_.Change(EnemyState::Chase); },
//       nullptr });                                      // 使わない処理は nullptr でよい
//   fsm_.Change(EnemyState::Idle);                       // 最初の状態
//
//   // 毎フレーム
//   fsm_.Update(dt);
//
//   fsm_.Current()     : いまの状態
//   fsm_.TimeInState() : いまの状態に入ってから何秒か
#include <functional>
#include <unordered_map>

template <typename StateId>
class StateMachine
{
public:
    struct State
    {
        std::function<void()> onEnter;        // 状態に入ったとき1回
        std::function<void(float)> onUpdate;  // 毎フレーム（引数は1フレームの秒数）
        std::function<void()> onExit;         // 状態から出るとき1回
    };

    // 状態を登録する
    void Add(StateId id, State state) { states_[id] = std::move(state); }

    // 状態を切り替える（いまの状態の onExit → 新しい状態の onEnter の順に呼ばれる）
    // 登録されていない状態へは切り替わらない。同じ状態への切り替えは何もしない
    void Change(StateId id)
    {
        if (states_.find(id) == states_.end()) {
            return;
        }
        if (hasState_) {
            if (current_ == id) {
                return;
            }
            const State& oldState = states_[current_];
            if (oldState.onExit) {
                oldState.onExit();
            }
        }
        previous_ = current_;
        current_ = id;
        hasState_ = true;
        time_ = 0.0f;
        const State& newState = states_[current_];
        if (newState.onEnter) {
            newState.onEnter();
        }
    }

    // 毎フレーム1回
    void Update(float dt)
    {
        if (!hasState_) {
            return;
        }
        time_ += dt;
        const StateId before = current_;
        const State& state = states_[current_];
        if (state.onUpdate) {
            state.onUpdate(dt);
        }
        (void)before;
    }

    bool HasState() const { return hasState_; }
    StateId Current() const { return current_; }
    StateId Previous() const { return previous_; }
    bool Is(StateId id) const { return hasState_ && current_ == id; }
    float TimeInState() const { return time_; }

private:
    std::unordered_map<StateId, State> states_;
    StateId current_{};
    StateId previous_{};
    bool hasState_ = false;
    float time_ = 0.0f;
};
