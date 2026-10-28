#ifndef SCENE_POSTPROCESSCOMPONENT_H
#define SCENE_POSTPROCESSCOMPONENT_H

#include "Engine/Scene/Component.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace Engine {

class PostProcessComponent : public Component {
public:
    enum class ToneMapMode {
        ACESFilmic = 0,
        StorybookNeutral = 1,
        Reinhard = 2,
        None = 3
    };

    enum class Preset {
        CultStorybook = 0,
        GoldenClearing = 1,
        BloodMoonRitual = 2,
        MidnightSanctum = 3,
        CrispNeutral = 4
    };

    bool Enabled = true;
    glm::vec3 BackdropColor = { 0.05f, 0.04f, 0.08f };

    // Multi-Mip HDR Bloom
    bool BloomEnabled = true;
    float BloomThreshold = 1.05f;
    float BloomKnee = 0.45f;
    float BloomIntensity = 0.42f;
    float BloomScatter = 0.72f;

    // Exposure & Tone Mapping
    ToneMapMode ToneMapping = ToneMapMode::StorybookNeutral;
    float Exposure = 0.05f; // EV stops: scales linear radiance by exp2(Exposure)

    // Storybook Color Grading & Split Toning
    float Temperature = 0.06f; // [-1, 1] cool blue to warm amber
    float Tint = 0.0f;         // [-1, 1] green to magenta
    float Contrast = 1.06f;    // [0.5, 2.0]
    float Saturation = 1.08f;  // [0.0, 2.0]
    glm::vec3 ShadowTint = { 0.92f, 0.90f, 1.04f };    // Subtle plum-indigo shadow lift
    glm::vec3 HighlightTint = { 1.03f, 1.00f, 0.94f }; // Warm ivory highlight gain

    // Atmospheric Diorama Vignette
    bool VignetteEnabled = true;
    float VignetteIntensity = 0.36f;
    float VignetteSmoothness = 0.62f;
    glm::vec3 VignetteColor = { 0.06f, 0.04f, 0.10f };

    void ApplyPreset(Preset preset) {
        Enabled = true;
        BloomEnabled = true;
        VignetteEnabled = true;

        switch (preset) {
            case Preset::CultStorybook:
                BackdropColor = { 0.05f, 0.04f, 0.08f };
                BloomThreshold = 1.05f;
                BloomKnee = 0.45f;
                BloomIntensity = 0.42f;
                BloomScatter = 0.72f;
                ToneMapping = ToneMapMode::StorybookNeutral;
                Exposure = 0.05f;
                Temperature = 0.06f;
                Tint = 0.0f;
                Contrast = 1.06f;
                Saturation = 1.08f;
                ShadowTint = { 0.92f, 0.90f, 1.04f };
                HighlightTint = { 1.03f, 1.00f, 0.94f };
                VignetteIntensity = 0.36f;
                VignetteSmoothness = 0.62f;
                VignetteColor = { 0.06f, 0.04f, 0.10f };
                break;

            case Preset::GoldenClearing:
                BackdropColor = { 0.07f, 0.06f, 0.05f };
                BloomThreshold = 0.98f;
                BloomKnee = 0.52f;
                BloomIntensity = 0.48f;
                BloomScatter = 0.78f;
                ToneMapping = ToneMapMode::StorybookNeutral;
                Exposure = 0.14f;
                Temperature = 0.22f;
                Tint = 0.02f;
                Contrast = 1.04f;
                Saturation = 1.12f;
                ShadowTint = { 0.96f, 0.92f, 1.02f };
                HighlightTint = { 1.06f, 1.01f, 0.90f };
                VignetteIntensity = 0.26f;
                VignetteSmoothness = 0.70f;
                VignetteColor = { 0.09f, 0.05f, 0.04f };
                break;

            case Preset::BloodMoonRitual:
                BackdropColor = { 0.07f, 0.02f, 0.04f };
                BloomThreshold = 0.92f;
                BloomKnee = 0.50f;
                BloomIntensity = 0.62f;
                BloomScatter = 0.76f;
                ToneMapping = ToneMapMode::ACESFilmic;
                Exposure = 0.02f;
                Temperature = 0.12f;
                Tint = 0.18f;
                Contrast = 1.16f;
                Saturation = 1.14f;
                ShadowTint = { 0.88f, 0.78f, 0.96f };
                HighlightTint = { 1.10f, 0.88f, 0.86f };
                VignetteIntensity = 0.50f;
                VignetteSmoothness = 0.58f;
                VignetteColor = { 0.12f, 0.02f, 0.05f };
                break;

            case Preset::MidnightSanctum:
                BackdropColor = { 0.02f, 0.03f, 0.07f };
                BloomThreshold = 0.88f;
                BloomKnee = 0.42f;
                BloomIntensity = 0.58f;
                BloomScatter = 0.80f;
                ToneMapping = ToneMapMode::ACESFilmic;
                Exposure = -0.08f;
                Temperature = -0.20f;
                Tint = 0.04f;
                Contrast = 1.12f;
                Saturation = 1.02f;
                ShadowTint = { 0.82f, 0.88f, 1.10f };
                HighlightTint = { 1.04f, 0.98f, 0.92f };
                VignetteIntensity = 0.46f;
                VignetteSmoothness = 0.55f;
                VignetteColor = { 0.02f, 0.03f, 0.08f };
                break;

            case Preset::CrispNeutral:
                BackdropColor = { 0.08f, 0.08f, 0.09f };
                BloomThreshold = 1.25f;
                BloomKnee = 0.35f;
                BloomIntensity = 0.20f;
                BloomScatter = 0.55f;
                ToneMapping = ToneMapMode::StorybookNeutral;
                Exposure = 0.0f;
                Temperature = 0.0f;
                Tint = 0.0f;
                Contrast = 1.0f;
                Saturation = 1.0f;
                ShadowTint = { 1.0f, 1.0f, 1.0f };
                HighlightTint = { 1.0f, 1.0f, 1.0f };
                VignetteEnabled = false;
                VignetteIntensity = 0.0f;
                VignetteSmoothness = 0.60f;
                VignetteColor = { 0.0f, 0.0f, 0.0f };
                break;
        }
    }

