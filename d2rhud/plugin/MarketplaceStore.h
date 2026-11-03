#pragma once
#include "MarketplaceData.h"
#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <fstream>

namespace Marketplace {
inline std::vector<uint8_t> ReadFile(const std::filesystem::path& path,size_t maximum=8*1024*1024) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("Could not open marketplace input file.");
    const auto size=file.tellg();
    if(size<0 || uint64_t(size)>maximum)throw std::runtime_error("Marketplace input file exceeds its size limit.");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));file.seekg(0);
    if(!bytes.empty()&&!file.read(reinterpret_cast<char*>(bytes.data()),size))throw std::runtime_error("Could not read marketplace input file.");
    return bytes;
}
inline std::string ReadSettingsFile(const std::filesystem::path& path) {
    if(!std::filesystem::exists(path))return {};
    const auto bytes=ReadFile(path,16*1024*1024);return {bytes.begin(),bytes.end()};
}
// Compare-and-replace under a per-file writer lease. Only the sidecar JSON is
// writable here. No code in this milestone opens a .d2i/.d2s for writing.
inline void SaveSettingsFile(const std::filesystem::path& path,const std::string& expected,const std::string& next) {
    if(path.extension()!=L".json")throw std::runtime_error("Marketplace preferences must be a JSON sidecar.");
    if(next.size()>16*1024*1024)throw std::runtime_error("Marketplace preferences exceed the size limit.");
    auto leasePath=path;leasePath+=L".lock";
    HANDLE lease=CreateFileW(leasePath.c_str(),GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(lease==INVALID_HANDLE_VALUE)throw std::runtime_error("Another game instance is saving marketplace settings.");
    struct Close {HANDLE h;~Close(){CloseHandle(h);}} closeLease{lease};
    if(ReadSettingsFile(path)!=expected)throw std::runtime_error("Marketplace settings changed in another instance. Reload before saving.");
    if(next==expected)return;
    static std::atomic<unsigned> sequence{0};auto temporary=path;
    temporary+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(++sequence)+L".tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Could not create marketplace settings temporary file.");
    DWORD written=0;const bool durable=WriteFile(file,next.data(),DWORD(next.size()),&written,nullptr)&&written==next.size()&&FlushFileBuffers(file);
    CloseHandle(file);
    if(!durable){DeleteFileW(temporary.c_str());throw std::runtime_error("Could not flush marketplace settings.");}
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(temporary.c_str());throw std::runtime_error("Could not replace marketplace settings.");
    }
}
}
