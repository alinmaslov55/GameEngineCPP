#ifndef ANIMATION_ANIMATIONCLIP_H
#define ANIMATION_ANIMATIONCLIP_H

#include "Engine/Renderer/SubTexture2D.h"
#include <glm/vec2.hpp>
#include <vector>
#include <memory>
#include <string>
#include <string_view>

namespace Engine {

struct AnimationEvent {
    uint32_t FrameIndex = 0;
    std::string EventID;            // e.g., "HitboxActive", "HitboxEnd", "Footstep", "ChopImpact", "Slam"
    glm::vec2 SocketOffset{ 0.0f }; // Local spawn offset (meters) relative to sprite root/pivot
};

struct AnimationFrame {
    std::shared_ptr<SubTexture2D> SubTex = nullptr;
    float DurationSeconds = 0.0833f; // Supports variable hold frames
};

enum class DirectionalScheme : uint8_t {
    Omni_1Way = 0,      // Death disintegration, Wisps
    FrontBack_2Way = 1, // Wendigo ("Front", "Back" + auto FlipX for East/West)
    Cardinal_4Way = 2   // Hero & Jiangshi ("Down", "Side" + auto FlipX, "Up", "Diag")
};

enum class FacingDirection : uint8_t {
    None = 0,
    Down = 1,
    Side = 2,
    Up = 3,
    Diag = 4,
    Front = Down,
    Back = Up
};

struct AnimationClip {
    std::string name;
    std::vector<AnimationFrame> Frames;
    // Maintained for backward compatibility:
    std::vector<std::shared_ptr<SubTexture2D>> frames;
    float frameDuration = 0.1f; // Default time per frame in seconds
    bool loop = true;

    // Events associated with this clip
    std::vector<AnimationEvent> Events;

    AnimationClip(const std::string& clipName, float duration, bool isLooping = true)
        : name(clipName), frameDuration(duration), loop(isLooping) {}

    void AddFrame(const std::shared_ptr<SubTexture2D>& frame, float customDuration = -1.0f) {
        frames.push_back(frame);
        AnimationFrame af;
        af.SubTex = frame;
        af.DurationSeconds = (customDuration > 0.0f) ? customDuration : frameDuration;
        Frames.push_back(std::move(af));
    }

    void AddFrame(const AnimationFrame& frame) {
        frames.push_back(frame.SubTex);
        Frames.push_back(frame);
    }

    void AddEvent(const AnimationEvent& ev) {
        Events.push_back(ev);
    }

    void AddEvent(uint32_t frameIndex, std::string_view eventID, const glm::vec2& socketOffset = { 0.0f, 0.0f }) {
        AnimationEvent ev;
        ev.FrameIndex = frameIndex;
        ev.EventID = std::string(eventID);
        ev.SocketOffset = socketOffset;
        Events.push_back(std::move(ev));
    }

    [[nodiscard]] const std::vector<AnimationEvent>& GetEvents() const noexcept { return Events; }
    void ClearEvents() noexcept { Events.clear(); }

    [[nodiscard]] float GetTotalDuration() const noexcept {
        float total = 0.0f;
        if (!Frames.empty()) {
            for (const auto& f : Frames) {
                total += f.DurationSeconds;
            }
        } else {
            total = static_cast<float>(frames.size()) * frameDuration;
        }
        return total;
    }
};

} // namespace Engine

#endif // ANIMATION_ANIMATIONCLIP_H