    // Quadratic soft-knee bright-pass filter matching the GPU Mip-0 prefilter shader
    glm::vec3 EvaluateSoftKneePrefilter(const glm::vec3& hdrColor) const {
        const glm::vec3 safeColor = glm::max(hdrColor, glm::vec3(0.0f));
        const float brightness = std::max(safeColor.r, std::max(safeColor.g, safeColor.b));
        if (brightness <= 1e-6f) {
            return glm::vec3(0.0f);
        }

        const float threshold = std::max(0.0f, BloomThreshold);
        const float knee = std::max(0.0f, BloomKnee);

        float rq = brightness - threshold;
        if (knee > 1e-5f) {
            float soft = std::clamp(brightness - threshold + knee, 0.0f, 2.0f * knee);
            soft = (soft * soft) / (4.0f * knee);
            rq = std::max(soft, brightness - threshold);
        } else {
            rq = std::max(0.0f, brightness - threshold);
        }

        const float weight = rq / brightness;
        return safeColor * weight;
    }

    // CPU reference implementation of the shader tone-mapping operators
    glm::vec3 ApplyToneMapCPU(const glm::vec3& linearHDR) const {
        glm::vec3 x = glm::max(linearHDR, glm::vec3(0.0f));

        switch (ToneMapping) {
            case ToneMapMode::ACESFilmic: {
                const glm::vec3 num = x * (2.51f * x + glm::vec3(0.03f));
                const glm::vec3 den = x * (2.43f * x + glm::vec3(0.59f)) + glm::vec3(0.14f);
                return glm::clamp(num / den, glm::vec3(0.0f), glm::vec3(1.0f));
            }

            case ToneMapMode::StorybookNeutral: {
                const float startCompression = 0.8f - 0.04f;
                const float desaturation = 0.15f;

                const float minCh = std::min(x.r, std::min(x.g, x.b));
                const float offset = (minCh < 0.08f) ? (minCh - 6.25f * minCh * minCh) : 0.04f;
                glm::vec3 c = x - glm::vec3(offset);

                const float peak = std::max(c.r, std::max(c.g, c.b));
                if (peak < startCompression) {
                    return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
                }

                const float d = 1.0f - startCompression;
                const float newPeak = 1.0f - (d * d) / (peak + d - startCompression);
                c *= (newPeak / peak);

                const float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
                return glm::clamp(glm::mix(c, glm::vec3(newPeak), g), glm::vec3(0.0f), glm::vec3(1.0f));
            }

            case ToneMapMode::Reinhard: {
                return glm::clamp(x / (glm::vec3(1.0f) + x), glm::vec3(0.0f), glm::vec3(1.0f));
            }

            case ToneMapMode::None:
            default: {
                return glm::clamp(x, glm::vec3(0.0f), glm::vec3(1.0f));
            }
        }
    }
};

} // namespace Engine

#endif // SCENE_POSTPROCESSCOMPONENT_H

