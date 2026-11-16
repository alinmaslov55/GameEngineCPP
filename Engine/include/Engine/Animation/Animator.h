#ifndef ANIMATION_ANIMATOR_H
#define ANIMATION_ANIMATOR_H

#include "Engine/Scene/Component.h"
#include "Engine/Animation/AnimationClip.h"
#include <algorithm>
#include <functional>
#include <vector>
#include <utility>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <unordered_map>
#include <string>
#include <string_view>
#include <memory>

namespace Engine {

class SpriteRenderer;

class Animator final : public Component {
public:
    using AnimEventCallback = std::function<void(std::string_view eventID, const AnimationEvent& ev)>;

    Animator() = default;
    ~Animator() override = default;

    void AddClip(const std::shared_ptr<AnimationClip>& clip);
    [[nodiscard]] const std::unordered_map<std::string, std::shared_ptr<AnimationClip>>& GetClips() const noexcept { return m_Clips; }
    [[nodiscard]] std::shared_ptr<AnimationClip> GetClip(std::string_view name) const;

    // Direct playback (backward compatible)
    void Play(const std::string& clipName);
    void Stop() noexcept;

    // Directional configuration & speed scaling
    void SetDirectionalScheme(DirectionalScheme scheme) noexcept { m_DirectionalScheme = scheme; }
    [[nodiscard]] DirectionalScheme GetDirectionalScheme() const noexcept { return m_DirectionalScheme; }

    void SetPlaybackSpeed(float speedMultiplier) noexcept { m_PlaybackSpeed = std::max(0.0f, speedMultiplier); }
    [[nodiscard]] float GetPlaybackSpeed() const noexcept { return m_PlaybackSpeed; }

    void SetHysteresisDegrees(float degrees) noexcept { m_HysteresisDegrees = std::clamp(degrees, 0.0f, 45.0f); }
    [[nodiscard]] float GetHysteresisDegrees() const noexcept { return m_HysteresisDegrees; }

    // Multi-directional state machine
    void PlayState(std::string_view stateName, const glm::vec2& facingXZ, bool forceRestart = false);
    void PlayOneShot(std::string_view stateName, const glm::vec2& facingXZ,
                     std::string_view fallbackLoopState = "Idle", int priority = 10);
    [[nodiscard]] bool IsOneShotPlaying() const noexcept { return m_IsOneShotPlaying; }
    [[nodiscard]] int GetCurrentOneShotPriority() const noexcept { return m_IsOneShotPlaying ? m_OneShotPriority : 0; }

    [[nodiscard]] const std::string& GetCurrentStateName() const noexcept { return m_CurrentBaseState; }
    [[nodiscard]] const std::string& GetCurrentClipName() const noexcept { return m_CurrentClipName; }
    [[nodiscard]] FacingDirection GetCurrentFacingDirection() const noexcept { return m_CurrentFacingDirection; }
    void SetCurrentFacingDirection(FacingDirection dir) noexcept { m_CurrentFacingDirection = dir; }
    [[nodiscard]] uint32_t GetCurrentFrameIndex() const noexcept { return m_CurrentFrameIndex; }
    [[nodiscard]] float GetCurrentTime() const noexcept { return m_CurrentTime; }
    [[nodiscard]] bool GetCurrentFlipX() const noexcept { return m_CurrentFlipX; }
    void SetCurrentFlipX(bool flipX) noexcept { m_CurrentFlipX = flipX; }

    // Animation event listener API
    uint32_t AddEventListener(AnimEventCallback callback);
    bool RemoveEventListener(uint32_t handle);
    void ClearEventListeners() noexcept;
    [[nodiscard]] size_t GetEventListenerCount() const noexcept { return m_EventListeners.size(); }

    // Returns local socket offset mirrored horizontally if the standee is flipped
    [[nodiscard]] glm::vec2 GetEventSocketOffset(const AnimationEvent& ev) const noexcept {
        return m_CurrentFlipX ? glm::vec2(-ev.SocketOffset.x, ev.SocketOffset.y) : ev.SocketOffset;
    }

    void OnPoolAcquire() override { ResetState(); }
    void OnPoolRelease() override { ResetState(); }
    void Update(float deltaTime) override;

    // Static direction resolution helper with angular hysteresis
    static FacingDirection ResolveDirection(
        const glm::vec2& facingXZ,
        const glm::vec3& camRightXZ,
        const glm::vec3& camForwardXZ,
        FacingDirection currentDirection,
        float hysteresisDegrees = 7.0f,
        DirectionalScheme scheme = DirectionalScheme::Cardinal_4Way,
        bool* outFlipX = nullptr);

private:
    void ResetState() noexcept;
    void UpdateSpriteRenderer();
    std::string ResolveClipName(std::string_view stateName, FacingDirection dir) const;
    void GetCameraBasis(glm::vec3& outRightXZ, glm::vec3& outForwardXZ) const;
    void TriggerFrameEvents(uint32_t frameIndex);
    void DispatchEvent(const AnimationEvent& ev);

private:
    std::unordered_map<std::string, std::shared_ptr<AnimationClip>> m_Clips;
    std::shared_ptr<AnimationClip> m_CurrentClip = nullptr;
    std::string m_CurrentClipName = "";
    std::string m_CurrentBaseState = "";

    float m_CurrentTime = 0.0f;
    uint32_t m_CurrentFrameIndex = 0;

    DirectionalScheme m_DirectionalScheme = DirectionalScheme::Cardinal_4Way;
    FacingDirection m_CurrentFacingDirection = FacingDirection::Down;
    glm::vec2 m_LastFacingXZ{ 0.0f, 1.0f };
    float m_HysteresisDegrees = 7.0f;
    float m_PlaybackSpeed = 1.0f;
    bool m_CurrentFlipX = false;

    // One-Shot Attack & Stun State
    bool m_IsOneShotPlaying = false;
    int m_OneShotPriority = 0;
    std::string m_OneShotFallbackState = "Idle";
    std::string m_QueuedLoopState = "";

    // Event listener callbacks & frame tracking
    std::vector<std::pair<uint32_t, AnimEventCallback>> m_EventListeners;
    uint32_t m_NextListenerHandle = 1;
    bool m_Frame0Fired = false;
};

} // namespace Engine

#endif // ANIMATION_ANIMATOR_H
