#include "../d2rhud/plugin/MarketplaceLaunchGuard.h"
#include <cassert>
#include <fstream>
#include <iostream>
int wmain(int argc,wchar_t** argv){
    assert(argc==2);const auto root=std::filesystem::absolute(argv[1]);
    const auto profile=root/L".d2r-marketplace";std::filesystem::create_directories(profile);
    SetEnvironmentVariableW(L"USERPROFILE",root.c_str());std::string error;
    const auto enrollment=profile/L"custody-enrollment.json",gate=profile/L"game.lock";
    assert(!std::filesystem::exists(enrollment));assert(MarketplaceLaunchGuard::Acquire(error));
    std::ofstream(enrollment)<<"{broken";
    assert(MarketplaceLaunchGuard::Acquire(error));
    auto writer=[&](){return CreateFileW(gate.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);};
    auto busy=writer();assert(busy==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_SHARING_VIOLATION);
    auto handover=CreateFileW(gate.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    assert(handover!=INVALID_HANDLE_VALUE);CloseHandle(handover);
    CloseHandle(MarketplaceLaunchGuard::gameLease);MarketplaceLaunchGuard::gameLease=INVALID_HANDLE_VALUE;
    std::filesystem::create_directories(profile/L"custody");
    std::ofstream(profile/L"custody/pending.json")<<"{}";
    auto cloud=CreateFileW((profile/L"custody/guard.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    assert(cloud!=INVALID_HANDLE_VALUE);assert(MarketplaceLaunchGuard::Acquire(error));CloseHandle(cloud);
    CloseHandle(MarketplaceLaunchGuard::gameLease);MarketplaceLaunchGuard::gameLease=INVALID_HANDLE_VALUE;
    busy=writer();assert(busy!=INVALID_HANDLE_VALUE);assert(!MarketplaceLaunchGuard::Acquire(error));CloseHandle(busy);
    assert(MarketplaceLaunchGuard::Acquire(error));CloseHandle(MarketplaceLaunchGuard::gameLease);MarketplaceLaunchGuard::gameLease=INVALID_HANDLE_VALUE;
    std::cout<<"Native gameplay lease: corrupt enrollment, pending transfers, cloud writer independence, shared handover and save-write exclusion passed.\n";
}
