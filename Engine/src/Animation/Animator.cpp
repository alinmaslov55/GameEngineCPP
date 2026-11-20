#include "Engine/Animation/Animator.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <glm/glm.hpp>

namespace Engine {

void Animator::AddClip(const std::shared_ptr<AnimationClip>& clip) {
    if (!clip) {
        ENGINE_CORE_WARN("Animator: Attempted to add null AnimationClip");
        return;
    }
    [[maybe_unused]] const size_t frameCount = !clip->Frames.empty() ? clip->Frames.size() : clip->frames.size();
    ENGINE_CORE_DEBUG("Animator: Added clip '{}' ({} frames, total duration: {:.3f}s, loop: {})",
                      clip->name, frameCount, clip->GetTotalDuration(), clip->loop);
    m_Clips[clip->name] = clip;
}

std::shared_ptr<AnimationClip> Animator::GetClip(std::string_view name) const {
    auto it = m_Clips.find(std::string(name));
    if (it != m_Clips.end()) {
        return it->second;
    }
    return nullptr;
}

void Animator::Play(const std::string& clipName) {
    if (m_CurrentClipName == clipName && m_CurrentClip) {
        return;
    }

    auto it = m_Clips.find(clipName);
    if (it == m_Clips.end()) {
        ENGINE_CORE_WARN("Animator: Clip '{}' not found", clipName);
        return;
    }

    ENGINE_CORE_TRACE("Animator: Playing clip '{}'", clipName);
    m_CurrentClipName = clipName;
    m_CurrentBaseState = clipName;
    m_CurrentClip = it->second;
    m_CurrentTime = 0.0f;
    m_CurrentFrameIndex = 0;
    m_IsOneShotPlaying = false;
    m_OneShotPriority = 0;
    m_Frame0Fired = false;

    if (auto* go = GetGameObject()) {
        if (auto* sr = go->GetComponent<SpriteRenderer>()) {
            if (sr->GetFlipX() && !m_CurrentFlipX) {
                m_CurrentFlipX = sr->GetFlipX();
            }
        }
    }

    UpdateSpriteRenderer();
}

void Animator::Stop() noexcept {
    ResetState();
}

void Animator::SetCurrentFrameIndex(uint32_t frameIndex) {
    ScrubToFrame(frameIndex, false);
}

void Animator::ScrubToFrame(uint32_t frameIndex, bool triggerEvents) {
    if (!m_CurrentClip) {
        return;
    }
    const size_t frameCount = !m_CurrentClip->Frames.empty()
        ? m_CurrentClip->Frames.size()
        : m_CurrentClip->frames.size();
    if (frameCount == 0) {
        return;
    }

    m_IsPreviewPlaying = false;
    m_CurrentFrameIndex = std::min(frameIndex, static_cast<uint32_t>(frameCount - 1));
    m_CurrentTime = 0.0f;

    if (triggerEvents) {
        TriggerFrameEvents(m_CurrentFrameIndex);
    }
    UpdateSpriteRenderer();
}

void Animator::SetCurrentFlipX(bool flipX) {
    m_CurrentFlipX = flipX;
    UpdateSpriteRenderer();
}

uint32_t Animator::AddEventListener(AnimEventCallback callback) {
    if (!callback) {
        return 0;
    }
    const uint32_t handle = m_NextListenerHandle++;
    m_EventListeners.emplace_back(handle, std::move(callback));
    return handle;
}

bool Animator::RemoveEventListener(uint32_t handle) {
    auto it = std::remove_if(m_EventListeners.begin(), m_EventListeners.end(),
        [handle](const auto& pair) { return pair.first == handle; });
    if (it != m_EventListeners.end()) {
        m_EventListeners.erase(it, m_EventListeners.end());
        return true;
    }
    return false;
}

void Animator::ClearEventListeners() noexcept {
    m_EventListeners.clear();
}

void Animator::TriggerFrameEvents(uint32_t frameIndex) {
    if (!m_CurrentClip || m_EventListeners.empty() || m_CurrentClip->Events.empty()) {
        return;
    }

    for (const auto& ev : m_CurrentClip->Events) {
        if (ev.FrameIndex == frameIndex) {
            DispatchEvent(ev);
        }
    }
}

void Animator::DispatchEvent(const AnimationEvent& ev) {
    if (m_EventListeners.empty()) {
        return;
    }

    // Take a snapshot copy of the listeners for re-entrancy safety
    auto snapshot = m_EventListeners;
    for (const auto& [handle, callback] : snapshot) {
        if (callback) {
            callback(ev.EventID, ev);
        }
    }
}

void Animator::ResetState() noexcept {
    m_CurrentClip = nullptr;
    m_CurrentClipName.clear();
    m_CurrentBaseState.clear();
    m_CurrentTime = 0.0f;
    m_CurrentFrameIndex = 0;
    m_IsOneShotPlaying = false;
    m_OneShotPriority = 0;
    m_OneShotFallbackState = "Idle";
    m_QueuedLoopState.clear();
    m_CurrentFlipX = false;
    m_Frame0Fired = false;
    m_IsPreviewPlaying = false;
}

void Animator::GetCameraBasis(glm::vec3& outRightXZ, glm::vec3& outForwardXZ) const {
    outRightXZ = glm::vec3(1.0f, 0.0f, 0.0f);
    outForwardXZ = glm::vec3(0.0f, 0.0f, -1.0f);

    const GameObject* go = GetGameObject();
    if (go) {
        if (const auto* scene = go->GetScene()) {
            const auto& cam = scene->GetActiveCamera();
            if (glm::dot(cam.GetRightXZ(), cam.GetRightXZ()) > 1e-4f && glm::dot(cam.GetForwardXZ(), cam.GetForwardXZ()) > 1e-4f) {
                outRightXZ = cam.GetRightXZ();
                outForwardXZ = cam.GetForwardXZ();
                return;
            }
        }
    }

    if (auto activeScene = SceneManager::GetActiveScene()) {
        const auto& cam = activeScene->GetActiveCamera();
        if (glm::dot(cam.GetRightXZ(), cam.GetRightXZ()) > 1e-4f && glm::dot(cam.GetForwardXZ(), cam.GetForwardXZ()) > 1e-4f) {
            outRightXZ = cam.GetRightXZ();
            outForwardXZ = cam.GetForwardXZ();
        }
    }
}

FacingDirection Animator::ResolveDirection(
    const glm::vec2& facingXZ,
    const glm::vec3& camRightXZ,
    const glm::vec3& camForwardXZ,
    FacingDirection currentDirection,
    float hysteresisDegrees,
    DirectionalScheme scheme,
    bool* outFlipX) {
    if (outFlipX) {
        *outFlipX = false;
    }

    if (scheme == DirectionalScheme::Omni_1Way) {
        return FacingDirection::None;
    }

    const float lenSq = facingXZ.x * facingXZ.x + facingXZ.y * facingXZ.y;
    if (lenSq < 1e-6f) {
        if (outFlipX && currentDirection == FacingDirection::Side) {
            // Keep previous flip state if available
        }
        return (currentDirection != FacingDirection::None) ? currentDirection : FacingDirection::Down;
    }

    const glm::vec3 worldDir = glm::normalize(glm::vec3(facingXZ.x, 0.0f, facingXZ.y));
    const glm::vec3 rAxis = (glm::dot(camRightXZ, camRightXZ) > 1e-4f) ? glm::normalize(camRightXZ) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 fAxis = (glm::dot(camForwardXZ, camForwardXZ) > 1e-4f) ? glm::normalize(camForwardXZ) : glm::vec3(0.0f, 0.0f, -1.0f);

    const float screenX = glm::dot(worldDir, rAxis);
    const float screenY = glm::dot(worldDir, fAxis);

    if (std::abs(screenX) < 1e-6f && std::abs(screenY) < 1e-6f) {
        return (currentDirection != FacingDirection::None) ? currentDirection : FacingDirection::Down;
    }

    float angleDeg = glm::degrees(std::atan2(screenY, screenX));
    if (angleDeg < 0.0f) {
        angleDeg += 360.0f;
    }

    const float h = std::clamp(hysteresisDegrees * 0.5f, 0.0f, 22.5f);

    if (scheme == DirectionalScheme::FrontBack_2Way) {
        FacingDirection resolved = currentDirection;
        if (currentDirection == FacingDirection::Front) {
            if (angleDeg > h && angleDeg < (180.0f - h)) {
                resolved = FacingDirection::Back;
            } else {
                resolved = FacingDirection::Front;
            }
        } else if (currentDirection == FacingDirection::Back) {
            if (angleDeg > (180.0f + h) && angleDeg < (360.0f - h)) {
                resolved = FacingDirection::Front;
            } else {
                resolved = FacingDirection::Back;
            }
        } else {
            resolved = (angleDeg >= 0.0f && angleDeg < 180.0f) ? FacingDirection::Back : FacingDirection::Front;
        }

        if (outFlipX) {
            *outFlipX = (screenX < 0.0f);
        }
        return resolved;
    }

    // Cardinal_4Way
    FacingDirection resolved = FacingDirection::None;

    if (currentDirection == FacingDirection::Down) {
        if (angleDeg >= (225.0f - h) && angleDeg <= (315.0f + h)) {
            resolved = FacingDirection::Down;
        }
    } else if (currentDirection == FacingDirection::Up) {
        if (angleDeg >= (45.0f - h) && angleDeg <= (135.0f + h)) {
            resolved = FacingDirection::Up;
        }
    } else if (currentDirection == FacingDirection::Side) {
        const bool inEast = (angleDeg >= (315.0f - h)) || (angleDeg <= (45.0f + h));
        const bool inWest = (angleDeg >= (135.0f - h)) && (angleDeg <= (225.0f + h));
        if (inEast || inWest) {
            resolved = FacingDirection::Side;
        }
    }

    if (resolved == FacingDirection::None) {
        if (angleDeg >= 45.0f && angleDeg < 135.0f) {
            resolved = FacingDirection::Up;
        } else if (angleDeg >= 135.0f && angleDeg < 225.0f) {
            resolved = FacingDirection::Side;
        } else if (angleDeg >= 225.0f && angleDeg < 315.0f) {
            resolved = FacingDirection::Down;
        } else {
            resolved = FacingDirection::Side;
        }
    }

    if (outFlipX) {
        if (resolved == FacingDirection::Side) {
            *outFlipX = (screenX < 0.0f);
        } else {
            *outFlipX = false;
        }
    }

    return resolved;
}

std::string Animator::ResolveClipName(std::string_view stateName, FacingDirection dir) const {
    const std::string base(stateName);

    std::vector<std::string> candidates;
    if (dir == FacingDirection::Down) {
        candidates = {
            base + "_Down", base + "_down", base + "_d",
            base + "_Front", base + "_front",
            base + "_South", base + "_south"
        };
    } else if (dir == FacingDirection::Up) {
        candidates = {
            base + "_Up", base + "_up", base + "_u",
            base + "_Back", base + "_back",
            base + "_North", base + "_north"
        };
    } else if (dir == FacingDirection::Side) {
        candidates = {
            base + "_Side", base + "_side", base + "_s",
            base + "_Right", base + "_right",
            base + "_East", base + "_east"
        };
    } else if (dir == FacingDirection::Diag) {
        candidates = {
            base + "_Diag", base + "_diag",
            base + "_Diagonal", base + "_diagonal"
        };
    }

    for (const auto& cand : candidates) {
        if (m_Clips.find(cand) != m_Clips.end()) {
            return cand;
        }
    }

    if (dir == FacingDirection::Diag) {
        const std::string sideFallbacks[] = {
            base + "_Side", base + "_side", base + "_s",
            base + "_Down", base + "_down", base + "_d"
        };
        for (const auto& cand : sideFallbacks) {
            if (m_Clips.find(cand) != m_Clips.end()) {
                return cand;
            }
        }
    }

    if (m_Clips.find(base) != m_Clips.end()) {
        return base;
    }

    for (const auto& [name, clip] : m_Clips) {
        if (name.size() == base.size()) {
            bool match = true;
            for (size_t i = 0; i < base.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(name[i])) !=
                    std::tolower(static_cast<unsigned char>(base[i]))) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return name;
            }
        }
    }

    return "";
}

