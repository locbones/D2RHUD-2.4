#pragma once
#include "../Pattern.h"
#include <Windows.h>
#include <atomic>
#include <memory>
#include <map>
#include <unordered_map>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace CompiledNames {
using Names=std::unordered_map<std::string,int>;
using Database=std::map<uint32_t,Names>;
inline std::atomic<std::shared_ptr<const Database>> database;
inline std::string status="Table symbols are not initialized.";
inline const char* Status(){return status.c_str();}
inline std::string Fold(std::string name){std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});return name;}
void Load(const std::filesystem::path& install,const std::string& mod);
inline int Resolve(uint32_t global,const char* name) {
    if(!name || !*name)return -1;
    uintptr_t linker=0;SIZE_T read=0;
    const auto address=Pattern::Address(global);
    if(address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&linker,sizeof(linker),&read) && read==sizeof(linker) && linker){
        const auto lookup=reinterpret_cast<int(__fastcall*)(void*,const char*)>(Pattern::Address(0x6d0470));
        if(lookup){const int id=lookup(reinterpret_cast<void*>(linker),name);if(id>=0)return id;}
    }
    const auto snapshot=database.load();if(!snapshot)return -1;
    const auto table=snapshot->find(global);if(table==snapshot->end())return -1;
    const auto entry=table->second.find(Fold(name));return entry==table->second.end()?-1:entry->second;
}
}
