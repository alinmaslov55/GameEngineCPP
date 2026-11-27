#ifndef SANDBOX_UI_WIDGET_BUILDER_H
#define SANDBOX_UI_WIDGET_BUILDER_H

#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <Engine/UI/UIButtonComponent.h>
#include <Engine/UI/UILabel.h>
#include <Engine/UI/UIPanel.h>
#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Sandbox::UI {

/// @brief Shared factory helpers for native screen-space UIPanel / UILabel / UIButtonComponent widgets.
class WidgetBuilder {
public:
    static Engine::UIPanel* MakePanel(Engine::GameObject& go,
                                      Engine::UIAnchor anchor,
                                      const glm::vec2& offset,
                                      const glm::vec2& size,
                                      int zOrder,
                                      const glm::vec4& color,
                                      bool ignoreSafeArea = false) {
        auto* panel = go.AddComponent<Engine::UIPanel>();
        panel->useRectTransform = true;
        panel->rectTransform.Anchor = anchor;
        panel->rectTransform.Pivot = { 0.5f, 0.5f };
        panel->rectTransform.AnchoredOffset = offset;
        panel->rectTransform.Size = size;
        panel->rectTransform.ZOrder = zOrder;
        panel->rectTransform.IgnoreSafeArea = ignoreSafeArea;
        panel->color = color;
        return panel;
    }

    static Engine::UILabel* MakeLabel(Engine::GameObject& go,
                                      const std::string& text,
                                      float scale = 1.0f) {
        auto* label = go.AddComponent<Engine::UILabel>();
        label->text = text;
        label->scale = scale;
        return label;
    }

    static Engine::GameObject* MakeButton(Engine::Scene& scene,
                                          const std::string& name,
                                          Engine::GameObject* parent,
                                          const glm::vec2& offset,
                                          const glm::vec2& size,
                                          int zOrder,
                                          const glm::vec4& color,
                                          const std::string& labelText,
                                          float labelScale,
                                          std::function<void()> onClick) {
        auto* bGo = scene.CreateGameObject(name);
        if (parent) {
            bGo->GetTransform()->SetParent(parent->GetTransform(), false);
        }
        MakePanel(*bGo, Engine::UIAnchor::Center, offset, size, zOrder, color);
        MakeLabel(*bGo, labelText, labelScale);
        auto* btn = bGo->AddComponent<Engine::UIButtonComponent>();
        btn->OnClick = std::move(onClick);
        return bGo;
    }

    static void WireHorizontalFocus(const std::vector<Engine::GameObject*>& buttons) {
        for (size_t i = 0; i < buttons.size(); ++i) {
            if (!buttons[i]) {
                continue;
            }
            if (auto* btn = buttons[i]->GetComponent<Engine::UIButtonComponent>()) {
                const uint64_t leftUUID = (i > 0 && buttons[i - 1]) ? buttons[i - 1]->GetUUID() : 0;
                const uint64_t rightUUID = (i + 1 < buttons.size() && buttons[i + 1]) ? buttons[i + 1]->GetUUID() : 0;
                btn->SetNeighbors(0, 0, leftUUID, rightUUID);
            }
        }
    }
};

} // namespace Sandbox::UI

#endif // SANDBOX_UI_WIDGET_BUILDER_H
