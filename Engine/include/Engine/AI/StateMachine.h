#ifndef AI_STATEMACHINE_H
#define AI_STATEMACHINE_H

#include <cmath>
#include <deque>
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
        if (!m_States.contains(nextStateID)) {
            return;
        }

        m_PendingTransitions.push_back(nextStateID);
        if (m_IsTransitioning) {
            return;
        }

        struct ScopedTransitionGuard {
            bool& flag;
            std::deque<int>& queue;
            ~ScopedTransitionGuard() noexcept {
                flag = false;
                queue.clear();
            }
        };

        m_IsTransitioning = true;
        ScopedTransitionGuard guard{ m_IsTransitioning, m_PendingTransitions };

        constexpr int kMaxChainedTransitions = 64;
        int steps = 0;
        while (!m_PendingTransitions.empty() && steps < kMaxChainedTransitions) {
            const int targetStateID = m_PendingTransitions.front();
            m_PendingTransitions.pop_front();
            ++steps;

            if (m_HasInitialState && m_CurrentState == targetStateID) {
                continue;
            }
            auto nextIt = m_States.find(targetStateID);
            if (nextIt == m_States.end()) {
                continue;
            }

            if (m_HasInitialState) {
                auto curIt = m_States.find(m_CurrentState);
                if (curIt != m_States.end() && curIt->second.OnExit) {
                    curIt->second.OnExit(ctx);
                }
            }

            m_PreviousState = m_CurrentState;
            m_CurrentState = targetStateID;
            m_ElapsedInState = 0.0f;
            m_HasInitialState = true;

            if (nextIt->second.OnEnter) {
                nextIt->second.OnEnter(ctx);
            }
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
        const float safeDt = (std::isfinite(dt) && dt >= 0.0f) ? dt : 0.0f;
        m_ElapsedInState += safeDt;

        auto it = m_States.find(m_CurrentState);
        if (it != m_States.end() && it->second.OnUpdate) {
            it->second.OnUpdate(ctx, safeDt);
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
    std::deque<int> m_PendingTransitions;
    int m_CurrentState = -1;
    int m_PreviousState = -1;
    float m_ElapsedInState = 0.0f;
    bool m_HasInitialState = false;
    bool m_IsTransitioning = false;
};

} // namespace Engine

#endif // AI_STATEMACHINE_H
