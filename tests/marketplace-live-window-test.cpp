#include "../d2rhud/plugin/MarketplaceLiveCustody.h"
#include "../d2rhud/plugin/MarketplaceGoldDeposit.h"
#include <cassert>
#include <iostream>
#include <vector>

static void VerifyGame(const std::filesystem::path& path){
    using namespace Marketplace::LiveCustody;
    std::ifstream file(path,std::ios::binary);
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    auto at=[&]<class T>(size_t offset){assert(offset<=bytes.size()&&sizeof(T)<=bytes.size()-offset);T v{};memcpy(&v,bytes.data()+offset,sizeof(T));return v;};
    const auto dos=at.operator()<IMAGE_DOS_HEADER>(0);assert(dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0);
    const auto nt=at.operator()<IMAGE_NT_HEADERS64>(dos.e_lfanew);assert(nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64);
    auto raw=[&](uint32_t rva){
        const size_t sections=dos.e_lfanew+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader;
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){const auto section=at.operator()<IMAGE_SECTION_HEADER>(sections+i*sizeof(IMAGE_SECTION_HEADER));
            if(rva>=section.VirtualAddress&&rva-section.VirtualAddress<section.SizeOfRawData)return size_t(section.PointerToRawData)+rva-section.VirtualAddress;}
        assert(false);return size_t(0);
    };
    assert((at.operator()<std::array<uint8_t,8>>(raw(0x18ee80))==BankOpenCode));
    assert((at.operator()<std::array<uint8_t,8>>(raw(0x18f050))==BankCloseCode));
    assert((at.operator()<std::array<uint8_t,7>>(raw(0x57e22f))==std::array<uint8_t,7>{0x4c,0x63,0x91,0x68,0x0d,0,0}));
    assert((at.operator()<std::array<uint8_t,18>>(raw(0x57e2e8))==std::array<uint8_t,18>{
        0x48,0x83,0xbb,0xf8,0x0a,0,0,0,0x74,0x36,0x48,0x83,0xbb,0,0x0b,0,0,0}));
    // Same native vtable owns the established draw/message and open/close slots.
    const std::array<std::pair<uint32_t,uint32_t>,4> slots{{{0x18,0x18eb90},{0x20,0x18ee50},{0x30,0x18ee80},{0x38,0x18f050}}};
    for(const auto [slot,rva]:slots)
        assert(at.operator()<uint64_t>(raw(0x157cec8+slot))==nt.OptionalHeader.ImageBase+rva);
    std::cout<<"Installed PE: exact native callback prefixes and BankPanel vtable slots passed.\n";
}

