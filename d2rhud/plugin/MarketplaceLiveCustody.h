#pragma once
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include "../json.hpp"
#include "MarketplaceStore.h"

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
inline void Write(const std::filesystem::path& path,const Json& value,bool durable=true){
    auto temporary=path;temporary+=L".hud-"+std::to_wstring(GetCurrentProcessId())+L".new";
    const auto bytes=value.dump();
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot publish the live custody handoff.");
    DWORD written=0;const bool ok=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&(!durable||FlushFileBuffers(file));
    CloseHandle(file);
    if(!ok||!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|(durable?MOVEFILE_WRITE_THROUGH:0))){
        DeleteFileW(temporary.c_str());throw std::runtime_error("Cannot commit the live custody handoff.");
    }
}
inline uint64_t Started(){FILETIME created{},exit{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user))return 0;return (uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;}
struct View {bool enabled=false,parked=false;unsigned deposit=0,delivery=0,parking=0;std::string status;};
class Window {
    std::mutex mutex;
    std::filesystem::path root,folder;
    std::string owner,catalog,epoch,status,syncHint;
    unsigned deposit=0,delivery=0,parking=0,restore=0,loaded=0;
    uint32_t player=0;
    bool hardcore=false,enabled=false,parked=false,passive=false;
    uint64_t began=0,published=0,sequence=0,syncPoll=0;
    HANDLE access=INVALID_HANDLE_VALUE;
    std::string refreshId,lastRefreshId,refreshKind;
    Json goldDeposit;
    unsigned refreshPage=0,refreshTab=0;
    uint64_t refreshBegan=0,refreshPoll=0;
    struct Stamp {std::filesystem::file_time_type time;uintmax_t size;bool operator==(const Stamp&) const=default;};
    std::optional<std::array<Stamp,3>> configuration;
    std::filesystem::path configuredEnrollment;
    static Stamp FileStamp(const std::filesystem::path& path){return {std::filesystem::last_write_time(path),std::filesystem::file_size(path)};}

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
    bool Pending(unsigned page){
        std::error_code error;if(!std::filesystem::exists(root/L"pending.json",error))return bool(error);
        try{const auto pending=Read(root/L"pending.json");
            // Legacy/invalid journals remain conservatively blocked. New
            // journals describe the exact page(s), not the entire stash.
            const auto id=pending.at("id").get<std::string>(),mode=pending.at("mode").get<std::string>();
            if(id.size()!=32||id.find_first_not_of("0123456789abcdef")!=std::string::npos||(mode!="SC"&&mode!="HC"))return true;
            const auto& pages=pending.at("pages");if(!pages.is_array()||pages.empty()||pages.size()>2)return true;
            bool affected=false;for(const auto& p:pages){if(!p.is_number_unsigned())return true;const auto n=p.get<unsigned>();if(n<1||n>64)return true;affected|=n==page;}
            if(mode!=(hardcore?"HC":"SC")||!affected)return false;
            if(pending.value("reload_safe",false)){
                const auto record=Read(root/L"transactions"/id/L"record.json");
                if(record.value("id",std::string())==id&&record.value("mode",std::string())==mode&&
                    (record.value("phase",std::string())=="applied"||record.value("phase",std::string())=="complete"))return false;
            }
        }catch(...){}return true;
    }
    bool Publish(uint64_t now){
        try{
            Write(root/L"live-window.json",{{"schema",passive?2:1},{"session",epoch},{"phase",passive?"unloaded":"parked"},{"pid",GetCurrentProcessId()},
                {"started",Started()},{"tick",now},{"owner",owner},{"catalog",catalog},{"player",player},{"hardcore",hardcore},
                {"deposit",deposit},{"delivery",delivery},{"parking",parking},{"loaded",loaded},{"save_directory",folder.string()},
                {"refresh_supported",2},{"listing_deposit_refresh",1},{"refresh_id",refreshId},{"refresh_kind",refreshKind},{"gold_deposit",goldDeposit}},false);
            published=now;return true;
        }catch(const std::exception& e){status=e.what();return false;}
    }
    void Wake(){
        const auto text=std::wstring(owner.begin(),owner.end());
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,(L"Local\\D2RMarketplaceWake-"+text).c_str());
        if(event){SetEvent(event);CloseHandle(event);}
    }