void Animator::PlayState(std::string_view stateName, const glm::vec2& facingXZ, bool forceRestart) {
    if (m_IsOneShotPlaying && !forceRestart) {
        m_QueuedLoopState = std::string(stateName);
        m_LastFacingXZ = facingXZ;
        return;
    }

    m_CurrentBaseState = std::string(stateName);
    m_LastFacingXZ = facingXZ;

    glm::vec3 camRight{ 1.0f, 0.0f, 0.0f };
    glm::vec3 camForward{ 0.0f, 0.0f, -1.0f };
    GetCameraBasis(camRight, camForward);

    bool flipX = false;
    const FacingDirection newDir = ResolveDirection(facingXZ, camRight, camForward, m_CurrentFacingDirection,
                                                    m_HysteresisDegrees, m_DirectionalScheme, &flipX);
    m_CurrentFacingDirection = newDir;
    m_CurrentFlipX = flipX;

    const std::string resolvedClipName = ResolveClipName(stateName, newDir);
    if (resolvedClipName.empty()) {
        ENGINE_CORE_WARN("Animator: No matching clip found for state '{}' and direction {}",
                         stateName, static_cast<int>(newDir));
        return;
    }

    if (resolvedClipName != m_CurrentClipName || forceRestart || !m_CurrentClip) {
        auto it = m_Clips.find(resolvedClipName);
        if (it != m_Clips.end()) {
            m_CurrentClip = it->second;
            m_CurrentClipName = resolvedClipName;
            m_CurrentTime = 0.0f;
            m_CurrentFrameIndex = 0;
            m_IsOneShotPlaying = false;
            m_OneShotPriority = 0;
            m_Frame0Fired = false;
        }
    }

    UpdateSpriteRenderer();
}