int wmain(int argc,wchar_t** argv){
    assert(argc==3);VerifyGame(argv[2]);using namespace Marketplace::LiveCustody;
    Marketplace::GoldDeposit::Detector gold;
    gold.Observe(17,11,1,1000,100,100);gold.Observe(17,11,1,900,100,200);assert(!gold.Pending());
    gold.Observe(17,11,1,900,200,300);assert(gold.Pending()&&gold.Pending()->carriedBefore==1000&&gold.Pending()->stashAfter==200);
    gold.Observe(17,11,3,900,200,400);assert(gold.Pending());gold.Taken();gold.Observe(17,11,3,900,200,500);assert(!gold.Pending());
    gold.Observe(17,11,3,1000,100,600);assert(!gold.Pending()); // withdrawal never triggers
    gold.Observe(17,11,3,1000,200,700);gold.Observe(17,11,3,1000,200,1800);assert(!gold.Pending()); // delivery alone
    gold.Reset();gold.Observe(17,11,1,1000,100,100);gold.Observe(17,11,1,900,100,200);gold.Observe(17,11,1,900,100,1300);gold.Observe(17,11,1,900,200,1400);assert(!gold.Pending()); // unrelated changes outside the match window
    gold.Reset();gold.Observe(17,11,1,1000,100,100);gold.Observe(18,11,1,900,200,200);assert(!gold.Pending());
    gold.Observe(18,2,1,800,300,300);assert(!gold.Pending());

    const auto root=std::filesystem::absolute(argv[1]),state=root/L"state",saves=root/L"saves";
    std::filesystem::create_directories(state);std::filesystem::create_directories(saves);
    const auto enrollment=root/L"enrollment.json";
    {
        const auto fresh=root/L"fresh";std::filesystem::create_directories(fresh);
        const auto prefs=fresh/L"Marketplace_2.4_SC.json",cap=state/L"live-capability.json";
        Write(enrollment,{{"schema",1},{"enrolled",true},{"state_directory",state.string()},{"save_directory",fresh.string()},{"owner","player"},{"catalog","catalog"}});
        Window first;
        assert(!first.Configure(enrollment,fresh,17,false,11)&&!std::filesystem::exists(prefs));
        Write(cap,{{"schema",1},{"owner","player"},{"catalog","catalog"}});
        Write(state/L"pending.json",{{"id","preserved recovery"}});
        assert(!first.Configure(enrollment,fresh,17,false,11)&&!std::filesystem::exists(prefs));
        std::filesystem::remove(state/L"pending.json");
        auto lockPath=prefs;lockPath+=L".lock";
        const auto held=CreateFileW(lockPath.c_str(),GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        assert(held!=INVALID_HANDLE_VALUE&&!first.Configure(enrollment,fresh,17,false,11));CloseHandle(held);
        assert(first.Configure(enrollment,fresh,17,false,11));
        const auto defaults=Read(prefs);assert(defaults==Marketplace::SettingsJson(Marketplace::Settings{},"RMD-MP",false));
        assert(defaults.at("enabled")==false&&defaults.at("auto_trade")==false);
        assert(first.OpenGrant(11,true));const auto same=Read(state/L"live-window.json");
        const auto stamp=std::filesystem::last_write_time(prefs);
        for(int i=0;i<30;++i)assert(first.Configure(enrollment,fresh,17,false,11));
        assert(Read(state/L"live-window.json")==same&&std::filesystem::last_write_time(prefs)==stamp);
        first.Tick(false);
        assert(first.Configure(enrollment,fresh,17,true,11));
        assert(Read(fresh/L"Marketplace_2.4_HC.json")==Marketplace::SettingsJson(Marketplace::Settings{},"RMD-MP",true));
        first.Tick(false);
        {std::ofstream file(prefs);file<<"malformed existing preferences";}
        assert(!first.Configure(enrollment,fresh,17,false,11));
        assert(Marketplace::ReadSettingsFile(prefs)=="malformed existing preferences");
    }
    Write(enrollment,{{"schema",1},{"enrolled",true},{"state_directory",state.string()},{"save_directory",saves.string()},{"owner","player"},{"catalog","catalog"}});
    Write(state/L"live-capability.json",{{"schema",1},{"owner","player"},{"catalog","catalog"}});
    Write(saves/L"Marketplace_2.4_SC.json",{{"mod","RMD-MP"},{"game_mode","2.4"},{"hardcore",false},{"page",11},{"withdraw_page",2}});
    Window bridge;assert(bridge.Configure(enrollment,saves,17,false,11));
    assert(bridge.RequestSync(10000));const auto manual=Read(state/L"sync-request.json");
    assert(manual.at("mode")=="SC"&&manual.at("id").get<std::string>().size()==32);
    assert(bridge.RequestSync(10001)&&Read(state/L"sync-request.json")==manual);
    assert(bridge.RequestSync(16000)&&Read(state/L"sync-request.json").at("id")!=manual.at("id"));
    assert(!std::filesystem::exists(state/L"pending.json")); // Wake is never write authority.
    auto writer=[&](){return CreateFileW((state/L"live.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);};
    auto handle=writer();assert(handle==INVALID_HANDLE_VALUE);
    assert(bridge.Close(11,true)==0&&!std::filesystem::exists(state/L"live-window.json"));
    assert(bridge.Close(11,false)==1);assert(!bridge.OpenGrant(11));
    assert(bridge.OpenGrant(1));auto grant=Read(state/L"live-window.json");
    assert(grant["loaded"]==1&&grant["deposit"]==11&&grant["delivery"]==2&&grant["pid"]==GetCurrentProcessId());
    handle=writer();assert(handle!=INVALID_HANDLE_VALUE);
    assert(!bridge.Permit(11)); // never enter during an atomic companion write
    CloseHandle(handle);assert(bridge.Permit(11)); // Navigation never waits for a cloud polling pass.
    assert(bridge.OpenGrant(1));grant=Read(state/L"live-window.json");
    Write(state/L"live-result.json",{{"session",grant["session"]},{"terminal",true}});
    Write(state/L"pending.json",{{"id","pending"}});
    assert(!bridge.Permit(11)&&!bridge.Permit(2));
    assert(bridge.Permit(3)); // unrelated pages remain usable
    assert(!std::filesystem::exists(state/L"live-window.json"));
    assert(bridge.OpenGrant(3));grant=Read(state/L"live-window.json");
    std::filesystem::remove(state/L"pending.json");
    Write(state/L"live-result.json",{{"session",grant["session"]},{"terminal",true}});
    assert(bridge.Permit(11));handle=writer();assert(handle==INVALID_HANDLE_VALUE);
    assert(!std::filesystem::exists(state/L"live-window.json"));
    // A loaded deposit page permits delivery to the other, unloaded page.
    // Selecting a page revokes the grant until its final native load ack.
    assert(bridge.OpenGrant(11,true));grant=Read(state/L"live-window.json");
    assert(grant["schema"]==2&&grant["phase"]=="unloaded"&&grant["loaded"]==11&&bridge.Restore()==0&&bridge.Snapshot().status.empty());
    handle=writer();assert(handle!=INVALID_HANDLE_VALUE);
    assert(!bridge.Permit(2));CloseHandle(handle);
    assert(bridge.Permit(2)); // passive grant never waits five seconds for a poll
    assert(!std::filesystem::exists(state/L"live-window.json"));
    handle=writer();assert(handle==INVALID_HANDLE_VALUE); // held throughout reload
    assert(bridge.OpenGrant(2,true));grant=Read(state/L"live-window.json");
    assert(grant["loaded"]==2&&grant["delivery"]==2);
    handle=writer();assert(handle!=INVALID_HANDLE_VALUE);CloseHandle(handle);
    assert(bridge.Permit(11));assert(bridge.OpenGrant(11,true));
    // A transaction blocks only its own pages. A durable applied receipt lets
    // the game load those pages while the remote commit is still retrying.
    const auto id=std::string(32,'a');
    Write(state/L"pending.json",{{"id",id},{"mode","SC"},{"pages",Json::array({2})},{"reload_safe",false}});
    assert(bridge.Permit(11));assert(bridge.OpenGrant(11,true));assert(!bridge.Permit(2));
    std::filesystem::create_directories(state/L"transactions"/id);
    Write(state/L"transactions"/id/L"record.json",{{"id",id},{"mode","SC"},{"phase","applying"}});
    Write(state/L"pending.json",{{"id",id},{"mode","SC"},{"pages",Json::array({2})},{"reload_safe",true}});
    assert(!bridge.Permit(2)); // A safe flag alone cannot bypass an unfinished write.
    Write(state/L"transactions"/id/L"record.json",{{"id",id},{"mode","SC"},{"phase","applied"}});
    assert(bridge.Permit(2));assert(bridge.OpenGrant(2,true));
    Write(state/L"pending.json",{{"id",id},{"mode","HC"},{"pages",Json::array({11})},{"reload_safe",false}});
    assert(bridge.Permit(11));assert(bridge.OpenGrant(11,true));
    Write(state/L"pending.json",{{"id","../unsafe"},{"mode","SC"},{"pages",Json::array({2})},{"reload_safe",true}});
    assert(!bridge.Permit(2));
    std::filesystem::remove(state/L"pending.json");
    // Parking acknowledgements and closed-panel retries must not replace the
    // page remembered at the real close edge with the invisible parking page.
    assert(bridge.Permit(2));assert(bridge.OpenGrant(2,true));
    bridge.RememberClosed(2);assert(bridge.Close(2,false)==1);
    assert(bridge.Permit(1));assert(bridge.Close(1,false,false)==0);assert(bridge.OpenGrant(1));
    for(int i=0;i<30;++i){assert(bridge.Close(1,false,false)==0);assert(bridge.OpenGrant(1));}
    assert(bridge.Restore()==2);
    bridge.RememberClosed(11);assert(bridge.Close(1,false,false)==0);assert(bridge.Restore()==11);
    // Viewed-page refresh: exact session request -> native parking ACK ->
    // local receipt -> reload lease. Stale requests/receipts cannot release it.
    assert(bridge.Permit(2));assert(bridge.OpenGrant(2,true));grant=Read(state/L"live-window.json");
    const auto refresh=std::string(32,'c');const auto now=GetTickCount64();
    auto request=Json{{"schema",1},{"kind","delivery"},{"id",refresh},{"owner","player"},{"catalog","catalog"},{"pid",GetCurrentProcessId()},
        {"started",Started()},{"player",17},{"hardcore",false},{"page",2},{"session","stale"},{"tick",now}};
    Write(state/L"live-refresh-request.json",request);assert(bridge.BeginRefresh(2,4,now)==0);
    request["session"]=grant["session"];Write(state/L"live-refresh-request.json",request);
    request["kind"]="deposit";Write(state/L"live-refresh-request.json",request);assert(bridge.BeginRefresh(2,4,now+500)==0);
    request["kind"]="delivery";Write(state/L"live-refresh-request.json",request);assert(bridge.BeginRefresh(11,4,now+1000)==0);
    assert(bridge.BeginRefresh(2,4,now+1000)==1);assert(bridge.Refreshing()&&bridge.RefreshPage()==2&&bridge.RefreshTab()==4);
    assert(!bridge.RefreshReady(now+500)); // the current native stash still exists
    assert(bridge.Permit(1));assert(bridge.OpenGrant(1,true));grant=Read(state/L"live-window.json");
    assert(grant["refresh_id"]==refresh&&grant["refresh_supported"]==2);
    Write(state/L"live-refresh-result.json",{{"id",refresh},{"session","stale"},{"ready",true}});assert(!bridge.RefreshReady(now+1000));
    Write(state/L"live-refresh-result.json",{{"id",refresh},{"session",grant["session"]},{"ready",true}});
    handle=writer();assert(handle!=INVALID_HANDLE_VALUE);assert(!bridge.RefreshReady(now+1000));CloseHandle(handle);
    Write(state/L"pending.json",{{"id",id},{"mode","SC"},{"pages",Json::array({2})},{"reload_safe",false}});
    assert(!bridge.RefreshReady(now+30000)); // a timeout never bypasses a pending write
    std::filesystem::remove(state/L"pending.json");assert(bridge.RefreshReady(now+1000));
    handle=writer();assert(handle==INVALID_HANDLE_VALUE); // reload gate now held
    assert(bridge.Permit(2));bridge.FinishRefresh();assert(bridge.OpenGrant(2,true));assert(!bridge.Refreshing());
    assert(bridge.BeginRefresh(2,4,now+30000)==0); // no replay of consumed request
    bridge.Tick(false);assert(!std::filesystem::exists(state/L"live-window.json"));
    // A single market page is both a debit and credit route. Its viewed
    // contents remain fenced until the native parking acknowledgement.
    Write(saves/L"Marketplace_2.4_SC.json",{{"mod","RMD-MP"},{"game_mode","2.4"},{"hardcore",false},{"page",11},{"withdraw_page",11}});
    assert(bridge.Configure(enrollment,saves,17,false,11));assert(bridge.OpenGrant(11,true));
    const auto single=Read(state/L"live-window.json");assert(single.at("deposit")==11&&single.at("delivery")==11&&single.at("loaded")==11);
    request["id"]=std::string(32,'e');request["page"]=11;request["session"]=single.at("session");request["tick"]=now+40000;
    Write(state/L"live-refresh-request.json",request);assert(bridge.BeginRefresh(11,3,now+40000)==1);
    assert(!bridge.RefreshReady(now+40001));assert(bridge.RefreshPage()==11&&bridge.RefreshTab()==3);
    bridge.FinishRefresh();
    assert(bridge.Permit(11));assert(bridge.OpenGrant(11,true));
    const auto goldNow=now+50000;
    Write(state/L"live-capability.json",{{"schema",1},{"owner","player"},{"catalog","catalog"},{"gold_deposit_refresh",1}});
    auto settings=Read(saves/L"Marketplace_2.4_SC.json");settings["enabled"]=true;Write(saves/L"Marketplace_2.4_SC.json",settings);
    assert(!bridge.RequestGoldDeposit(11,1,1000,900,100,201,goldNow));
    assert(bridge.RequestGoldDeposit(11,1,1000,900,100,200,goldNow));
    assert(bridge.BeginRefresh(11,1,goldNow)==1);assert(bridge.Refreshing());
    assert(!bridge.RefreshReady(goldNow+1));assert(bridge.Permit(1));assert(bridge.OpenGrant(1,true));
    auto goldWindow=Read(state/L"live-window.json");assert(goldWindow["refresh_kind"]=="gold_deposit"&&goldWindow["gold_deposit"]["carried_before"]==1000);
    Write(state/L"live-refresh-result.json",{{"id",goldWindow["refresh_id"]},{"session",goldWindow["session"]},{"ready",true}});
    assert(bridge.RefreshReady(goldNow+1));assert(bridge.Permit(11));bridge.FinishRefresh();assert(bridge.OpenGrant(11,true));
    const auto listingNow=goldNow+1000;const auto listingWindow=Read(state/L"live-window.json");
    assert(listingWindow["listing_deposit_refresh"]==1);
    request["kind"]="listing_deposit";request["id"]=std::string(32,'f');request["page"]=11;
    request["session"]=listingWindow["session"];request["tick"]=listingNow;
    Write(state/L"live-refresh-request.json",request);
    assert(bridge.BeginRefresh(11,4,listingNow)==1&&bridge.RefreshTab()==4);
    assert(!bridge.RefreshReady(listingNow+1));assert(bridge.Permit(1));assert(bridge.OpenGrant(1,true));
    const auto parked=Read(state/L"live-window.json");assert(parked["refresh_kind"]=="listing_deposit");
    Write(state/L"live-refresh-result.json",{{"id",parked["refresh_id"]},{"session",parked["session"]},{"ready",true}});
    assert(bridge.RefreshReady(listingNow+1));assert(bridge.Permit(11));bridge.FinishRefresh();assert(bridge.OpenGrant(11,true));
    settings["enabled"]=false;Write(saves/L"Marketplace_2.4_SC.json",settings);
    assert(!bridge.RequestGoldDeposit(11,1,1000,900,100,200,goldNow+1));
    // Reproduce a closed stash still granting the old route after Setup (or
    // a website intent) changes it. The next open/load callback must re-read
    // settings even when the previous grant was already parked.
    assert(bridge.Permit(11));bridge.Tick(false);
    settings["page"]=settings["withdraw_page"]=1;Write(saves/L"Marketplace_2.4_SC.json",settings);
    assert(bridge.Configure(enrollment,saves,17,false,11));
    assert(bridge.Close(11,false)==0&&bridge.OpenGrant(11));
    const auto oldRoute=Read(state/L"live-window.json");assert(oldRoute["deposit"]==1&&oldRoute["loaded"]==11);
    settings["page"]=settings["withdraw_page"]=11;Write(saves/L"Marketplace_2.4_SC.json",settings);
    handle=writer();assert(handle!=INVALID_HANDLE_VALUE);
    assert(!bridge.Configure(enrollment,saves,17,false,11));
    assert(Read(state/L"live-window.json")==oldRoute); // never bypass an active writer
    CloseHandle(handle);
    assert(bridge.Configure(enrollment,saves,17,false,11));
    assert(!std::filesystem::exists(state/L"live-window.json"));
    assert(bridge.OpenGrant(11,true));grant=Read(state/L"live-window.json");
    assert(grant["schema"]==2&&grant["deposit"]==11&&grant["delivery"]==11&&grant["loaded"]==11&&grant["session"]!=oldRoute["session"]);
    // A page switch acknowledges its real loaded page under the new routes.
    assert(bridge.Permit(5));assert(bridge.Configure(enrollment,saves,17,false,5));assert(bridge.OpenGrant(5,true));
    grant=Read(state/L"live-window.json");assert(grant["loaded"]==5&&grant["deposit"]==11);
    assert(!bridge.Configure(enrollment,root/L"other-saves",17,false,11));
    std::cout<<"Live native window: cursor safety, confirmed parking, sync acknowledgement, atomic write exclusion, pending-page freeze, unrelated navigation and lease reacquisition passed.\n";
}
