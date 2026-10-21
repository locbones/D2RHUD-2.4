// The vcpkg CascLib ABI uses ANSI paths. Keep this translation unit in ANSI mode.
#undef UNICODE
#undef _UNICODE
#include "GameAssets.h"
#define CASCLIB_NO_AUTO_LINK_LIBRARY
#include <CascLib.h>
#include <fstream>
#include <algorithm>
#include <cstring>
#include "TableMetadata.h"

struct GameAssets::MetadataCache { std::map<std::filesystem::path,nlohmann::json> layers; };

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
        const auto* entry=TableMetadata::Entry(it->second,name,files.binary);
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