void Animator::PlayOneShot(std::string_view stateName, const glm::vec2& facingXZ,
                           std::string_view fallbackLoopState, int priority) {
    if (m_IsOneShotPlaying && priority < m_OneShotPriority) {
        return;
    }

    m_IsOneShotPlaying = true;
    m_OneShotPriority = priority;
    m_OneShotFallbackState = std::string(fallbackLoopState);
    m_LastFacingXZ = facingXZ;

    glm::vec3 camRight{ 1.0f, 0.0f, 0.0f };
    glm::vec3 camForward{ 0.0f, 0.0f, -1.0f };
    GetCameraBasis(camRight, camForward);

    bool flipX = false;
    const FacingDirection newDir = ResolveDirection(facingXZ, camRight, camForward, m_CurrentFacingDirection,
                                                    m_HysteresisDegrees, m_DirectionalScheme, &flipX);
    m_CurrentFacingDirection = newDir;
    m_CurrentFlipX = flipX;

    const std::string resolvedClipName = ResolveClipName(stateName, newDir);
    if (resolvedClipName.empty()) {
        m_IsOneShotPlaying = false;
        m_OneShotPriority = 0;
        ENGINE_CORE_WARN("Animator: OneShot clip not found for state '{}'", stateName);
        return;
    }

    auto it = m_Clips.find(resolvedClipName);
    if (it != m_Clips.end()) {
        m_CurrentClip = it->second;
        m_CurrentClipName = resolvedClipName;
        m_CurrentTime = 0.0f;
        m_CurrentFrameIndex = 0;
        m_Frame0Fired = false;
    } else {
        m_IsOneShotPlaying = false;
        m_OneShotPriority = 0;
    }

    UpdateSpriteRenderer();
}

