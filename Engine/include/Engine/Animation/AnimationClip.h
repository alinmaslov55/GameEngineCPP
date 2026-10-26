#ifndef ANIMATION_ANIMATIONCLIP_H
#define ANIMATION_ANIMATIONCLIP_H

#include "Engine/Renderer/SubTexture2D.h"
#include <vector>
#include <memory>
#include <string>

namespace Engine {
struct AnimationClip {
    std::string name;
    std::vector<std::shared_ptr<SubTexture2D>> frames;
    float frameDuration = 0.1f; // Time per frame in seconds
    bool loop = true;

    AnimationClip(const std::string& clipName, float duration, bool isLooping = true)
        : name(clipName), frameDuration(duration), loop(isLooping) {}

    void AddFrame(const std::shared_ptr<SubTexture2D>& frame) {
        frames.push_back(frame);
    }
};
} // namespace Engine

#endif // ANIMATION_ANIMATIONCLIP_H
