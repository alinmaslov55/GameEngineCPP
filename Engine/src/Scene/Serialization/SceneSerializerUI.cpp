#include "JsonHelpers.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/UI/Font.h"
#include "Engine/UI/UIButtonComponent.h"
#include "Engine/UI/UILabel.h"
#include "Engine/UI/UIMinimapComponent.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/UI/UIRadialWheelComponent.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <unordered_map>

namespace Engine::Serialization {

void SerializeUIPanels(const GameObject* go, json& compsJson) {
    auto uiPanels = go->GetComponents<UIPanel>();
    if (uiPanels.empty()) return;

    json panelsJson = json::array();
    for (const auto* panel : uiPanels) {
        json panelJson = {
            {"Position", ToJson(panel->position)},
            {"Size",     ToJson(panel->size)},
            {"Color",    ToJson(panel->color)},
            {"UseRectTransform", panel->useRectTransform},
            {"RenderMode", static_cast<int>(panel->renderMode)},
            {"NineSliceBordersPx", ToJson(panel->nineSliceBordersPx)},
            {"FillAmount", panel->fillAmount},
            {"InvertFill", panel->invertFill}
        };
        if (panel->useRectTransform) {
            panelJson["RectTransform"] = {
                {"Anchor", static_cast<int>(panel->rectTransform.Anchor)},
                {"PortraitAnchorOverride", static_cast<int>(panel->rectTransform.PortraitAnchorOverride)},
                {"UsePortraitOverride", panel->rectTransform.UsePortraitOverride},
                {"Pivot", ToJson(panel->rectTransform.Pivot)},
                {"AnchoredOffset", ToJson(panel->rectTransform.AnchoredOffset)},
                {"Size", ToJson(panel->rectTransform.Size)},
                {"ZOrder", panel->rectTransform.ZOrder},
                {"IgnoreSafeArea", panel->rectTransform.IgnoreSafeArea}
            };
        }
        if (panel->texture) {
            panelJson["TexturePath"] = ResourceManager::ToRelativePath(panel->texture->GetFilepath());
        }
        panelsJson.push_back(std::move(panelJson));
    }
    compsJson["UIPanels"] = std::move(panelsJson);
}

void SerializeUIButtonComponent(const GameObject* go, json& compsJson) {
    auto* btn = go->GetComponent<UIButtonComponent>();
    if (!btn) return;

    compsJson["UIButtonComponent"] = {
        {"NormalTint", ToJson(btn->NormalTint)},
        {"HoverTint", ToJson(btn->HoverTint)},
        {"PressedTint", ToJson(btn->PressedTint)},
        {"DisabledTint", ToJson(btn->DisabledTint)},
        {"FocusedTint", ToJson(btn->FocusedTint)},
        {"Interactable", btn->Interactable},
        {"AutoTintPanel", btn->AutoTintPanel},
        {"UpNeighborUUID", btn->UpNeighborUUID},
        {"DownNeighborUUID", btn->DownNeighborUUID},
        {"LeftNeighborUUID", btn->LeftNeighborUUID},
        {"RightNeighborUUID", btn->RightNeighborUUID}
    };
}

void SerializeUIMinimapComponent(const GameObject* go, json& compsJson) {
    auto* minimap = go->GetComponent<UIMinimapComponent>();
    if (!minimap) return;

    json minimapJson = {
        {"UseRectTransform", minimap->useRectTransform},
        {"PinSize", minimap->pinSize},
        {"FrameColor", ToJson(minimap->frameColor)},
        {"PlayerPinColor", ToJson(minimap->playerPinColor)}
    };
    if (minimap->useRectTransform) {
        minimapJson["RectTransform"] = {
            {"Anchor", static_cast<int>(minimap->rectTransform.Anchor)},
            {"PortraitAnchorOverride", static_cast<int>(minimap->rectTransform.PortraitAnchorOverride)},
            {"UsePortraitOverride", minimap->rectTransform.UsePortraitOverride},
            {"Pivot", ToJson(minimap->rectTransform.Pivot)},
            {"AnchoredOffset", ToJson(minimap->rectTransform.AnchoredOffset)},
            {"Size", ToJson(minimap->rectTransform.Size)},
            {"ZOrder", minimap->rectTransform.ZOrder},
            {"IgnoreSafeArea", minimap->rectTransform.IgnoreSafeArea}
        };
    }
    if (minimap->frameTexture) {
        minimapJson["FrameTexturePath"] = ResourceManager::ToRelativePath(minimap->frameTexture->GetFilepath());
    }
    compsJson["UIMinimapComponent"] = std::move(minimapJson);
}

void SerializeUIRadialWheelComponent(const GameObject* go, json& compsJson) {
    auto* wheel = go->GetComponent<UIRadialWheelComponent>();
    if (!wheel) return;

    json wheelJson = {
        {"Enabled", wheel->IsEnabled()},
        {"UseRectTransform", wheel->useRectTransform},
        {"SlowMoTimeScale", wheel->SlowMoTimeScale},
        {"AvailableWood", wheel->AvailableWood}
    };
    if (wheel->useRectTransform) {
        wheelJson["RectTransform"] = {
            {"Anchor", static_cast<int>(wheel->rectTransform.Anchor)},
            {"PortraitAnchorOverride", static_cast<int>(wheel->rectTransform.PortraitAnchorOverride)},
            {"UsePortraitOverride", wheel->rectTransform.UsePortraitOverride},
            {"Pivot", ToJson(wheel->rectTransform.Pivot)},
            {"AnchoredOffset", ToJson(wheel->rectTransform.AnchoredOffset)},
            {"Size", ToJson(wheel->rectTransform.Size)},
            {"ZOrder", wheel->rectTransform.ZOrder},
            {"IgnoreSafeArea", wheel->rectTransform.IgnoreSafeArea}
        };
    }
    if (wheel->WheelTexture) {
        wheelJson["WheelTexturePath"] = ResourceManager::ToRelativePath(wheel->WheelTexture->GetFilepath());
    }
    if (wheel->HighlightTexture) {
        wheelJson["HighlightTexturePath"] = ResourceManager::ToRelativePath(wheel->HighlightTexture->GetFilepath());
    }
    json slotsJson = json::array();
    for (const auto& slot : wheel->Slots) {
        json slotEntry = {
            {"Item", static_cast<int>(slot.Item)},
            {"Name", slot.Name},
            {"WoodCost", slot.WoodCost},
            {"ShortcutKey", slot.ShortcutKey}
        };
        if (slot.Icon) {
            slotEntry["IconPath"] = ResourceManager::ToRelativePath(slot.Icon->GetFilepath());
        }
        if (slot.IconSubTex) {
            slotEntry["IconSubTexture"] = SerializeSubTexture(slot.IconSubTex);
        }
        slotsJson.push_back(std::move(slotEntry));
    }
    wheelJson["Slots"] = std::move(slotsJson);
    compsJson["UIRadialWheelComponent"] = std::move(wheelJson);
}

void SerializeUILabels(const GameObject* go, json& compsJson) {
    auto uiLabels = go->GetComponents<UILabel>();
    if (uiLabels.empty()) return;

    json labelsJson = json::array();
    for (const auto* label : uiLabels) {
        json labelJson = {
            {"Text",     label->text},
            {"Position", ToJson(label->position)},
            {"Scale",    label->scale},
            {"Color",    ToJson(label->color)},
            {"UseRectTransform", label->useRectTransform},
            {"Alignment", static_cast<int>(label->alignment)},
            {"MaxWrapWidth", label->maxWrapWidth},
            {"DropShadow", label->dropShadow},
            {"ShadowOffset", ToJson(label->shadowOffset)},
            {"ShadowColor", ToJson(label->shadowColor)}
        };
        if (label->useRectTransform) {
            labelJson["RectTransform"] = {
                {"Anchor", static_cast<int>(label->rectTransform.Anchor)},
                {"PortraitAnchorOverride", static_cast<int>(label->rectTransform.PortraitAnchorOverride)},
                {"UsePortraitOverride", label->rectTransform.UsePortraitOverride},
                {"Pivot", ToJson(label->rectTransform.Pivot)},
                {"AnchoredOffset", ToJson(label->rectTransform.AnchoredOffset)},
                {"Size", ToJson(label->rectTransform.Size)},
                {"ZOrder", label->rectTransform.ZOrder},
                {"IgnoreSafeArea", label->rectTransform.IgnoreSafeArea}
            };
        }

        if (label->font) {
            json fontJson;
            if (!label->font->GetFilepath().empty()) {
                fontJson["BMFontPath"] = ResourceManager::ToRelativePath(label->font->GetFilepath());
            }
            if (label->font->atlasTexture) {
                fontJson["AtlasTexturePath"] = ResourceManager::ToRelativePath(label->font->atlasTexture->GetFilepath());
            }

            std::vector<char> sortedChars;
            sortedChars.reserve(label->font->GetCharacters().size());
            for (const auto& [chKey, _] : label->font->GetCharacters()) {
                sortedChars.push_back(chKey);
            }
            std::sort(sortedChars.begin(), sortedChars.end());

            json charsJson = json::array();
            for (char chKey : sortedChars) {
                const Character& chData = label->font->GetCharacters().at(chKey);
                json charEntry = {
                    {"Char",    std::string(1, chKey)},
                    {"Size",    ToJson(chData.size)},
                    {"Bearing", ToJson(chData.bearing)},
                    {"Advance", chData.advance}
                };
                if (chData.subTexture) {
                    charEntry["SubTexture"] = SerializeSubTexture(chData.subTexture);
                }
                charsJson.push_back(std::move(charEntry));
            }
            fontJson["Characters"] = std::move(charsJson);
            labelJson["Font"] = std::move(fontJson);
        }

        labelsJson.push_back(std::move(labelJson));
    }
    compsJson["UILabels"] = std::move(labelsJson);
}


namespace {
void DeserializeSingleUIPanel(GameObject* go, const json& panelJson) {
    glm::vec2 pos = AsVec2(panelJson["Position"]);
    glm::vec2 sz  = AsVec2(panelJson["Size"]);
    glm::vec4 col = AsVec4(panelJson["Color"], glm::vec4(1.0f));
    auto* panel = go->AddComponent<UIPanel>(pos, sz, col);
    if (panelJson.contains("TexturePath") && panelJson["TexturePath"].is_string()) {
        std::string texPath = panelJson["TexturePath"].get<std::string>();
        if (!texPath.empty()) {
            panel->texture = ResourceManager::Load<Texture>(texPath);
        }
    }
    panel->useRectTransform = panelJson.value("UseRectTransform", false);
    if (panelJson.contains("RectTransform")) {
        const auto& rtJson = panelJson["RectTransform"];
        panel->rectTransform.Anchor = static_cast<UIAnchor>(rtJson.value("Anchor", 0));
        panel->rectTransform.PortraitAnchorOverride = static_cast<UIAnchor>(rtJson.value("PortraitAnchorOverride", 0));
        panel->rectTransform.UsePortraitOverride = rtJson.value("UsePortraitOverride", false);
        if (rtJson.contains("Pivot")) panel->rectTransform.Pivot = AsVec2(rtJson["Pivot"], panel->rectTransform.Pivot);
        if (rtJson.contains("AnchoredOffset")) panel->rectTransform.AnchoredOffset = AsVec2(rtJson["AnchoredOffset"], panel->rectTransform.AnchoredOffset);
        if (rtJson.contains("Size")) panel->rectTransform.Size = AsVec2(rtJson["Size"], panel->rectTransform.Size);
        panel->rectTransform.ZOrder = rtJson.value("ZOrder", 0);
        panel->rectTransform.IgnoreSafeArea = rtJson.value("IgnoreSafeArea", false);
    }
    panel->renderMode = static_cast<UIRenderMode>(panelJson.value("RenderMode", 0));
    if (panelJson.contains("NineSliceBordersPx")) {
        panel->nineSliceBordersPx = AsVec4(panelJson["NineSliceBordersPx"], panel->nineSliceBordersPx);
    }
    panel->fillAmount = panelJson.value("FillAmount", 1.0f);
    panel->invertFill = panelJson.value("InvertFill", false);
}


void DeserializeSingleUILabel(GameObject* go, const json& labelJson) {
    std::string text = labelJson.value("Text", "");
    glm::vec2 pos = AsVec2(labelJson["Position"]);

    std::shared_ptr<Font> font = nullptr;
    if (labelJson.contains("Font") && labelJson["Font"].is_object()) {
        const auto& fontJson = labelJson["Font"];
        std::string bmFontPath;
        if (fontJson.contains("BMFontPath") && fontJson["BMFontPath"].is_string()) {
            bmFontPath = ResourceManager::ResolvePath(fontJson["BMFontPath"].get<std::string>());
        }
        std::string atlasPath;
        if (fontJson.contains("AtlasTexturePath") && fontJson["AtlasTexturePath"].is_string()) {
            atlasPath = ResourceManager::ResolvePath(fontJson["AtlasTexturePath"].get<std::string>());
        }

        static std::mutex s_FontCacheMutex;
        static std::unordered_map<std::string, std::shared_ptr<Font>> s_FontCache;

        const std::string& cacheKey = !bmFontPath.empty() ? bmFontPath : atlasPath;

        if (!cacheKey.empty()) {
            std::lock_guard<std::mutex> lock(s_FontCacheMutex);
            auto it = s_FontCache.find(cacheKey);
            if (it != s_FontCache.end()) {
                font = it->second;
            }
        }

        if (!font && !bmFontPath.empty() && std::filesystem::exists(bmFontPath)) {
            font = Font::LoadBMFont(bmFontPath);
            if (font && !cacheKey.empty()) {
                std::lock_guard<std::mutex> lock(s_FontCacheMutex);
                s_FontCache[cacheKey] = font;
            }
        }

        if (!font) {
            font = std::make_shared<Font>();
            if (!atlasPath.empty()) {
                font->atlasTexture = ResourceManager::Load<Texture>(atlasPath);
            }
            if (fontJson.contains("Characters") && fontJson["Characters"].is_array()) {
                for (const auto& chJson : fontJson["Characters"]) {
                    std::string chStr = chJson.value("Char", "");
                    if (chStr.empty()) continue;

                    Character ch{};
                    if (chJson.contains("SubTexture")) {
                        ch.subTexture = DeserializeSubTexture(chJson["SubTexture"]);
                    }
                    if (chJson.contains("Size")) {
                        ch.size = AsVec2(chJson["Size"]);
                    }
                    if (chJson.contains("Bearing")) {
                        ch.bearing = AsVec2(chJson["Bearing"]);
                    }
                    ch.advance = chJson.value("Advance", 0.0f);
                    font->AddCharacter(chStr[0], ch);
                }
            }
            if (!cacheKey.empty()) {
                std::lock_guard<std::mutex> lock(s_FontCacheMutex);
                s_FontCache[cacheKey] = font;
            }
        }
    }

    auto* label = go->AddComponent<UILabel>(text, font, pos);
    label->scale = labelJson.value("Scale", 1.0f);
    if (labelJson.contains("Color")) {
        label->color = AsVec4(labelJson["Color"], label->color);
    }
    label->alignment = static_cast<TextAlignH>(labelJson.value("Alignment", 0));
    label->maxWrapWidth = labelJson.value("MaxWrapWidth", 0.0f);
    label->dropShadow = labelJson.value("DropShadow", true);
    if (labelJson.contains("ShadowOffset")) {
        label->shadowOffset = AsVec2(labelJson["ShadowOffset"], label->shadowOffset);
    }
    if (labelJson.contains("ShadowColor")) {
        label->shadowColor = AsVec4(labelJson["ShadowColor"], label->shadowColor);
    }
    label->useRectTransform = labelJson.value("UseRectTransform", false);
    if (labelJson.contains("RectTransform")) {
        const auto& rtJson = labelJson["RectTransform"];
        label->rectTransform.Anchor = static_cast<UIAnchor>(rtJson.value("Anchor", 0));
        label->rectTransform.PortraitAnchorOverride = static_cast<UIAnchor>(rtJson.value("PortraitAnchorOverride", 0));
        label->rectTransform.UsePortraitOverride = rtJson.value("UsePortraitOverride", false);
        if (rtJson.contains("Pivot")) label->rectTransform.Pivot = AsVec2(rtJson["Pivot"], label->rectTransform.Pivot);
        if (rtJson.contains("AnchoredOffset")) label->rectTransform.AnchoredOffset = AsVec2(rtJson["AnchoredOffset"], label->rectTransform.AnchoredOffset);
        if (rtJson.contains("Size")) label->rectTransform.Size = AsVec2(rtJson["Size"], label->rectTransform.Size);
        label->rectTransform.ZOrder = rtJson.value("ZOrder", 0);
        label->rectTransform.IgnoreSafeArea = rtJson.value("IgnoreSafeArea", false);
    }
}


} // namespace

void DeserializeUIButtonComponent(GameObject* go, const json& j, const std::string&) {
    if (!j.is_object()) return;
    auto* btn = go->AddComponent<UIButtonComponent>();
    if (j.contains("NormalTint")) btn->NormalTint = AsVec4(j["NormalTint"], btn->NormalTint);
    if (j.contains("HoverTint")) btn->HoverTint = AsVec4(j["HoverTint"], btn->HoverTint);
    if (j.contains("PressedTint")) btn->PressedTint = AsVec4(j["PressedTint"], btn->PressedTint);
    if (j.contains("DisabledTint")) btn->DisabledTint = AsVec4(j["DisabledTint"], btn->DisabledTint);
    if (j.contains("FocusedTint")) btn->FocusedTint = AsVec4(j["FocusedTint"], btn->FocusedTint);
    btn->Interactable = j.value("Interactable", true);
    btn->AutoTintPanel = j.value("AutoTintPanel", true);
    btn->UpNeighborUUID = j.value("UpNeighborUUID", uint64_t(0));
    btn->DownNeighborUUID = j.value("DownNeighborUUID", uint64_t(0));
    btn->LeftNeighborUUID = j.value("LeftNeighborUUID", uint64_t(0));
    btn->RightNeighborUUID = j.value("RightNeighborUUID", uint64_t(0));
}

void DeserializeUIMinimapComponent(GameObject* go, const json& j, const std::string&) {
    if (!j.is_object()) return;
    auto* minimap = go->AddComponent<UIMinimapComponent>();
    minimap->useRectTransform = j.value("UseRectTransform", true);
    minimap->pinSize = j.value("PinSize", 8.0f);
    if (j.contains("FrameColor")) minimap->frameColor = AsVec4(j["FrameColor"], minimap->frameColor);
    if (j.contains("PlayerPinColor")) minimap->playerPinColor = AsVec4(j["PlayerPinColor"], minimap->playerPinColor);
    if (j.contains("RectTransform")) {
        const auto& rtJson = j["RectTransform"];
        minimap->rectTransform.Anchor = static_cast<UIAnchor>(rtJson.value("Anchor", 0));
        minimap->rectTransform.PortraitAnchorOverride = static_cast<UIAnchor>(rtJson.value("PortraitAnchorOverride", 0));
        minimap->rectTransform.UsePortraitOverride = rtJson.value("UsePortraitOverride", false);
        if (rtJson.contains("Pivot")) minimap->rectTransform.Pivot = AsVec2(rtJson["Pivot"], minimap->rectTransform.Pivot);
        if (rtJson.contains("AnchoredOffset")) minimap->rectTransform.AnchoredOffset = AsVec2(rtJson["AnchoredOffset"], minimap->rectTransform.AnchoredOffset);
        if (rtJson.contains("Size")) minimap->rectTransform.Size = AsVec2(rtJson["Size"], minimap->rectTransform.Size);
        minimap->rectTransform.ZOrder = rtJson.value("ZOrder", 20);
        minimap->rectTransform.IgnoreSafeArea = rtJson.value("IgnoreSafeArea", false);
    }
    if (j.contains("FrameTexturePath") && j["FrameTexturePath"].is_string()) {
        std::string path = j["FrameTexturePath"].get<std::string>();
        if (!path.empty()) {
            minimap->frameTexture = ResourceManager::Load<Texture>(path);
        }
    }
}

void DeserializeUIRadialWheelComponent(GameObject* go, const json& j, const std::string&) {
    if (!j.is_object()) return;
    auto* wheel = go->AddComponent<UIRadialWheelComponent>();
    wheel->SetEnabled(j.value("Enabled", true));
    wheel->useRectTransform = j.value("UseRectTransform", true);
    wheel->SlowMoTimeScale = j.value("SlowMoTimeScale", 0.15f);
    wheel->AvailableWood = j.value("AvailableWood", 0u);
    if (j.contains("RectTransform")) {
        const auto& rtJson = j["RectTransform"];
        wheel->rectTransform.Anchor = static_cast<UIAnchor>(rtJson.value("Anchor", 0));
        wheel->rectTransform.PortraitAnchorOverride = static_cast<UIAnchor>(rtJson.value("PortraitAnchorOverride", 0));
        wheel->rectTransform.UsePortraitOverride = rtJson.value("UsePortraitOverride", false);
        if (rtJson.contains("Pivot")) wheel->rectTransform.Pivot = AsVec2(rtJson["Pivot"], wheel->rectTransform.Pivot);
        if (rtJson.contains("AnchoredOffset")) wheel->rectTransform.AnchoredOffset = AsVec2(rtJson["AnchoredOffset"], wheel->rectTransform.AnchoredOffset);
        if (rtJson.contains("Size")) wheel->rectTransform.Size = AsVec2(rtJson["Size"], wheel->rectTransform.Size);
        wheel->rectTransform.ZOrder = rtJson.value("ZOrder", 100);
        wheel->rectTransform.IgnoreSafeArea = rtJson.value("IgnoreSafeArea", false);
    }
    if (j.contains("WheelTexturePath") && j["WheelTexturePath"].is_string()) {
        const std::string texPath = j["WheelTexturePath"].get<std::string>();
        if (!texPath.empty()) {
            wheel->WheelTexture = ResourceManager::Load<Texture>(texPath);
        }
    }
    if (j.contains("HighlightTexturePath") && j["HighlightTexturePath"].is_string()) {
        const std::string texPath = j["HighlightTexturePath"].get<std::string>();
        if (!texPath.empty()) {
            wheel->HighlightTexture = ResourceManager::Load<Texture>(texPath);
        }
    }
    if (j.contains("Slots") && j["Slots"].is_array()) {
        size_t idx = 0;
        for (const auto& slotJson : j["Slots"]) {
            if (idx >= wheel->Slots.size()) break;
            wheel->Slots[idx].Item = static_cast<RadialWheelItem>(slotJson.value("Item", 0));
            wheel->Slots[idx].Name = slotJson.value("Name", wheel->Slots[idx].Name);
            wheel->Slots[idx].WoodCost = slotJson.value("WoodCost", wheel->Slots[idx].WoodCost);
            wheel->Slots[idx].ShortcutKey = slotJson.value("ShortcutKey", wheel->Slots[idx].ShortcutKey);
            if (slotJson.contains("IconPath") && slotJson["IconPath"].is_string()) {
                const std::string iconPath = slotJson["IconPath"].get<std::string>();
                if (!iconPath.empty()) {
                    wheel->Slots[idx].Icon = ResourceManager::Load<Texture>(iconPath);
                }
            }
            if (slotJson.contains("IconSubTexture") && slotJson["IconSubTexture"].is_object()) {
                wheel->Slots[idx].IconSubTex = DeserializeSubTexture(slotJson["IconSubTexture"]);
            }
            idx++;
        }
    }
}

void DeserializeUIPanels(GameObject* go, const json& j, const std::string&) {
    if (j.is_array()) {
        for (const auto& panelJson : j) {
            DeserializeSingleUIPanel(go, panelJson);
        }
    } else if (j.is_object()) {
        DeserializeSingleUIPanel(go, j);
    }
}


void DeserializeUILabels(GameObject* go, const json& j, const std::string&) {
    if (j.is_array()) {
        for (const auto& labelJson : j) {
            DeserializeSingleUILabel(go, labelJson);
        }
    } else if (j.is_object()) {
        DeserializeSingleUILabel(go, j);
    }
}


} // namespace Engine::Serialization