void Animator::Update(float deltaTime) {
    if (!m_CurrentClip || deltaTime <= 0.0f || m_PlaybackSpeed <= 0.0f) {
        return;
    }

    const size_t frameCount = !m_CurrentClip->Frames.empty()
        ? m_CurrentClip->Frames.size()
        : m_CurrentClip->frames.size();
    if (frameCount == 0) {
        return;
    }

    // Trigger Frame 0 events on the very first update of this clip/cycle
    if (!m_Frame0Fired && m_CurrentFrameIndex == 0) {
        m_Frame0Fired = true;
        const auto clipBefore = m_CurrentClip;
        TriggerFrameEvents(0);
        if (!m_CurrentClip || m_CurrentClip != clipBefore) {
            UpdateSpriteRenderer();
            return;
        }
    }

    const float effectiveDelta = deltaTime * m_PlaybackSpeed;
    m_CurrentTime += effectiveDelta;

    auto getFrameDuration = [&](uint32_t idx) -> float {
        if (!m_CurrentClip->Frames.empty() && idx < m_CurrentClip->Frames.size()) {
            return std::max(1e-4f, m_CurrentClip->Frames[idx].DurationSeconds);
        }
        return std::max(1e-4f, m_CurrentClip->frameDuration);
    };

    float currentDuration = getFrameDuration(m_CurrentFrameIndex);
    bool changed = false;

    while (m_CurrentTime >= currentDuration) {
        m_CurrentTime -= currentDuration;
        m_CurrentFrameIndex++;
        changed = true;

        if (m_CurrentFrameIndex >= frameCount) {
            if (m_IsOneShotPlaying) {
                m_IsOneShotPlaying = false;
                m_OneShotPriority = 0;
                m_CurrentFrameIndex = static_cast<uint32_t>(frameCount - 1);
                m_CurrentTime = 0.0f;
                m_Frame0Fired = false;

                std::string returnState = m_QueuedLoopState;
                m_QueuedLoopState.clear();

                glm::vec3 camRight{ 1.0f, 0.0f, 0.0f };
                glm::vec3 camForward{ 0.0f, 0.0f, -1.0f };
                GetCameraBasis(camRight, camForward);
                bool flipX = false;
                const FacingDirection dir = ResolveDirection(m_LastFacingXZ, camRight, camForward,
                                                             m_CurrentFacingDirection, m_HysteresisDegrees,
                                                             m_DirectionalScheme, &flipX);

                if (returnState.empty() || ResolveClipName(returnState, dir).empty()) {
                    returnState = m_OneShotFallbackState;
                }

                if (!returnState.empty()) {
                    PlayState(returnState, m_LastFacingXZ, /*forceRestart=*/true);
                    return;
                }
                break;
            } else if (m_CurrentClip->loop) {
                m_CurrentFrameIndex = 0;
                const auto clipBefore = m_CurrentClip;
                TriggerFrameEvents(0);
                if (!m_CurrentClip || m_CurrentClip != clipBefore) {
                    UpdateSpriteRenderer();
                    return;
                }
            } else {
                m_CurrentFrameIndex = static_cast<uint32_t>(frameCount - 1);
                m_CurrentTime = 0.0f;
                break;
            }
        } else {
            const auto clipBefore = m_CurrentClip;
            TriggerFrameEvents(m_CurrentFrameIndex);
            if (!m_CurrentClip || m_CurrentClip != clipBefore) {
                UpdateSpriteRenderer();
                return;
            }
        }
        currentDuration = getFrameDuration(m_CurrentFrameIndex);
    }

    if (changed) {
        UpdateSpriteRenderer();
    }
}

void Animator::UpdateSpriteRenderer() {
    if (!m_CurrentClip || !GetGameObject()) {
        return;
    }

    auto spriteRenderer = GetGameObject()->GetComponent<SpriteRenderer>();
    if (!spriteRenderer) {
        return;
    }

    if (!m_CurrentClip->Frames.empty() && m_CurrentFrameIndex < m_CurrentClip->Frames.size()) {
        spriteRenderer->SetSubTexture(m_CurrentClip->Frames[m_CurrentFrameIndex].SubTex);
    } else if (!m_CurrentClip->frames.empty() && m_CurrentFrameIndex < m_CurrentClip->frames.size()) {
        spriteRenderer->SetSubTexture(m_CurrentClip->frames[m_CurrentFrameIndex]);
    }

    spriteRenderer->SetFlipX(m_CurrentFlipX);
}

} // namespace Engine
