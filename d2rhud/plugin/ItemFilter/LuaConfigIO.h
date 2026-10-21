#pragma once
#include "LuaConfigDocument.h"
#include <lua.hpp>
#include <Windows.h>

namespace LootFilterConfig {
inline bool ValidateSyntax(const std::string& source, std::string& error) {
    lua_State* state = luaL_newstate();
    if (!state) { error = "Unable to create Lua validator"; return false; }
    const size_t bom = source.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    const int status = luaL_loadbufferx(state, source.data() + bom, source.size() - bom, "lootfilter_config.lua", "t");
    if (status) error = lua_tostring(state, -1);
    lua_close(state);
    return status == LUA_OK;
}

inline std::string DecodeString(const std::string& expression) {
    Document probe;
    if (!probe.IsString(expression)) return expression;
    lua_State* state = luaL_newstate();
    if (!state) return expression;
    std::string result = expression, code = "return " + expression;
    // The lexer proved this is exactly one literal, so no config code can run here.
    if (!luaL_loadbufferx(state, code.data(), code.size(), "literal", "t") && !lua_pcall(state, 0, 1, 0)) {
        size_t size = 0;
        if (const char* text = lua_tolstring(state, -1, &size)) result.assign(text, size);
    }
    lua_close(state);
    return result;
}

inline bool Save(const std::string& path, const std::string& expected, const std::string& source, std::string& error) {
    if (!ValidateSyntax(source, error)) return false;
    std::string disk;
    if (!Read(path, disk) || disk != expected) {
        error = "Config changed on disk. Reload it before saving your draft."; return false;
    }
    if (source == expected) return true;
    const auto target = std::filesystem::u8path(path);
    auto temporary = target;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(source.data(), std::streamsize(source.size())) || !file.flush()) {
            error = "Unable to write temporary config";
            std::error_code ec; std::filesystem::remove(temporary, ec); return false;
        }
    }
    auto backup = target; backup += L".bak";
    // ReplaceFile preserves the old file as .bak and atomically replaces the target.
    // Both paths are beside the config, on the same filesystem.
    DeleteFileW(backup.c_str());
    if (!ReplaceFileW(target.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr)) {
        error = "Unable to replace config (Windows error " + std::to_string(GetLastError()) + ")";
        std::error_code ec; std::filesystem::remove(temporary, ec); return false;
    }
    return true;
}
}
