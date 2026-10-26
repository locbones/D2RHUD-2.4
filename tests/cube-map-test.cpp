#ifndef CUBEMAP_UI_TEST
#define CUBEMAP_NO_UI
#endif
#include "../d2rhud/plugin/CubeMap.cpp"
#include "../d2rhud/plugin/Sunder.h"
#include "../d2rhud/plugin/D3WhirlwindData.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "quest-journal-stubs.inl"

void CubeVisual::SetMapPrepare(MapPrepare){}
uintptr_t testImage=0;
std::map<uint32_t,uintptr_t> bindings;
DWORD64 Pattern::Address(uint32_t rva){return testImage ? testImage+rva : bindings[rva];}
using namespace CubeMap;
using namespace CubeMap::Data;
static_assert(CubeMap::LayoutData::Operation!=D3Whirlwind::Data::Operation);
template<class T>void Put(void* p,size_t o,T value){memcpy(static_cast<uint8_t*>(p)+o,&value,sizeof(value));}
template<size_t N>using Bytes=std::array<uint8_t,N>;
#include "cube-map-destinations-test.inl"
#include "cube-map-rewards-test.inl"
LONG WINAPI Fault(EXCEPTION_POINTERS* e){
    if(testImage && e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION)std::cerr<<"Native map fixture fault RVA "<<std::hex<<(e->ContextRecord->Rip-testImage)<<std::dec<<'\n';
    return EXCEPTION_CONTINUE_SEARCH;
}
void Parsing(){
    for(auto text:{"map=1"," map = 256 ","MAP=512","\"map=108\"","useitem,map=78","map=78,useitem"})assert(Parse(text).present && Parse(text).valid);
    assert(Parse("useitem,map=78").useItem);
    for(auto text:{"map=0","map=-1","map=+1","map=1x","map=1.0","map=65536","map=","map=2,map=3","useitem,useitem,map=3","map=2,vis=hax"})assert(Parse(text).present && !Parse(text).valid);
    assert(!Parse("useitem,vis=cap").present);
    auto source=CubeVisual::Data::ParseSource("enabled\tnuminputs\toutput\toutput b\n1\t1\tMAP=512\t\n1\t1\tuseitem\t\n");
    assert(source.rows==2 && source.visuals.size()==1 && source.visuals.at(0).rawOutputs[0]=="MAP=512");
    for(int level:{1,256,512,65535}){auto out=Encode(level);assert(IsMap(out) && out.base==level && out.type==4);out.special=0;assert(!IsMap(out));}
    Census census;census.Observe(1,false);census.Observe(2,false);census.ready=true;census.entered=true;
    assert(census.Remaining()==2 && !census.Complete());census.Observe(1,true);
    assert(census.Remaining()==1 && !census.Complete()); // Missing ID 2 is not a kill.
    census.Observe(2,true);assert(census.Complete());census.Observe(2,false);assert(!census.Complete()); // Resurrection.
    census.ready=false;assert(!census.Complete());
    Packet packet;packet.active=1;packet.run=7;packet.level=256;packet.effects=2;packet.rolls[0]={0,10};packet.rolls[1]={12,-10};
    assert(Data::Valid(packet) && sizeof(packet)==0xce);
    assert(!Receive(reinterpret_cast<uint8_t*>(&packet),sizeof(packet)-1));
    packet.sequence=3;assert(Receive(reinterpret_cast<uint8_t*>(&packet),sizeof(packet)));assert(!Receive(reinterpret_cast<uint8_t*>(&packet),sizeof(packet)));
    packet.sequence=2;assert(!Receive(reinterpret_cast<uint8_t*>(&packet),sizeof(packet)));
    packet.sequence=4;packet.rolls[1]=packet.rolls[0];assert(!Receive(reinterpret_cast<uint8_t*>(&packet),sizeof(packet)));
    packet.effects=13;assert(!Data::Valid(packet));packet.effects=0;packet.level=1;assert(!Data::Valid(packet));
    packet.active=0;packet.completed=0;packet.die=6;packet.reward=0;
    packet.event=Event::Trial;packet.eventPhase=EventPhase::WaitingTown;assert(Data::Valid(packet));
    packet.reward=2;assert(!Data::Valid(packet));packet.success=1;packet.eventPhase=EventPhase::Finished;assert(Data::Valid(packet));
    packet.event=Event::GoblinGrotto;assert(!Data::Valid(packet));packet.reward=0;assert(Data::Valid(packet));
    packet.failure=EventFailure(255);assert(!Data::Valid(packet));packet.failure=EventFailure::None;
    packet.eventSeconds=301;assert(!Data::Valid(packet));packet.eventSeconds=30;packet.die=7;assert(!Data::Valid(packet));packet.die=0;assert(!Data::Valid(packet));
    for(uint8_t die=1;die<=6;++die){int secret=0;for(unsigned roll=0;roll<100;++roll){const auto event=SelectEvent(die,roll,false);secret+=event==Event::Trial;assert(roll<5 || event==RegularEvents[die-1]);assert(SelectEvent(die,roll,true)==Event::Trial);}assert(secret==5);}
    assert(EventRemaining(100,100,Event::GoblinGrotto)==60);assert(EventRemaining(1599,100,Event::GoblinGrotto)==1);assert(!EventRemaining(1600,100,Event::GoblinGrotto));
    assert(EventRemaining(0xfffffff0u+25,0xfffffff0u,Event::Trial)==299);
    std::cout<<"Map parsing, source recovery, missing/dead/revived census and bounded ordered packets passed.\n";
}
struct Fixture {
    Bytes<0x5900> game{};
    Bytes<0x100> act{};
    Bytes<0x900> drlg{};
    Bytes<0x288> level{};
    std::array<Bytes<0x1c0>,3> r2{};
    std::array<Bytes<0x108>,3> r1{};
    std::array<Bytes<0x38>,3> preset{};
    std::array<Unit,3> old{},fresh{};
    std::array<Unit,3> objects{};
    std::array<void*,6> acts{};
    std::array<Bytes<0x8c>,6> actInfo{};
    std::array<Bytes<0x38>,3> nodes{};
    std::array<Bytes<0x60>,3> logic{};
    Game* GamePtr(){return reinterpret_cast<Game*>(game.data());}
    int Index(Room* room){for(int i=0;i<3;++i)if(room==reinterpret_cast<Room*>(r1[i].data()))return i;assert(false);return 0;}
} fixture;
std::vector<std::string> steps;
uint32_t levels=600;int actCount=5;
uint8_t* actTable=nullptr;
Bytes<0x2d0> reloadRegion{};
Room* __fastcall MockRoom(Unit* unit){
    for(int i=0;i<3;++i)if(unit==&fixture.old[i] || unit==&fixture.fresh[i] || unit==&fixture.objects[i])return reinterpret_cast<Room*>(fixture.r1[i].data());
    return nullptr;
}
int __fastcall MockStat(Unit*,uint32_t stat,uint16_t){assert(stat==172);return 0;}
Room* __fastcall MockSpawn(void*,int level,int tile,int* x,int* y){assert(level==256 && tile==11);*x=10;*y=20;return reinterpret_cast<Room*>(fixture.r1[0].data());}
void __fastcall MockPopulate(Game*,Room* room){steps.push_back("restore"+std::to_string(fixture.Index(room)));}
void __fastcall MockRemove(Game*,Unit* unit){
    const int i=fixture.Index(MockRoom(unit));assert(unit==&fixture.old[i]);
    // No destructive removal happens before all destination rooms were checked.
    assert(steps.size()>=3);steps.push_back("remove"+std::to_string(i));
    Put(fixture.r1[i].data(),0xa8,&fixture.objects[i]);
}
void __fastcall MockCompress(Game*,Unit* unit){assert(unit->dwUnitType==2);steps.push_back("preserve"+std::to_string(unit->dwUnitId));}
void __fastcall MockUnload(void* act,Room* room){
    assert(act==fixture.act.data());int i=fixture.Index(room);steps.push_back("unload"+std::to_string(i));
    Put(fixture.r2[i].data(),0x50,Field<uint32_t>(fixture.r2[i].data(),0x50)&~(1u<<20));
    Put(fixture.r2[i].data(),0x58,uintptr_t(0));
}
void __fastcall MockActivate(void* room){
    for(int i=0;i<3;++i)if(room==fixture.r2[i].data()) {
        steps.push_back("reload"+std::to_string(i));Put(room,0x50,Field<uint32_t>(room,0x50)|(1u<<20));
        Put(room,0x58,fixture.r1[i].data());Put(fixture.r1[i].data(),0xa8,uintptr_t(0));return;
    }
    assert(false);
}
void __fastcall MockRestore(Game*,Room* room){steps.push_back("loot"+std::to_string(fixture.Index(room)));}
void __fastcall MockRoomStatus(void* room,uint8_t status){Put(room,0x70,status);}
void __fastcall MockPreset(Game*,Room* room,void* preset){
    int i=fixture.Index(room);assert(preset==fixture.preset[i].data() && !(Field<uint32_t>(preset,0x28)&1));
    steps.push_back("preset"+std::to_string(i));
    Put(preset,0x28,uint32_t(1));
}
void __fastcall MockPresets(Game* game,Room* room){MockPreset(game,room,fixture.preset[fixture.Index(room)].data());}
void __fastcall MockPopulation(Game*,Room* room){
    int i=fixture.Index(room);assert(!Field<int>(fixture.nodes[i].data(),0x20));steps.push_back("fresh"+std::to_string(i));
    Put(room,0xa8,&fixture.fresh[i]);
}
void Reset(){
    fixture={};steps.clear();games.clear();installed=true;SetSurvivorsEnabled(true);
    reloadRegion={};Put(fixture.game.data(),0x1d8+size_t(256)*8,reloadRegion.data());
    actTable=fixture.actInfo[0].data();for(int i=0;i<5;++i)Put(fixture.actInfo[i].data(),4,i*100+1);
    fixture.acts[2]=fixture.act.data();Put(fixture.game.data(),0x198,fixture.acts.data());Put(fixture.act.data(),0x70,fixture.drlg.data());
    Put(fixture.level.data(),0x1f8,int(256));Put(fixture.level.data(),0x10,fixture.r2[0].data());
    static D2MonStatsTxt monster;static D2MonsterDataStrc monsterData;monster={};monster.dwMonStatsFlags=(1u<<15);monsterData={};monsterData.pMonstatsTxt=&monster;
    for(int i=0;i<3;++i) {
        auto& r2=fixture.r2[i];auto& r1=fixture.r1[i];
        Put(r2.data(),0x90,fixture.level.data());Put(r2.data(),0x58,r1.data());Put(r2.data(),0x50,uint32_t((1u<<24)|(1u<<25)|(1u<<20)));
        Put(r2.data(),0x48,i<2 ? fixture.r2[i+1].data() : nullptr);Put(r2.data(),0x98,fixture.preset[i].data());
        Put(r2.data(),0xa8,fixture.logic[i].data());Put(fixture.logic[i].data(),0x58,fixture.nodes[i].data());Put(fixture.nodes[i].data(),0x20,int(1));
        Put(r1.data(),0x18,r2.data());
        auto& old=fixture.old[i];old.dwUnitType=1;old.dwUnitId=uint32_t(i+1);old.dwAnimMode=1;old.pMonsterData=&monsterData;old.pRoomNext=&fixture.objects[i];
        auto& fresh=fixture.fresh[i];fresh=old;fresh.dwUnitId=uint32_t(i+11);fresh.pRoomNext=nullptr;
        fixture.objects[i].dwUnitType=2;fixture.objects[i].dwUnitId=uint32_t(i+21);Put(r1.data(),0xa8,&old);
        Put(fixture.preset[i].data(),0x20,int(1));Put(fixture.preset[i].data(),0x28,uint32_t(1));
    }
    bindings={{0x1c9fd10,reinterpret_cast<uintptr_t>(&levels)},{0x1c9ea80,reinterpret_cast<uintptr_t>(&actTable)},{0x1c9ea88,reinterpret_cast<uintptr_t>(&actCount)},
        {0x209530,reinterpret_cast<uintptr_t>(MockRoom)},{0x1e5620,reinterpret_cast<uintptr_t>(MockStat)},{0x25a080,reinterpret_cast<uintptr_t>(MockSpawn)},
        {0x27d2d0,reinterpret_cast<uintptr_t>(MockPopulate)},{0x2edb80,reinterpret_cast<uintptr_t>(MockCompress)},{0x1d1420,reinterpret_cast<uintptr_t>(MockUnload)},
        {0x241e50,reinterpret_cast<uintptr_t>(MockActivate)},{0x241fa0,reinterpret_cast<uintptr_t>(MockRoomStatus)},{0x2ed040,reinterpret_cast<uintptr_t>(MockRestore)},{0x2c5900,reinterpret_cast<uintptr_t>(MockPreset)},{0x2c59d0,reinterpret_cast<uintptr_t>(MockPresets)}};
    originalRemove=MockRemove;populationCode=reinterpret_cast<uint8_t*>(MockPopulation);
}
void Reloading(){
    Reset();Run run;run.level=256;
    // A remote observer on the last room blocks the reset of every room.
    Put(fixture.r1[2].data(),0x70,uint64_t(1));assert(!Reload(fixture.GamePtr(),run));
    assert(steps.empty());Unpin(run);
    for(auto& room:fixture.r2)assert(!(Field<uint32_t>(room.data(),0x50)&Pinned));
    Reset();run={};run.level=256;run.targetPacks=run.spawnedPacks=8;assert(Reload(fixture.GamePtr(),run));
    assert(run.pins.size()==3);
    auto reload=std::find(steps.begin(),steps.end(),"reload0");auto unload=std::find(steps.begin(),steps.end(),"unload2");assert(reload>unload);
    for(int i=0;i<3;++i) {
        assert(std::count(steps.begin(),steps.end(),"preserve"+std::to_string(i+21))==1);
        assert(Field<uint32_t>(fixture.preset[i].data(),0x28)&1);
        assert(Field<uint32_t>(fixture.r1[i].data(),0x54)&3);
    }
    Unpin(run);
    auto& state=State(fixture.GamePtr());state.run=std::make_unique<Run>();state.run->level=256;
    assert(IsMapLevel(fixture.GamePtr(),256) && !IsMapLevel(fixture.GamePtr(),257));
    fixture.GamePtr()->dwInitSeed=8;assert(!IsMapLevel(fixture.GamePtr(),256));State(fixture.GamePtr());assert(!games.at(fixture.GamePtr()).run);
    populationCode=nullptr;installed=false;
    std::cout<<"Occupied destinations refuse all removal; reload restores loot, refreshes every room and preserves game boundaries.\n";
}
std::map<uint32_t,Unit*> gateUnits;
std::map<Unit*,Room*> gateRooms;
std::vector<uint32_t> gateDeletes;
Unit* __fastcall GateLookup(Game*,int,uint32_t id){auto it=gateUnits.find(id);return it==gateUnits.end() ? nullptr : it->second;}
Room* __fastcall GateRoom(Unit* unit){auto it=gateRooms.find(unit);return it==gateRooms.end() ? nullptr : it->second;}
void __fastcall GateDelete(Room*,int type,uint32_t id){assert(type==2);gateDeletes.push_back(id);}
void __fastcall GateRemove(Game*,Unit*){}
void PartyEntry(){
    Reset();gateUnits.clear();gateRooms.clear();gateDeletes.clear();
    Unit first{},second{},entry{},exit{};first.dwUnitId=1;second.dwUnitId=2;entry.dwUnitId=10;exit.dwUnitId=11;
    Client a{},b{};a.pGame=b.pGame=fixture.GamePtr();a.dwUnitGUID=1;b.dwUnitGUID=2;a.pNext=&b;fixture.GamePtr()->pClientList=&a;
    Bytes<0x108> town{},map{};Bytes<0x1c0> town2{},map2{};Bytes<0x288> townLevel{},mapLevel{};
    Put(town.data(),0x18,town2.data());Put(map.data(),0x18,map2.data());Put(town2.data(),0x90,townLevel.data());Put(map2.data(),0x90,mapLevel.data());
    Put(townLevel.data(),0x1f8,int(1));Put(mapLevel.data(),0x1f8,int(256));
    auto townRoom=reinterpret_cast<Room*>(town.data()),mapRoom=reinterpret_cast<Room*>(map.data());
    gateUnits={{1,&first},{2,&second},{10,&entry},{11,&exit}};gateRooms={{&first,mapRoom},{&second,townRoom},{&entry,townRoom},{&exit,mapRoom}};
    bindings[0x2c49c0]=reinterpret_cast<uintptr_t>(GateLookup);bindings[0x209530]=reinterpret_cast<uintptr_t>(GateRoom);bindings[0x1d1280]=reinterpret_cast<uintptr_t>(GateDelete);originalRemove=GateRemove;
    Run run;run.level=256;b.dwClientState=1;CaptureMapParty(fixture.GamePtr(),run,&first);
    assert(run.entryParty==std::set<uint32_t>({1,2}));run.portals={10,11};run.census.entered=true;
    CloseMapEntry(fixture.GamePtr(),run);assert(gateDeletes.empty() && !run.entryClosed); // Second participant is in town.
    gateRooms[&second]=mapRoom;CloseMapEntry(fixture.GamePtr(),run);assert(gateDeletes==std::vector<uint32_t>({10,11}) && !run.portals[0] && !run.portals[1] && run.entryClosed);
    CloseMapEntry(fixture.GamePtr(),run);assert(gateDeletes.size()==2); // Each portal is removed once.
    run.portals={10,11};gateDeletes.clear();gateRooms[&second]=townRoom;a.pNext=nullptr;
    CloseMapEntry(fixture.GamePtr(),run);assert(gateDeletes.size()==2); // Disconnected participants cannot lock the portal.
    run.portals={10,11};gateDeletes.clear();run.event=Event::Survivors;
    CloseMapEntry(fixture.GamePtr(),run);assert(gateDeletes.empty());
    installed=false;
    std::cout<<"Both map portals close once the town party arrives; loading/missing players wait, disconnects unblock entry, and Survivors portals stay independent.\n";
}
#include "cube-map-travel-test.inl"
void OrdinaryRestoration(){
    Reset();bindings[NativeData::Source]=reinterpret_cast<uintptr_t>(MockPopulation);
    Run ordinary;ordinary.level=256;assert(Reload(fixture.GamePtr(),ordinary,true));
    assert(rebuilding==nullptr && ordinary.roster.empty() && ordinary.bosses.empty());
    for(int i=0;i<3;++i)assert(std::count(steps.begin(),steps.end(),"fresh"+std::to_string(i))==1);
    Unpin(ordinary);
    State(fixture.GamePtr()).run=std::make_unique<Run>();State(fixture.GamePtr()).run->level=256;State(fixture.GamePtr()).run->phase=Phase::Closed;
    assert(!InitializeMonster(fixture.GamePtr(),reinterpret_cast<Room*>(fixture.r1[0].data()),&fixture.fresh[0]));
    Reset();Run blocked;blocked.level=256;Put(fixture.r1[2].data(),0x70,uint64_t(1));
    assert(!Reload(fixture.GamePtr(),blocked,true) && steps.empty() && !blocked.populationChanged);Unpin(blocked);
    populationCode=nullptr;installed=false;
    std::cout<<"Ordinary restoration uses the native population routine without map rosters and waits for all room observers.\n";
}
int __fastcall RewardStatName(void*,const char* name){assert(std::string(name)=="statpts");return 4;}
int rewardPoints=0,rewardWrites=0;
int __fastcall RewardBase(void*,uint32_t key,void*){assert(key==4u<<16);return rewardPoints;}
void __fastcall RewardSet(Unit*,int stat,int points,uint16_t layer){assert(stat==4 && !layer);rewardPoints=points;++rewardWrites;}
void RandomizationAndRewards(){
    Reset();
    std::vector<Bytes<0x184>> levelRows(4);std::vector<Bytes<0x1dc>> monsters(10);
    uint64_t monsterCount=monsters.size();uint8_t* monsterTable=monsters[0].data();uint8_t* levelTable=levelRows[0].data();uint32_t levelCount=levelRows.size();
    for(auto& level:levelRows)for(size_t offset:{size_t(0x3a),size_t(0x6c)})for(size_t i=0;i<25;++i)Put(level.data(),offset+i*2,int16_t(-1));
    for(int i=0;i<10;++i){Put(monsters[i].data(),0x3c,uint32_t((1u<<25)|(1u<<15)|1u));Put(levelRows[2].data(),0x3a+i*2,int16_t(i));}
    Put(monsters[2].data(),0x3c,Field<uint32_t>(monsters[2].data(),0x3c)|(1u<<6)); // boss
    Put(monsters[3].data(),0x3c,Field<uint32_t>(monsters[3].data(),0x3c)|(1u<<8)); // NPC
    Put(monsters[4].data(),0x80,uint8_t(2)); // ally
    Put(monsters[5].data(),0x3c,uint32_t(1u<<15)); // disabled/not spawnable
    Put(monsters[6].data(),0x3c,Field<uint32_t>(monsters[6].data(),0x3c)|(1u<<18)); // nevercount
    bindings[0x1c9f878]=reinterpret_cast<uintptr_t>(&monsterTable);bindings[0x1c9f880]=reinterpret_cast<uintptr_t>(&monsterCount);
    bindings[0x1c9fd08]=reinterpret_cast<uintptr_t>(&levelTable);bindings[0x1c9fd10]=reinterpret_cast<uintptr_t>(&levelCount);
    Run run;run.random.seed(123);assert(BuildRoster(run));assert(std::set<int>(run.roster.begin(),run.roster.end())==std::set<int>({0,1,7,8,9}));
    for(int i=0;i<100;++i)assert(RandomCandidate(RandomMonster(run)));
    std::array<Bytes<0x144>,512> statRows{};uint8_t* statTable=statRows[0].data();uint64_t statCount=512;
    bindings[0x1c9fb78]=reinterpret_cast<uintptr_t>(&statTable);bindings[0x1c9fb80]=reinterpret_cast<uintptr_t>(&statCount);
    void* linker=reinterpret_cast<void*>(1);
    bindings[0x1c9fb90]=reinterpret_cast<uintptr_t>(&linker);bindings[0x6d0470]=reinterpret_cast<uintptr_t>(RewardStatName);
    bindings[0x1e2e00]=reinterpret_cast<uintptr_t>(RewardBase);bindings[0x1e5440]=reinterpret_cast<uintptr_t>(RewardSet);
    Unit player{};player.dwUnitId=55;player.pStatListEx=reinterpret_cast<D2StatListExStrc*>(1);
    rewardPoints=17;rewardWrites=0;assert(!AwardTrial(run,&player) && !rewardWrites);
    run.event=Event::Trial;run.eventPhase=EventPhase::Finished;assert(!AwardTrial(run,&player) && !rewardWrites);
    run.success=true;assert(AwardTrial(run,&player) && rewardPoints==19 && rewardWrites==1);assert(AwardTrial(run,&player) && rewardWrites==1);
    player.dwUnitId=56;assert(AwardTrial(run,&player) && rewardPoints==21 && rewardWrites==2);
    run.event=Event::GoblinGrotto;player.dwUnitId=57;assert(!AwardTrial(run,&player) && rewardWrites==2);
    populationCode=nullptr;installed=false;
    std::cout<<"Random map pools exclude bosses/NPCs/allies/disabled units; only a completed victorious Trial grants each participant +2 once.\n";
}
int observedLevel=0,observedTile=0,actTransitions=0;
Client* observedClient=nullptr;
void __fastcall NativeActChange(Game*,Client* client,int level,int tile){observedClient=client;observedLevel=level;observedTile=tile;++actTransitions;}
void __fastcall NativeNpcActChange(Game*,Unit*,int previous,int next){assert(previous==109 && (next==2 || next==240));}
Room* freedRoom=nullptr;
void __fastcall CaptureRoomFree(Room* room){freedRoom=room;}
int createdPresets=0;
Unit* __fastcall CapturePresetCreate(Game* game,Room* room,int type,int index,int x,int y,int mode){
    assert(game==fixture.GamePtr() && room && type==1 && index==9 && x==103 && y==204 && mode==1);++createdPresets;return nullptr;
}
Bytes<0x68> buffList{};int buffFrees=0;
void* __fastcall NativeAllocateBuff(uint32_t flags,int duration,uint32_t type,uint32_t id){assert(flags==0 && !duration && !type && id==55);Put(buffList.data(),0x1c,flags);return buffList.data();}
void __fastcall NativeFreeBuff(void* list){assert(list==buffList.data());++buffFrees;}
uintptr_t __fastcall Nothing(){return 0;}
Bytes<0x700> threadContext{};int notifications=0;
void* __fastcall RewardThread(){return threadContext.data();}
void __fastcall StatNotification(void*,void* event){assert(Field<int>(event,8)==4);++notifications;}
Unit* labelPlayer=nullptr;
Unit* __fastcall LabelPlayer(uint32_t){return labelPlayer;}
const char* __fastcall LocalizedLocation(void* view){
    const auto key=Field<const char*>(view,0);const auto length=Field<size_t>(view,8);
    const auto text=std::string(key,length);assert(text=="custom_map_name" || text=="custom_town_name");
    return text=="custom_map_name" ? "Localized Custom Map" : "Localized Town";
}
int calculatedLevel=0;
void __fastcall InitModifiers(Game*,int* values,void*,Unit*){values[0]=values[1]=values[4]=1;values[2]=0;values[3]=0;}
BOOL __fastcall CaptureCalculation(int,int,int,int level,short,D2MonStatsInitStrc* stats){calculatedLevel=level;*stats={};return TRUE;}
void NativeInitializers(uint8_t* image){
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    auto first=BuildInitializer(83),second=BuildInitializer(86);assert(first && second && first->code!=second->code);
    // Simulate the HUD's later detour. Run copies must use the pristine startup
    // snapshot, not copy the detour and recurse into monster initialization.
    image[0x33f380]=0xcc;auto third=BuildInitializer(89);assert(third && third->code[0]==0x4d);
    std::vector<Bytes<0x144>> stats(512);Put(image,0x1c9fb78,stats.data());Put(image,0x1c9fb80,uint64_t(stats.size()));
    std::array<Bytes<0x1dc>,3> monstats{};Bytes<0x128> extended{};
    Put(image,0x1c9f878,monstats.data());Put(image,0x1c9f880,uint64_t(monstats.size()));Put(image,0x1c9f8b8,extended.data());Put(image,0x1c9f8c0,uint64_t(1));
    std::vector<Bytes<0x184>> levels(3);Put(image,0x1c9fd08,levels.data());Put(image,0x1c9fd10,int(levels.size()));
    Put(levels[2].data(),0x1a,uint16_t(5));Put(levels[2].data(),0x1c,uint16_t(6));Put(levels[2].data(),0x1e,uint16_t(7));
    Unit monster{};monster.dwUnitType=1;monster.dwClassId=2;monster.dwAnimMode=1;
    D2MonsterDataStrc monsterData{};monsterData.pMonstatsTxt=reinterpret_cast<D2MonStatsTxt*>(monstats[2].data());monster.pMonsterData=&monsterData;
    Bytes<0x108> room{};Bytes<0x1c0> room2{};Bytes<0x288> level{};
    Put(room.data(),0x18,room2.data());Put(room2.data(),0x90,level.data());Put(level.data(),0x1f8,int(2));
    stub(0x341270,Nothing);stub(0x341120,InitModifiers);stub(0x1e4910,Nothing);stub(0x2356b0,CaptureCalculation);
    stub(0x14383a0,&memmove);stub(0x1438100,&memset);stub(0x1434c50,Nothing);
    // Exit after the real initialization prefix and captured native stat
    // calculator call; unrelated equipment/AI initialization is outside this fixture.
    for(auto code:{first->code,second->code,third->code}){
        DWORD previous=0;assert(VirtualProtect(code,MonsterNativeData::Allocation,PAGE_EXECUTE_READWRITE,&previous));
        code[0xaea]=0xe9;const int32_t jump=0x12c5-0xaef;memcpy(code+0xaeb,&jump,4);
    }
    Bytes<0x5900> game{};Put(game.data(),0x108,int(1));
    for(int difficulty:{0,1,2})for(auto pair:{std::pair{first->code,83},std::pair{second->code,86},std::pair{third->code,89}}) {
        struct Entry{uint32_t key;int value;};std::array<Entry,32> base{},full{},mods{};Bytes<0xb58> root{};
        Put(root.data(),0x1c,uint32_t(0x80000000));
        auto vector=[](void* p,size_t o,auto& v){Put(p,o,v.data());Put(p,o+8,uint64_t(0));Put(p,o+16,uint64_t(0x8000000000000020));};
        vector(root.data(),0x30,base);vector(root.data(),0x80,full);vector(root.data(),0x98,mods);Put(root.data(),0x78,&monster);
        monster.pStatListEx=reinterpret_cast<D2StatListExStrc*>(root.data());Put(game.data(),0x104,uint8_t(difficulty));
        FlushInstructionCache(GetCurrentProcess(),image,0x2000000);FlushInstructionCache(GetCurrentProcess(),pair.first,MonsterNativeData::Allocation);
        reinterpret_cast<void(__fastcall*)(Game*,Room*,Unit*,void*)>(pair.first)(reinterpret_cast<Game*>(game.data()),reinterpret_cast<Room*>(room.data()),&monster,nullptr);
        assert(Stat(&monster,12)==pair.second && calculatedLevel==pair.second);
        DWORD64 runtimeBase=0;assert(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(pair.first+0x500),&runtimeBase,nullptr) && runtimeBase==reinterpret_cast<DWORD64>(pair.first));
    }
    std::cout<<"Actual native initialization selects the run level before stat calculation in all three difficulties, independently across runs and later HUD detours.\n";
}
void NativeTemporaryBuff(uint8_t* image){
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    struct Entry{uint32_t key;int32_t value;};
    std::array<Entry,32> base{},full{},mods{};base[0]=full[0]={39u<<16,75};
    Bytes<0xb58> root{};Put(root.data(),0x1c,uint32_t(0x80000000));
    auto vector=[](void* p,size_t offset,auto& entries,size_t n){Put(p,offset,entries.data());Put(p,offset+8,uint64_t(n));Put(p,offset+16,uint64_t(0x8000000000000020));};
    vector(root.data(),0x30,base,1);vector(root.data(),0x80,full,1);buffList={};vector(buffList.data(),0x30,mods,0);
    Unit unit{};unit.dwUnitId=55;unit.dwAnimMode=1;unit.pStatListEx=reinterpret_cast<D2StatListExStrc*>(root.data());Put(root.data(),0x78,&unit);
    std::vector<Bytes<0x144>> stats(512);stats[39][4]=2;stats[39][8]=9;
    Put(image,0x1c9fb78,stats.data());Put(image,0x1c9fb80,uint64_t(stats.size()));Put(image,0x1c9ebf8,uint64_t(260));
    stub(0x1e4700,NativeAllocateBuff);stub(0x1e45b0,NativeFreeBuff);stub(0x14383a0,&memmove);stub(0x1438100,&memset);stub(0x1434c50,Nothing);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    Run run;run.state=259;run.stats.fill(-1);run.stats[12]=39;run.rolls.push_back({12,-25});
    const uint64_t sentinel=0x123456789abcdef0;Put(root.data(),0xb48,sentinel);
    auto buff=Attach(&unit,run,Side::Player);assert(buff.list==buffList.data() && ContainsList(&unit,buff.list));
    assert(Stat(&unit,39)==50 && base[0].value==75 && Field<int>(buff.list,0x20)==259);
    assert(Field<uint32_t>(root.data(),0xac8+8*4)==8 && Field<uint32_t>(root.data(),0xac8+17*4)==8);
    assert(!(Field<uint32_t>(buff.list,0x1c)&0x40)); // Never a saved MAGIC item list.
    Detach(&unit,buff,259);assert(Stat(&unit,39)==75 && base[0].value==75 && !ContainsList(&unit,buff.list) && buffFrees==1);
    assert(!Field<uint32_t>(root.data(),0xac8+8*4) && Field<uint64_t>(root.data(),0xb48)==sentinel);
    run.rolls.clear();run.eventPlayerStats={{39,200}};
    buff=Attach(&unit,run,Side::Player);assert(Stat(&unit,39)==200 && base[0].value==75);
    Detach(&unit,buff,259);assert(Stat(&unit,39)==75 && base[0].value==75);
    stub(0x214900,RewardThread);stub(0x1e68b0,StatNotification);stub(0x6d0470,RewardStatName);
    Put(image,0x1c9fb90,uintptr_t(1));FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    UnitStat(&unit,4,17);notifications=0;
    Run reward;reward.event=Event::Trial;reward.success=true;reward.eventPhase=EventPhase::Finished;
    assert(AwardTrial(reward,&unit) && Stat(&unit,4)==19 && notifications==1);
    assert(AwardTrial(reward,&unit) && Stat(&unit,4)==19 && notifications==1);
    std::cout<<"Actual native base-stat insertion persists winning points and emits exactly one player stat update.\n";
    stub(0x6d0470,+[](void*,const char* name)->int{return !strcmp(name,"damageresist")?36:!strcmp(name,"item_fastermovevelocity")?96:-1;});
    stats[36][4]=stats[96][4]=2;stats[36][8]=stats[96][8]=9;
    run.rolls.clear();run.eventPlayerStats.clear();run.event=Event::Survivors;
    run.survival.builds[55].ranks[size_t(Survivors::Upgrade::Guard)]=2;run.survival.builds[55].ranks[size_t(Survivors::Upgrade::Fleet)]=3;
    buff=Attach(&unit,run,Side::Player);assert(Stat(&unit,36)==20 && Stat(&unit,96)==24);
    Detach(&unit,buff,259);assert(!Stat(&unit,36) && !Stat(&unit,96) && Stat(&unit,39)==75);
    std::cout<<"Survivors movement/physical-resistance upgrades attach to real unsaved native stat lists and fully disappear on detach.\n";
    std::vector<Bytes<0x184>> levels(400);memcpy(levels[300].data()+0xf9,"custom_map_name",16);memcpy(levels[109].data()+0xf9,"custom_town_name",17);
    Put(image,0x1c9fd08,levels.data());Put(image,0x1c9fd10,int(levels.size()));Put(image,0x1d442d8,uint32_t(0));
    stub(0x6b050,LabelPlayer);stub(0x413960,LocalizedLocation);FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    labelPlayer=&unit;view={};view.recipient=55;view.level=300;view.town=109;view.entrancePortal=99;view.exitPortal=100;receivedAt=GetTickCount64();
    Unit portal{};portal.dwUnitType=2;portal.dwClassId=60;portal.dwUnitId=99;
    assert(std::string(MapPortalName(&portal))=="Localized Custom Map");char label[1024]{};ObjectLabelHook(label,&portal);assert(std::string(label)=="Localized Custom Map");
    portal.dwUnitId=100;assert(std::string(NameHook(&portal))=="Localized Town");portal.dwUnitId=101;assert(!MapPortalName(&portal));
    std::cout<<"Both portal label paths resolve localized destinations above 255; unrelated objects remain native.\n";
    std::cout<<"Actual native temporary stat insertion, attachment and removal restore base resistance and high-state storage.\n";
}
struct LifetimeEntry{uint32_t key;int value;};
Bytes<0x68> lifetimeList{};std::array<LifetimeEntry,32> lifetimeMods{};int lifetimeAllocations=0,lifetimeFrees=0;
void* __fastcall LifetimeAllocate(uint32_t flags,int expiration,uint32_t type,uint32_t id){
    assert(!flags && !expiration && type<=1 && id==77);++lifetimeAllocations;
    lifetimeList={};lifetimeMods={};Put(lifetimeList.data(),8,type);Put(lifetimeList.data(),12,id);Put(lifetimeList.data(),0x1c,flags);
    Put(lifetimeList.data(),0x30,lifetimeMods.data());Put(lifetimeList.data(),0x40,uint64_t(0x8000000000000020));return lifetimeList.data();
}
void __fastcall LifetimeFree(void* list){assert(list==lifetimeList.data());Native<void(__fastcall*)(void*)>(0x1e4300)(list);++lifetimeFrees;}
void NativeBuffLifetime(uint8_t* image){
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    stub(0x1e4700,LifetimeAllocate);stub(0x1e45b0,LifetimeFree);stub(0x1e68b0,Nothing);
    std::vector<Bytes<0x144>> costs(512);costs[39][4]=2;costs[39][8]=9;
    Put(image,0x1c9fb78,costs.data());Put(image,0x1c9fb80,uint64_t(costs.size()));Put(image,0x1c9ebf8,uint64_t(260));
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    const auto expire=Native<void(__fastcall*)(Unit*,int,float)>(0x1e6260);
    for(uint32_t type:{0u,1u}){
        std::array<LifetimeEntry,32> base{},full{},mods{};base[0]=full[0]={6u<<16,40<<8};base[1]=full[1]={7u<<16,100<<8};base[2]=full[2]={39u<<16,75};
        Bytes<0xb58> root{};Put(root.data(),0x1c,uint32_t(0x80000000));
        auto vector=[](void* p,size_t offset,auto& entries,size_t count){Put(p,offset,entries.data());Put(p,offset+8,uint64_t(count));Put(p,offset+16,uint64_t(0x8000000000000020));};
        vector(root.data(),0x30,base,3);vector(root.data(),0x80,full,3);vector(root.data(),0x98,mods,0);
        Unit unit{};unit.dwUnitType=type;unit.dwUnitId=77;unit.dwAnimMode=1;unit.pStatListEx=reinterpret_cast<D2StatListExStrc*>(root.data());Put(root.data(),0x78,&unit);
        Run run;run.state=259;run.stats.fill(-1);const auto effect=type ? 2 : 12;run.stats[effect]=39;run.rolls.push_back({uint8_t(effect),int16_t(type ? 40 : -25)});
        lifetimeAllocations=lifetimeFrees=0;
        if(type){EnsureMonsterBuff(&unit,run);assert(Stat(&unit,6)==100<<8);UnitStat(&unit,6,40<<8);}
        else run.players[77]=Attach(&unit,run,Side::Player);
        const auto resistance=type ? 115 : 50;
        for(int frame:{1,5,25,100,1000,10000}) {
            expire(&unit,frame,1.0f);expire(&unit,0,100.0f);
            if(type)EnsureMonsterBuff(&unit,run);
            assert(ContainsList(&unit,lifetimeList.data()) && Stat(&unit,39)==resistance && Stat(&unit,6)==40<<8);
            assert(lifetimeAllocations==1 && lifetimeFrees==0);
        }
        // Reproduce the previous zero-duration flag with the actual game
        // expiration pass, then prove a repair cannot refill combat damage.
        Put(lifetimeList.data(),0x1c,uint32_t(2));expire(&unit,10001,1.0f);
        assert(!ContainsList(&unit,lifetimeList.data()) && Stat(&unit,39)==75 && lifetimeFrees==1);
        if(type){EnsureMonsterBuff(&unit,run);assert(Stat(&unit,6)==40<<8 && Stat(&unit,39)==115 && lifetimeAllocations==2);}
        else run.players[77]=Attach(&unit,run,Side::Player);
        if(type){
            int pierce=50;
            auto project=[&](){
                const int base=Native<int(__fastcall*)(void*,uint32_t,void*)>(0x1e2e00)(unit.pStatListEx,39u<<16,costs[39].data());
                const int bonus=Stat(&unit,39)-base;
                return Sunder::Calculate(base,(std::max)(bonus,0),(std::min)(bonus,0),pierce);
            };
            for(int frame=10002;frame<10102;++frame){expire(&unit,frame,1.0f);EnsureMonsterBuff(&unit,run);const auto resistance=project();assert(resistance.valid && resistance.effective==99 && Stat(&unit,39)==115 && Stat(&unit,6)==40<<8 && lifetimeAllocations==2);}
            pierce=0;assert(project().effective==115 && Stat(&unit,39)==115);
        }
        Detach(&unit,type ? run.monsters.at(77) : run.players.at(77),259);
        assert(Stat(&unit,39)==75 && Stat(&unit,6)==40<<8 && lifetimeFrees==2);
    }
    std::cout<<"Native expiration reproduces the old modifier loss; untimed player/monster buffs, damaged-life repairs and sundered map bonuses stay stable.\n";
}
#include "cube-map-events-test.inl"
#include "cube-map-event-updates-test.inl"
#include "cube-map-demo-test.inl"
#include "cube-map-automap-test.inl"
#include "cube-map-survivors-test.inl"
void SurvivorCompatibilityDiagnostics(){
    std::array<uint8_t,64> code{};
    const auto address=reinterpret_cast<uintptr_t>(code.data());
    uintptr_t target=0;int32_t displacement=-5;
    code[0]=0xe9;memcpy(code.data()+1,&displacement,4);
    assert(SurvivorEntryJump(address,code,target) && target==address);
    assert(!SurvivorEntryJump(address,std::span(code).first(4),target));
    code[0]=0xeb;code[1]=0xfe;
    assert(SurvivorEntryJump(address,code,target) && target==address);
    code={};code[0]=0xff;code[1]=0x25;displacement=10;memcpy(code.data()+2,&displacement,4);
    const auto destination=reinterpret_cast<uintptr_t>(&SurvivorCompatibilityDiagnostics);
    memcpy(code.data()+16,&destination,8);
    assert(SurvivorEntryJump(address,code,target) && target==destination);
    assert(!SurvivorEntryJump(0,std::span(code).first(6),target)); // Unreadable indirect slot.
    code={};code[0]=0x48;code[1]=0xb8;code[10]=0xff;code[11]=0xe0;
    memcpy(code.data()+2,&destination,8);
    assert(SurvivorEntryJump(address,code,target) && target==destination);
    SurvivorSignatureFailure snapshot;
    snapshot.rva=0x28f2b0;snapshot.size=32;snapshot.address=address;snapshot.expected=0x1234;
    snapshot.complete=true;snapshot.bytes.assign(code.begin(),code.begin()+32);
    const auto captured=DescribeSurvivorFailure(snapshot);
    assert(captured.find("D2R.exe+0x28f2b0")!=std::string::npos);
    assert(captured.find("Read: 32/32 bytes; Win32 error 0")!=std::string::npos);
    assert(captured.find("Expected FNV-1a64: 0000000000001234")!=std::string::npos);
    assert(captured.find("Entry jump 1:")!=std::string::npos);
    assert(captured.find(".exe+0x")!=std::string::npos); // Destination resolves to this image.
    assert(captured.find("no loaded image owns this address")!=std::string::npos); // Stack source.
    code[1]=0xbb;code[0]=0x49;code[10]=0x41;code[11]=0xff;code[12]=0xe3;
    assert(SurvivorEntryJump(address,code,target) && target==destination);
    code={};code[0]=0xeb;code[1]=0xfe;
    snapshot.bytes.assign(code.begin(),code.begin()+32);
    assert(DescribeSurvivorFailure(snapshot).find("Stopped at repeated jump target.")!=std::string::npos);
    // A long chain stops after four reads, and does not execute any target.
    for(size_t i=0;i<6;++i){code[i*8]=0xeb;code[i*8+1]=6;}
    snapshot.bytes.assign(code.begin(),code.begin()+32);
    assert(DescribeSurvivorFailure(snapshot).find("Stopped at four entry jumps.")!=std::string::npos);
    code.fill(0xcc);
    assert(snapshot.bytes[0]==0xeb); // Initial evidence does not alias the source memory.
    const std::array<SurvivorsNative::Signature,1> invalid{{{0x123456,32,0}}};
    bindings[0x123456]=1;uint32_t failed=0;
    assert(!SurvivorSignaturesSupported(invalid,&failed,&snapshot));
    assert(failed==0x123456 && !snapshot.complete && snapshot.error && snapshot.bytes.empty());
    assert(DescribeSurvivorFailure(snapshot).find("Read: 0/32 bytes")!=std::string::npos);
    bindings.erase(0x123456);
    std::cout<<"Compatibility diagnostics preserve failed reads/entry bytes, resolve image owners and bound read-only detour traces.\n";
}
#include "cube-map-layout-test.inl"
#include "cube-map-packs-test.inl"
#include "cube-map-event-packs-test.inl"
#include "cube-map-loot-test.inl"
#include "cube-map-modifiers-test.inl"
void Executable(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary|std::ios::ate);assert(input);std::vector<uint8_t> file(size_t(input.tellg()));input.seekg(0);input.read(reinterpret_cast<char*>(file.data()),file.size());
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data()+dos->e_lfanew);
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(image);
    for(auto section=IMAGE_FIRST_SECTION(nt);section<IMAGE_FIRST_SECTION(nt)+nt->FileHeader.NumberOfSections;++section)memcpy(image+section->VirtualAddress,file.data()+section->PointerToRawData,section->SizeOfRawData);
    testImage=reinterpret_cast<uintptr_t>(image);assert(Supported());
    for(const auto& signature:LayoutNative::Signatures){image[signature.rva]^=1;assert(!Supported());image[signature.rva]^=1;assert(Supported());}
    for(const auto& signature:LootNativeData::Signatures){image[signature.rva]^=1;assert(!Supported());image[signature.rva]^=1;assert(Supported());}
    for(const auto& signature:PackNative::Signatures){image[signature.rva]^=1;assert(!Supported());image[signature.rva]^=1;assert(Supported());}
    assert(SurvivorsSupported() && SaveLoadSupported());
    for(const auto signature:SurvivorsNative::Signatures){
        image[signature.rva]^=1;
        uint32_t failed=0;SurvivorSignatureFailure evidence;
        assert(!SurvivorsSupported(&failed,&evidence) && failed==signature.rva);
        const auto checked=signature.rva==SurvivorsNative::SaveDispatchOriginal.rva ? SurvivorsNative::SaveDispatchOriginal : signature;
        assert(evidence.complete && !evidence.error && evidence.bytes.size()==checked.size);
        assert(!memcmp(evidence.bytes.data(),image+signature.rva,checked.size));
        assert(evidence.expected==checked.hash);
        const bool saveEntry=signature.rva==0x2934b0 || signature.rva==0x318c80;
        assert(SaveLoadSupported()==!saveEntry);
        image[signature.rva]^=1;
    }
    for(const auto signature:NativeData::Signatures){image[signature.rva]^=1;assert(!Supported());image[signature.rva]^=1;}
    DWORD old=0;assert(VirtualProtect(image,nt->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&old));
    const auto exceptionDirectory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    auto functions=reinterpret_cast<RUNTIME_FUNCTION*>(image+exceptionDirectory.VirtualAddress);
    auto ownerRoomFunction=std::find_if(functions,functions+exceptionDirectory.Size/sizeof(RUNTIME_FUNCTION),[](const auto& f){return f.BeginAddress==0x38d1a0;});
    assert(ownerRoomFunction!=functions+exceptionDirectory.Size/sizeof(RUNTIME_FUNCTION));
    assert(RtlAddFunctionTable(ownerRoomFunction,1,reinterpret_cast<DWORD64>(image)));
    NativeSurvivorOwnerRoom(image);
    assert(RtlDeleteFunctionTable(ownerRoomFunction));
    // An unrelated Survivors mismatch must leave character persistence usable.
    // A mismatch in either actual save/load entry must still reject those hooks.
    for(const auto damaged:{0x70065u,0x28f2b0u,0x2934b0u,0x318c80u}){
        image[damaged]^=1;
        std::array<uint8_t,32> load{},save{};
        memcpy(load.data(),image+0x2934b0,32);memcpy(save.data(),image+0x318c80,32);
        assert(Install() && !survivorsAvailable);
        const bool independent=damaged==0x70065 || damaged==0x28f2b0;
        assert(SummonLifecycleAvailable()==independent);
        assert(strstr(SurvivorsCompatibilityDetails(),"Captured before Survivors hooks"));
        assert(strstr(SurvivorsCompatibilityDetails(),damaged==0x28f2b0 ? "Read: 599/599 bytes; Win32 error 0" : "Read: 32/32 bytes; Win32 error 0"));
        if(independent){
            assert(memcmp(load.data(),image+0x2934b0,32));
            assert(memcmp(save.data(),image+0x318c80,32));
            assert(SummonPersistence::Install());
            SummonPersistence::Uninstall();
        }else{
            assert(!memcmp(load.data(),image+0x2934b0,32));
            assert(!memcmp(save.data(),image+0x318c80,32));
            assert(!SummonPersistence::Install());
        }
        assert(strstr(Status(),"signature mismatch at D2R.exe+0x"));
        Uninstall();assert(!SummonLifecycleAvailable());
        assert(!memcmp(load.data(),image+0x2934b0,32));
        assert(!memcmp(save.data(),image+0x318c80,32));
        image[damaged]^=1;
    }
    assert(Install());NativeMapTravelHooks(image);assert(!*SurvivorsCompatibilityDetails());assert(populationCode && populationCode[0xd8]==0xeb && image[0x2ddc18]==0x75);Uninstall();assert(Supported());
    // Reproduce the exact launcher edit from the colleague's report. All eight
    // stack/buffer operands must agree, including those after the first window.
    for(const auto rva:SurvivorsNative::SaveLimitPatchSites)image[rva]=0x80;
    assert(SurvivorsSupported());
    for(const auto missing:SurvivorsNative::SaveLimitPatchSites){
        image[missing]=0x20;uint32_t failed=0;
        assert(!SurvivorsSupported(&failed) && failed==0x28f2b0);image[missing]=0x80;
    }
    image[0x28f33c]^=1;assert(!SurvivorsSupported());image[0x28f33c]^=1;
    for(const auto rva:SurvivorsNative::SaveLimitPatchSites)image[rva]=0x40;
    assert(!SurvivorsSupported()); // An unaudited size is not a wildcard.
    for(const auto rva:SurvivorsNative::SaveLimitPatchSites)image[rva]=0x80;
    assert(Install() && survivorsAvailable && SummonLifecycleAvailable());
    Uninstall();assert(SurvivorsSupported());
    for(const auto rva:SurvivorsNative::SaveLimitPatchSites)assert(image[rva]==0x80);
    for(const auto rva:SurvivorsNative::SaveLimitPatchSites)image[rva]=0x20;
    assert(SurvivorsSupported());
    NativeSurvivorSaveDispatch(image);
    // Reproduce an earlier component detouring the exact entry from the remote
    // report. Diagnostics must resolve its relay, and neither installation nor
    // teardown may overwrite that component's patch.
    auto foreign=+[](Game*,Unit*,const char*,Client*){assert(false);};
    void* foreignOriginal=nullptr;const auto saveEntry=image+0x28f2b0;
    assert(MH_CreateHook(saveEntry,reinterpret_cast<void*>(foreign),&foreignOriginal)==MH_OK);
    assert(MH_EnableHook(saveEntry)==MH_OK);
    std::array<uint8_t,32> foreignBytes{};memcpy(foreignBytes.data(),saveEntry,foreignBytes.size());
    assert(Install() && !survivorsAvailable && SummonLifecycleAvailable());
    assert(strstr(Status(),"D2R.exe+0x28f2b0"));
    assert(strstr(SurvivorsCompatibilityDetails(),"Entry jump 1:"));
    assert(strstr(SurvivorsCompatibilityDetails(),".exe+0x"));
    const std::string retained=SurvivorsCompatibilityDetails();
    Uninstall();assert(!memcmp(saveEntry,foreignBytes.data(),foreignBytes.size()));
    assert(MH_DisableHook(saveEntry)==MH_OK && MH_RemoveHook(saveEntry)==MH_OK);
    assert(retained==SurvivorsCompatibilityDetails() && SurvivorsSupported());
    std::cout<<"A pre-existing save-dispatch detour is diagnosed through its real MinHook relay and preserved without bypassing Survivors checks.\n";
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    // Exercise the real room-removal wrapper with native adjacency and level
    // fields. Only the final allocator/free boundary is intercepted.
    Bytes<0x100> act{};Bytes<0x108> a{},b{},c{};Bytes<0x1c0> r2{};
    Room* ra=reinterpret_cast<Room*>(a.data());Room* rb=reinterpret_cast<Room*>(b.data());Room* rc=reinterpret_cast<Room*>(c.data());
    std::array<Room*,3> na{ra,rb,rc},nb{ra,rb,rc},nc{ra,rb,rc};
    Put(act.data(),0x18,ra);Put(a.data(),0xb0,rb);Put(b.data(),0xb0,rc);
    Put(a.data(),0,na.data());Put(b.data(),0,nb.data());Put(c.data(),0,nc.data());
    for(auto r:{ra,rb,rc})Put(r,0x40,int(3));
    Put(b.data(),0x18,r2.data());Put(b.data(),0x54,uint32_t(3));Put(r2.data(),0x58,rb);
    stub(0x1d1300,CaptureRoomFree);FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    Native<void(__fastcall*)(void*,Room*)>(0x1d1420)(act.data(),rb);
    assert(freedRoom==rb && Field<Room*>(act.data(),0x18)==ra && Field<Room*>(a.data(),0xb0)==rc);
    assert(Field<int>(ra,0x40)==2 && Field<int>(rc,0x40)==2 && !Field<Room*>(r2.data(),0x58) && Field<int>(r2.data(),0xa0)==1);
    Bytes<0x38> preset{};Put(preset.data(),0,int(1));Put(preset.data(),4,int(9));Put(preset.data(),8,int(3));Put(preset.data(),0x20,int(1));Put(preset.data(),0x24,int(4));
    Put(a.data(),0x80,int(100));Put(a.data(),0x84,int(200));stub(0x2c5760,CapturePresetCreate);FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    Native<void(__fastcall*)(Game*,Room*,void*)>(0x2c5900)(fixture.GamePtr(),ra,preset.data());assert(createdPresets==1);
    // Execute the real warp routine. Only act transition/network side effects
    // are intercepted, proving the cross-act branch and its actual ABI.
    std::array<Bytes<0x8c>,6> acts{};for(int i=0;i<6;++i)Put(acts[i].data(),4,i==0 ? 1 : i==1 ? 40 : i==2 ? 75 : i==3 ? 103 : i==4 ? 109 : 240);
    Put(image,0x1c9ea80,acts.data());Put(image,0x1c9ea88,int(5));
    Unit player{};player.dwUnitType=0;player.dwAnimMode=1;D2PlayerDataStrc data{};Client client{};player.pPlayerData=&data;Put(&data,0xf0,&client);Put(&client,0x238,uint8_t(4));
    D2DynamicPathStrc dynamic{};player.pDynamicPath=&dynamic;Bytes<0x108> room{};Bytes<0x1c0> room2{};Bytes<0x288> level{};
    Put(&dynamic,0x20,room.data());Put(room.data(),0x18,room2.data());Put(room2.data(),0x90,level.data());Put(level.data(),0x1f8,int(109));
    stub(0x2e1140,NativeActChange);stub(0x2ec080,NativeNpcActChange);
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    Native<void(__fastcall*)(Game*,Unit*,int,int)>(0x2e16d0)(fixture.GamePtr(),&player,2,11);
    assert(observedClient==&client && observedLevel==2 && observedTile==11 && actTransitions==1);
    assert(GetRoom(&player)==reinterpret_cast<Room*>(room.data()) && Level(&player)==109);
    Put(image,0x1c9ea88,int(6));
    assert(Act(109)==4 && Act(239)==4 && Act(240)==5 && Act(248)==5);
    SetActTown(240);assert(Town(240) && !Town(241));SetActTown(0);assert(!Town(240));
    Native<void(__fastcall*)(Game*,Unit*,int,int)>(0x2e16d0)(fixture.GamePtr(),&player,240,11);
    assert(observedClient==&client && observedLevel==240 && observedTile==11 && actTransitions==2);
    Put(image,0x1c9ea88,int(5));
    NativeLoot(image);NativeEventQuality(image);NativeEventCreation(image);NativeInitializers(image);NativeTemporaryBuff(image);NativeBuffLifetime(image);NativeMapModifierBuffs(image);NativeEventAutomap(image);NativeSurvivorsABI(image);NativeLayouts(image);NativeMapPacks(image);NativeChampionClashPacks(image);
    auto runtime=reinterpret_cast<RUNTIME_FUNCTION*>(populationCode+0xc00);assert(RtlDeleteFunctionTable(runtime));VirtualFree(populationCode,0,MEM_RELEASE);populationCode=nullptr;
    testImage=0;VirtualFree(image,0,MEM_RELEASE);
    std::cout<<"Audited signatures, hook lifecycle, scoped population copy/unwind metadata and real cross-act native warp passed.\n";
}
int main(int argc,char** argv){std::cout<<std::unitbuf;assert(!SurvivorsEnabled());AddVectoredExceptionHandler(1,Fault);Parsing();TimedRewards();RandomDestinations();MapModifierRolls();MapPacks();ChampionClashPacks();Reloading();PartyEntry();MapTravelLock();OrdinaryRestoration();LayoutRecords();LayoutSynchronization();LayoutRerolls();MapReplacement();RandomizationAndRewards();EventSynchronization();EventUpdates();EventDemoShortcuts();EventMana();MidasDrops();EncounterRosters();RegularEventPopulation();SoulSurprisePresetPopulation();TrialImmunities();GoblinObjectives();EventAutomapPackets();EventAutomapGeometry();EventAutomapScope();EventAutomapSynchronization();SurvivorsAttributes();SurvivorsClientPause();SurvivorsRosterPersistence();SurvivorsProgression();SurvivorsCombat();SurvivorsEmptyArena();SurvivorsSpawningAndLoot();SurvivorsAuthentication();SurvivorsNpcEntry();SurvivorCompatibilityDiagnostics();if(argc>1)Executable(argv[1]);return 0;}
