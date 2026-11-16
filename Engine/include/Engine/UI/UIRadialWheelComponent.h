#ifndef UI_UIRADIALWHEELCOMPONENT_H
#define UI_UIRADIALWHEELCOMPONENT_H

#include "Engine/Scene/Component.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/SubTexture2D.h"
#include "Engine/UI/UICanvas.h"
#include "Engine/UI/UIPanel.h"
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

enum class RadialWheelItem : uint8_t {
    SplinterBarricade = 0, // [1] Top
    FlarePeg          = 1, // [2] Right
    Thumper           = 2, // [3] Bottom
    TarSiphon         = 3, // [4] Left
    None              = 255
};

struct RadialWheelSlot {
    RadialWheelItem Item = RadialWheelItem::None;
    std::string Name;
    uint32_t WoodCost = 0;
    std::shared_ptr<Texture> Icon{ nullptr };
    std::shared_ptr<SubTexture2D> IconSubTex{ nullptr };
    std::string ShortcutKey = "1";
};

class UIRadialWheelComponent final : public Component {
public:
    UIRectTransform rectTransform;
    bool useRectTransform = true;

    bool IsOpen = false;
    float SlowMoTimeScale = 0.15f;
    float PreviousTimeScale = 1.0f;
    int ActiveWedge = -1; // -1 = None, 0..3
    uint32_t AvailableWood = 0;
    bool WasStickDeflected = false;

    std::array<RadialWheelSlot, 4> Slots = {
        RadialWheelSlot{ RadialWheelItem::SplinterBarricade, "Splinter Barricade", 5,  nullptr, nullptr, "1" },
        RadialWheelSlot{ RadialWheelItem::FlarePeg,          "Flare Peg",          10, nullptr, nullptr, "2" },
        RadialWheelSlot{ RadialWheelItem::Thumper,           "Thumper",            15, nullptr, nullptr, "3" },
        RadialWheelSlot{ RadialWheelItem::TarSiphon,         "Tar Siphon",         20, nullptr, nullptr, "4" }
    };

    std::shared_ptr<Texture> WheelTexture{ nullptr };
    std::shared_ptr<Texture> HighlightTexture{ nullptr };

    std::function<void(int wedgeIndex, RadialWheelItem item)> OnConfirmDrop;

    UIRadialWheelComponent();

    // Resolves quadrant wedge 0..3 from angle in radians [-PI, PI]
    [[nodiscard]] static int ResolveWedgeFromAngle(float angleRad) noexcept;

    // Resolves quadrant wedge from 2D directional stick or cursor delta (returns -1 if within deadzone)
    [[nodiscard]] static int ResolveWedgeFromVector(const glm::vec2& dir, float deadzone = 0.35f) noexcept;

    void Open();
    void Close();
    void Toggle();

    // Selects and confirms wedge if affordable
    bool ConfirmSelection(int wedgeIndex);

    void Update(float deltaTime) override;

    // Generates UI render quads (background, 4 sectors with affordability tinting, and icons)
    [[nodiscard]] std::vector<UIQuad> GenerateQuads(const glm::vec2& resolvedPos, const glm::vec2& resolvedSize, float borderScale = 1.0f) const;
};

} // namespace Engine

#endif // UI_UIRADIALWHEELCOMPONENT_H
