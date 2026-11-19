#ifndef DATA_DATAREGISTRY_H
#define DATA_DATAREGISTRY_H

#include "Engine/Utils/Logger.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine {

template<typename TDef>
class DataRegistry {
public:
    using ParserFn = std::function<bool(const nlohmann::json&, TDef&)>;

    DataRegistry() = default;

    bool LoadDirectoryOrFile(const std::string& virtualAssetPath, ParserFn parser) {
        if (!parser) {
            ENGINE_CORE_ERROR("DataRegistry::LoadDirectoryOrFile: Null parser function provided for '{}'", virtualAssetPath);
            return false;
        }

        m_SourcePath = virtualAssetPath;
        m_Parser = parser;

        std::vector<TDef> newDefs;
        std::unordered_map<std::string, size_t> newLookup;

        std::error_code ec;
        const std::filesystem::path p(virtualAssetPath);
        if (!std::filesystem::exists(p, ec)) {
            ENGINE_CORE_ERROR("DataRegistry: Source path does not exist: '{}'", virtualAssetPath);
            return false;
        }

        bool anyLoaded = false;
        if (std::filesystem::is_directory(p, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(p, ec)) {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
                    if (ParseFile(entry.path().string(), parser, newDefs, newLookup)) {
                        anyLoaded = true;
                    }
                }
            }
        } else {
            if (ParseFile(virtualAssetPath, parser, newDefs, newLookup)) {
                anyLoaded = true;
            }
        }

        if (!anyLoaded && newDefs.empty()) {
            ENGINE_CORE_WARN("DataRegistry: No valid entries loaded from '{}'", virtualAssetPath);
            return false;
        }

        m_OrderedDefs = std::move(newDefs);
        m_Lookup = std::move(newLookup);
        ENGINE_CORE_INFO("DataRegistry: Successfully loaded {} entries from '{}'", m_OrderedDefs.size(), virtualAssetPath);
        return true;
    }

    bool LoadFromString(std::string_view jsonContent, ParserFn parser, const std::string& debugSource = "<memory>") {
        if (!parser) {
            ENGINE_CORE_ERROR("DataRegistry::LoadFromString: Null parser function provided for '{}'", debugSource);
            return false;
        }

        std::vector<TDef> newDefs;
        std::unordered_map<std::string, size_t> newLookup;

        if (!ParseJsonString(jsonContent, parser, debugSource, newDefs, newLookup)) {
            return false;
        }

        m_SourcePath = debugSource;
        m_Parser = parser;
        m_OrderedDefs = std::move(newDefs);
        m_Lookup = std::move(newLookup);
        return true;
    }

    bool ReloadAll() {
        if (m_SourcePath.empty() || m_SourcePath == "<memory>" || !m_Parser) {
            ENGINE_CORE_WARN("DataRegistry::ReloadAll: No disk source path or parser registered to reload");
            return false;
        }
        ENGINE_CORE_INFO("DataRegistry: Reloading from '{}'...", m_SourcePath);
        return LoadDirectoryOrFile(m_SourcePath, m_Parser);
    }

    [[nodiscard]] const TDef* Find(std::string_view id) const noexcept {
        const auto it = m_Lookup.find(std::string(id));
        if (it != m_Lookup.end()) {
            return &m_OrderedDefs[it->second];
        }
        return nullptr;
    }

    [[nodiscard]] const std::vector<TDef>& GetAll() const noexcept { return m_OrderedDefs; }
    [[nodiscard]] size_t Count() const noexcept { return m_OrderedDefs.size(); }
    [[nodiscard]] bool Empty() const noexcept { return m_OrderedDefs.empty(); }
    [[nodiscard]] const std::string& GetSourcePath() const noexcept { return m_SourcePath; }

    void Clear() noexcept {
        m_OrderedDefs.clear();
        m_Lookup.clear();
        m_SourcePath.clear();
    }

private:
    bool ParseFile(const std::string& filePath,
                   const ParserFn& parser,
                   std::vector<TDef>& outDefs,
                   std::unordered_map<std::string, size_t>& outLookup) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            ENGINE_CORE_ERROR("DataRegistry: Failed to open file '{}'", filePath);
            return false;
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return ParseJsonString(content, parser, filePath, outDefs, outLookup);
    }

    bool ParseJsonString(std::string_view jsonStr,
                         const ParserFn& parser,
                         const std::string& debugSource,
                         std::vector<TDef>& outDefs,
                         std::unordered_map<std::string, size_t>& outLookup) {
        nlohmann::json root = nlohmann::json::parse(jsonStr, nullptr, false);
        if (root.is_discarded()) {
            ENGINE_CORE_ERROR("DataRegistry: JSON parse error in '{}'", debugSource);
            return false;
        }

        auto processItem = [&](const nlohmann::json& itemJson, size_t indexHint) {
            try {
                TDef def{};
                if (!parser(itemJson, def)) {
                    ENGINE_CORE_WARN("DataRegistry: Parser rejected malformed entry #{} in '{}'", indexHint, debugSource);
                    return;
                }
                if (def.Id.empty()) {
                    ENGINE_CORE_WARN("DataRegistry: Entry #{} in '{}' has empty 'Id', skipping", indexHint, debugSource);
                    return;
                }
                if (outLookup.find(def.Id) != outLookup.end()) {
                    ENGINE_CORE_WARN("DataRegistry: Duplicate ID '{}' in '{}', updating existing entry", def.Id, debugSource);
                    outDefs[outLookup[def.Id]] = def;
                } else {
                    outLookup[def.Id] = outDefs.size();
                    outDefs.push_back(std::move(def));
                }
            } catch (const std::exception& e) {
                ENGINE_CORE_WARN("DataRegistry: Exception while parsing entry in '{}': {}", debugSource, e.what());
            }
        };

        if (root.is_array()) {
            for (size_t i = 0; i < root.size(); ++i) {
                processItem(root[i], i);
            }
        } else if (root.is_object()) {
            bool foundArray = false;
            for (auto it = root.begin(); it != root.end(); ++it) {
                if (it.value().is_array()) {
                    foundArray = true;
                    for (size_t i = 0; i < it.value().size(); ++i) {
                        processItem(it.value()[i], i);
                    }
                }
            }
            if (!foundArray) {
                processItem(root, 0);
            }
        } else {
            ENGINE_CORE_WARN("DataRegistry: Root element in '{}' is neither an array nor an object", debugSource);
            return false;
        }

        return true;
    }

    std::string m_SourcePath;
    ParserFn m_Parser;
    std::vector<TDef> m_OrderedDefs;
    std::unordered_map<std::string, size_t> m_Lookup;
};

} // namespace Engine

#endif // DATA_DATAREGISTRY_H