public:
    ~Window(){if(access!=INVALID_HANDLE_VALUE)CloseHandle(access);}
    bool RequestSync(uint64_t now){
        std::lock_guard lock(mutex);if(!enabled)return false;
        try{
            const auto path=root/L"sync-request.json";
            try{const auto old=Read(path);const auto at=old.value("tick",uint64_t(0));if(now>=at&&now-at<5000){Wake();return true;}}catch(...){}
            std::array<unsigned char,16> random{};
            if(BCryptGenRandom(nullptr,random.data(),ULONG(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return false;
            std::string id;for(auto b:random){id+="0123456789abcdef"[b>>4];id+="0123456789abcdef"[b&15];}
            const auto at=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            Write(path,{{"schema",1},{"id",id},{"mode",hardcore?"HC":"SC"},{"at",at},{"tick",now}});
            Wake();return true;
        }catch(...){return false;}
    }
    bool Configure(const std::filesystem::path& enrollment,const std::filesystem::path& saveFolder,uint32_t unit,bool hc,unsigned page){
        std::lock_guard lock(mutex);
        try{
            if(!unit||page<1||page>64)return false;
            const auto settingsPath=saveFolder/(hc?L"Marketplace_2.4_HC.json":L"Marketplace_2.4_SC.json");
            if(enabled&&configuredEnrollment==enrollment&&folder==saveFolder&&player==unit&&hardcore==hc&&loaded==page&&configuration){
                const std::array stamps{FileStamp(enrollment),FileStamp(root/L"live-capability.json"),FileStamp(settingsPath)};
                if(stamps==*configuration)return parked||Shared();
            }
            const auto enrollmentStamp=FileStamp(enrollment);const auto cfg=Read(enrollment);
            if(cfg.at("schema")!=1||cfg.at("enrolled")!=true||!unit||page<1||page>64)return false;
            const auto state=std::filesystem::path(cfg.at("state_directory").get<std::string>());
            const auto saves=std::filesystem::path(cfg.at("save_directory").get<std::string>());
            if(!state.is_absolute()||!saves.is_absolute()||_wcsicmp(saves.lexically_normal().c_str(),saveFolder.lexically_normal().c_str()))return false;
            const auto capabilityStamp=FileStamp(state/L"live-capability.json");const auto capability=Read(state/L"live-capability.json");
            if(capability.value("schema",0)!=1||capability.value("owner",std::string())!=cfg.at("owner").get<std::string>()||capability.value("catalog",std::string())!=cfg.at("catalog").get<std::string>())return false;
            // Initialize JSON preferences under Setup's CAS lease. No game-save
            // edit, replacement of player choices or automatic trading opt-in.
            if(!std::filesystem::exists(settingsPath)){
                if(std::filesystem::exists(state/L"pending.json"))return false;
                SaveSettingsFile(settingsPath,"",SettingsJson(Settings{},"RMD-MP",hc).dump(2)+"\n");
            }
            const auto settingsStamp=FileStamp(settingsPath);const auto settings=Read(settingsPath);
            const auto in=settings.at("page").get<unsigned>(),out=settings.at("withdraw_page").get<unsigned>();
            if(settings.at("mod")!="RMD-MP"||settings.at("game_mode")!="2.4"||settings.at("hardcore")!=hc||!in||in>64||!out||out>64)return false;
            const std::array stamps{enrollmentStamp,capabilityStamp,settingsStamp};
            if(stamps!=std::array{FileStamp(enrollment),FileStamp(state/L"live-capability.json"),FileStamp(settingsPath)})return false;
            if(enabled&&(player!=unit||hardcore!=hc||deposit!=in||delivery!=out||root!=state||owner!=cfg.at("owner").get<std::string>()||catalog!=cfg.at("catalog").get<std::string>())){
                if(!Shared())return false;Revoke();
            }
            root=state;folder=saves;owner=cfg.at("owner");catalog=cfg.at("catalog");
            player=unit;hardcore=hc;deposit=in;delivery=out;loaded=page;
            parking=1;while(parking==deposit||parking==delivery)++parking;
            enabled=true;if(!parked&&!Shared())return false;
            configuredEnrollment=enrollment;configuration=stamps;
            return true;
        }catch(...){return false;}
    }
    unsigned RefreshPage(){std::lock_guard lock(mutex);return refreshPage;}
    unsigned RefreshTab(){std::lock_guard lock(mutex);return refreshTab;}
    bool Refreshing(){std::lock_guard lock(mutex);return refreshPage!=0;}
    bool RequestGoldDeposit(unsigned page,unsigned tab,uint64_t carriedBefore,uint64_t carriedAfter,uint64_t stashBefore,uint64_t stashAfter,uint64_t now){
        std::lock_guard lock(mutex);
        if(!enabled||refreshPage||page!=deposit||page!=loaded||!parked||!passive||tab<1||tab>7||
           carriedBefore>INT32_MAX||carriedAfter>=carriedBefore||stashAfter<=stashBefore||stashAfter>uint64_t(INT32_MAX)*7||
           carriedBefore-carriedAfter!=stashAfter-stashBefore)return false;
        try{
            if(Read(root/L"live-capability.json").value("gold_deposit_refresh",0)!=1)return false;
            const auto settings=Read(folder/(hardcore?L"Marketplace_2.4_HC.json":L"Marketplace_2.4_SC.json"));
            if(!settings.value("enabled",false)||settings.at("page")!=page)return false;
            std::array<unsigned char,16> random{};
            if(BCryptGenRandom(nullptr,random.data(),ULONG(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return false;
            std::string id;for(auto b:random){id+="0123456789abcdef"[b>>4];id+="0123456789abcdef"[b&15];}
            Json proof={{"carried_before",carriedBefore},{"carried_after",carriedAfter},{"stash_before",stashBefore},{"stash_after",stashAfter}};
            Write(root/L"live-refresh-request.json",{{"schema",1},{"kind","gold_deposit"},{"id",id},{"page",page},{"tab",tab},{"tick",now},
                {"owner",owner},{"catalog",catalog},{"pid",GetCurrentProcessId()},{"started",Started()},{"player",player},{"hardcore",hardcore},
                {"session",epoch},{"gold_deposit",proof}},false);
            refreshPoll=0;Wake();return true;
        }catch(...){return false;}
    }
    unsigned BeginRefresh(unsigned page,unsigned tab,uint64_t now){
        std::lock_guard lock(mutex);if(!enabled||refreshPage||tab>7||page!=loaded||!parked||!passive||now<refreshPoll)return 0;
        refreshPoll=now+500;
        try{const auto r=Read(root/L"live-refresh-request.json");const auto id=r.at("id").get<std::string>();
            const auto tick=r.at("tick").get<uint64_t>();const auto kind=r.value("kind",std::string());
            if(r.value("schema",0)!=1||(kind!="delivery"&&kind!="gold_deposit"&&kind!="listing_deposit")||id.size()!=32||id.find_first_not_of("0123456789abcdef")!=std::string::npos||id==lastRefreshId||
                r.at("owner")!=owner||r.at("catalog")!=catalog||r.at("pid")!=GetCurrentProcessId()||r.at("started")!=Started()||
                r.at("player")!=player||r.at("hardcore")!=hardcore||r.at("page")!=page||r.at("session")!=epoch||
                (page!=deposit&&page!=delivery)||now<tick||now-tick>15000)return 0;
            if(page!=(kind=="delivery"?delivery:deposit))return 0;
            Json proof;
            if(kind=="gold_deposit"){
                proof=r.at("gold_deposit");
                for(const auto key:{"carried_before","carried_after","stash_before","stash_after"})if(!proof.at(key).is_number_unsigned())return 0;
                const auto cb=proof.at("carried_before").get<uint64_t>(),ca=proof.at("carried_after").get<uint64_t>(),sb=proof.at("stash_before").get<uint64_t>(),sa=proof.at("stash_after").get<uint64_t>();
                if(cb>INT32_MAX||ca>=cb||sa<=sb||sa>uint64_t(INT32_MAX)*7||cb-ca!=sa-sb||r.at("tab")!=tab)return 0;
            }
            refreshKind=kind;goldDeposit=std::move(proof);
            refreshId=id;lastRefreshId=id;refreshPage=page;refreshTab=tab;refreshBegan=now;restore=page;
            return parking;
        }catch(...){return 0;}
    }
    bool RefreshReady(uint64_t now){
        std::lock_guard lock(mutex);if(!refreshPage||loaded!=parking||!parked)return false;
        if(!Shared())return false;
        // A durable local receipt releases the page before a slow cloud commit.
        bool done=false;
        try{const auto r=Read(root/L"live-refresh-result.json");done=r.at("id")==refreshId&&r.at("session")==epoch&&r.value("ready",false);}catch(...){}
        if(!done&&now>=refreshBegan&&now-refreshBegan>=12000)done=true;
        const bool ready=done&&!Pending(refreshPage);
        if(!ready){CloseHandle(access);access=INVALID_HANDLE_VALUE;}
        return ready;
    }
    void FinishRefresh(){std::lock_guard lock(mutex);refreshPage=0;refreshId.clear();refreshKind.clear();goldDeposit=nullptr;}
    bool Protected(unsigned page){std::lock_guard lock(mutex);return enabled&&(page==deposit||page==delivery);}
    std::optional<std::filesystem::path> PageFile(unsigned page){std::lock_guard lock(mutex);if(!enabled||(page!=deposit&&page!=delivery))return {};return folder/(std::wstring(L"Stash_")+(hardcore?L"HC":L"SC")+L"_Page"+std::to_wstring(page)+L".d2i");}
    void Error(const std::string& value){std::lock_guard lock(mutex);status=value;}
    void RememberClosed(unsigned page){std::lock_guard lock(mutex);if(enabled&&page>=1&&page<=64)restore=page;}
    unsigned Close(unsigned page,bool cursor,bool remember=true){
        std::lock_guard lock(mutex);if(!enabled||cursor)return 0;
        if(remember)restore=page;loaded=page;
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
    bool Permit(unsigned page){
        std::lock_guard lock(mutex);if(!enabled)return true;
        if(!Shared()){status="Finishing a stash update...";return false;}
        if(page==deposit||page==delivery){
            if(Pending(page)){
                CloseHandle(access);access=INVALID_HANDLE_VALUE;
                status="Trade sync pending. Your other stash pages are available.";return false;
            }
        }
        Revoke();loaded=page;status.clear();return true;
    }
    unsigned Restore(bool reopening=false){std::lock_guard lock(mutex);return enabled&&(reopening||!passive)?restore:0;}
    void Tick(bool inGame){
        std::lock_guard lock(mutex);if(!enabled)return;
        if(!inGame){if(Shared()){Revoke();enabled=false;refreshPage=0;refreshId.clear();refreshKind.clear();goldDeposit=nullptr;}return;}
        const auto now=GetTickCount64();if(parked&&now>=published&&now-published>=1000)Publish(now);
        if(now>=syncPoll){
            syncPoll=now+2000;syncHint.clear();
            try{const auto report=Read(root/L"status.json");const auto at=report.value("at",int64_t(0));
                const auto utc=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                if(utc>=at&&utc-at<90){const auto progress=report.at("stash_sync").at(hardcore?"HC":"SC");const auto phase=progress.value("state",std::string());
                    if(phase=="no_space")syncHint="Make room on your market page. Your remaining delivery is protected in the Vault.";
                    else if(phase=="waiting_for_idle")syncHint="Delivery queued. Leave the stash idle with an empty cursor to sync.";
                    else if(phase=="waiting_for_stash")syncHint="Waiting for the stash connection. Sync retries automatically.";
                    else if(phase=="recovering")syncHint="Safely recovering a previous transfer. Your items are protected.";
                    else if(phase=="retrying")syncHint="Marketplace connection interrupted. Retrying automatically.";
                }
            }catch(...){}
        }
    }
    View Snapshot(){std::lock_guard lock(mutex);return {enabled,parked,deposit,delivery,parking,status.empty()?syncHint:status};}
};
inline Window window;
}
