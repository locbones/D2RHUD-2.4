#include "../d2rhud/plugin/ExtendedActs.cpp"
#include <cassert>
#include <iostream>
#include <map>

uintptr_t fixtureBase=0;
std::map<uint32_t,uintptr_t> bindings;
DWORD64 Pattern::Address(uint32_t rva){return fixtureBase?fixtureBase+rva:bindings[rva];}
bool lifecycle=true;int town=0;void(*observer)(D2GameStrc*)=nullptr;
bool CubeMap::SummonLifecycleAvailable(){return lifecycle;}
void CubeMap::SetActTick(void(*value)(D2GameStrc*)){observer=value;}
void CubeMap::SetActTown(int value){town=value;}
bool RestartGame::AfterCredits(const char*,int,void(*)(bool)){return false;}

int passedTiles=0;
void __fastcall PassTile(void*,uint64_t,int,int x,int y,int flags){assert(x==3&&y==4&&flags==7);++passedTiles;}
void ExitTests(){
    using namespace ExtendedActs;
    std::array<std::array<uint8_t,0x9c>,249> definitions{};
    auto table=definitions.data();int count=249;
    bindings[0x1c9fd28]=reinterpret_cast<uintptr_t>(&table);bindings[0x1c9fd10]=reinterpret_cast<uintptr_t>(&count);
    std::array<uint8_t,0x200> area{};std::array<uint8_t,0xa0> room{};
    auto level=area.data();memcpy(room.data()+0x90,&level,8);
    originalTile=&PassTile;installed=true;levels={240,241,242,243,244,245,246,247,248};
    auto call=[&](int id,int kind,unsigned index,int vis,bool expected){
        memcpy(area.data()+0x1f8,&id,4);
        if(index<8)memcpy(definitions[id].data()+0x48+4*index,&vis,4);
        const int before=passedTiles;TileHook(room.data(),uint64_t(index)<<20,kind,3,4,7);
        assert(passedTiles==before+int(expected));
    };
    for(int id=240;id<249;++id)for(unsigned index=0;index<8;++index){
        call(id,10,index,0,false);call(id,11,index,0,false);
        call(id,10,index,241,true);call(id,11,index,240,true);
        call(id,0,index,0,true);call(id,1,index,0,true);
    }
    call(113,10,0,0,true);call(109,11,1,0,true);call(239,10,0,0,true);
    call(240,10,33,0,true); // town entry / pop markers are never suppressed.
    installed=false;call(241,10,0,0,true);
    bindings.clear();originalTile=nullptr;
}

