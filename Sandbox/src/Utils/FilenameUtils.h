#ifndef SANDBOX_FILENAME_UTILS_H
#define SANDBOX_FILENAME_UTILS_H

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Sandbox {

/// @brief Sanitizes an arbitrary entity or asset name into a safe cross-platform filename stem.
[[nodiscard]] inline std::string SanitizeFilename(std::string_view name) {
    std::string safe;
    safe.reserve(name.size());
    for (char c : name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 32 || uc == 127 ||
            c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            safe += '_';
        } else {
            safe += c;
        }
    }
    while (!safe.empty() && (safe.front() == ' ' || safe.front() == '\t')) safe.erase(safe.begin());
    while (!safe.empty() && (safe.back() == ' ' || safe.back() == '\t' || safe.back() == '.')) safe.pop_back();
    if (safe.empty()) {
        return "GameObject";
    }

    // Prefix Windows reserved device names (CON, PRN, AUX, NUL, COM1-9, LPT1-9)
    const size_t dotPos = safe.find('.');
    const std::string upperStem = [&]() {
        std::string s = safe.substr(0, dotPos);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return s;
    }();
    static constexpr const char* kReservedNames[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    for (const char* reserved : kReservedNames) {
        if (upperStem == reserved) {
            safe.insert(safe.begin(), '_');
            break;
        }
    }
    return safe;
}

} // namespace Sandbox

#endif // SANDBOX_FILENAME_UTILS_H
