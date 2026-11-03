#pragma once
#include <Windows.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include "../json.hpp"

// No game pointers, native inventory writes, network requests or worker threads.
// The existing client/server page-change handshake proves that the trade pages
// are unloaded. A separate, short file lease arbitrates access to those pages.
namespace Marketplace::LiveCustody {
using Json=nlohmann::json;
// The redundant REX prefix (0x40) is part of BankPanel::OnOpen's bytes.
// Disassembly displays only `push rdi`; compatibility must match the bytes.
inline constexpr std::array<uint8_t,8> BankOpenCode{0x40,0x57,0x48,0x83,0xec,0x40,0x48,0x8b};
inline constexpr std::array<uint8_t,8> BankCloseCode{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c};
inline Json Read(const std::filesystem::path& path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file||file.tellg()<0||file.tellg()>1024*1024)throw std::runtime_error("Live custody input is unavailable.");
    file.seekg(0);Json value;file>>value;return value;
}
inline void Write(const std::filesystem::path& path,const Json& value){
    auto temporary=path;temporary+=L".hud-"+std::to_wstring(GetCurrentProcessId())+L".new";
    const auto bytes=value.dump();
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot publish the live custody handoff.");
    DWORD written=0;const bool ok=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);
    CloseHandle(file);
    if(!ok||!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(temporary.c_str());throw std::runtime_error("Cannot commit the live custody handoff.");
    }
}
inline uint64_t Started(){FILETIME created{},exit{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user))return 0;return (uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;}
struct View {bool enabled=false,parked=false;unsigned deposit=0,delivery=0,parking=0;std::string status;};
class Window {
    std::mutex mutex;
    std::filesystem::path root,folder;
    std::string owner,catalog,epoch,status;
    unsigned deposit=0,delivery=0,parking=0,restore=0,loaded=0;
    uint32_t player=0;
    bool hardcore=false,enabled=false,parked=false,passive=false;
    uint64_t began=0,published=0,sequence=0;
    HANDLE access=INVALID_HANDLE_VALUE;
    bool Shared(){
        if(access!=INVALID_HANDLE_VALUE)return true;
        access=CreateFileW((root/L"live.lock").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        return access!=INVALID_HANDLE_VALUE;
    }
    void Revoke(){
        if(parked){
            const auto path=root/L"live-window.json";
            try{if(Read(path).value("session",std::string())==epoch)DeleteFileW(path.c_str());}catch(...){}
        }
        parked=false;
    }
    bool Done(uint64_t now){
        try{const auto result=Read(root/L"live-result.json");if(result.value("session",std::string())==epoch&&result.value("terminal",false))return true;}catch(...){}
        return now>=began&&now-began>=5000;
    }
    bool Publish(uint64_t now){
        try{
            Write(root/L"live-window.json",{{"schema",passive?2:1},{"session",epoch},{"phase",passive?"unloaded":"parked"},{"pid",GetCurrentProcessId()},
                {"started",Started()},{"tick",now},{"owner",owner},{"catalog",catalog},{"player",player},{"hardcore",hardcore},
                {"deposit",deposit},{"delivery",delivery},{"parking",parking},{"loaded",loaded},{"save_directory",folder.string()}});
            published=now;return true;
        }catch(const std::exception& e){status=e.what();return false;}
    }
    void Wake(){
        const auto text=std::wstring(owner.begin(),owner.end());
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,(L"Local\\D2RMarketplaceWake-"+text).c_str());
        if(event){SetEvent(event);CloseHandle(event);}
    }
public:
    bool Configure(const std::filesystem::path& enrollment,const std::filesystem::path& saveFolder,uint32_t unit,bool hc,unsigned page){
        std::lock_guard lock(mutex);
        try{
            const auto cfg=Read(enrollment);
            if(cfg.at("schema")!=1||cfg.at("enrolled")!=true||!unit||page<1||page>64)return false;
            const auto state=std::filesystem::path(cfg.at("state_directory").get<std::string>());
            const auto saves=std::filesystem::path(cfg.at("save_directory").get<std::string>());
            if(!state.is_absolute()||!saves.is_absolute()||_wcsicmp(saves.lexically_normal().c_str(),saveFolder.lexically_normal().c_str()))return false;
            const auto capability=Read(state/L"live-capability.json");
            if(capability.value("schema",0)!=1||capability.value("owner",std::string())!=cfg.at("owner").get<std::string>()||capability.value("catalog",std::string())!=cfg.at("catalog").get<std::string>())return false;
            const auto settings=Read(saves/(hc?L"Marketplace_2.4_HC.json":L"Marketplace_2.4_SC.json"));
            const auto in=settings.at("page").get<unsigned>(),out=settings.at("withdraw_page").get<unsigned>();
            if(settings.at("mod")!="RMD-MP"||settings.at("game_mode")!="2.4"||settings.at("hardcore")!=hc||!in||in>64||!out||out>64||in==out)return false;
            if(enabled&&(player!=unit||hardcore!=hc||deposit!=in||delivery!=out||root!=state)){
                if(!Shared())return false;Revoke();
            }
            root=state;folder=saves;owner=cfg.at("owner");catalog=cfg.at("catalog");
            player=unit;hardcore=hc;deposit=in;delivery=out;loaded=page;
            parking=1;while(parking==deposit||parking==delivery)++parking;
            enabled=true;if(!parked&&!Shared())return false;return true;
        }catch(...){return false;}
    }
    bool Protected(unsigned page){std::lock_guard lock(mutex);return enabled&&(page==deposit||page==delivery);}
    std::optional<std::filesystem::path> PageFile(unsigned page){std::lock_guard lock(mutex);if(!enabled||(page!=deposit&&page!=delivery))return {};return folder/(std::wstring(L"Stash_")+(hardcore?L"HC":L"SC")+L"_Page"+std::to_wstring(page)+L".d2i");}
    void Error(const std::string& value){std::lock_guard lock(mutex);status=value;}
    unsigned Close(unsigned page,bool cursor){
        std::lock_guard lock(mutex);if(!enabled||cursor)return 0;
        restore=page;loaded=page;
        if(page==deposit||page==delivery)return parking;
        return 0;
    }
    bool OpenGrant(unsigned page,bool pageOnly=false){
        std::lock_guard lock(mutex);
        if(!enabled||(!pageOnly&&(page==deposit||page==delivery))||page<1||page>64)return false;
        // Called only after the native page-load acknowledgement (or on opening
        // the already loaded stash). Never release the lease during a switch.
        if(parked&&loaded==page&&passive==pageOnly)return true;
        if(!Shared())return false;
        Revoke();loaded=page;passive=pageOnly;
        epoch=std::to_string(GetCurrentProcessId())+"-"+std::to_string(Started())+"-"+std::to_string(++sequence);
        began=GetTickCount64();parked=true;
        if(!Publish(began)){parked=false;return false;}
        if(access!=INVALID_HANDLE_VALUE){CloseHandle(access);access=INVALID_HANDLE_VALUE;}
        status=pageOnly?std::string():"Trading sync in progress. Other stash pages remain available.";Wake();return true;
    }
    bool Permit(unsigned page,bool waitForPass=true){
        std::lock_guard lock(mutex);if(!enabled)return true;
        if(!Shared()){status="Finishing a stash update...";return false;}
        if(page==deposit||page==delivery){
            std::error_code error;
            const bool pending=std::filesystem::exists(root/L"pending.json",error);
            if(error||pending||(parked&&!passive&&waitForPass&&!Done(GetTickCount64()))){
                CloseHandle(access);access=INVALID_HANDLE_VALUE;
                status="Trade sync pending. Your other stash pages are available.";return false;
            }
        }
        Revoke();loaded=page;status.clear();return true;
    }
    unsigned Restore(){std::lock_guard lock(mutex);return enabled&&!passive?restore:0;}
    void Tick(bool inGame){
        std::lock_guard lock(mutex);if(!enabled)return;
        if(!inGame){if(Shared()){Revoke();enabled=false;}return;}
        const auto now=GetTickCount64();if(parked&&now>=published&&now-published>=1000)Publish(now);
    }
    View Snapshot(){std::lock_guard lock(mutex);return {enabled,parked,deposit,delivery,parking,status};}
};
inline Window window;
}
