#include "../d2rhud/plugin/MarketplaceLiveCustody.h"
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
    // Same native vtable owns the established draw/message and open/close slots.
    const std::array<std::pair<uint32_t,uint32_t>,4> slots{{{0x18,0x18eb90},{0x20,0x18ee50},{0x30,0x18ee80},{0x38,0x18f050}}};
    for(const auto [slot,rva]:slots)
        assert(at.operator()<uint64_t>(raw(0x157cec8+slot))==nt.OptionalHeader.ImageBase+rva);
    std::cout<<"Installed PE: exact native callback prefixes and BankPanel vtable slots passed.\n";
}

int wmain(int argc,wchar_t** argv){
    assert(argc==3);VerifyGame(argv[2]);using namespace Marketplace::LiveCustody;
    const auto root=std::filesystem::absolute(argv[1]),state=root/L"state",saves=root/L"saves";
    std::filesystem::create_directories(state);std::filesystem::create_directories(saves);
    const auto enrollment=root/L"enrollment.json";
    Write(enrollment,{{"schema",1},{"enrolled",true},{"state_directory",state.string()},{"save_directory",saves.string()},{"owner","player"},{"catalog","catalog"}});
    Write(state/L"live-capability.json",{{"schema",1},{"owner","player"},{"catalog","catalog"}});
    Write(saves/L"Marketplace_2.4_SC.json",{{"mod","RMD-MP"},{"game_mode","2.4"},{"hardcore",false},{"page",11},{"withdraw_page",2}});
    Window bridge;assert(bridge.Configure(enrollment,saves,17,false,11));
    auto writer=[&](){return CreateFileW((state/L"live.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);};
    auto handle=writer();assert(handle==INVALID_HANDLE_VALUE);
    assert(bridge.Close(11,true)==0&&!std::filesystem::exists(state/L"live-window.json"));
    assert(bridge.Close(11,false)==1);assert(!bridge.OpenGrant(11));
    assert(bridge.OpenGrant(1));auto grant=Read(state/L"live-window.json");
    assert(grant["loaded"]==1&&grant["deposit"]==11&&grant["delivery"]==2&&grant["pid"]==GetCurrentProcessId());
    handle=writer();assert(handle!=INVALID_HANDLE_VALUE);
    assert(!bridge.Permit(11)); // never enter during an atomic companion write
    CloseHandle(handle);assert(!bridge.Permit(11)); // first sync pass not acknowledged
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
    bridge.Tick(false);assert(!std::filesystem::exists(state/L"live-window.json"));
    assert(!bridge.Configure(enrollment,root/L"other-saves",17,false,11));
    std::cout<<"Live native window: cursor safety, confirmed parking, sync acknowledgement, atomic write exclusion, pending-page freeze, unrelated navigation and lease reacquisition passed.\n";
}
