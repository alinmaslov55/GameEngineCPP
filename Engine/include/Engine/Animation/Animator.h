#ifndef ANIMATION_ANIMATOR_H
#define ANIMATION_ANIMATOR_H

#include "Engine/Scene/Component.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Animation/AnimationClip.h"
#include "Engine/Utils/Logger.h"
#include <unordered_map>
#include <string>

namespace Engine {
class Animator : public Component {
public:
    void AddClip(const std::shared_ptr<AnimationClip>& clip) {
        if (!clip) {
            ENGINE_CORE_WARN("Animator: Attempted to add null AnimationClip");
            return;
        }
        ENGINE_CORE_DEBUG("Animator: Added clip '{}' ({} frames, duration: {:.2f}s, loop: {})",
                          clip->name, clip->frames.size(), clip->frameDuration, clip->loop);
        m_Clips[clip->name] = clip;
    }

    void Play(const std::string& clipName) {
        if (m_CurrentClipName == clipName) {
            return;
        }

        if (m_Clips.find(clipName) == m_Clips.end()) {
            ENGINE_CORE_WARN("Animator: Clip '{}' not found", clipName);
            return;
        }

        ENGINE_CORE_TRACE("Animator: Playing clip '{}'", clipName);
        m_CurrentClipName = clipName;
        m_CurrentClip = m_Clips[clipName];
        m_CurrentTime = 0.0f;
        m_CurrentFrameIndex = 0;
        
        UpdateSpriteRenderer();
    }

    void Update(float deltaTime) override {
        if (!m_CurrentClip || m_CurrentClip->frames.empty()) return;

        m_CurrentTime += deltaTime;

        if (m_CurrentTime >= m_CurrentClip->frameDuration) {
            m_CurrentTime -= m_CurrentClip->frameDuration;
            m_CurrentFrameIndex++;

            if (m_CurrentFrameIndex >= m_CurrentClip->frames.size()) {
                if (m_CurrentClip->loop) {
                    m_CurrentFrameIndex = 0;
                } else {
                    m_CurrentFrameIndex = static_cast<uint32_t>(m_CurrentClip->frames.size() - 1);
                }
            }
            UpdateSpriteRenderer();
        }
    }

private:
    void UpdateSpriteRenderer() {
        auto spriteRenderer = GetGameObject()->GetComponent<SpriteRenderer>();
        if (spriteRenderer && m_CurrentClip) {
            spriteRenderer->SetSubTexture(m_CurrentClip->frames[m_CurrentFrameIndex]);
        }
    }

private:
    std::unordered_map<std::string, std::shared_ptr<AnimationClip>> m_Clips;
    std::shared_ptr<AnimationClip> m_CurrentClip = nullptr;
    std::string m_CurrentClipName = "";
    
    float m_CurrentTime = 0.0f;
    uint32_t m_CurrentFrameIndex = 0;
};
} // namespace Engine

#endif // ANIMATION_ANIMATOR_H