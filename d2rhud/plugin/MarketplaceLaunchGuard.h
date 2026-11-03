#pragma once
#include <Windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace MarketplaceLaunchGuard {
// Intentionally retained until process exit. No network work or destructor is
// attached to DLL unload; Windows releases the lease when the game terminates.
inline HANDLE gameLease=INVALID_HANDLE_VALUE;
inline bool Acquire(std::string& error){
    try{
        wchar_t local[32768]{};
        const auto n=GetEnvironmentVariableW(L"USERPROFILE",local,32768);
        if(!n||n>=32768)throw std::runtime_error("Cannot locate the marketplace enrollment.");
        const auto enrollment=std::filesystem::path(local)/L".d2r-marketplace/custody-enrollment.json";
        if(!std::filesystem::exists(enrollment))return true;
        // Keep gameplay independent of enrollment parsing, Python, network work,
        // binary pins and pending journals. Writers use this separate gate only
        // around local writes; their cloud transaction lock never delays launch.
        const auto gate=enrollment.parent_path()/L"game.lock";
        for(unsigned attempt=0;attempt<20;++attempt){
            gameLease=CreateFileW(gate.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(gameLease!=INVALID_HANDLE_VALUE)return true;
            if(GetLastError()!=ERROR_SHARING_VIOLATION)break;
            Sleep(25);
        }
        if(gameLease==INVALID_HANDLE_VALUE)throw std::runtime_error("Marketplace save writes are paused while the game is running.");
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