void ProgressTests(){
    using ExtendedActs::Progress::Complete;
    for(unsigned diff=0;diff<3;++diff)for(unsigned prior=0;prior<32;++prior){
        const uint16_t flags=uint16_t(0xe0ad|(prior<<8));
        assert(Complete(flags,diff,false,false)==flags);
        assert(Complete(flags,diff,true,false)==flags); // Baal alone never advances.
        assert(Complete(flags,diff,false,true)==flags);
        const auto completed=Complete(flags,diff,true,true);
        assert(unsigned((completed>>8)&31)==(std::max)(prior,5*(diff+1)));
        assert((completed&0xe0ff)==(flags&0xe0ff));
        assert(Complete(completed,diff,true,true)==completed);
        assert(Complete(uint16_t(flags&~0x20),diff,true,true)==uint16_t(flags&~0x20));
    }
    assert(Complete(0x4020,3,true,true)==0x4020);
}
void TransitionTests(){
    using namespace ExtendedActs::Transition;
    const char hero[16]="MyHero",other[16]="OtherHero";Flow flow;
    assert(!flow.Open() && !flow.Close(0));
    flow.Observe(hero,1,100,77,10,false);assert(flow.phase==Phase::Idle);
    flow.Observe(hero,1,100,77,15,true);assert(flow.phase==Phase::Armed);
    assert(!flow.Open());flow.Ending(); // Manual Credits after killing Baal cannot trigger reentry.
    assert(flow.Open() && !flow.Open());assert(flow.Close(200) && !flow.Close(201));
    flow.Observe(hero,1,200,88,0,true); // a fresh native game after the credits
    assert(flow.Due(hero,1,300) && flow.expires==120200);
    assert(!flow.Due(other,1,300) && flow.phase==Phase::Idle);
    flow={};flow.Observe(hero,0,100,77,0,true); // legacy Baal credit cannot start credits return
    assert(!flow.Open());flow.Observe(hero,0,200,88,0,false);
    flow.Observe(hero,0,200,88,5,true);flow.Ending();assert(flow.Open() && flow.Close(0));
    assert(!flow.Due(hero,0,120000));
    flow={};flow.Observe(hero,2,100,77,0,false);flow.Observe(hero,2,100,77,5,true);
    flow.Ending();assert(flow.Open() && flow.Close(0));flow.attempts=3;assert(!flow.Due(hero,2,1));
}
void DataTests(){
    using namespace ExtendedActs;
    auto root=std::filesystem::temp_directory_path()/("act6-switch-test-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directory(root);
    auto put=[&](const char* name,const char* bytes){std::ofstream(root/name,std::ios::binary)<<bytes;};
    auto hash=[](const char* s){return LegacyTransmog::Data::Hash({reinterpret_cast<const uint8_t*>(s),strlen(s)});};
    put("actinfo.txt","original\r\n");put("six.txt","six\r\n");put("five.txt","original\r\n");
    nlohmann::json config={{"switch_files",nlohmann::json::array({{{"path","actinfo.txt"},{"enabled","six.txt"},{"disabled","five.txt"},{"enabled_hash",hash("six\r\n")},{"disabled_hash",hash("original\r\n")}}})}};
    assert(SelectData(root,config,true));assert(SelectData(root,config,true));
    assert(SelectData(root,config,false));
    bool needsText=false;try{SelectData(root,config,true,false);}catch(const NeedsCompilation&){needsText=true;}assert(needsText);
    std::ifstream input(root/"actinfo.txt",std::ios::binary);std::string restored(std::istreambuf_iterator<char>(input),{});input.close();
    assert(restored=="original\r\n");
    put("actinfo.txt","user edit");bool rejected=false;
    try{SelectData(root,config,true);}catch(...){rejected=true;}assert(rejected);
    for(const char* name:{"actinfo.txt","six.txt","five.txt"})std::filesystem::remove(root/name);
    std::filesystem::remove(root);
}
#include "act6-level-edits-test.inl"
#include "act6-string-edits-test.inl"
void StartupDataTests(const char* sourcePath){
    using namespace ExtendedActs;
    const auto source=std::filesystem::absolute(sourcePath);
    std::ifstream manifest(source/"Pandemonium/act6.json");assert(manifest);
    const auto config=nlohmann::json::parse(manifest);
    const auto temp=std::filesystem::canonical(std::filesystem::temp_directory_path());
    const auto data=temp/("act6-startup-test-"+std::to_string(GetCurrentProcessId()));
    assert(!std::filesystem::exists(data));std::filesystem::create_directory(data);
    auto copy=[&](const std::string& from,const std::string& to){const auto destination=data/to;std::filesystem::create_directories(destination.parent_path());std::filesystem::copy_file(source/from,destination);};
    for(const auto& file:config["switch_files"]){
        copy(file["enabled"],file["enabled"]);copy(file["disabled"],file["disabled"]);copy(file["disabled"],file["path"]);
    }
    auto check=[&](bool launch,bool layout){
        std::set<std::string> layouts;for(const auto& name:config["native_layouts"])layouts.insert(name.get<std::string>());
        for(const auto& f:config["switch_files"]){
            const bool enabled=layouts.contains(f["path"].get<std::string>())?layout:launch;
            std::ifstream input(data/f["path"].get<std::string>(),std::ios::binary);
            std::string text(std::istreambuf_iterator<char>(input),{});
            const auto hash=LegacyTransmog::Data::Hash({reinterpret_cast<const uint8_t*>(text.data()),text.size()});
            assert(hash==f[enabled?"enabled_hash":"disabled_hash"].get<uint64_t>());
        }
    };
    // Real injector order: ActInfo has compiled before the States gate. Its
    // ordinary five-act count must not prevent a disabled HUD from starting.
    assert(ConfigureData(data,config,false,true,5)==Config::Disabled);check(false,false);
    SetRequested(true);check(true,false); // Never expose six-tab files to this five-act process.
    assert(ConfigureData(data,config,true,true,6)==Config::Enabled && !pendingRestart);check(true,true);
    SetRequested(false);check(false,true); // Its six-act process keeps six-tab layouts until restart.
    assert(ConfigureData(data,config,false,true,5)==Config::Disabled && !pendingRestart);check(false,false);
    assert(ConfigureData(data,config,true,true,5)==Config::Disabled && pendingRestart);check(true,false);
    assert(ConfigureData(data,config,true,true,6)==Config::Enabled && !pendingRestart);check(true,true);
    assert(ConfigureData(data,config,false,true,6)==Config::Enabled && pendingRestart);check(false,true);
    assert(ConfigureData(data,config,false,true,5)==Config::Disabled);check(false,false);
    LevelEditTests(data,config);check(false,false);
    StringEditTests(data,config,source);check(false,false);
    // Distribute only compiled tables and the existing JSON/layout assets.
    // No authoring or enabled/disabled template TXT is needed for startup.
    for(const auto& path:std::filesystem::recursive_directory_iterator(data))if(path.is_regular_file()&&path.path().extension()==".txt")std::filesystem::remove(path.path());
    assert(ConfigureData(data,config,false,false,5)==Config::Disabled && !compileText);
    assert(ConfigureData(data,config,true,false,5)==Config::Disabled && pendingRestart);
    // Current releases removed the D2RLAN themes but retain their historical
    // manifest entries. They must not fail the extended-state startup gate.
    size_t absentThemes=0;
    for(const auto& entry:config["native_layouts"]){const auto relative=entry.get<std::string>();if(relative.starts_with("D2RLAN/UI Theme/")){assert(std::filesystem::remove(data/relative));++absentThemes;}}
    assert(absentThemes>0);
    assert(ConfigureData(data,config,false,false,5)==Config::Disabled);
    assert(ConfigureData(data,config,true,false,5)==Config::Disabled && pendingRestart);
    const auto required=data/"global/ui/layouts/questlogpanelexpansionhd.json";
    std::filesystem::rename(required,required.string()+".backup");bool missingRequired=false;
    try{ConfigureData(data,config,false,false,5);}catch(const std::exception& e){missingRequired=std::string(e.what()).find("global/ui/layouts/questlogpanelexpansionhd.json")!=std::string::npos;}
    assert(missingRequired);std::filesystem::rename(required.string()+".backup",required);
    auto bin=[&](const char* name,size_t stride,uint32_t count){
        std::vector<uint8_t> bytes(4+size_t(count)*stride);memcpy(bytes.data(),&count,4);
        if(std::string(name)=="actinfo")for(uint32_t n=0;n<count;++n){const auto id=n+1;memcpy(bytes.data()+4+size_t(n)*stride,&id,4);}
        if(std::string(name)=="levels")for(uint32_t n=0;n<count;++n){auto row=bytes.data()+4+size_t(n)*stride;memcpy(row,&n,4);if(n>=240&&n<249){row[7]=5;row[0xe8]=uint8_t(39+n-240);}}
        std::ofstream file(data/"global/excel"/(std::string(name)+".bin"),std::ios::binary);file.write(reinterpret_cast<char*>(bytes.data()),bytes.size());
    };
    bin("actinfo",0x8c,6);bin("levels",0x184,249);
    assert(ConfigureData(data,config,true,false,6)==Config::Enabled && !pendingRestart && !compileText);
    assert(ConfigureData(data,config,false,false,6)==Config::Enabled && pendingRestart);
    bin("levels",0x184,248);bool bad=false;
    try{ConfigureData(data,config,true,false,6);}catch(...){bad=true;}assert(bad);
    bin("levels",0x184,249);bin("actinfo",0x8c,5);bad=false;
    try{ConfigureData(data,config,true,false,6);}catch(...){bad=true;}assert(bad);
    configured=false;configuredManifest={};configuredData.clear();
    assert(std::filesystem::canonical(data).parent_path()==temp && data.filename().string().starts_with("act6-startup-test-"));
    std::filesystem::remove_all(data);
    std::cout<<"Actual five/six-row startup snapshots, one-restart checkbox transitions and current-process layout isolation passed.\n";
}
std::array<uint8_t,0x40> room{};
std::array<uint8_t,0xa0> roomEx{};
std::array<uint8_t,0x200> area{};
int packets=0,moves=0;
void* __fastcall Room(D2UnitStrc*){return room.data();}
void __fastcall Waypoint(void* data,uint16_t index){assert(index>=39 && index<48);auto words=static_cast<uint16_t*>(data);words[1+index/16]|=uint16_t(1u<<(index%16));}
void __fastcall Send(void*,uint32_t,void** range){auto begin=static_cast<uint8_t*>(range[0]);auto end=static_cast<uint8_t*>(range[1]);assert(end-begin==103 && begin[0]==0x28 && begin[1]==6);++packets;}
void __fastcall Move(D2GameStrc*,D2UnitStrc*,int level,int warp){assert(level==240 && warp==11);memcpy(area.data()+0x1f8,&level,4);++moves;}
void __fastcall ModalBoundary(void*){}
D2PlayerDataStrc* __fastcall PlayerData(D2UnitStrc* player){return player->pPlayerData;}
void Jump(uint8_t* where,uintptr_t target){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(where,jump,6);memcpy(where+6,&target,8);}
void TickTests(){
    using namespace ExtendedActs;
    uint64_t count=6;int mode=0;uint8_t lan=0,state=1;
    D2GameStrc game{};D2ClientStrc client{};D2ClientStrc* head=&client;D2UnitStrc player{};D2PlayerDataStrc data{};
    std::array<std::array<uint8_t,96>,3> bytes{};std::array<BitBuffer,3> flags{};
    std::array<std::array<uint16_t,8>,3> waypoints{};
    game.nClients=1;game.pClientList=&client;game.dwGameFrame=5;game.dwInitSeed=99;
    client.pGame=&game;client.dwClientState=4;client.dwUnitGUID=77;client.pPlayer=&player;
    player.dwUnitId=77;player.pPlayerData=&data;data.pClient=&client;strcpy_s(data.szName,"Hero");
    for(unsigned d=0;d<3;++d){flags[d]={bytes[d].data(),768};data.pQuestData[d]=reinterpret_cast<D2BitBufferStrc*>(&flags[d]);waypoints[d][0]=0x102;auto p=waypoints[d].data();memcpy(reinterpret_cast<uint8_t*>(&data)+0x58+8*d,&p,8);}
    auto re=roomEx.data();auto ar=area.data();memcpy(room.data()+0x18,&re,8);memcpy(roomEx.data()+0x90,&ar,8);
    bindings={{0x1c9ea88,reinterpret_cast<uintptr_t>(&count)},{0x1bfa31c,reinterpret_cast<uintptr_t>(&mode)},
        {0x18882e8,reinterpret_cast<uintptr_t>(&lan)},{0x1d637f0,reinterpret_cast<uintptr_t>(&head)},{0x1dc76f8,reinterpret_cast<uintptr_t>(&state)},
        {0x209530,reinterpret_cast<uintptr_t>(&Room)},{0x259250,reinterpret_cast<uintptr_t>(&Waypoint)},
        {0x422ac0,reinterpret_cast<uintptr_t>(&Send)},{0x2e16d0,reinterpret_cast<uintptr_t>(&Move)}};
    bindings[0x148adb]=0x100148adb;bindings[0x148adc]=0x100148adc;
    levels={240,241,242,243,244,245,246,247,248};installed=true;
    auto setLevel=[](int level){memcpy(area.data()+0x1f8,&level,4);};
    for(unsigned d=0;d<3;++d){
        game.nDifficulty=uint8_t(d);uint16_t clientFlags=uint16_t(0x402d|(5*d+4)<<8);memcpy(reinterpret_cast<uint8_t*>(&client)+0xa,&clientFlags,2);
        setLevel(246);Tick(&game);assert(!Flag(&flags[d],47,0));
        setLevel(109);SetFlag(&flags[d],40,0);Tick(&game);
        assert(waypoints[d][3]&(1u<<7));assert(!(waypoints[d][3]&(1u<<8)));
        for(int i=1;i<=6;++i){setLevel(240+i);game.dwGameFrame+=5;Tick(&game);assert(Flag(&flags[d],41+i,0));if(i<6)assert(Field<uint16_t>(&client,0xa)==clientFlags);}
        assert(unsigned((Field<uint16_t>(&client,0xa)>>8)&31)==5*(d+1));
        for(unsigned other=d+1;other<3;++other)for(auto byte:bytes[other])assert(!byte);
    }
    transition={};game.nDifficulty=0;bytes[0].fill(0);setLevel(109);game.dwGameFrame=100;Tick(&game);
    SetFlag(&flags[0],40,0);game.dwGameFrame=105;Tick(&game);assert(transition.phase==Transition::Phase::Armed);
    EndingReached(Pattern::Address(0x148adb),9);assert(!transition.ending);
    EndingReached(Pattern::Address(0x148adc),10);assert(!transition.ending);
    EndingReached(Pattern::Address(0x148adb),10);assert(transition.ending);
    originalCreditsOpen=&ModalBoundary;originalCreditsClose=&ModalBoundary;CreditsOpen(nullptr);CreditsClose(nullptr);
    assert(transition.phase==Transition::Phase::Returning);
    game.dwGameFrame=125;Tick(&game);assert(moves==1 && Field<int>(area.data(),0x1f8)==240);
    game.dwGameFrame=130;Tick(&game);assert(transition.phase==Transition::Phase::Idle);
    assert(packets>=18);installed=false;bindings.clear();
}
void NativeInstallTests(const char* path){
    using namespace ExtendedActs;
    std::ifstream input(path,std::ios::binary|std::ios::ate);assert(input);
    std::vector<uint8_t> raw(size_t(input.tellg()));input.seekg(0);input.read(reinterpret_cast<char*>(raw.data()),raw.size());
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(raw.data());auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(raw.data()+dos->e_lfanew);
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
    auto sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)memcpy(image+sections[i].VirtualAddress,raw.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    fixtureBase=reinterpret_cast<uintptr_t>(image);
    // Uninitialized globals in the private image are zero, as at startup.
    assert(Supported());lifecycle=false;assert(!Activate());lifecycle=true;
    image[0x33be69]^=1;assert(!Activate() && !storage);image[0x33be69]^=1;
    levels={240,241,242,243,244,245,246,247,248};
    for(unsigned i=0;i<6;++i)labels[i]={uint16_t(64010+3*i),uint16_t(64011+3*i),uint16_t(64012+3*i)};
    assert(Activate() && Active() && town==240 && observer==&Tick);
    for(uint32_t rva:{0x2a3d32u,0x33be69u,0x33c20du,0x33cc8eu})assert(image[rva]==0x90);
    // Execute the native Baal credit routine, with only room/player lookup
    // bound to fixture objects. Its original quest-bit writes stay active.
    Jump(image+0x20c230,reinterpret_cast<uintptr_t>(&PlayerData));
    Jump(image+0x209530,reinterpret_cast<uintptr_t>(&Room));
    std::array<std::array<uint8_t,0x8c>,6> acts{};
    for(unsigned i=0;i<6;++i){int first=std::array<int,6>{1,40,75,103,109,240}[i];memcpy(acts[i].data()+4,&first,4);}
    auto table=acts.data();uint64_t count=6;memcpy(image+0x1c9ea80,&table,8);memcpy(image+0x1c9ea88,&count,8);
    D2GameStrc game{};D2UnitStrc player{};D2PlayerDataStrc data{};D2ClientStrc client{};
    player.pPlayerData=&data;data.pClient=&client;
    std::array<uint8_t,96> bytes{};BitBuffer buffer{bytes.data(),768};
    auto re=roomEx.data();auto ar=area.data();memcpy(room.data()+0x18,&re,8);memcpy(roomEx.data()+0x90,&ar,8);int chamber=132;memcpy(area.data()+0x1f8,&chamber,4);
    auto baal=reinterpret_cast<void(__fastcall*)(D2GameStrc*,D2UnitStrc*)>(image+0x33bd30);
    for(unsigned diff=0;diff<3;++diff){
        game.nDifficulty=uint8_t(diff);data.pQuestData[diff]=reinterpret_cast<D2BitBufferStrc*>(&buffer);bytes.fill(0);
        uint16_t before=uint16_t(0x402d|((5*diff+4)<<8));memcpy(reinterpret_cast<uint8_t*>(&client)+0xa,&before,2);
        baal(&game,&player);assert(Flag(&buffer,40,0) && Field<uint16_t>(&client,0xa)==before);
        SyncProgress(&player,&buffer,diff);assert(Field<uint16_t>(&client,0xa)==before);
        SetFlag(&buffer,47,0);SyncProgress(&player,&buffer,diff);
        assert(unsigned((Field<uint16_t>(&client,0xa)>>8)&31)==5*(diff+1));
    }
    // Call the real hooked native tile entry with the exact class of DS1
    // marker that previously asserted in 26dfe0 (no linked room). A mock of
    // the native tile routine would miss the crash reported in live testing.
    std::array<std::array<uint8_t,0x9c>,249> definitionRows{};
    auto definitions=definitionRows.data();int definitionCount=249;
    memcpy(image+0x1c9fd28,&definitions,8);memcpy(image+0x1c9fd10,&definitionCount,4);
    auto tile=reinterpret_cast<void(__fastcall*)(void*,uint64_t,int,int,int,int)>(image+0x26e880);
    for(int id=240;id<249;++id){
        memcpy(area.data()+0x1f8,&id,4);
        for(unsigned index=0;index<8;++index)for(int kind:{10,11})
            tile(roomEx.data(),0x80000081ULL|(uint64_t(index)<<20),kind,3,4,7);
        tile(roomEx.data(),0x82100089,10,3,4,7); // native town-entry marker 33.
    }
    memset(image+0x1c9fd28,0,8);memset(image+0x1c9fd10,0,4);
    memset(image+0x1c9ea80,0,16);
    Uninstall();assert(!Active() && !storage && !town && !observer && Supported());
    // Execute the original modern save loader's saved-act normalization and
    // bound check with its original zero fallback (ESI). A disabled Act VI
    // clamps to Act I; valid original acts and the enabled sixth act survive.
    const uint8_t result[]{0x44,0x89,0xc0,0xc3};memcpy(image+0x31aa58,result,sizeof(result));
    uint8_t wrapper[]{0x56,0x31,0xf6,0x48,0x83,0xec,0x20,0x48,0xb8,0,0,0,0,0,0,0,0,
        0xff,0xd0,0x48,0x83,0xc4,0x20,0x5e,0xc3};
    auto target=reinterpret_cast<uintptr_t>(image+0x31aa33);memcpy(wrapper+9,&target,8);
    auto code=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(code);memcpy(code,wrapper,sizeof(wrapper));
    auto normalize=reinterpret_cast<unsigned(__fastcall*)(unsigned)>(code);
    for(unsigned loaded:{5u,6u}){memcpy(image+0x1c9ea88,&loaded,4);for(unsigned act=0;act<6;++act)assert(normalize(0x80|act)==(act<loaded?act:0));}
    memset(image+0x1c9ea88,0,8);VirtualFree(code,0,MEM_RELEASE);
    // An unrelated existing hook is never removed by a failed installation.
    void* previous=nullptr;assert(MH_CreateHook(image+0xc32320,reinterpret_cast<void*>(&CreditsOpen),&previous)==MH_OK);
    assert(!Activate() && !storage && Supported());assert(MH_RemoveHook(image+0xc32320)==MH_OK);
    VirtualFree(image,0,MEM_RELEASE);fixtureBase=0;
}
int main(int argc,char** argv){
    ProgressTests();TransitionTests();DataTests();TickTests();ExitTests();
    if(argc>2 && argv[2][0])StartupDataTests(argv[2]);
    if(argc>1)NativeInstallTests(argv[1]);
    std::cout<<"Act VI difficulty progression, credits identity/timeout guards, data switching and native install/rollback passed.\n";
    return 0;
}

