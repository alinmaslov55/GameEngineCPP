#ifndef ANIMATION_ANIMATOR_H
#define ANIMATION_ANIMATOR_H

#include "Engine/Scene/Component.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Animation/AnimationClip.h"
#include <unordered_map>
#include <string>

namespace Engine {
class Animator : public Component {
public:
    void AddClip(const std::shared_ptr<AnimationClip>& clip) {
        m_Clips[clip->name] = clip;
    }

    void Play(const std::string& clipName) {
        if (m_CurrentClipName == clipName || m_Clips.find(clipName) == m_Clips.end()) 
            return;

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