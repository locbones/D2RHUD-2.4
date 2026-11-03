// The vcpkg CascLib ABI uses ANSI paths. Keep this translation unit in ANSI mode.
#undef UNICODE
#undef _UNICODE
#include "GameAssets.h"
#define CASCLIB_NO_AUTO_LINK_LIBRARY
#include <CascLib.h>
#include "../RuntimeTelemetry.h"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include "TableMetadata.h"

struct GameAssets::MetadataCache { std::map<std::filesystem::path,nlohmann::json> layers; };
std::filesystem::path GameAssets::ExecutableDirectory() {
    std::wstring buffer(260,L'\0');
    while(buffer.size()<=32768){
        const auto length=GetModuleFileNameW(nullptr,buffer.data(),DWORD(buffer.size()));
        if(!length)throw std::runtime_error("Could not locate game assets");
        if(length<buffer.size()){buffer.resize(length);return std::filesystem::path(buffer).parent_path();}
        buffer.resize((std::min)(size_t(32768),buffer.size()*2));
        if(length==32768)break;
    }
    throw std::runtime_error("Game path is too long");
}

GameAssets::GameAssets(const std::filesystem::path& install, const std::string& mod) : install_(install) {
    if (!mod.empty()) mod_ = install / "Mods" / mod / (mod + ".mpq");
}
GameAssets::~GameAssets() { if (storage_) CascCloseStorage(storage_); }

GameAssets::TableFiles GameAssets::ReadTable(const std::string& name, bool companionBinary) {
    if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos) return {};
    const auto relative = std::filesystem::path("global/excel") / name;
    auto loose = [](std::filesystem::path path, const char* extension) {
        path += extension;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        std::vector<uint8_t> bytes;
        if (!file) return bytes;
        const auto size = file.tellg();
        if (size <= 0 || size > 64 * 1024 * 1024) return bytes;
        bytes.resize(static_cast<size_t>(size));file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) bytes.clear();
        return bytes;
    };
    for (const auto& root : {mod_, install_}) {
        if (root.empty()) continue;
        TableFiles files;files.layer=root/"data";files.text = loose(root / "data" / relative, ".txt");
        if (files.text.empty() || companionBinary) files.binary = loose(root / "data" / relative, ".bin");
        if (!files.text.empty() || !files.binary.empty()) return files;
    }
    TableFiles files;files.text = Read(relative.generic_string() + ".txt");
    if (files.text.empty() || companionBinary) files.binary = Read(relative.generic_string() + ".bin");
    return files;
}

std::string GameAssets::MetadataField(const std::string& name,const TableFiles& files,const char* field) {
    if(files.layer.empty() || files.binary.empty())return {};
    if(!metadataCache_)metadataCache_=std::make_shared<MetadataCache>();
    auto [it,created]=metadataCache_->layers.try_emplace(files.layer);
    if(created){std::ifstream input(files.layer/"D2RHUD/table-metadata.json",std::ios::binary);if(input)it->second=nlohmann::json::parse(input,nullptr,false);}
    try {
        const nlohmann::json* entry=nullptr;
        if(name=="cubemain" && !strcmp(field,"source")){
            // Recipe descriptions and HUD annotations are not a fingerprint
            // of the whole cube table. Ordinary recipes change independently
            // of quests. Consumers validate the relevant native recipe fields.
            const auto& document=it->second;
            if(!document.is_object() || document.value("version",0)!=1 || !document.contains("tables") ||
               !document["tables"].is_object() || !document["tables"].contains(name))return {};
            entry=&document["tables"][name];if(!entry->is_object())return {};
        }else entry=TableMetadata::Entry(it->second,name,files.binary);
        if(!entry || !entry->contains(field))return {};
        if(!strcmp(field,"names") || !strcmp(field,"labels"))return (*entry)[field].is_array()?(*entry)[field].dump():std::string{};
        if(!strcmp(field,"id_base"))return (*entry)[field].is_number_unsigned()||(*entry)[field].is_number_integer()?(*entry)[field].dump():std::string{};
        return (*entry)[field].is_string()?(*entry)[field].get<std::string>():std::string{};
    }catch(...){return {};}
}
std::vector<std::string> GameAssets::ReadTableNames(const std::string& name, bool preferBinary) {
    const auto files=ReadTable(name,true);
    if(!files.text.empty() && !(preferBinary && !files.binary.empty())){
        auto names=TableMetadata::Names(files.text,name);
        if(name=="treasureclassex")try{const auto base=MetadataField(name,files,"id_base");if(!base.empty()){const auto n=std::stoul(base);if(n<=65535)names.insert(names.begin(),n,"");}}catch(...){}
        return names;
    }
    const auto names=MetadataField(name,files,"names");
    try {if(!names.empty()){
        auto result=nlohmann::json::parse(names).get<std::vector<std::string>>();
        if(name=="treasureclassex"){const auto base=MetadataField(name,files,"id_base");if(base.empty())return {};const auto n=std::stoul(base);if(n>65535)return {};result.insert(result.begin(),n,"");}
        return result;
    }}catch(...){}
    return {};
}
std::vector<std::string> GameAssets::ReadTableLabels(const std::string& name) {
    const auto files=ReadTable(name,true);const auto text=MetadataField(name,files,"labels");
    try{if(!text.empty())return nlohmann::json::parse(text).get<std::vector<std::string>>();}catch(...){}
    return {};
}
std::string GameAssets::ReadTableSource(const std::string& name, bool preferBinary) {
    const auto files=ReadTable(name,preferBinary);
    if(!files.text.empty() && !(preferBinary && !files.binary.empty()))return {files.text.begin(),files.text.end()};
    return MetadataField(name,files,"source");
}
std::string GameAssets::ReadTableOverlay(const std::string& name) {
    const auto files=ReadTable(name);
    if(!files.text.empty())return {files.text.begin(),files.text.end()};
    return MetadataField(name,files,"overlay");
}
std::string GameAssets::ReadAuxiliaryTable(const std::string& name) {
    if(name!="transmog_table")return {};
    for(const auto& root:{mod_,install_}){
        if(root.empty())continue;
        std::ifstream text(root/"data/global/excel"/(name+".txt"),std::ios::binary);
        if(text)return {std::istreambuf_iterator<char>(text),{}};
        std::ifstream json(root/"data/D2RHUD"/(name+".json"),std::ios::binary);
        if(json)try{const auto config=nlohmann::json::parse(json);return config.value("version",0)==1?config.value("source",std::string{}):std::string{};}catch(...){return {};}
    }
    auto text=Read("global/excel/"+name+".txt");if(!text.empty())return {text.begin(),text.end()};
    auto bytes=Read("D2RHUD/"+name+".json");
    try {const auto config=nlohmann::json::parse(bytes.begin(),bytes.end());if(config.value("version",0)==1)return config.value("source",std::string{});}catch(...){}
    return {};
}

