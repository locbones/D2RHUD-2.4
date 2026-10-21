// The vcpkg CascLib ABI uses ANSI paths. Keep this translation unit in ANSI mode.
#undef UNICODE
#undef _UNICODE
#include "GameAssets.h"
#define CASCLIB_NO_AUTO_LINK_LIBRARY
#include <CascLib.h>
#include <fstream>
#include <algorithm>

GameAssets::GameAssets(const std::filesystem::path& install, const std::string& mod) : install_(install) {
    if (!mod.empty()) mod_ = install / "Mods" / mod / (mod + ".mpq");
}
GameAssets::~GameAssets() { if (storage_) CascCloseStorage(storage_); }

std::vector<uint8_t> GameAssets::Read(const std::string& relative) {
    std::string path = relative;
    std::replace(path.begin(), path.end(), '/', '\\');
    if (path.empty() || path.front() == '\\' || path.find(':') != std::string::npos) return {};
    for (auto& part : std::filesystem::path(path)) if (part == "..") return {};
    for (const auto& root : { mod_, install_ }) {
        if (root.empty()) continue;
        std::ifstream file(root / "data" / path, std::ios::binary | std::ios::ate);
        if (!file) continue;
        const auto size = file.tellg();
        if (size <= 0 || size > 64 * 1024 * 1024) return {};
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        file.seekg(0);
        if (file.read(reinterpret_cast<char*>(bytes.data()), size)) return bytes;
        return {};
    }
    if (!triedStorage_) {
        triedStorage_ = true;
        // vcpkg builds CascLib in ANSI mode, independently of the HUD's UNICODE setting.
        CascOpenStorage(install_.string().c_str(), CASC_LOCALE_ALL, &storage_);
    }
    if (!storage_) return {};
    for (const char* prefix : { "data:data\\", "data:data:", "data:", "data\\", "" }) {
        HANDLE file = nullptr;
        if (!CascOpenFile(storage_, (prefix + path).c_str(), CASC_LOCALE_ALL, CASC_OPEN_BY_NAME | CASC_STRICT_DATA_CHECK, &file)) continue;
        DWORD high = 0, size = CascGetFileSize(file, &high), read = 0;
        std::vector<uint8_t> bytes;
        if (!high && size && size != CASC_INVALID_SIZE && size <= 64 * 1024 * 1024) {
            bytes.resize(size);
            if (!CascReadFile(file, bytes.data(), size, &read) || read != size) bytes.clear();
        }
        CascCloseFile(file);
        return bytes;
    }
    return {};
}
