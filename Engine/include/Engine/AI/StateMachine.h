#ifndef AI_STATEMACHINE_H
#define AI_STATEMACHINE_H

#include <functional>
#include <unordered_map>
#include <type_traits>
#include <utility>

namespace Engine {

template<typename TContext>
class StateMachine {
public:
    struct State {
        std::function<void(TContext&)> OnEnter;
        std::function<void(TContext&, float dt)> OnUpdate;
        std::function<void(TContext&)> OnExit;
    };

    StateMachine() = default;
    ~StateMachine() = default;

    void RegisterState(int stateID, State state) {
        m_States[stateID] = std::move(state);
    }

    template<typename TEnum>
    requires std::is_enum_v<TEnum>
    void RegisterState(TEnum stateID, State state) {
        RegisterState(static_cast<int>(stateID), std::move(state));
    }

    void TransitionTo(int nextStateID, TContext& ctx) {
        if (m_CurrentState == nextStateID && m_HasInitialState) {
            return;
        }

        // Call OnExit on current state if active
        if (m_HasInitialState) {
            auto it = m_States.find(m_CurrentState);
            if (it != m_States.end() && it->second.OnExit) {
                it->second.OnExit(ctx);
            }
        }

        m_PreviousState = m_CurrentState;
        m_CurrentState = nextStateID;
        m_ElapsedInState = 0.0f;
        m_HasInitialState = true;

        // Call OnEnter on new state
        auto nextIt = m_States.find(nextStateID);
        if (nextIt != m_States.end() && nextIt->second.OnEnter) {
            nextIt->second.OnEnter(ctx);
        }
    }

    template<typename TEnum>
    requires std::is_enum_v<TEnum>
    void TransitionTo(TEnum nextStateID, TContext& ctx) {
        TransitionTo(static_cast<int>(nextStateID), ctx);
    }

    void SetInitialState(int stateID, TContext& ctx) {
        TransitionTo(stateID, ctx);
    }

    template<typename TEnum>
    requires std::is_enum_v<TEnum>
    void SetInitialState(TEnum stateID, TContext& ctx) {
        SetInitialState(static_cast<int>(stateID), ctx);
    }

    void Update(TContext& ctx, float dt) {
        if (!m_HasInitialState) {
            return;
        }

        m_ElapsedInState += dt;

        auto it = m_States.find(m_CurrentState);
        if (it != m_States.end() && it->second.OnUpdate) {
            it->second.OnUpdate(ctx, dt);
        }
    }

    [[nodiscard]] int GetCurrentState() const noexcept { return m_CurrentState; }
    [[nodiscard]] int GetPreviousState() const noexcept { return m_PreviousState; }
    [[nodiscard]] float GetStateElapsedTime() const noexcept { return m_ElapsedInState; }
    [[nodiscard]] bool HasState(int stateID) const noexcept { return m_States.contains(stateID); }
    [[nodiscard]] size_t GetRegisteredStateCount() const noexcept { return m_States.size(); }
    [[nodiscard]] bool HasActiveState() const noexcept { return m_HasInitialState; }

    template<typename TEnum>
    requires std::is_enum_v<TEnum>
    [[nodiscard]] bool IsInState(TEnum stateID) const noexcept {
        return m_HasInitialState && (m_CurrentState == static_cast<int>(stateID));
    }

private:
    std::unordered_map<int, State> m_States;
    int m_CurrentState = -1;
    int m_PreviousState = -1;
    float m_ElapsedInState = 0.0f;
    bool m_HasInitialState = false;
};

} // namespace Engine

#endif // AI_STATEMACHINE_H