namespace {
std::string StoragePath(const std::filesystem::path& path) {
    // The linked CascLib uses CreateFileA. Avoid lossy/best-fit replacements in
    // user/install paths, and use an existing short path when necessary.
    auto convert = [](const std::wstring& text) {
        const auto page = GetACP();
        BOOL replaced = FALSE;
        const auto flags = page == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
        auto used = page == CP_UTF8 ? nullptr : &replaced;
        const auto size = WideCharToMultiByte(page, flags, text.c_str(), int(text.size()), nullptr, 0, nullptr, used);
        if (!size || replaced)
            return std::string{};
        std::string result(size, '\0');
        if (!WideCharToMultiByte(page, flags, text.c_str(), int(text.size()), result.data(), size, nullptr, used) || replaced)
            return std::string{};
        return result;
    };
    if (auto normal = convert(path.wstring()); !normal.empty())
        return normal;
    std::wstring shortPath(32768, '\0');
    const auto length = GetShortPathNameW(path.c_str(), shortPath.data(), DWORD(shortPath.size()));
    if (!length || length >= shortPath.size())
        return {};
    shortPath.resize(length);
    return convert(shortPath);
}
}
std::vector<uint8_t> GameAssets::Read(const std::string& relative) {
    HudTelemetry::Scope measured(HudTelemetry::Feature::AssetRead);
    readError_.clear();
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
        std::error_code ec;
        const auto resolved = std::filesystem::weakly_canonical(install_, ec);
        std::vector<std::filesystem::path> roots{install_};
        if (!ec && resolved != install_)
            roots.push_back(resolved);
        roots.push_back(install_ / "Data");
        DWORD error = ERROR_PATH_NOT_FOUND;
        for (const auto& root : roots) {
            const auto ansi = StoragePath(root);
            if (ansi.empty()) {
                error = ERROR_NO_UNICODE_TRANSLATION;
                continue;
            }
            if (CascOpenStorage(ansi.c_str(), CASC_LOCALE_ALL, &storage_))
                break;
            error = GetLastError();
        }
        if (!storage_)
            readError_ = "Packed game storage could not be opened (Windows error " + std::to_string(error) + ").";
    }
    if (!storage_) {
        if (readError_.empty())
            readError_ = "Packed game storage is unavailable in this asset reader.";
        return {};
    }
    for (const char* prefix : { "data:data\\", "data:data:", "data:", "data\\", "" }) {
        HANDLE file = nullptr;
        if (!CascOpenFile(storage_, (prefix + path).c_str(), CASC_LOCALE_ALL, CASC_OPEN_BY_NAME | CASC_STRICT_DATA_CHECK, &file)) continue;
        DWORD high = 0, size = CascGetFileSize(file, &high), read = 0;
        std::vector<uint8_t> bytes;
        if (!high && size && size != CASC_INVALID_SIZE && size <= 64 * 1024 * 1024) {
            bytes.resize(size);
            if (!CascReadFile(file, bytes.data(), size, &read) || read != size) bytes.clear();
        }
        const auto error = GetLastError();
        CascCloseFile(file);
        if (!bytes.empty())
            return bytes;
        readError_ = "Packed asset could not be read (Windows error " + std::to_string(error) + ").";
    }
    if (readError_.empty())
        readError_ = "Asset not found in loose mod files or packed game storage.";
    return {};
}
