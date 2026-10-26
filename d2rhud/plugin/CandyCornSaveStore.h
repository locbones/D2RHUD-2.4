#pragma once
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <system_error>
#include <string>
#include <vector>

namespace CandyCornSave::Storage {
inline uint64_t Hash(std::span<const uint8_t> bytes) {
    uint64_t value=14695981039346656037ULL;
    for(auto byte:bytes)value=(value^byte)*1099511628211ULL;
    return value;
}
inline std::wstring Hex(uint64_t value) {
    wchar_t text[17]{};swprintf_s(text,L"%016llx",static_cast<unsigned long long>(value));return text;
}
// CREATE_NEW and a byte-for-byte comparison make retries safe. An existing
// backup is never replaced, even when a character has the same display name.
inline bool Backup(const std::filesystem::path& directory,std::span<const uint8_t> bytes,
    std::filesystem::path& path,std::string& error,bool stash=false) {
    HANDLE file=INVALID_HANDLE_VALUE;bool created=false;
    try {
        if(directory.empty() || bytes.empty() || bytes.size()>16*1024*1024)throw std::runtime_error("The original character backup is empty or too large.");
        std::filesystem::create_directories(directory);
        path=directory/(Hex(Hash(bytes))+L"-"+std::to_wstring(bytes.size())+(stash?L".before-candy-corn.d2i":L".before-candy-corn.d2s"));
        file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) {
            const auto failure=GetLastError();
            if(failure!=ERROR_FILE_EXISTS && failure!=ERROR_ALREADY_EXISTS)throw std::system_error(int(failure),std::system_category());
            file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file==INVALID_HANDLE_VALUE)throw std::system_error(int(GetLastError()),std::system_category());
            LARGE_INTEGER size{};
            if(!GetFileSizeEx(file,&size) || size.QuadPart!=int64_t(bytes.size()))throw std::runtime_error("The existing conversion backup does not match the original save.");
            std::vector<uint8_t> saved(bytes.size());DWORD count=0;
            if(!ReadFile(file,saved.data(),DWORD(saved.size()),&count,nullptr) || count!=saved.size() || !std::equal(saved.begin(),saved.end(),bytes.begin()))
                throw std::runtime_error("The existing conversion backup does not match the original save.");
        } else {
            created=true;DWORD count=0;
            if(!WriteFile(file,bytes.data(),DWORD(bytes.size()),&count,nullptr) || count!=bytes.size() || !FlushFileBuffers(file))
                throw std::system_error(int(GetLastError()),std::system_category());
        }
        CloseHandle(file);return true;
    } catch(const std::exception& failure) {
        error=failure.what();
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
        // This path belongs to this CREATE_NEW call; never delete a prior backup.
        if(created)DeleteFileW(path.c_str());
        return false;
    }
}
}
