#pragma once
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

namespace HudHotkeys {
inline short VirtualKey(std::string token, const std::unordered_map<std::string, short>& names) {
    std::transform(token.begin(), token.end(), token.begin(),
                   [](unsigned char c) { return char(std::toupper(c)); });
    if (token == "CTRL" || token == "CONTROL" || token == "VK_CTRL" || token == "VK_CONTROL")
        return VK_CONTROL;
    if (token == "SHIFT" || token == "VK_SHIFT")
        return VK_SHIFT;
    if (token == "ALT" || token == "MENU" || token == "VK_ALT" || token == "VK_MENU")
        return VK_MENU;
    // Human-readable defaults and editor-produced VK_* names are equivalent.
    if (token.size() == 1 && ((token[0] >= 'A' && token[0] <= 'Z') || (token[0] >= '0' && token[0] <= '9')))
        return short(token[0]);
    if (token.size() >= 2 && token[0] == 'F') {
        int number = 0;
        for (size_t i = 1; i < token.size(); ++i) {
            if (token[i] < '0' || token[i] > '9')
                return 0;
            number = number * 10 + token[i] - '0';
            if (number > 24)
                return 0;
        }
        if (number >= 1)
            return short(VK_F1 + number - 1);
    }
    // Mouse aliases were accepted by the HUD before the shared parser.
    const auto found = names.find(token);
    if (found != names.end())
        return found->second;
    if (!token.starts_with("VK_"))
        if (const auto alias = names.find("VK_" + token); alias != names.end())
            return alias->second;
    return 0;
}
} // namespace HudHotkeys
