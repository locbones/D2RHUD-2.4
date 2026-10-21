#include "CubeMap.h"
#include "CubeMapData.h"
#include "CubeMapAutomapData.h"
#include "CubeMapNative.h"
#include "CubeMapMonsterNative.h"
#include "CubeMapQualityNative.h"
#include "CubeMapLayoutNative.h"
#include "CubeMapLayoutData.h"
#include "CubeMapLootNative.h"
#include "CubeMapPackNative.h"
#include "CubeMapRewardsData.h"
#include "CubeMapJungleData.h"
#include "SurvivorsData.h"
#include "SurvivorsNative.h"
#include "SurvivorsRoster.h"
#include "SummonPersistence.h"
#include "QuestJournal.h"
#include "CubeVisual.h"
#include "CompiledNames.h"
#include "LegacyTransmogData.h"
#include "../Pattern.h"
#include "../CrashDiagnostics.h"
#include "../d2/Reclass.h"
#include <MinHook.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <random>
#include <optional>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <tuple>
#ifndef CUBEMAP_NO_UI
#include <imgui.h>
#endif

// Read-only diagnostics for checking the loaded build without altering a run.
struct CubeMapDiagnostics {
    std::atomic<uint32_t> installed{0},stage{0},level{0},rooms{0},remaining{0},run{0},eventFailure{0};
    std::atomic<uint32_t> survivorsEnabled{0},survivorWave{0},survivorKills{0};
    std::atomic<uint32_t> survivorAttempts{0},survivorEffects{0},survivorDamage{0};
};
extern "C" {__declspec(dllexport) CubeMapDiagnostics D2RHUD_CubeMapDiagnostics;}

namespace CubeMap {
namespace {
using namespace Data;
using Game=D2GameStrc;
using Unit=D2UnitStrc;
using Room=D2ActiveRoomStrc;
using Client=D2ClientStrc;
Unit* LocalPlayer();
int LookupName(uint32_t global,const char* name);
template<class T>T Native(uint32_t rva){return reinterpret_cast<T>(Pattern::Address(rva));}
template<class T>bool Read(uintptr_t address,T& result){SIZE_T n=0;return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&result,sizeof(result),&n) && n==sizeof(result);}
template<class T>T Field(const void* address,size_t offset){T result{};Read(reinterpret_cast<uintptr_t>(address)+offset,result);return result;}
template<class T>void Set(void* address,size_t offset,T value){memcpy(static_cast<uint8_t*>(address)+offset,&value,sizeof(value));}
using Tick=void(__fastcall*)(Game*);
using Remove=void(__fastcall*)(Game*,Unit*);
using Destroy=void(__fastcall*)(uint64_t,Game*);
using SpawnPreset=void(__fastcall*)(Game*,Room*,void*);
using Choose=int(__fastcall*)(Game*,void*,Room*,void**,uint8_t,int);
using Name=const char*(__fastcall*)(Unit*);
using ObjectLabel=void(__fastcall*)(char*,Unit*);
struct Operation {Game* game;Unit* object;Unit* player;void* region;int index;};
using Operate=int(__fastcall*)(Operation*,int);
Tick originalTick=nullptr;
Remove originalRemove=nullptr;
Operate originalOperate=nullptr;
Destroy originalDestroy=nullptr;
SpawnPreset originalSpawnPreset=nullptr;
Choose originalChoose=nullptr;
Name originalName=nullptr;
ObjectLabel originalObjectLabel=nullptr;
thread_local Game* rebuilding=nullptr;
std::atomic<bool> installed{false};
std::atomic<bool> survivorsRequested{false};
std::atomic<void(*)(Game*)> actTick{nullptr};
std::atomic<const char*> status{"Cube maps are not installed."};
std::atomic<uint32_t> serial{0};
std::atomic<uint32_t> layoutSerial{0};
uint8_t* populationCode=nullptr;
// Capture before the HUD's shared monster initializer detour is attached.
std::array<uint8_t,MonsterNativeData::Size> initializerSource{};
constexpr uint32_t Pinned=0x400000;
constexpr size_t MaxRooms=4096,MaxUnits=1000000;
struct Position {int level=0,x=0,y=0;};
struct Pin {void* room=nullptr;bool original=false;};
struct SurvivorAvatar {
    Game* game=nullptr;Client* client=nullptr;Unit* hero=nullptr;Unit* avatar=nullptr;
    Client saved{},creation{};void* heroSlot=nullptr;int slot=-1;
    Room* townRoom=nullptr;Position town{};
    Pin townPin{};
    std::array<int,3> baseMaximum{};
    std::array<int,4> baseAttributes{};
};
struct Buff {Unit* unit=nullptr;void* list=nullptr;};
struct MonsterInitializer {
    uint8_t* code=nullptr;
    ~MonsterInitializer(){if(code){RtlDeleteFunctionTable(reinterpret_cast<RUNTIME_FUNCTION*>(code+0x2800));VirtualFree(code,0,MEM_RELEASE);}}
};
#include "CubeMapLoot.inl"
std::unique_ptr<MonsterInitializer> BuildInitializer(int level){
    namespace N=MonsterNativeData;
    auto result=std::make_unique<MonsterInitializer>();
    SYSTEM_INFO info{};GetSystemInfo(&info);const auto base=Pattern::Address(0),step=uintptr_t(info.dwAllocationGranularity);
    for(uintptr_t delta=step;delta<0x40000000 && !result->code;delta+=step)for(int direction:{1,-1}) {
        const auto candidate=(direction>0 ? base+delta : base>delta ? base-delta : 0)&~(step-1);
        if(candidate)result->code=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(candidate),N::Allocation,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(result->code)break;
    }
    auto code=result->code;if(!code)return nullptr;
    memcpy(code,initializerSource.data(),N::Size);
    for(const auto fix:N::Fixups) {
        const auto difference=int64_t(Pattern::Address(fix.target))-int64_t(reinterpret_cast<uintptr_t>(code)+fix.next);
        if(difference<INT32_MIN || difference>INT32_MAX)return nullptr;
        const int32_t value=int32_t(difference);memcpy(code+fix.offset,&value,4);
    }
    // Immutable, per-run copy: override Normal and expansion NM/Hell level
    // selection before native HP, damage, attack rating, XP and skills initialize.
    for(const auto [offset,length]:{std::pair{0x21bu,9u},std::pair{0x28bu,6u}}) {
        memset(code+offset,0x90,length);code[offset]=0x41;code[offset+1]=0xbf;memcpy(code+offset+2,&level,4);
    }
    for(const auto& block:N::UnwindBlocks)memcpy(code+block.offset,block.bytes.data(),block.size);
    auto runtime=reinterpret_cast<RUNTIME_FUNCTION*>(code+0x2800);
    for(size_t i=0;i<N::RuntimeFunctions.size();++i){const auto& r=N::RuntimeFunctions[i];runtime[i]={r.begin,r.end,r.unwind};}
    // Retain the native GS exception handler and its original cookie offset.
    const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(code+0x2d00,jump,6);
    const auto handler=Pattern::Address(0x14359a4);memcpy(code+0x2d06,&handler,8);
    DWORD old=0;if(!VirtualProtect(code,N::Allocation,PAGE_EXECUTE_READ,&old))return nullptr;
    FlushInstructionCache(GetCurrentProcess(),code,N::Allocation);
    if(!RtlAddFunctionTable(runtime,DWORD(N::RuntimeFunctions.size()),reinterpret_cast<uintptr_t>(code)))return nullptr;
    return result;
}
struct Run {
    uint32_t id=0,sequence=0,created=0,lastSent=0;
    int level=0,state=0;
    Phase phase=Phase::Preparing;
    Position entrance{};
    std::array<uint32_t,2> portals{};
    std::set<uint32_t> entryParty;
    bool entryCaptured=false,populationChanged=false;
    uint32_t layoutSeed=0;
    std::vector<Pin> pins;
    std::optional<Pin> entrancePin;
    std::vector<Roll> rolls;
    std::array<int,Effects.size()> stats{};
    std::array<int,Effects.size()> pairedStats{};
    std::map<uint32_t,Position> returns;
    std::set<uint32_t> returning;
    std::optional<uint32_t> clearedAt;
    std::map<uint32_t,Buff> players,monsters;
    std::vector<std::pair<int,Position>> bosses;
    int throne=-1;
    int monsterLevel=0;
    std::unique_ptr<MonsterInitializer> initializer;
    std::vector<int> roster;
    std::mt19937 random;
    std::map<uint32_t,uint8_t> rewards;
    std::optional<uint32_t> completedAt;
    Event event=Event::None;
    EventPhase eventPhase=EventPhase::None;
    EventFailure failure=EventFailure::None;
    uint32_t eventFrame=0,eventStarted=0,creator=0;
    int characterLevel=1;
    uint8_t die=0;bool success=false,timedOut=false;
    std::set<uint32_t> ready,objectives,goldDropped,levelsFinalized;
    struct Encounter {int monster=-1,unique=-1;};
    std::vector<Encounter> encounters;
    std::map<uint32_t,uint8_t> immunities;
    std::vector<std::pair<int,int>> eventPlayerStats;
    std::vector<AutomapData::Entry> automapCells;
    std::set<uint32_t> automapSent;
    uint32_t automapRevision=0;
    struct Survival {
        std::map<uint32_t,Survivors::Build> builds;
        std::set<uint32_t> credited;
        std::set<uint32_t> elites;
        std::map<uint32_t,uint32_t> corpses;
        std::map<uint32_t,int> life;
        uint32_t nextWave=0,boss=0;
        uint16_t wave=0;
        bool bossSpawned=false;
        uint64_t pauseSynced=0;
    } survival;
    Census census;
    uint8_t targetPacks=0;uint16_t spawnedPacks=0;
    int eventTreasureClass=-1;
};
struct GameState {
    int seed=0;uint32_t frame=0;
    std::map<uint32_t,Position> towns;
    std::unique_ptr<Run> run;
    std::set<int> restoreLevels;
    std::map<int,uint32_t> layoutSeeds;
    std::map<std::tuple<uint32_t,uint32_t,int>,uint32_t> layoutAcks;
    uint32_t lastLayoutSent=0;
    uint32_t layoutEpoch=0;
    std::set<std::pair<uint32_t,uint32_t>> layoutResets;
    std::map<uint32_t,std::map<Event,uint32_t>> timedRewards;
    uint32_t rewardSequence=0,lastRewardSent=0;
};
void EventTrace(Game* game,const Run& run,const char* stage,int monster=-1,int unique=-1){
    if(run.event!=Event::None)CrashDiagnostics::Event(stage,run.id,unsigned(run.event),game->dwGameFrame,monster,unique);
}
std::recursive_mutex serverMutex;
std::map<Game*,GameState> games;
std::mutex viewMutex;
Packet view{};uint64_t receivedAt=0;
std::atomic<uint64_t(*)()> artworkProvider{nullptr};
std::atomic<ImFont*(*)(float)> fontProvider{nullptr};
uint64_t readySince=0,lastReadySent=0;
bool survivorsAvailable=false;
bool saveLoadAvailable=false;
char saveLoadStatus[192]{"Character save/load hooks are not installed."};
char compatibilityStatus[192]{};
std::string survivorsCompatibilityDetails;
std::map<Client*,std::unique_ptr<SurvivorAvatar>> survivorAvatars;
std::unique_ptr<Survivors::Roster> survivorRoster;
std::recursive_mutex survivorRosterMutex;
std::atomic<bool> survivorRosterError{false};
Survivors::SnapshotBody survivorView{};uint64_t survivorReceived=0;
using Quality=int(__fastcall*)(void*,Unit*,Unit*,int,int,void*);
Quality originalQuality=nullptr;
std::unique_ptr<MonsterInitializer> uniqueQuality,setQuality,treasureQuality;
using ManaConsume=int(__fastcall*)(Unit*,int);
using ManaCheck=int(__fastcall*)(Unit*);
ManaConsume originalManaConsume=nullptr;ManaCheck originalManaCheck=nullptr;
using ManaStat=void(__fastcall*)(Unit*,int,int,uint16_t);
ManaStat originalManaSet=nullptr,originalManaAdd=nullptr;
Room* GetRoom(Unit* unit){return unit ? Native<Room*(__fastcall*)(Unit*)>(0x209530)(unit) : nullptr;}
int Level(Room* room){const auto r2=Field<void*>(room,0x18);const auto level=r2 ? Field<void*>(r2,0x90) : nullptr;return level ? Field<int>(level,0x1f8) : 0;}
int Level(Unit* unit){return Level(GetRoom(unit));}
Position Location(Unit* unit){
    Position p{Level(unit)};
    if(!unit || !unit->pDynamicPath)return p;
    p.x=Field<uint16_t>(unit->pDynamicPath,2);p.y=Field<uint16_t>(unit->pDynamicPath,6);return p;
}
Unit* Lookup(Game* game,uint32_t type,uint32_t id){return Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0)(game,int(type),id);}
int Stat(Unit* unit,int stat){return Native<int(__fastcall*)(Unit*,uint32_t,uint16_t)>(0x1e5620)(unit,stat,0);}
void UnitStat(Unit* unit,int stat,int value){Native<void(__fastcall*)(Unit*,int,int,uint16_t)>(0x1e5440)(unit,stat,value,0);}
int LookupName(uint32_t global,const char* name){return CompiledNames::Resolve(global,name);}
int MapState(){
    const int id=LookupName(0x1c9ec08,"hud_cube_map");
    const auto count=Field<uint64_t>(Native<void*>(0x1c9ebf8),0);
    const auto table=Field<uint8_t*>(Native<void*>(0x1c9ebf0),0);
    if(id<=0 || uint64_t(id)>=count || count>512 || !table || (table[size_t(id)*0x40+0x10]&1))return -1;
    return id;
}
bool Dead(Unit* unit){return !unit || (unit->dwFlags&0x10000) || unit->dwAnimMode==0 || unit->dwAnimMode==(unit->dwUnitType==0 ? 17u : 12u);}
bool Monster(Unit* unit,bool count=true){
    if(!unit || unit->dwUnitType!=1 || !unit->pMonsterData || !unit->pMonsterData->pMonstatsTxt)return false;
    const auto flags=unit->pMonsterData->pMonstatsTxt->dwMonStatsFlags;
    if(!(flags&(1u<<15)) || (flags&0x700) || (count && (flags&(1u<<18))))return false;
    // Alignment excludes mercenaries, summons and neutral units. Converted
    // enemies already in the census stay outstanding until death/return.
    return Stat(unit,172)==0;
}
bool RunMonster(Unit* unit,const Run& run){
    // Event objectives are explicitly selected and validated by the server.
    // Mod treasure goblins use neverCount to stay out of normal area totals;
    // they still belong to the objective count when spawned for this event.
    const bool objective=unit && run.event!=Event::None && run.objectives.contains(unit->dwUnitId);
    return Monster(unit,!objective);
}
std::vector<Unit*> RoomUnits(Room* room){
    std::vector<Unit*> units;
    for(auto unit=Field<Unit*>(room,0xa8);unit && units.size()<MaxUnits;unit=unit->pRoomNext)units.push_back(unit);
    return units;
}
std::vector<Unit*> Players(Game* game){
    std::vector<Unit*> result;
    auto client=game->pClientList;
    for(size_t n=0;client && n<64;++n,client=client->pNext) {
        if(client->pGame!=game || client->dwClientState!=4)continue;
        if(auto player=Lookup(game,0,client->dwUnitGUID))result.push_back(player);
    }
    return result;
}
bool ContainsList(Unit* unit,void* wanted){
    if(!unit || !unit->pStatListEx || !wanted)return false;
    for(size_t head:{size_t(0x68),size_t(0x70)}) {
        auto list=Field<void*>(unit->pStatListEx,head);
        for(size_t n=0;list && n<1024;++n) {
            if(list==wanted)return true;
            const auto next=Field<void*>(list,0x48);if(next==list)break;list=next;
        }
    }
    return false;
}
void StateBit(Unit* unit,int state,bool on){
    if(!unit || !unit->pStatListEx)return;
    const unsigned count=unsigned(Field<uint64_t>(Native<void*>(0x1c9ebf8),0));
    if(state<=0 || unsigned(state)>=count || count>512 || Field<int>(unit->pStatListEx,0x1c)>=0)return;
    const size_t word=size_t(state)/32,words=(count+31)/32;
    const auto bit=uint32_t(1)<<(state%32);
    auto active=Field<uint32_t>(unit->pStatListEx,0xac8+word*4);
    Set(unit->pStatListEx,0xac8+word*4,on ? active|bit : active&~bit);
    auto changed=Field<uint32_t>(unit->pStatListEx,0xac8+(words+word)*4);
    Set(unit->pStatListEx,0xac8+(words+word)*4,changed|bit);
    Native<void(__fastcall*)(Unit*)>(0x22e750)(unit);
}
void ClampLife(Unit* unit){const int maximum=Stat(unit,7),life=Stat(unit,6);if(maximum>0 && life>maximum)UnitStat(unit,6,maximum);}
void Detach(Unit* unit,Buff buff,int state){
    // Death or unit destruction may already have freed the temporary list.
    if(unit==buff.unit && ContainsList(unit,buff.list)) {
        Native<void(__fastcall*)(void*)>(0x1e4300)(buff.list);
        Native<void(__fastcall*)(void*)>(0x1e45b0)(buff.list);
    }
    if(unit && unit==buff.unit){StateBit(unit,state,false);ClampLife(unit);}
}
Buff Attach(Unit* unit,Run& run,Side side,bool fill=false){
    if(side==Side::Player && run.event!=Event::None)CrashDiagnostics::Event("player-buff-begin",run.id,unsigned(run.event),0,int(unit->dwUnitId));
    // NEWLENGTH (0x2) with a zero expiration is removed on the next native
    // expiration pass. Map effects are untimed and explicitly detached by us.
    void* list=Native<void*(__fastcall*)(uint32_t,int,uint32_t,uint32_t)>(0x1e4700)(0,0,unit->dwUnitType,unit->dwUnitId);
    if(!list)return {};
    Set(list,0x20,run.state);
    for(const auto roll:run.rolls)if(roll.effect!=LevelEffect && roll.effect!=TreasureEffect && Effects[roll.effect].side==side){
        Native<int(__fastcall*)(void*,int,int,uint16_t)>(0x1e5270)(list,run.stats[roll.effect],roll.value,0);
        if(Effects[roll.effect].pairedStat)Native<int(__fastcall*)(void*,int,int,uint16_t)>(0x1e5270)(list,run.pairedStats[roll.effect],PairedValue(Effects[roll.effect],roll.value),0);
    }
    if(side==Side::Player)for(const auto [stat,value]:run.eventPlayerStats)
        Native<int(__fastcall*)(void*,int,int,uint16_t)>(0x1e5270)(list,stat,(std::max)(0,value-Stat(unit,stat)),0);
    if(side==Side::Player && run.event==Event::Survivors){
        const auto& ranks=run.survival.builds[unit->dwUnitId].ranks;
        for(const auto [name,value]:{std::pair{"item_fastermovevelocity",8*int(ranks[size_t(Survivors::Upgrade::Fleet)])},std::pair{"damageresist",10*int(ranks[size_t(Survivors::Upgrade::Guard)])}}){
            const int stat=LookupName(0x1c9fb90,name);if(stat>=0 && stat<511 && value)Native<int(__fastcall*)(void*,int,int,uint16_t)>(0x1e5270)(list,stat,value,0);
        }
    }
    Native<void(__fastcall*)(Unit*,void*,int)>(0x1e4ea0)(unit,list,1);
    if(!ContainsList(unit,list)){Native<void(__fastcall*)(void*)>(0x1e45b0)(list);return {};}
    StateBit(unit,run.state,true);
    if(fill){const int maximum=Stat(unit,7);if(maximum>0)UnitStat(unit,6,maximum);}else ClampLife(unit);
    if(side==Side::Player && run.event!=Event::None)CrashDiagnostics::Event("player-buff-complete",run.id,unsigned(run.event),0,int(unit->dwUnitId));
    return {unit,list};
}
void ApplyTrialImmunities(Unit* unit,Run& run){
    if(run.event==Event::Trial && run.objectives.contains(unit->dwUnitId) && !run.immunities.contains(unit->dwUnitId)) {
        constexpr std::array<int,6> resistance{36,37,39,41,43,45};std::array<unsigned,6> order{0,1,2,3,4,5};std::shuffle(order.begin(),order.end(),run.random);
        uint8_t mask=0;for(size_t n=0;n<3;++n){const int id=resistance[order[n]];UnitStat(unit,id,(std::max)(150,Stat(unit,id)));mask|=uint8_t(1u<<order[n]);}
        run.immunities.emplace(unit->dwUnitId,mask);
    }
 }
void EnsureMonsterBuff(Unit* unit,Run& run){
    ApplyTrialImmunities(unit,run);
    auto [entry,inserted]=run.monsters.try_emplace(unit->dwUnitId);
    auto& buff=entry->second;
    const bool first=inserted || (buff.unit && buff.unit!=unit);
    if(first || !ContainsList(unit,buff.list)) {
        buff=Attach(unit,run,Side::Monster,first);
        // Remember an attempted first application even after an allocation
        // failure: a later repair must never refill existing combat damage.
        if(!buff.unit)buff.unit=unit;
    }
}
GameState& State(Game* game){
    auto& state=games[game];
    if(state.seed!=game->dwInitSeed || state.frame>game->dwGameFrame) {
        // Old game's native allocations are already gone. Never dereference
        // pins/list pointers from a game whose address has been reused.
        state=GameState{};state.seed=game->dwInitSeed;
    }
    state.frame=game->dwGameFrame;if(!state.layoutEpoch){state.layoutEpoch=++layoutSerial;if(!state.layoutEpoch)state.layoutEpoch=++layoutSerial;}return state;
}
Run* Active(Game* game){const auto it=games.find(game);return it!=games.end() && it->second.seed==game->dwInitSeed ? it->second.run.get() : nullptr;}
bool ValidLevel(int level){const auto count=Field<uint32_t>(Native<void*>(0x1c9fd10),0);return level>0 && level<1024 && unsigned(level)<count && !Town(level);}
#include "CubeMapDestinations.inl"
int Act(int level){
    const auto table=Field<uint8_t*>(Native<void*>(0x1c9ea80),0);const auto count=Field<int>(Native<void*>(0x1c9ea88),0);
    if(!table || count<1 || count>6)return -1;
    for(int i=count-1;i>=0;--i)if(level>=Field<int>(table+size_t(i)*0x8c,4))return i;
    return -1;
}
void* LoadAct(Game* game,int level){
    const int act=Act(level);if(act<0)return nullptr;
    const auto acts=Field<void**>(game,0x198);if(!acts)return nullptr;
    if(!acts[act])Native<void(__fastcall*)(Game*,int)>(0x2e0fb0)(game,act);
    return acts[act];
}
Room* Spawn(Game* game,int level,Position& position){
    const auto act=LoadAct(game,level);if(!act)return nullptr;
    auto drlg=Field<void*>(act,0x70);int x=-1,y=-1;
    auto room=Native<Room*(__fastcall*)(void*,int,int,int*,int*)>(0x25a080)(drlg,level,11,&x,&y);
    if(!room || x<0 || y<0 || x>13000 || y>13000 || Level(room)!=level)return nullptr;
    position={level,x*5+3,y*5+3};return room;
}
Room* Activate(void* room){
    auto flags=Field<uint32_t>(room,0x50);
    if(!(flags&(1u<<24)))Native<void(__fastcall*)(void*)>(0x26f360)(room);
    flags=Field<uint32_t>(room,0x50);
    if(!(flags&(1u<<25)) && Field<int>(room,0x74)==2)Native<void(__fastcall*)(void*)>(0x25d830)(room);
    if(!(Field<uint32_t>(room,0x50)&(1u<<20)))Native<void(__fastcall*)(void*)>(0x241e50)(room);
    return Field<Room*>(room,0x58);
}
void Unpin(Run& run){
    for(const auto pin:run.pins)if(!pin.original)Set(pin.room,0x50,Field<uint32_t>(pin.room,0x50)&~Pinned);
    run.pins.clear();
    if(run.entrancePin && !run.entrancePin->original)Set(run.entrancePin->room,0x50,Field<uint32_t>(run.entrancePin->room,0x50)&~Pinned);
    run.entrancePin.reset();
}
bool FreePosition(Room*& room,Position& position){
    struct Coord{int x,y;} coord{position.x,position.y};Room* available=nullptr;
    Native<void(__fastcall*)(Room*,Coord*,int,int,Room**)>(0x2a80d0)(room,&coord,3,0x400,&available);
    if(!available || Level(available)!=position.level)return false;
    room=available;position.x=coord.x;position.y=coord.y;return true;
}
bool Occupied(Room* room){
    if(Field<uint64_t>(room,0x70))return true; // includes adjacent observers
    for(auto unit:RoomUnits(room)) {
        if(unit->dwUnitType==0)return true; // also player corpses
        if(unit->dwUnitType==1 && unit->pMonsterData && unit->pMonsterData->pMonstatsTxt &&
           !(unit->pMonsterData->pMonstatsTxt->dwMonStatsFlags&0x700) && Stat(unit,172)==2)return true;
    }
    return false;
}
#include "CubeMapLayouts.inl"
bool RandomCandidate(int id){
    const auto count=Field<uint64_t>(Native<void*>(0x1c9f880),0);
    const auto table=Field<uint8_t*>(Native<void*>(0x1c9f878),0);
    if(id<0 || uint64_t(id)>=count || !table)return false;
    const auto record=table+size_t(id)*0x1dc;const auto flags=Field<uint32_t>(record,0x3c);
    return (flags&((1u<<25)|(1u<<15)|1u))==((1u<<25)|(1u<<15)|1u) &&
        !(flags&(0x700|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<18))) && record[0x80]==0;
}
bool BuildRoster(Run& run){
    const auto levels=Field<uint8_t*>(Native<void*>(0x1c9fd08),0);
    const auto count=Field<uint32_t>(Native<void*>(0x1c9fd10),0);
    if(!levels || !count || count>1024)return false;
    // Only monsters a mod already uses for ordinary level population enter
    // the pool. Quest actors, pets, traps and multipart-only monsters stay out.
    std::set<int> pool;
    for(size_t level=1;level<count;++level)for(size_t offset:{size_t(0x3a),size_t(0x6c)})for(size_t n=0;n<25;++n) {
        const int id=Field<int16_t>(levels+level*0x184,offset+n*2);
        if(RandomCandidate(id))pool.insert(id);
    }
    run.roster.assign(pool.begin(),pool.end());std::shuffle(run.roster.begin(),run.roster.end(),run.random);
    if(run.roster.size()>8)run.roster.resize(8);
    return !run.roster.empty();
}
int RandomMonster(Run& run){return run.roster[std::uniform_int_distribution<size_t>(0,run.roster.size()-1)(run.random)];}
int __fastcall ChooseHook(Game* game,void* region,Room* room,void** record,uint8_t chance,int unique){
    std::lock_guard lock(serverMutex);const auto run=Active(game);
    if(game==rebuilding && run && !run->roster.empty() && run->level==Level(room)) {
        const int id=RandomMonster(*run);
        if(record)*record=Field<uint8_t*>(Native<void*>(0x1c9f878),0)+size_t(id)*0x1dc;
        return id;
    }
    return originalChoose(game,region,room,record,chance,unique);
}
#include "CubeMapEncounters.inl"
#include "CubeMapPacks.inl"
#include "CubeMapEventPacks.inl"
bool Reload(Game* game,Run& run,bool ordinary=false){
    EventTrace(game,run,"reload-begin");
    if(run.event!=Event::None)run.failure=EventFailure::Reset;
    Position entry;auto initial=Spawn(game,run.level,entry);if(!initial)return false;
    auto level=Field<void*>(Field<void*>(initial,0x18),0x90);
    std::set<void*> seen;
    for(auto room=Field<void*>(level,0x10);room;room=Field<void*>(room,0x48)) {
        if(!seen.insert(room).second || seen.size()>MaxRooms)return false;
        const auto flags=Field<uint32_t>(room,0x50);
        run.pins.push_back({room,(flags&Pinned)!=0});Set(room,0x50,flags|Pinned);
        auto active=Activate(room);if(!active)return false;
        if(Occupied(active)){if(run.event!=Event::None)run.failure=EventFailure::Occupied;return false;}
    }
    if(run.pins.empty())return false;
    D2RHUD_CubeMapDiagnostics.rooms=uint32_t(run.pins.size());D2RHUD_CubeMapDiagnostics.stage=2;
    // Restore inactive units first. Old dead-monster records must not get
    // restored over the fresh population after the portal has opened.
    for(const auto pin:run.pins) {
        auto room=Field<Room*>(pin.room,0x58);
        Native<void(__fastcall*)(Game*,Room*)>(0x2ed040)(game,room);
    }
    for(const auto pin:run.pins)if(Occupied(Field<Room*>(pin.room,0x58))){if(run.event!=Event::None)run.failure=EventFailure::Occupied;return false;}
    D2RHUD_CubeMapDiagnostics.stage=3;
    EventTrace(game,run,"reload-restored-old-units");
    run.throne=!ordinary && run.level==131 ? LookupName(0x1c9f890,"baalthrone") : -1;
    const int scriptedBoss=ordinary || run.event!=Event::None ? -1 : run.level==108 ? LookupName(0x1c9f890,"diablo") :
        (run.level==131 || run.level==132) ? LookupName(0x1c9f890,"baalcrab") : -1;
    std::optional<Position> thronePosition;
    for(const auto pin:run.pins)for(auto unit:RoomUnits(Field<Room*>(pin.room,0x58)))if(unit->dwUnitType==1 && unit->pMonsterData && unit->pMonsterData->pMonstatsTxt) {
        if(int(unit->dwClassId)==run.throne)thronePosition=Location(unit);
        if(!ordinary && run.event==Event::None && Monster(unit,false) && (unit->pMonsterData->pMonstatsTxt->dwMonStatsFlags&(1u<<6)))run.bosses.push_back({int(unit->dwClassId),Location(unit)});
    }
    if(scriptedBoss>=0 && std::none_of(run.bosses.begin(),run.bosses.end(),[&](const auto& boss){return boss.first==scriptedBoss;}))
        run.bosses.push_back({scriptedBoss,thronePosition.value_or(entry)});
    // Every occupancy check precedes this point. From here the game thread
    // performs its normal room unload, preserving items and non-monster units.
    run.populationChanged=true;
    for(const auto pin:run.pins) {
        auto room=Field<Room*>(pin.room,0x58);
        for(auto unit:RoomUnits(room)) {
            if(unit->dwUnitType==1 && (Monster(unit,false) || int(unit->dwClassId)==run.throne))originalRemove(game,unit);
            else Native<void(__fastcall*)(Game*,Unit*)>(0x2edb80)(game,unit);
        }
        auto act=LoadAct(game,run.level);
        Native<void(__fastcall*)(void*,Room*)>(0x1d1420)(act,room);
        Set(pin.room,0xa0,uint32_t(0)); // population is no longer restored
        // Preset objects retain their prior state; only monster presets recur.
        for(auto preset=Field<void*>(pin.room,0x98);preset;preset=Field<void*>(preset,0x10))
            if(Field<int>(preset,0x20)==1 || (run.level>=133 && run.level<=136))Set(preset,0x28,Field<uint32_t>(preset,0x28)&~1u);
        auto info=Field<void*>(pin.room,0xa8);
        for(auto node=info ? Field<void*>(info,0x58) : nullptr;node;node=Field<void*>(node,0x30))Set(node,0x20,int(0));
    }
    D2RHUD_CubeMapDiagnostics.stage=4;
    EventTrace(game,run,"reload-unloaded-old-rooms");
    for(const auto pin:run.pins)if(!Activate(pin.room))return false;
    // Native superuniques also have a per-game spawned bit, independent of
    // the room's preset flag. Clear only IDs present in this destination.
    const auto monsters=Field<uint64_t>(Native<void*>(0x1c9f880),0);
    const auto uniques=Field<uint64_t>(Native<void*>(0x1c9f9a0),0);
    for(const auto pin:run.pins)for(auto preset=Field<void*>(pin.room,0x98);preset;preset=Field<void*>(preset,0x10))if(Field<int>(preset,0x20)==1) {
        const int64_t unique=int64_t(Field<int>(preset,4))-int64_t(monsters);
        if(unique>=0 && uint64_t(unique)<uniques && unique<512) {
            const size_t offset=0x5670+size_t(unique)/8;
            Set(game,offset,uint8_t(Field<uint8_t>(game,offset)&~(1u<<(unique%8))));
        }
    }
    // Native Uber Duriel chooses one preset room using this population counter.
    if(run.level==134)Set(game,0x5848,uint32_t(0));
    MapPackPopulationScope packScope(game,run,!ordinary);if(!packScope.Valid())return false;
    run.phase=Phase::Open;
    struct Scope {Game* previous;Scope(Game* game):previous(rebuilding){rebuilding=game;}~Scope(){rebuilding=previous;}} scope(ordinary ? nullptr : game);
    for(const auto pin:run.pins) {
        auto room=Field<Room*>(pin.room,0x58);
        Native<void(__fastcall*)(Game*,Room*)>(0x2ed040)(game,room);
        // Spawn only monsters. Native object restoration above preserves
        // chests, quest objects and existing loot without duplicating them.
        // Retain native special-level boss triggers. The scoped preset hook
        // reuses existing non-monster objects instead of duplicating them.
        Native<void(__fastcall*)(Game*,Room*)>(0x2c59d0)(game,room);
        if(ordinary)Native<void(__fastcall*)(Game*,Room*)>(NativeData::Source)(game,room);
        else if(run.event==Event::None || RegularEvent(run.event) || run.event==Event::SoulSurprise)reinterpret_cast<void(__fastcall*)(Game*,Room*)>(populationCode)(game,room);
        Set(room,0x54,Field<uint32_t>(room,0x54)|3u);
    }
    // Scripted bosses may already have consumed their quest trigger. Respawn
    // the combat unit, rather than changing any player's saved quest flags.
    std::map<int,size_t> existing,wanted;
    for(const auto pin:run.pins)for(auto unit:RoomUnits(Field<Room*>(pin.room,0x58)))if(unit->dwUnitType==1 && !Dead(unit))++existing[int(unit->dwClassId)];
    for(const auto& [boss,saved]:run.bosses) {
        if(++wanted[boss]<=existing[boss])continue;
        Position position=saved;Room* room=nullptr;
        for(const auto pin:run.pins) {
            auto candidate=Field<Room*>(pin.room,0x58);
            const int x=Field<int>(candidate,0x80),y=Field<int>(candidate,0x84),w=Field<int>(candidate,0x88),h=Field<int>(candidate,0x8c);
            if(position.x>=x && position.y>=y && position.x<x+w && position.y<y+h){room=candidate;break;}
        }
        if(!room)room=Spawn(game,run.level,position);
        if(!room || !FreePosition(room,position))return false;
        if(!Native<Unit*(__fastcall*)(Game*,Room*,int,int,int,int,int)>(0x2c5760)(game,room,1,boss,position.x,position.y,1))return false;
    }
    if(!ordinary && !PopulateMapPacks(game,run,entry)){run.failure=EventFailure::Placement;return false;}
    if(!ordinary && !PopulateChampionClash(game,run,entry)){run.failure=EventFailure::Placement;return false;}
    if((run.event==Event::Trial || run.event==Event::GoblinGrotto) && !SpawnEncounters(game,run,entry)){run.failure=EventFailure::Placement;return false;}
    if(RegularEvent(run.event)){
        bool populated=false;for(const auto pin:run.pins)for(auto unit:RoomUnits(Field<Room*>(pin.room,0x58)))if(Monster(unit) && !Dead(unit))populated=true;
        if(!populated){run.failure=EventFailure::Placement;return false;}
    }
    EventTrace(game,run,"reload-complete");
    D2RHUD_CubeMapDiagnostics.stage=5;
    return true;
}
Unit* Portal(Game* game,Room* room,Position position,int destination){
    if(!FreePosition(room,position))return nullptr;
    auto portal=Native<Unit*(__fastcall*)(int,int,int,int,Game*,Room*,uint16_t,int,int)>(0x2c42a0)(2,60,position.x,position.y,game,room,1,2,0);
    if(!portal || !portal->pObjectData)return nullptr;
    // Both portals remain ordinary red portal objects for rendering. Only our
    // GUIDs use the extended destination and cross-act interaction handler.
    Set(portal->pObjectData,8,uint8_t(destination<=255 ? destination : 0));Set(portal->pObjectData,9,uint8_t(3));
    return portal;
}
void DeletePortal(Game* game,uint32_t id){
    if(auto portal=Lookup(game,2,id)) {
        auto room=GetRoom(portal);
        Native<void(__fastcall*)(Room*,int,uint32_t)>(0x1d1280)(room,2,id);
        originalRemove(game,portal);
    }
}
void CaptureMapParty(Game* game,Run& run,Unit* entrant=nullptr){
    if(run.event!=Event::None || run.entryCaptured)return;
    size_t n=0;for(auto client=game->pClientList;client && n++<64;client=client->pNext)if(client->pGame==game){
        auto player=Lookup(game,0,client->dwUnitGUID);
        if(!player || Town(Level(player)) || Level(player)==run.level)run.entryParty.insert(client->dwUnitGUID);
    }
    if(entrant)run.entryParty.insert(entrant->dwUnitId);run.entryCaptured=true;
}
void CloseMapEntry(Game* game,Run& run){
    if(run.event!=Event::None || !run.census.entered || !run.entryCaptured)return;
    std::set<uint32_t> connected;
    size_t n=0;for(auto client=game->pClientList;client && n++<64;client=client->pNext)
        if(client->pGame==game)connected.insert(client->dwUnitGUID);
    for(auto id:run.entryParty)if(connected.contains(id)) {
        auto player=Lookup(game,0,id);
        if(!player || Level(player)!=run.level)return;
    }
    for(auto& id:run.portals)if(id){const auto old=id;id=0;DeletePortal(game,old);}
}
bool RestoreSurvivor(Game*,Unit*,bool notify=true);
void Clean(Game* game,Run& run){
    for(const auto& [id,buff]:run.players)Detach(Lookup(game,0,id),buff,run.state);
    for(const auto& [id,buff]:run.monsters){
        auto unit=Lookup(game,1,id);Detach(unit,buff,run.state);
        if(run.event!=Event::None && unit && unit==buff.unit && Level(unit)==run.level)originalRemove(game,unit);
    }
    run.players.clear();run.monsters.clear();
    if(run.event==Event::Survivors){auto participants=Players(game);for(auto p:participants)RestoreSurvivor(game,p);}
    for(auto& id:run.portals)if(id){const auto old=id;id=0;DeletePortal(game,old);}
    if(run.populationChanged){State(game).restoreLevels.insert(run.level);run.populationChanged=false;}
    Unpin(run);run.phase=Phase::Closed;
    D2RHUD_CubeMapDiagnostics.stage=9;
}
bool CanReplaceMap(Game* game,const Run& run){
    if(run.event!=Event::None || (run.phase!=Phase::Open && run.phase!=Phase::Running))return false;
    for(auto player:Players(game))if(Level(player)==run.level)return false;
    for(const auto pin:run.pins){
        if(LayoutRoomReferenced(pin.room))return false;
        if(auto room=Field<Room*>(pin.room,0x58);room && Occupied(room))return false;
    }
    return true;
}
bool ReleaseMapForRecipe(Game* game,int destination){
    auto& state=State(game);if(!state.run)return true;
    if(!CanReplaceMap(game,*state.run))return false;
    const int oldLevel=state.run->level;
    Clean(game,*state.run);state.run.reset();
    // A same-level recipe immediately replaces the abandoned population.
    // Keep its restoration queued until RerollLayout succeeds, so a refusal
    // still restores ordinary monsters. Other destinations restore now.
    if(oldLevel!=destination && state.restoreLevels.contains(oldLevel)){
        Run ordinary;ordinary.level=oldLevel;
        const bool restored=Reload(game,ordinary,true);Unpin(ordinary);
        if(!restored)return false;
        state.restoreLevels.erase(oldLevel);
    }
    return true;
}
bool RollModifiers(Run& run,Game* game){
    run.stats.fill(-1);run.pairedStats.fill(-1);run.rolls.clear();
    const auto resolve=[&](const char* name,int low,int high){
        if(!name)return -1;const int id=LookupName(0x1c9fb90,name);
        const auto table=Field<uint8_t*>(Native<void*>(0x1c9fb78),0);const auto total=Field<uint64_t>(Native<void*>(0x1c9fb80),0);
        if(id<0 || id>=511 || uint64_t(id)>=total || !table)return -1;
        const auto record=table+size_t(id)*0x144;const auto bits=record[8];const bool signedValue=(Field<uint32_t>(record,4)&2)!=0;
        if(!bits || bits>32)return -1;
        const int64_t minimum=signedValue ? -(int64_t(1)<<(bits-1)) : 0;
        const int64_t maximum=signedValue ? (int64_t(1)<<(bits-1))-1 : (int64_t(1)<<bits)-1;
        return low<minimum || high>maximum ? -1 : id;
    };
    std::mt19937 random(uint32_t(game->dwInitSeed)^game->dwGameFrame^(run.id*0x9e3779b9));
    for(Side side:{Side::Monster,Side::Player}) {
        std::vector<uint8_t> available;
        for(size_t i=0;i<Effects.size();++i)if(Effects[i].side==side) {
            if(i==TreasureEffect){available.push_back(uint8_t(i));continue;}
            const auto& effect=Effects[i];const int id=i==LevelEffect ? LookupName(0x1c9fb90,effect.stat) : resolve(effect.stat,effect.minimum,effect.maximum);
            if(id<0 || id>=511)continue;
            if(effect.pairedStat){
                const auto a=PairedValue(effect,effect.minimum),b=PairedValue(effect,effect.maximum);
                const int paired=resolve(effect.pairedStat,(std::min)(a,b),(std::max)(a,b));
                if(paired<0)continue;run.pairedStats[i]=paired;
            }
            run.stats[i]=id;available.push_back(uint8_t(i));
        }
        if(available.size()<3)return false;
        std::shuffle(available.begin(),available.end(),random);
        const size_t count=side==Side::Monster ? 3 : 4;
        size_t selected=0;for(const auto i:available) {
            if(std::any_of(run.rolls.begin(),run.rolls.end(),[&](auto roll){return ConflictingEffects(roll.effect,i);}))continue;
            const auto& effect=Effects[i];
            run.rolls.push_back({i,int16_t(std::uniform_int_distribution<int>(effect.minimum,effect.maximum)(random))});
            if(++selected==count)break;
        }
        if(selected<3)return false;
    }
    return true;
}
#include "CubeMapAutomapServer.inl"
void SendSurvivorState(Game*,Run&,Client*);
void Sync(Game* game,Run& run,bool force=false){
    const bool eventMap=run.event!=Event::None && (run.eventPhase==EventPhase::Entering || run.eventPhase==EventPhase::Active);
    if(!force && game->dwGameFrame-run.lastSent<(eventMap?5u:25u))return;
    run.lastSent=game->dwGameFrame;++run.sequence;
    D2RHUD_CubeMapDiagnostics.level=run.level;
    D2RHUD_CubeMapDiagnostics.eventFailure=uint32_t(run.failure);
    if(run.event!=Event::None)D2RHUD_CubeMapDiagnostics.stage=9+uint32_t(run.eventPhase);
    const auto markers=eventMap?EventMonsterMarkers(run):std::vector<AutomapData::Entry>{};
    if(eventMap && !++run.automapRevision)++run.automapRevision;
    auto client=game->pClientList;
    for(size_t n=0;client && n<64;++n,client=client->pNext) {
        if(client->pGame!=game || client->dwClientState!=4)continue;
        auto player=Lookup(game,0,client->dwUnitGUID);
        Packet packet;packet.run=run.id;packet.sequence=run.sequence;packet.recipient=client->dwUnitGUID;packet.session=uint32_t(game->dwInitSeed);
        packet.active=run.phase!=Phase::Closed && player && Level(player)==run.level;
        if(run.event!=Event::None && run.returns.contains(packet.recipient)) {
            packet.event=run.event;packet.eventPhase=run.eventPhase;packet.die=run.die;
            packet.eventElapsed=(game->dwGameFrame-run.eventFrame)*40;
            packet.eventSeconds=run.eventPhase==EventPhase::Active ? EventRemaining(game->dwGameFrame,run.eventStarted,run.event) : 0;
            packet.success=run.success;packet.failure=run.failure;
            if(run.eventPhase==EventPhase::Finished && run.rewards.contains(packet.recipient))packet.reward=2;
        }
        packet.level=uint16_t(run.level);packet.remaining=run.census.Remaining();packet.effects=uint8_t(run.rolls.size());
        packet.entrancePortal=run.phase==Phase::Closed ? 0 : run.portals[0];packet.exitPortal=run.phase==Phase::Closed ? 0 : run.portals[1];
        packet.town=uint16_t(run.entrance.level);packet.monsterLevel=uint16_t(run.monsterLevel);
        packet.elitePacks=run.event==Event::None ? uint8_t(run.spawnedPacks) : 0;
        std::copy(run.rolls.begin(),run.rolls.end(),packet.rolls.begin());
        Native<void(__fastcall*)(Client*)>(0x2d0130)(client);
        void* range[]{&packet,reinterpret_cast<uint8_t*>(&packet)+sizeof(packet)};
        Native<void(__fastcall*)(void*,uint32_t,void**)>(0x422ac0)(Native<void*>(0x18682b0),client->dwClientId,range);
        SendSurvivorState(game,run,client);
        if(eventMap && packet.active && packet.event!=Event::None){
            if(run.automapSent.insert(client->dwClientId).second)SendAutomap(client,packet,AutomapData::Stream::Geometry,1,run.automapCells);
            SendAutomap(client,packet,AutomapData::Stream::Monsters,run.automapRevision,markers);
        }else run.automapSent.erase(client->dwClientId);
    }
}
void CensusAndBuffs(Game* game,Run& run,bool scanMonsters=true){
    if(scanMonsters)for(const auto pin:run.pins)for(auto unit:RoomUnits(Field<Room*>(pin.room,0x58))) {
        if(unit->dwUnitType!=1)continue;
        if(run.event!=Event::None && RunMonster(unit,run) && !Dead(unit) && run.levelsFinalized.insert(unit->dwUnitId).second){
            UnitStat(unit,12,run.monsterLevel);if(run.event==Event::Survivors)UnitStat(unit,13,0);
        }
        if(RunMonster(unit,run) && (run.event!=Event::Trial || run.objectives.contains(unit->dwUnitId)))run.census.Observe(unit->dwUnitId,Dead(unit));
        else if(run.census.dead.contains(unit->dwUnitId) && Dead(unit))run.census.Observe(unit->dwUnitId,true);
        if(RunMonster(unit,run) && !Dead(unit)) {
            EnsureMonsterBuff(unit,run);
        }
    }
    for(auto it=run.players.begin();it!=run.players.end();) {
        auto unit=Lookup(game,0,it->first);
        if(!unit || Level(unit)!=run.level || Dead(unit)) {Detach(unit,it->second,run.state);it=run.players.erase(it);}else ++it;
    }
    for(auto player:Players(game))if(Level(player)==run.level) {
        run.census.entered=true;
        if(!run.returns.contains(player->dwUnitId))run.returns[player->dwUnitId]=State(game).towns.contains(player->dwUnitId) ? State(game).towns[player->dwUnitId] : run.entrance;
        if(!Dead(player) && run.phase!=Phase::Returning) {
            auto& buff=run.players[player->dwUnitId];
            if(buff.unit!=player || !ContainsList(player,buff.list))buff=Attach(player,run,Side::Player);
        }
    }
}
bool Move(Game* game,Unit* player,Position destination){
    if(Dead(player))return false;
    if(Level(player)!=destination.level)Native<void(__fastcall*)(Game*,Unit*,int,int)>(0x2e16d0)(game,player,destination.level,11);
    if(Level(player)!=destination.level)return false;
    const auto client=Field<Client*>(player->pPlayerData,0xf0);
    if(!client || client->dwClientState!=4)return false;
    auto room=GetRoom(player);
    auto target=Native<Room*(__fastcall*)(Room*,int,int)>(0x2232c0)(room,destination.x,destination.y);
    // A town spawn and the saved location can be several rooms apart. Load
    // that room through DRLG instead of repeatedly searching spawn adjacency.
    if(!target) {
        auto act=LoadAct(game,destination.level);
        auto drlg=act ? Field<void*>(act,0x70) : nullptr;
        size_t levels=0;
        for(auto level=drlg ? Field<void*>(drlg,0x868) : nullptr;level && levels++<1024;level=Field<void*>(level,0x1b8))if(Field<int>(level,0x1f8)==destination.level) {
            size_t rooms=0;
            for(auto r2=Field<void*>(level,0x10);r2 && rooms++<MaxRooms;r2=Field<void*>(r2,0x48)) {
                const int x=Field<int>(r2,0x60)*5,y=Field<int>(r2,0x64)*5,w=Field<int>(r2,0x68)*5,h=Field<int>(r2,0x6c)*5;
                if(destination.x>=x && destination.y>=y && destination.x<x+w && destination.y<y+h) {
                    target=Activate(r2);
                    if(target)Native<void(__fastcall*)(Game*,Room*)>(0x27d2d0)(game,target);
                    break;
                }
            }
            break;
        }
    }
    if(!target)return false;
    return Native<int(__fastcall*)(Game*,Unit*,Room*,int,int,int)>(0x2c7a90)(game,player,target,destination.x,destination.y,0)!=0;
}
bool AwardTrial(Run& run,Unit* player){
    if(run.event!=Event::Trial || !run.success || run.eventPhase!=EventPhase::Finished || !player || player->dwUnitType!=0 || !player->pStatListEx || LookupName(0x1c9fb90,"statpts")!=4)return false;
    if(run.rewards.contains(player->dwUnitId))return run.rewards.at(player->dwUnitId)==2;
    const auto stats=Field<uint8_t*>(Native<void*>(0x1c9fb78),0);
    if(!stats || Field<uint64_t>(Native<void*>(0x1c9fb80),0)<=4)return false;
    const auto basePoints=[&](){return Native<int(__fastcall*)(void*,uint32_t,void*)>(0x1e2e00)(player->pStatListEx,4u<<16,stats+4*0x144);};
    const int points=basePoints();if(points<0 || points>INT_MAX-2)return false;
    UnitStat(player,4,points+2);if(basePoints()!=points+2)return false;
    run.rewards.emplace(player->dwUnitId,2);return true;
}
Unit* LocalPlayer();
bool DemoSixHeld(Unit* player){
    if(!(GetAsyncKeyState(VK_CONTROL)&0x8000) || !((GetAsyncKeyState('6')|GetAsyncKeyState(VK_NUMPAD6))&0x8000))return false;
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if(foreground!=GetCurrentProcessId())return false;
    // Only the participating local host can request the shared demo event.
    const auto local=LocalPlayer();return local && player && local->dwUnitId==player->dwUnitId;
}
#include "CubeMapSurvivors.inl"
#include "CubeMapRewards.inl"
#include "CubeMapEvents.inl"
void Process(Game* game){
    std::lock_guard lock(serverMutex);auto& state=State(game);
    SendLayouts(game,state);
    ProcessTimedRewards(game,state);
    // Room observers can remain briefly after the return warp. Let native
    // client teardown finish before restoring the ordinary population.
    for(auto it=state.restoreLevels.begin();it!=state.restoreLevels.end();) {
        Run ordinary;ordinary.level=*it;
        const bool restored=Reload(game,ordinary,true);Unpin(ordinary);
        if(restored)it=state.restoreLevels.erase(it);else ++it;
    }
    for(auto player:Players(game)){const auto p=Location(player);if(Town(p.level))state.towns[player->dwUnitId]=p;}
    if(!state.run)return;
    auto& run=*state.run;
    if(ProcessEvent(game,run,state))return;
    if(run.phase==Phase::Closed) {
        Sync(game,run);
        if(run.completedAt && game->dwGameFrame-*run.completedAt>=300)state.run.reset();
        return;
    }
    const bool scanMonsters=game->dwGameFrame%5==0;
    CensusAndBuffs(game,run,scanMonsters);
    if(run.census.entered)CaptureMapParty(game,run);
    CloseMapEntry(game,run);
    D2RHUD_CubeMapDiagnostics.remaining=run.census.Remaining();
    if(scanMonsters && !run.census.Complete()) {
        run.clearedAt.reset();
        if(run.phase==Phase::Returning && !run.timedOut){run.phase=Phase::Running;run.returning.clear();}
    }
    if(scanMonsters && run.census.Complete() && !run.clearedAt)run.clearedAt=game->dwGameFrame;
    // Allow on-death spawns and resurrection events to settle before moving
    // the party. Any new live monster cancels the clear transition.
    if(run.event!=Event::Survivors && scanMonsters && run.census.Complete() && run.phase!=Phase::Returning && run.clearedAt && game->dwGameFrame-*run.clearedAt>=50){
        run.phase=Phase::Returning;
        if(run.event!=Event::None){run.success=true;run.eventPhase=EventPhase::Returning;run.eventFrame=game->dwGameFrame;for(const auto& [id,p]:run.returns)run.returning.insert(id);}
        for(auto player:Players(game))if(Level(player)==run.level)run.returning.insert(player->dwUnitId);
        status.store("Map cleared; returning players to town.");
        D2RHUD_CubeMapDiagnostics.stage=8;
    }
    if(run.phase==Phase::Returning) {
        if(run.event==Event::Survivors){
            // Restore even a dead arena avatar. Native Hardcore/death rules
            // apply to the separate unit, never to the parked normal hero.
            Sync(game,run,true);auto participants=Players(game);
            for(auto p:participants)if(AvatarFor(game,p)){RestoreSurvivor(game,p);}
            run.returning.clear();
        }
        bool occupied=false;
        for(auto player:Players(game))if(Level(player)==run.level)run.returning.insert(player->dwUnitId);
        for(auto it=run.returning.begin();it!=run.returning.end();) {
            // Keep the exit open while a dead player needs to revive or an
            // act transition has not finished. Retry on later game frames.
            auto player=Lookup(game,0,*it);
            if(!player){it=run.returning.erase(it);continue;}
            const auto destination=run.returns.contains(*it) ? run.returns[*it] : run.entrance;
            if(!Move(game,player,destination)){occupied=true;++it;}else it=run.returning.erase(it);
        }
        for(auto player:Players(game))if(Level(player)==run.level)occupied=true;
        if(!occupied){
            Clean(game,run);
            if(run.event==Event::None) {
                bool demo=false;for(const auto& [id,position]:run.returns)if(auto player=Lookup(game,0,id);player && DemoSixHeld(player))demo=true;
                run.die=uint8_t(std::uniform_int_distribution<int>(1,6)(run.random));
                run.event=SelectEvent(run.die,std::uniform_int_distribution<int>(0,99)(run.random),demo);
                if(demo)run.die=6;
                run.eventPhase=EventPhase::WaitingTown;run.eventFrame=game->dwGameFrame;run.ready.clear();
                status.store("Map cleared. Waiting for the party to finish loading town.");
            }else {
                run.eventPhase=EventPhase::Finished;run.eventFrame=game->dwGameFrame;run.completedAt=game->dwGameFrame;
                if(run.event==Event::Trial && run.success)for(const auto& [id,position]:run.returns) {
                    auto player=Lookup(game,0,id);if(player && !Dead(player) && Town(Level(player)))if(!AwardTrial(run,player))status.store("Trial cleared, but a participant stat award could not be applied.");
                }
                status.store(run.success ? "Event cleared. The party has returned to town." : "Event time expired. The party has returned to town.");
            }
            Sync(game,run,true);return;
        }
    }
    Sync(game,run);
}
void __fastcall TickHook(Game* game){
    if(installed.load() && game){
        std::lock_guard lock(serverMutex);auto run=Active(game);bool paused=false;
        if(run && run->event==Event::Survivors && run->eventPhase==EventPhase::Active)
            for(const auto& [id,b]:run->survival.builds)if(b.Paused())if(auto player=Lookup(game,0,id);player && !Dead(player) && Level(player)==run->level){paused=true;break;}
        if(paused){
            // Network input/flush runs outside GAME_UpdateProgress. Leave it
            // running so choices, disconnects and Save & Exit remain responsive.
            const auto now=GetTickCount64();if(!run->survival.pauseSynced || now-run->survival.pauseSynced>=500){run->survival.pauseSynced=now;Sync(game,*run,true);}
            return;
        }
    }
    originalTick(game);
    SummonPersistence::Tick(game);
    QuestJournal::Tick(game);
    if(auto observe=actTick.load())observe(game);
    if(installed.load() && game)Process(game);
}
void __fastcall DestroyHook(uint64_t id,Game* game){
    {std::lock_guard lock(serverMutex);auto players=Players(game);for(auto p:players)RestoreSurvivor(game,p,false);games.erase(game);}
    {std::lock_guard lock(layoutMutex);std::erase_if(serverLayoutSeeds,[&](const auto& entry){return entry.second.game==game;});}
    SummonPersistence::Destroyed(game);
    QuestJournal::Destroyed(game);
    originalDestroy(id,game);
}
void __fastcall SpawnPresetHook(Game* game,Room* room,void* preset){
    if(game==rebuilding)if(auto run=Active(game);run && run->event!=Event::None && Field<int>(preset,0x20)==1){Set(preset,0x28,Field<uint32_t>(preset,0x28)|1u);return;}
    if(game==rebuilding && Field<int>(preset,0x20)!=1){Set(preset,0x28,Field<uint32_t>(preset,0x28)|1u);return;}
    if(game==rebuilding) {
        const auto run=Active(game);
        if(run && run->throne>=0 && Field<int>(preset,4)==run->throne){Set(preset,0x28,Field<uint32_t>(preset,0x28)|1u);return;}
        if(run && !run->roster.empty() && RandomCandidate(Field<int>(preset,4))) {
            std::array<uint8_t,0x30> copy{};memcpy(copy.data(),preset,copy.size());Set(copy.data(),4,RandomMonster(*run));
            originalSpawnPreset(game,room,copy.data());Set(preset,0x28,Field<uint32_t>(copy.data(),0x28));return;
        }
    }
    originalSpawnPreset(game,room,preset);
}
void __fastcall RemoveHook(Game* game,Unit* unit){
    {
        std::lock_guard lock(serverMutex);
        if(auto run=Active(game);run && unit && run->census.dead.contains(unit->dwUnitId) && unit->dwUnitType==1 && Dead(unit))run->census.Observe(unit->dwUnitId,true);
    }
    SummonPersistence::Removed(game,unit);
    QuestJournal::Removed(game,unit);
    originalRemove(game,unit);
}
int __fastcall OperateHook(Operation* op,int operation){
    if(!op || !op->game || !op->object || !op->player)return originalOperate(op,operation);
    std::lock_guard lock(serverMutex);auto run=Active(op->game);
    // Chaos seals are quest triggers, not Champion Clash encounters. Keep
    // their persisted objects without allowing them to spawn quest bosses.
    if(run && run->event==Event::ChampionClash && run->phase!=Phase::Closed && Level(op->object)==108 && op->object->dwClassId>=392 && op->object->dwClassId<=396)return 0;
    if(!run || (op->object->dwUnitId!=run->portals[0] && op->object->dwUnitId!=run->portals[1]))return originalOperate(op,operation);
    if(Dead(op->player) || op->object->dwClassId!=60 || op->object->dwAnimMode!=2)return 0;
    const bool entering=op->object->dwUnitId==run->portals[0];
    if(entering && run->phase==Phase::Returning)return 0;
    if(run->event==Event::Survivors && !AvatarFor(op->game,op->player))return 0;
    auto& state=State(op->game);
    if(entering) {
        if(run->event==Event::None && run->layoutSeed){
            const auto client=Field<Client*>(op->player->pPlayerData,0xf0);
            if(!client || client->pGame!=op->game || client->dwUnitGUID!=op->player->dwUnitId || state.layoutAcks[{client->dwClientId,op->player->dwUnitId,run->level}]!=run->layoutSeed){SendLayouts(op->game,state,true);status.store("Map entry is waiting for the client's matching layout. All players need this HUD build.");return 0;}
        }
        CaptureMapParty(op->game,*run,op->player);
        const auto position=Location(op->player);
        run->returns[op->player->dwUnitId]=Town(position.level) ? position : state.towns.contains(op->player->dwUnitId) ? state.towns[op->player->dwUnitId] : run->entrance;
        Native<void(__fastcall*)(Game*,Unit*,int,int)>(0x2e16d0)(op->game,op->player,run->level,11);
        if(Level(op->player)==run->level){run->phase=Phase::Running;run->census.entered=true;CensusAndBuffs(op->game,*run);D2RHUD_CubeMapDiagnostics.stage=7;}
    }else {
        if(auto buff=run->players.find(op->player->dwUnitId);buff!=run->players.end()){Detach(op->player,buff->second,run->state);run->players.erase(buff);}
        if(Move(op->game,op->player,run->returns.contains(op->player->dwUnitId) ? run->returns[op->player->dwUnitId] : run->entrance) && run->event==Event::Survivors){Sync(op->game,*run,true);RestoreSurvivor(op->game,op->player);}
    }
    Sync(op->game,*run,true);return 1;
}
#include "CubeMapEventNative.inl"
bool Supported(){
    for(const auto& signature:PackNative::Signatures){
        std::vector<uint8_t> bytes(signature.size);SIZE_T count=0;
        if(!ReadProcessMemory(GetCurrentProcess(),Native<void*>(signature.rva),bytes.data(),bytes.size(),&count) || count!=bytes.size() || LegacyTransmog::Data::Hash(bytes)!=signature.hash)return false;
    }
    for(const auto& signature:LootNativeData::Signatures){
        std::vector<uint8_t> bytes(signature.size);SIZE_T count=0;
        if(!ReadProcessMemory(GetCurrentProcess(),Native<void*>(signature.rva),bytes.data(),bytes.size(),&count) || count!=bytes.size() || LegacyTransmog::Data::Hash(bytes)!=signature.hash)return false;
    }
    for(const auto& signature:LayoutNative::Signatures){
        std::vector<uint8_t> bytes(signature.size);SIZE_T count=0;
        if(!ReadProcessMemory(GetCurrentProcess(),Native<void*>(signature.rva),bytes.data(),bytes.size(),&count) || count!=bytes.size() || LegacyTransmog::Data::Hash(bytes)!=signature.hash)return false;
    }
    for(const auto& signature:NativeData::Signatures) {
        std::vector<uint8_t> bytes(signature.size);SIZE_T size=0;
        if(!ReadProcessMemory(GetCurrentProcess(),Native<void*>(signature.rva),bytes.data(),bytes.size(),&size) || size!=bytes.size() || LegacyTransmog::Data::Hash(bytes)!=signature.hash)return false;
    }
    for(const auto& signature:MonsterNativeData::Signatures) {
        std::vector<uint8_t> bytes(signature.size);SIZE_T size=0;
        if(!ReadProcessMemory(GetCurrentProcess(),Native<void*>(signature.rva),bytes.data(),bytes.size(),&size) || size!=bytes.size() || LegacyTransmog::Data::Hash(bytes)!=signature.hash)return false;
    }
    for(const auto& signature:QualityNativeData::Signatures) {
        std::vector<uint8_t> bytes(signature.size);SIZE_T size=0;
        if(!ReadProcessMemory(GetCurrentProcess(),Native<void*>(signature.rva),bytes.data(),bytes.size(),&size) || size!=bytes.size() || LegacyTransmog::Data::Hash(bytes)!=signature.hash)return false;
    }
    return true;
}
bool BuildPopulation(){
    if(populationCode)return true;
    SYSTEM_INFO info{};GetSystemInfo(&info);const auto base=Pattern::Address(0),step=uintptr_t(info.dwAllocationGranularity);
    for(uintptr_t delta=step;delta<0x40000000 && !populationCode;delta+=step)for(int direction:{1,-1}) {
        const auto candidate=(direction>0 ? base+delta : base>delta ? base-delta : 0)&~(step-1);
        if(candidate)populationCode=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(candidate),NativeData::Allocation,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(populationCode)break;
    }
    if(!populationCode)return false;
    auto fail=[](){VirtualFree(populationCode,0,MEM_RELEASE);populationCode=nullptr;return false;};
    memcpy(populationCode,Native<void*>(NativeData::Source),NativeData::Size);
    for(const auto& fixup:NativeData::Fixups) {
        const int64_t difference=int64_t(Pattern::Address(fixup.target))-int64_t(reinterpret_cast<uintptr_t>(populationCode)+fixup.next);
        if(difference<INT32_MIN || difference>INT32_MAX)return fail();
        const int32_t value=int32_t(difference);memcpy(populationCode+fixup.offset,&value,4);
    }
    populationCode[0xd8]=0xeb; // skip the quest-cleared check only in this copy
    for(const auto& block:NativeData::UnwindBlocks)memcpy(populationCode+block.offset,block.bytes.data(),block.size);
    auto runtime=reinterpret_cast<RUNTIME_FUNCTION*>(populationCode+0xc00);
    for(size_t i=0;i<NativeData::RuntimeFunctions.size();++i){const auto& r=NativeData::RuntimeFunctions[i];runtime[i]={r.begin,r.end,r.unwind};}
    DWORD old=0;if(!VirtualProtect(populationCode,NativeData::Allocation,PAGE_EXECUTE_READ,&old))return fail();
    FlushInstructionCache(GetCurrentProcess(),populationCode,NativeData::Allocation);
    if(!RtlAddFunctionTable(runtime,DWORD(NativeData::RuntimeFunctions.size()),reinterpret_cast<uintptr_t>(populationCode)))return fail();
    return true;
}
Unit* LocalPlayer(){uint32_t selector=8;if(!Read(Pattern::Address(0x1d442d8),selector) || selector>=8)return nullptr;return Native<Unit*(__fastcall*)(uint32_t)>(0x6b050)(selector);}
#include "CubeMapAutomapClient.inl"
const char* LocationName(int level){
    const auto count=Field<uint32_t>(Native<void*>(0x1c9fd10),0);const auto table=Field<uint8_t*>(Native<void*>(0x1c9fd08),0);
    if(level<=0 || unsigned(level)>=count || !table)return nullptr;
    std::array<char,40> key{};memcpy(key.data(),table+size_t(level)*0x184+0xf9,39);
    if(!key[0])return nullptr;
    struct View{const char* data;size_t size;} text{key.data(),strlen(key.data())};
    return Native<const char*(__fastcall*)(const View*)>(0x413960)(&text);
}
const char* MapPortalName(Unit* unit){
    if(!unit || unit->dwUnitType!=2 || unit->dwClassId!=60)return nullptr;
    Packet packet;{std::lock_guard lock(viewMutex);if(!receivedAt || GetTickCount64()-receivedAt>4000)return nullptr;packet=view;}
    auto player=LocalPlayer();if(!player || player->dwUnitId!=packet.recipient)return nullptr;
    const int destination=unit->dwUnitId==packet.entrancePortal && packet.entrancePortal ? packet.level :
        unit->dwUnitId==packet.exitPortal && packet.exitPortal ? packet.town : 0;
    return destination ? LocationName(destination) : nullptr;
}
const char* __fastcall NameHook(Unit* unit){if(auto name=MapPortalName(unit))return name;return originalName(unit);}
void __fastcall ObjectLabelHook(char* buffer,Unit* unit){
    if(buffer)if(auto name=MapPortalName(unit)){const auto length=(std::min)(strlen(name),size_t(1023));memcpy(buffer,name,length);buffer[length]=0;return;}
    originalObjectLabel(buffer,unit);
}
}
bool Prepare(Game* game,Unit* player,CubeVisual::Data::Recipe& recipe){
    if(!installed.load() || !game || !player)return false;
    std::lock_guard lock(serverMutex);auto& state=State(game);
    if(state.run && state.run->phase==Phase::Closed && (state.run->eventPhase==EventPhase::Finished || state.run->eventPhase==EventPhase::Cancelled))state.run.reset();
    int destination=0,mapOutputs=0;for(const auto& output:recipe.outputs)if(IsMap(output)){destination=output.base;++mapOutputs;}
    if(mapOutputs!=1){status.store("Map recipe refused: exactly one map output is required.");return false;}
    if(destination==RandomDestination){
        std::mt19937 selection(uint32_t(game->dwInitSeed)^game->dwGameFrame^((serial.load()+1)*0x85ebca6b));
        destination=SelectRandomMapDestination(game,selection);
        if(!destination){status.store("Map recipe refused: no eligible random destinations in the loaded level tables.");return false;}
    }
    if(!ValidLevel(destination) || !Town(Level(player))){status.store("Map recipe refused: use a valid non-town level and transmute in town.");return false;}
    if(state.run && !CanReplaceMap(game,*state.run)){status.store("Map recipe refused: everyone must leave the current map before replacing it; active events must finish.");return false;}
    if(!state.restoreLevels.empty()){status.store("Map recipe refused: the previous area's normal population is still being restored.");return false;}
    const int mapState=MapState();
    if(mapState<0){status.store("Map recipe refused: hud_cube_map must exist in the loaded state table; BIN-only releases also need matching HUD table metadata.");return false;}
    auto run=std::make_unique<Run>();run->id=++serial;if(!run->id)run->id=++serial;
    run->creator=player->dwUnitId;run->characterLevel=(std::max)(1,Stat(player,12));
    run->level=destination;run->state=mapState;run->created=game->dwGameFrame;run->entrance=Location(player);
    run->random.seed(uint32_t(game->dwInitSeed)^game->dwGameFrame^(run->id*0x85ebca6b));
    D2RHUD_CubeMapDiagnostics.level=destination;D2RHUD_CubeMapDiagnostics.run=run->id;D2RHUD_CubeMapDiagnostics.stage=1;
    auto sourceRoom=GetRoom(player);Position source=run->entrance;
    if(!sourceRoom || !FreePosition(sourceRoom,source) || !RollModifiers(*run,game)){status.store("Map recipe refused: no portal space or required modifier stats.");return false;}
    run->monsterLevel=(std::max)(1,Stat(player,12))+3;
    for(const auto roll:run->rolls)if(roll.effect==LevelEffect)run->monsterLevel+=roll.value;
    // Level is sent in the native monster packet as a byte. Do not wrap it.
    run->monsterLevel=(std::min)(255,run->monsterLevel);
    run->initializer=BuildInitializer(run->monsterLevel);
    if(!run->initializer || !BuildRoster(*run)){status.store("Map recipe refused: monster initialization or random roster unavailable.");return false;}
    if(!ReleaseMapForRecipe(game,destination)){status.store("Map recipe refused: the previous area is occupied or still awaiting restoration.");return false;}
    state.run=std::move(run);auto& current=*state.run;
    if(!RerollLayout(game,current,player)){Clean(game,current);state.run.reset();status.store("Map recipe refused: destination is occupied or its layout could not be regenerated.");return false;}
    state.restoreLevels.erase(destination);
    if(!Reload(game,current)) {Clean(game,current);state.run.reset();status.store("Map recipe refused: destination unavailable, occupied or failed to reload.");return false;}
    Position arrival;auto targetRoom=Spawn(game,destination,arrival);
    auto in=targetRoom ? Portal(game,targetRoom,arrival,current.entrance.level) : nullptr;
    auto out=in ? Portal(game,sourceRoom,source,current.level) : nullptr;
    if(in)current.portals[1]=in->dwUnitId;if(out)current.portals[0]=out->dwUnitId;
    if(!in || !out){Clean(game,current);state.run.reset();status.store("Map recipe refused: unable to create the linked red portals.");return false;}
    auto sourceR2=Field<void*>(GetRoom(out),0x18);
    current.entrancePin=Pin{sourceR2,(Field<uint32_t>(sourceR2,0x50)&Pinned)!=0};
    Set(sourceR2,0x50,Field<uint32_t>(sourceR2,0x50)|Pinned);
    current.phase=Phase::Open;CensusAndBuffs(game,current);current.census.ready=true;
    for(auto& output:recipe.outputs)if(IsMap(output))output.base=uint16_t(destination);
    D2RHUD_CubeMapDiagnostics.remaining=current.census.Remaining();D2RHUD_CubeMapDiagnostics.stage=6;
    status.store("Shared map open. Clear every remaining monster to return to town.");SendLayouts(game,state,true);Sync(game,current,true);return true;
}
bool IsMapLevel(Game* game,int level){if(!installed.load() || !game)return false;std::lock_guard lock(serverMutex);const auto run=Active(game);return run && run->level==level && run->phase!=Phase::Closed;}
bool IsMapMonster(Game* game,Unit* unit){return unit && IsMapLevel(game,Level(unit));}
bool InitializeNativeMonster(Game* game,Room* room,Unit* unit,int64_t* region){
    if(!installed.load() || !game || !unit)return false;
    std::lock_guard lock(serverMutex);const auto run=Active(game);
    if(!run || run->phase==Phase::Preparing || run->phase==Phase::Closed || run->level!=Level(room) || !run->initializer)return false;
    reinterpret_cast<void(__fastcall*)(Game*,Room*,Unit*,int64_t*)>(run->initializer->code)(game,room,unit,region);return true;
}
bool InitializeMonster(Game* game,Room* room,Unit* unit){
    if(!installed.load() || !game)return false;
    std::lock_guard lock(serverMutex);auto run=Active(game);
    if(!run || run->phase==Phase::Closed || run->level!=Level(room))return false;
    if(run->phase!=Phase::Preparing && run->phase!=Phase::Returning && RunMonster(unit,*run)) {
        // Do this before the shared initializer applies sunder. Changing base
        // resistance after sunder would invalidate its remembered reduction.
        if(run->event==Event::Survivors)for(int resistance:{36,37,39,41,43,45})if(Stat(unit,resistance)>75)UnitStat(unit,resistance,75);
        EnsureMonsterBuff(unit,*run);
    }
    return true;
}
namespace {
void GoldSixfold(Unit* item){
    if(!item || item->dwUnitType!=4)return;
    const auto table=Field<uint8_t*>(Native<void*>(0x1ca0390),0);
    const auto rows=Field<uint64_t>(Native<void*>(0x1ca0398),0);
    if(!table || rows>1000000 || item->dwClassId>=rows || Field<uint32_t>(table+size_t(item->dwClassId)*0x1b4,0x80)!=0x20646c67)return;
    const auto value=(std::max)(int64_t(0),int64_t(Stat(item,14)))*6;
    UnitStat(item,14,int((std::min)(int64_t(INT_MAX),value)));item->dwFlags|=2;
}
int EventTreasureClass(Game* game,const Run* run,Unit* source,bool timed){
    if(!source || source->dwUnitType!=1)return -1;
    if(run && run->event!=Event::None && run->event!=Event::Survivors && !TimedEvent(run->event) &&
       (run->eventPhase==EventPhase::Entering || run->eventPhase==EventPhase::Active) &&
       Level(source)==run->level && RunMonster(source,*run))return run->eventTreasureClass;
    // Location-free rewards change random hostile monster drops. Named quest
    // encounters retain their own table, while all sources receive loot bonuses.
    if(timed && Monster(source) && RandomCandidate(int(source->dwClassId)) && !(source->pMonsterData->nTypeFlag&2)){
        const int tc=LookupName(0x1c9f6a8,"TormentGroupA");return tc>0 ? tc : -1;
    }
    return -1;
}
}
bool Drop(Game* game,Unit* monster,Unit* player,int tc,int quality,int level,int a7,Unit** items,int* count,int maximum,DropFunction original){
    if(!installed.load() || !game || !monster || !original)return false;
    std::lock_guard lock(serverMutex);auto run=Active(game);
    if(run && run->event==Event::Survivors && run->eventPhase==EventPhase::Active && Level(monster)==run->level){
        // Ordinary waves feed the run XP system. Elite waves and the final
        // boss retain native item drops without flooding the arena with loot.
        if(!run->survival.elites.contains(monster->dwUnitId) && monster->dwUnitId!=run->survival.boss){if(count)*count=0;return true;}
    }
    const bool treasure=HasTimedReward(game,player,Event::TreasureTrove);
    const bool golden=HasTimedReward(game,player,Event::GoldenGoose);
    const bool plentiful=HasTimedReward(game,player,Event::PlentifulPirating);
    const int overrideTC=EventTreasureClass(game,run,monster,treasure || golden || plentiful);
    int bonus=0;
    if(run && run->event==Event::None && (run->phase==Phase::Open || run->phase==Phase::Running) && monster->dwUnitType==1 && Level(monster)==run->level)
        for(const auto roll:run->rolls)if(roll.effect==TreasureEffect)bonus=roll.value;
    if(overrideTC>0 || bonus || plentiful || golden)
        return DropWithTreasureRules(game,monster,player,tc,quality,level,a7,items,count,maximum,overrideTC,bonus,plentiful,golden ? GoldSixfold : nullptr);
    if(!run || run->event!=Event::MidasMeddling || run->eventPhase!=EventPhase::Active || Level(monster)!=run->level)return false;
    const bool first=run->goldDropped.insert(monster->dwUnitId).second;
    std::array<Unit*,32> captured{};int capturedCount=0;
    // Capture the real native drops even when the caller doesn't request an
    // output array. No global treasure-class or saved item data is edited.
    original(game,monster,player,tc,quality,level,a7,captured.data(),&capturedCount,maximum>0 ? (std::min)(maximum,32) : 6);
    bool foundGold=false;const auto table=Field<uint8_t*>(Native<void*>(0x1ca0390),0);const auto rows=Field<uint64_t>(Native<void*>(0x1ca0398),0);
    auto gold=[&](Unit* item){return item && item->dwUnitType==4 && table && item->dwClassId<rows && Field<uint32_t>(table+size_t(item->dwClassId)*0x1b4,0x80)==0x20646c67;};
    auto multiply=[&](Unit* item){const auto value=int64_t(Stat(item,14))*6;UnitStat(item,14,int((std::min)(int64_t(INT_MAX),value)));item->dwFlags|=2;};
    for(int n=0;n<(std::clamp)(capturedCount,0,32);++n)if(gold(captured[n])){foundGold=true;multiply(captured[n]);}
    if(first && !foundGold && table && rows<1000000){
        int id=-1;for(size_t n=0;n<rows;++n)if(Field<uint32_t>(table+n*0x1b4,0x80)==0x20646c67){id=int(n);break;}
        if(id>=0)if(auto pile=Native<Unit*(__fastcall*)(Game*,Unit*,int,int,int,int)>(0x2f8820)(game,monster,id,2,0,0)){
            Native<void(__fastcall*)(Unit*,Unit*)>(0x2f9060)(pile,player);
            // A mod may initialize gold with zero quantity. Still guarantee a
            // useful level-scaled pile for every event monster's first death.
            if(Stat(pile,14)<=0)UnitStat(pile,14,(std::max)(1,run->monsterLevel)*std::uniform_int_distribution<int>(5,15)(run->random));
            multiply(pile);if(capturedCount<32)captured[capturedCount++]=pile;
        }
    }
    if(count)*count=items ? (std::min)((std::clamp)(capturedCount,0,32),(std::max)(0,maximum)) : (std::clamp)(capturedCount,0,32);
    if(items && maximum>0)std::copy_n(captured.begin(),(std::min)((std::clamp)(capturedCount,0,32),maximum),items);
    return true;
}
bool Command(Game* game,Unit* player,const uint8_t* bytes,size_t size){
    if(bytes && size==sizeof(LayoutData::Ack) && bytes[9]==LayoutData::Operation){LayoutData::Ack packet;memcpy(&packet,bytes,sizeof(packet));return AcknowledgeLayout(game,player,packet);}
    if(SurvivorCommand(game,player,bytes,size))return true;
    if(!bytes || size!=sizeof(CommandPacket))return false;CommandPacket packet;memcpy(&packet,bytes,sizeof(packet));
    if(packet.opcode!=3 || packet.magic!=PacketMagic || packet.operation!=PacketOperation)return false;
    if(packet.version!=5 || !installed.load() || !game || !player || player->dwUnitType!=0)return true;
    std::lock_guard lock(serverMutex);auto run=Active(game);
    if(!run || packet.run!=run->id || packet.session!=uint32_t(game->dwInitSeed) || packet.phase!=run->eventPhase || !run->returns.contains(player->dwUnitId) || Dead(player))return true;
    const auto client=Field<Client*>(player->pPlayerData,0xf0);
    if(!client || client->pGame!=game || client->dwClientState!=4 || client->dwUnitGUID!=player->dwUnitId)return true;
    if(run->event!=Event::None && run->eventPhase==EventPhase::Active && Level(player)==run->level){
        // Recover an event-only snapshot after client act teardown/re-entry.
        // This request cannot change the timer, event phase or reward ledger.
        run->automapSent.erase(client->dwClientId);Sync(game,*run,true);return true;
    }
    if((run->eventPhase==EventPhase::WaitingTown && Town(Level(player))) || (run->eventPhase==EventPhase::Entering && Level(player)==run->level)){
        if(run->ready.insert(player->dwUnitId).second && run->eventPhase==EventPhase::Entering)run->automapSent.erase(client->dwClientId);
    }
    return true;
}
void SetArtworkProvider(uint64_t(*provider)()){artworkProvider.store(provider);}
void SetActTick(void(*observer)(D2GameStrc*)){actTick.store(observer);}
void SetActTown(int level){additionalTown.store(level>0 && level<1024?level:0);}
void SetFontProvider(ImFont*(*provider)(float)){fontProvider.store(provider);}
uint64_t ClientLayerSeed(uint32_t layer){
    std::lock_guard lock(layoutMutex);if(!clientLayoutSessionKnown)return 0;
    const auto table=Field<uint8_t*>(Native<void*>(0x1c9fd28),0);const auto count=Field<uint32_t>(Native<void*>(0x1c9fd10),0);
    if(!table || count>1024)return 0;
    uint32_t hash=2166136261u;bool found=false;
    for(const auto [level,seed]:clientLayoutSeeds)if(unsigned(level)<count && Field<uint32_t>(table+size_t(level)*0x9c,8)==layer){hash=(hash^uint32_t(level))*16777619u;hash=(hash^seed)*16777619u;found=true;}
    hash^=clientLayoutSession;
    return found ? (uint64_t(clientLayoutEpoch)<<32)|(hash ? hash : 1u) : 0;
}
bool Receive(const uint8_t* bytes,size_t size){
    if(size!=sizeof(Packet) || !bytes)return false;
    if(bytes[9]==RewardsData::Operation)return ReceiveRewards(bytes,size);
    if(bytes[9]==LayoutData::Operation){LayoutData::Wire packet;memcpy(&packet,bytes,sizeof(packet));return ReceiveLayout(packet);}
    if(bytes[9]==Survivors::Operation)return ReceiveSurvivor(bytes,size);
    if(bytes[9]==AutomapData::Operation){AutomapData::Wire packet;memcpy(&packet,bytes,sizeof(packet));return ReceiveAutomap(packet);}
    Packet packet;memcpy(&packet,bytes,sizeof(packet));
    if(!Data::Valid(packet))return false;
    std::lock_guard lock(viewMutex);
    if(packet.session==view.session && packet.run==view.run && receivedAt && int32_t(packet.sequence-view.sequence)<=0)return false;
    if(packet.run!=view.run || packet.session!=view.session || packet.recipient!=view.recipient ||
       !AutomapData::Visible(packet,0,packet.recipient,packet.level))ClearEventAutomap();
    view=packet;receivedAt=GetTickCount64();return true;
}
bool Display(std::string& title,std::string& modifiers){
    Packet packet;
    {std::lock_guard lock(viewMutex);if(!receivedAt || GetTickCount64()-receivedAt>4000 || !view.active)return false;packet=view;}
    auto player=LocalPlayer();if(!player || player->dwUnitId!=packet.recipient || Level(player)!=packet.level)return false;
    const auto name=LocationName(packet.level);
    title=packet.event==Event::None ? std::string("Map - ")+(name ? name : std::to_string(packet.level)) : Info(packet.event).name;modifiers.clear();
    modifiers="Monster level: "+std::to_string(packet.monsterLevel)+"\n";
    if(packet.event==Event::None && packet.elitePacks)modifiers+="Champion/unique packs: "+std::to_string(packet.elitePacks)+"\n";
    if(packet.event!=Event::None){modifiers+=Info(packet.event).description;modifiers+='\n';}
    for(size_t i=0;i<packet.effects;++i) {
        const auto roll=packet.rolls[i];const auto& effect=Effects[roll.effect];
        modifiers+=effect.label;modifiers+=": ";if(roll.value>0)modifiers+='+';
        modifiers+=std::to_string(roll.value);if(effect.percent)modifiers+='%';modifiers+='\n';
        if(effect.pairedStat){
            const auto paired=PairedValue(effect,roll.value);modifiers+="  ";modifiers+=effect.pairedLabel;modifiers+=": ";if(paired>0)modifiers+='+';
            modifiers+=std::to_string(paired);if(effect.percent)modifiers+='%';modifiers+='\n';
        }
    }
    return true;
}
#ifndef CUBEMAP_NO_UI
ImFont* EventFont(float size){
    if(const auto provider=fontProvider.load())if(auto font=provider(size))return font;
    return ImGui::GetFont();
}
void DrawDie(ImDrawList* draw,ImVec2 center,float radius,uint8_t result,float age,bool idle=false){
    struct Point{float x,y,z;};
    // The briefing keeps the chosen face while the die gently rocks and floats.
    const float progress=idle ? 1.0f : (std::clamp)(age/3.0f,0.0f,1.0f),spin=18.0f*(1-progress)*(1-progress);
    const float ax=-0.22f+spin+(idle ? 0.10f*sinf(age*1.4f) : 0),
        ay=0.3f+spin*1.3f+(idle ? 0.14f*sinf(age*1.1f) : 0),
        az=spin*0.7f+(idle ? 0.05f*sinf(age*0.8f) : 0);
    auto rotate=[&](Point p){
        const float y=p.y*cosf(ax)-p.z*sinf(ax),z=p.y*sinf(ax)+p.z*cosf(ax);p.y=y;p.z=z;
        const float x=p.x*cosf(ay)+p.z*sinf(ay);p.z=-p.x*sinf(ay)+p.z*cosf(ay);p.x=x;
        return Point{p.x*cosf(az)-p.y*sinf(az),p.x*sinf(az)+p.y*cosf(az),p.z};
    };
    center.y-=sinf(progress*18.8495559f)*radius*0.15f*(1-progress);
    if(idle)center.y-=sinf(age*1.6f)*radius*0.06f;
    const std::array<Point,8> vertices{{{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1},{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1}}};
    const std::array<std::array<int,4>,6> faces{{{{0,1,2,3}},{{5,4,7,6}},{{4,0,3,7}},{{1,5,6,2}},{{4,5,1,0}},{{3,2,6,7}}}};
    std::array<Point,8> v;for(size_t i=0;i<v.size();++i)v[i]=rotate(vertices[i]);
    std::array<int,6> order{0,1,2,3,4,5};
    auto depth=[&](int face){float z=0;for(int i:faces[face])z+=v[i].z;return z/4;};
    std::sort(order.begin(),order.end(),[&](int a,int b){return depth(a)<depth(b);});
    const int rolled=progress<0.9f ? 1+(int(age*19)%6) : result;
    std::array<int,6> values{rolled,7-rolled,1,6,2,5};
    int next=1;for(size_t f=2;f<6;f+=2){while(next==rolled || next==7-rolled || (f==4 && (next==values[2] || next==values[3])))++next;values[f]=next;values[f+1]=7-next;++next;}
    auto project=[&](Point p){return ImVec2{center.x+p.x*radius,center.y+p.y*radius};};
    for(int face:order){
        if(depth(face)<0)continue;
        const auto indices=faces[face];std::array<ImVec2,4> polygon;for(size_t i=0;i<4;++i)polygon[i]=project(v[indices[i]]);
        const int shade=int(160+depth(face)*60);draw->AddConvexPolyFilled(polygon.data(),4,IM_COL32(shade,shade-12,shade-38,255));
        draw->AddPolyline(polygon.data(),4,IM_COL32(250,222,171,255),ImDrawFlags_Closed,1.5f);
        const int value=values[face];std::vector<ImVec2> pips;
        if(value&1)pips.push_back({0.5f,0.5f});
        if(value>=2){pips.push_back({0.25f,0.25f});pips.push_back({0.75f,0.75f});}
        if(value>=4){pips.push_back({0.75f,0.25f});pips.push_back({0.25f,0.75f});}
        if(value==6){pips.push_back({0.25f,0.5f});pips.push_back({0.75f,0.5f});}
        for(auto uv:pips){
            Point p{};const float weights[]{(1-uv.x)*(1-uv.y),uv.x*(1-uv.y),uv.x*uv.y,(1-uv.x)*uv.y};
            for(size_t i=0;i<4;++i){p.x+=v[indices[i]].x*weights[i];p.y+=v[indices[i]].y*weights[i];p.z+=v[indices[i]].z*weights[i];}
            draw->AddCircleFilled(project(p),radius*0.09f,IM_COL32(31,22,16,255),16);
        }
    }
}
#include "CubeMapSurvivorsUI.inl"
void DrawCelebration(float scale,bool clientReady=true){
    auto player=LocalPlayer();Packet packet;uint64_t received;
    {std::lock_guard lock(viewMutex);packet=view;received=receivedAt;}
    const uint64_t now=GetTickCount64();
    if(!received || now-received>4000 || !player || packet.recipient!=player->dwUnitId || packet.event==Event::None){readySince=lastReadySent=0;return;}
    const auto phase=packet.eventPhase;
    static uint32_t readyRun=0,readySession=0;static EventPhase readyPhase=EventPhase::None;
    if(packet.run!=readyRun || packet.session!=readySession || phase!=readyPhase){readySince=lastReadySent=0;readyRun=packet.run;readySession=packet.session;readyPhase=phase;}
    if(phase==EventPhase::WaitingTown || phase==EventPhase::Entering){
        const bool location=phase==EventPhase::WaitingTown ? Town(Level(player)) : Level(player)==packet.level;
        if(!clientReady || !location){readySince=0;return;}
        if(!readySince)readySince=now;
        if(now-readySince>=3000 && (!lastReadySent || now-lastReadySent>=1000)){
            CommandPacket command;command.run=packet.run;command.session=packet.session;command.phase=phase;
            Native<void(__fastcall*)(void*,int)>(0x10dce0)(&command,sizeof(command));lastReadySent=now;
        }
        return;
    }
    if(!clientReady || !Town(Level(player)))return;
    if(phase!=EventPhase::Ceremony && phase!=EventPhase::Briefing && phase!=EventPhase::Finished && phase!=EventPhase::Cancelled)return;
    auto& io=ImGui::GetIO();auto draw=ImGui::GetForegroundDrawList();
    const float width=io.DisplaySize.x,height=io.DisplaySize.y;
    const float ui=(std::min)((std::max)(scale,0.5f),(std::min)(width/1000.0f,height/750.0f));
    const auto elapsed=packet.eventElapsed+uint32_t(now-received);
    auto text=[&](const char* value,float size,float y,ImU32 color=IM_COL32(220,190,133,255)){
        size*=ui;const auto font=EventFont(size);const auto extent=font->CalcTextSizeA(size,FLT_MAX,0,value);const ImVec2 p{std::round((width-extent.x)*0.5f),std::round(y)};
        draw->AddText(font,size,{p.x+2*ui,p.y+2*ui},IM_COL32(0,0,0,230),value);draw->AddText(font,size,p,color,value);
    };
    if(phase==EventPhase::Finished || phase==EventPhase::Cancelled){
        if(elapsed>6000)return;
        text(phase==EventPhase::Cancelled ? "The event could not begin" : TimedEvent(packet.event) && packet.success ? "Ten-minute bonus granted!" : packet.success ? "Victory!" : "Time has run out",48,height*0.12f);
        if(TimedEvent(packet.event) && phase!=EventPhase::Cancelled)text(Info(packet.event).name,28,height*0.12f+65*ui);
        if(phase==EventPhase::Cancelled)text(FailureMessage(packet.failure),24,height*0.12f+65*ui);
        if(packet.reward==2)text("You received +2 stat points",28,height*0.12f+65*ui);
        return;
    }
    const float fade=phase==EventPhase::Briefing ? 1.0f : (std::clamp)(float(elapsed)/CeremonyFadeMs,0.0f,1.0f);
    if(fade>0)draw->AddRectFilled({0,0},{width,height},IM_COL32(0,0,0,int(fade*255)));
    // Artwork is embedded in the DLL; descriptor recreation follows the HUD
    // texture cache across a device reset. Text and animation remain vector sharp.
    if(auto provider=artworkProvider.load())if(const auto texture=provider()){
        const float w=960*ui,h=640*ui;const ImVec2 p{(width-w)*0.5f,height*0.015f};
        draw->AddImage(ImTextureID(texture),p,{p.x+w,p.y+h},{0,0},{1,1},IM_COL32(255,255,255,phase==EventPhase::Briefing ? 235 : 85));
    }
    const ImVec2 center{width*0.5f,height*0.48f};const float age=float(elapsed)/1000;
    for(int ring=0;ring<3;++ring){
        const float r=(128+ring*12)*ui;draw->AddCircle(center,r,IM_COL32(172,125,60,75-ring*15),96,1.4f*ui);
    }
    for(int n=0;n<24;++n){
        const float a=n*6.2831853f/24+age*0.09f,r=166*ui;
        const ImVec2 p{center.x+cosf(a)*r,center.y+sinf(a)*r};
        draw->AddCircleFilled(p,(1.2f+0.8f*sinf(age*2+n))*ui,IM_COL32(239,190,89,140),8);
    }
    if(phase==EventPhase::Ceremony){
        text("Rolling the dice of destiny...",36,height*0.13f);
        DrawDie(draw,center,76*ui,packet.die,(std::max)(0.0f,(float(elapsed)-CeremonyFadeMs)/1000));
        if(packet.event==Event::Trial && elapsed>=SecretRevealMs){
            const int count=(std::min)(6,1+int((elapsed-SecretRevealMs)/350));
            for(int n=0;n<count;++n){const float a=n*6.2831853f/6+age*0.7f;DrawDie(draw,{center.x+cosf(a)*235*ui,center.y+sinf(a)*155*ui},29*ui,uint8_t(1+n),age);}
            text("A secret trial awakens...",34,height*0.77f);
        }else if(elapsed>=DiceRevealMs)text(Info(packet.event).name,42,height*0.76f);
    }else {
        if(packet.event!=Event::Survivors)DrawDie(draw,center,76*ui,packet.die,age,true);
        else {
            for(int n=0;n<3;++n){const float angle=age*0.4f+n*6.2831853f/3;
                const ImVec2 p{center.x+cosf(angle)*64*ui,center.y+sinf(angle)*64*ui};
                const ImU32 color=n==0?IM_COL32(190,130,245,255):n==1?IM_COL32(110,190,245,255):IM_COL32(245,215,120,255);
                draw->AddCircleFilled(p,22*ui,color,32);draw->AddCircle(p,30*ui,color,32,2*ui);
            }
        }
        text(Info(packet.event).name,44,height*0.13f);
        const float descriptionSize=23*ui,wrap=850*ui;
        const auto description=Info(packet.event).description;
        const auto font=EventFont(descriptionSize);const auto extent=font->CalcTextSizeA(descriptionSize,FLT_MAX,wrap,description);
        draw->AddText(font,descriptionSize,{(width-extent.x)*0.5f,height*0.71f},IM_COL32(225,213,190,255),description,nullptr,wrap);
        text(packet.event==Event::Trial ? "Five bosses. No second chances." : packet.event==Event::Survivors ? "Keep moving. Choose upgrades. Survive." : TimedEvent(packet.event) ? "A ten-minute bonus. Explore wherever you like." : packet.event==Event::ChampionClash ? "Clear the Chaos Sanctuary. No time limit." : "Clear the realm before time runs out.",22,height*0.80f);
        const auto seconds=(std::max)(0,10-int(elapsed/1000));
        const auto countdown="The event begins in "+std::to_string(seconds);
        text(countdown.c_str(),34,height*0.87f);
    }
}
#endif
void Draw(float scale,bool clientReady){
#ifndef CUBEMAP_NO_UI
    DrawSurvivors(scale,clientReady);
    if(clientReady){
        const auto rewards=TimedRewardLines();auto& io=ImGui::GetIO();const float size=16*(std::max)(0.5f,scale);const auto font=EventFont(size);
        float y=io.DisplaySize.y*0.68f;auto draw=ImGui::GetForegroundDrawList();
        for(const auto& line:rewards){const auto extent=font->CalcTextSizeA(size,FLT_MAX,0,line.c_str());
            const float x=(std::max)(8*scale,io.DisplaySize.x-extent.x-24*scale);draw->AddText(font,size,{std::round(x),std::round(y)},IM_COL32(245,215,145,255),line.c_str());y+=size+4*scale;}
    }
    std::string title,modifiers;
    if(Display(title,modifiers)){
        uint32_t remaining=0;Packet packet;uint64_t received;
        {std::lock_guard lock(viewMutex);remaining=view.remaining;packet=view;received=receivedAt;}
        if(packet.event!=Event::None && packet.eventPhase==EventPhase::Active && Info(packet.event).seconds){
            const int total=int(Info(packet.event).seconds*1000);
            bool paused=false;{std::lock_guard lock(viewMutex);paused=packet.event==Event::Survivors && survivorView.run==packet.run && survivorView.session==packet.session && survivorView.recipient==packet.recipient && survivorView.paused;}
            const int ms=(std::max)(0,total-int(packet.eventElapsed)-(paused?0:int(GetTickCount64()-received)));const int seconds=(ms+999)/1000;
            char time[32]{};snprintf(time,sizeof(time),"%d:%02d",seconds/60,seconds%60);
            auto& io=ImGui::GetIO();const float size=32*(std::max)(0.5f,scale);const auto font=EventFont(size);
            const auto extent=font->CalcTextSizeA(size,FLT_MAX,0,time);const ImVec2 p{std::round((io.DisplaySize.x-extent.x)/2),std::round(io.DisplaySize.y*0.745f)};
            const bool urgent=seconds<=10;auto draw=ImGui::GetForegroundDrawList();
            if(urgent)for(int n=4;n>0;--n)draw->AddRectFilled({p.x-(8+n*2)*scale,p.y-(4+n)*scale},{p.x+extent.x+(8+n*2)*scale,p.y+extent.y+(4+n)*scale},IM_COL32(160,0,0,int((12+10*sinf(float(GetTickCount64()%100000)/110))*n)),7*scale);
            draw->AddText(font,size,p,urgent ? IM_COL32(255,65,45,255) : IM_COL32(245,215,145,255),time);
        }
        const auto text="Monsters remaining: "+std::to_string(remaining);
        auto& io=ImGui::GetIO();const float size=16*(std::max)(0.5f,scale);
        const auto font=EventFont(size);auto extent=font->CalcTextSizeA(size,FLT_MAX,0,text.c_str());
        const ImVec2 position{std::round((io.DisplaySize.x-extent.x)/2),std::round(io.DisplaySize.y*0.81f)};
        auto draw=ImGui::GetBackgroundDrawList();const float pad=6*scale;
        draw->AddRectFilled({position.x-pad,position.y-pad},{position.x+extent.x+pad,position.y+extent.y+pad},IM_COL32(0,0,0,190));
        draw->AddText(font,size,position,IM_COL32(240,215,165,255),text.c_str());
    }
    DrawCelebration(scale,clientReady);
#endif
}
const char* Status(){
    if(installed.load() && survivorsAvailable && !survivorsRequested.load())
        return "Cube maps available. Survivor Arena is disabled in D2RHUD Options > Workshop.";
    return status.load();
}
void SetSurvivorsEnabled(bool enabled){survivorsRequested.store(enabled);}
bool SurvivorsEnabled(){return survivorsRequested.load();}
bool SummonLifecycleAvailable(){return installed.load() && saveLoadAvailable;}
const char* SummonLifecycleStatus(){return installed.load() ? saveLoadStatus : Status();}
const char* SurvivorsCompatibilityDetails(){return survivorsCompatibilityDetails.c_str();}
bool Install(){
    if(installed.load())return true;
    survivorsCompatibilityDetails.clear();
    if(!Supported() || !BuildPopulation() || !BuildChampionClashPopulation() || !BuildEventQuality() || !BuildLootSelector()){status.store("Cube maps unavailable: executable signature or native population setup failed.");return false;}
    uint32_t saveLoadFailed=0,survivorsFailed=0;
    saveLoadAvailable=SaveLoadSupported(&saveLoadFailed);
    SurvivorSignatureFailure failure;
    survivorsAvailable=SurvivorsSupported(&survivorsFailed,&failure);
    // Capture the exact failing read and resolve existing jumps before our own
    // hooks can change either the entry bytes or their destination.
    if(!survivorsAvailable)survivorsCompatibilityDetails=DescribeSurvivorFailure(failure);
    if(saveLoadAvailable)std::snprintf(saveLoadStatus,sizeof(saveLoadStatus),"Character save/load hooks are available independently of Survivors.");
    else std::snprintf(saveLoadStatus,sizeof(saveLoadStatus),"Character save/load signature mismatch at D2R.exe+0x%x.",saveLoadFailed);
    if(survivorsAvailable)LoadSurvivorSkillNames();
    memcpy(initializerSource.data(),Native<void*>(MonsterNativeData::Source),initializerSource.size());
    const auto initialized=MH_Initialize();if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
    std::vector<uint32_t> hooks{0x27e1d0,0x3c1080,0x2c35b0,0x27c360,0x2c5900,0x28b920,0x6c750,0x85060,0x2f8a60,0x294e70,0x294f40,0x1e5440,0x1e5590,0x969f0,0x93690};
    std::vector<void*> replacements{reinterpret_cast<void*>(TickHook),reinterpret_cast<void*>(OperateHook),reinterpret_cast<void*>(RemoveHook),reinterpret_cast<void*>(DestroyHook),reinterpret_cast<void*>(SpawnPresetHook),reinterpret_cast<void*>(ChooseHook),reinterpret_cast<void*>(NameHook),reinterpret_cast<void*>(ObjectLabelHook),reinterpret_cast<void*>(QualityHook),reinterpret_cast<void*>(ManaConsumeHook),reinterpret_cast<void*>(ManaCheckHook),reinterpret_cast<void*>(ManaSetHook),reinterpret_cast<void*>(ManaAddHook),reinterpret_cast<void*>(MapCellsHook),reinterpret_cast<void*>(MapResetHook)};
    std::vector<void**> originals{reinterpret_cast<void**>(&originalTick),reinterpret_cast<void**>(&originalOperate),reinterpret_cast<void**>(&originalRemove),reinterpret_cast<void**>(&originalDestroy),reinterpret_cast<void**>(&originalSpawnPreset),reinterpret_cast<void**>(&originalChoose),reinterpret_cast<void**>(&originalName),reinterpret_cast<void**>(&originalObjectLabel),reinterpret_cast<void**>(&originalQuality),reinterpret_cast<void**>(&originalManaConsume),reinterpret_cast<void**>(&originalManaCheck),reinterpret_cast<void**>(&originalManaSet),reinterpret_cast<void**>(&originalManaAdd),reinterpret_cast<void**>(&originalMapCells),reinterpret_cast<void**>(&originalMapReset)};
    if(saveLoadAvailable){
        for(auto hook:{0x2934b0u,0x318c80u})hooks.push_back(hook);
        for(auto hook:{reinterpret_cast<void*>(SurvivorLoadHook),reinterpret_cast<void*>(SurvivorSaveHook)})replacements.push_back(hook);
        for(auto original:{reinterpret_cast<void**>(&originalSurvivorLoad),reinterpret_cast<void**>(&originalSurvivorSave)})originals.push_back(original);
    }
    if(survivorsAvailable){
        for(auto hook:{0x28f2b0u,0x29f350u,0x29f090u,0x28c740u,0x6ff60u})hooks.push_back(hook);
        for(auto hook:{reinterpret_cast<void*>(SurvivorSendSaveHook),reinterpret_cast<void*>(SurvivorDisconnectHook),reinterpret_cast<void*>(SurvivorPreviewHook),reinterpret_cast<void*>(SurvivorStarterHook)})replacements.push_back(hook);
        for(auto original:{reinterpret_cast<void**>(&originalSurvivorSendSave),reinterpret_cast<void**>(&originalSurvivorDisconnect),reinterpret_cast<void**>(&originalSurvivorPreview),reinterpret_cast<void**>(&originalSurvivorStarter)})originals.push_back(original);
        replacements.push_back(reinterpret_cast<void*>(SurvivorClientTickHook));originals.push_back(reinterpret_cast<void**>(&originalSurvivorClientTick));
    }
    hooks.push_back(0x1e8390u);replacements.push_back(reinterpret_cast<void*>(InitLevelHook));originals.push_back(reinterpret_cast<void**>(&originalInitLevel));
    hooks.push_back(0x1e7f70u);replacements.push_back(reinterpret_cast<void*>(AllocateLevelHook));originals.push_back(reinterpret_cast<void**>(&originalAllocateLevel));
    hooks.push_back(0x1d0a20u);replacements.push_back(reinterpret_cast<void*>(FreeActHook));originals.push_back(reinterpret_cast<void**>(&originalFreeAct));
    size_t n=0;for(;n<hooks.size();++n)if(MH_CreateHook(Native<void*>(hooks[n]),replacements[n],originals[n])!=MH_OK)break;
    bool ok=n==hooks.size();
    if(ok){for(auto hook:hooks)if(MH_QueueEnableHook(Native<void*>(hook))!=MH_OK)ok=false;ok=ok && MH_ApplyQueued()==MH_OK;}
    if(!ok){for(size_t i=0;i<n;++i){MH_DisableHook(Native<void*>(hooks[i]));MH_RemoveHook(Native<void*>(hooks[i]));}status.store("Cube map hook installation failed.");return false;}
    installed.store(true);D2RHUD_CubeMapDiagnostics.installed=1;D2RHUD_CubeMapDiagnostics.survivorsEnabled=survivorsAvailable;CubeVisual::SetMapPrepare(Prepare);
    if(survivorsAvailable)status.store("Cube maps: map=<level ID> or map=zzz for random. Sanctuary Survivors: approach Charsi in Act 1.");
    else {
        std::snprintf(compatibilityStatus,sizeof(compatibilityStatus),"Cube maps available. Survivors unavailable: signature mismatch at D2R.exe+0x%x.",survivorsFailed);
        status.store(compatibilityStatus);
    }
    return true;
}
void Uninstall(){
    CubeVisual::SetMapPrepare(nullptr);if(!installed.exchange(false))return;
    D2RHUD_CubeMapDiagnostics.installed=0;
    D2RHUD_CubeMapDiagnostics.survivorsEnabled=0;
    std::vector<uint32_t> hooks{0x27e1d0u,0x3c1080u,0x2c35b0u,0x27c360u,0x2c5900u,0x28b920u,0x6c750u,0x85060u,0x2f8a60u,0x294e70u,0x294f40u,0x1e5440u,0x1e5590u,0x969f0u,0x93690u};
    hooks.push_back(0x1e8390u);hooks.push_back(0x1e7f70u);hooks.push_back(0x1d0a20u);
    if(saveLoadAvailable)for(auto hook:{0x2934b0u,0x318c80u})hooks.push_back(hook);
    if(survivorsAvailable)for(auto hook:{0x28f2b0u,0x29f350u,0x29f090u,0x28c740u,0x6ff60u})hooks.push_back(hook);
    for(auto hook:hooks)MH_QueueDisableHook(Native<void*>(hook));
    if(MH_ApplyQueued()==MH_OK)for(auto hook:hooks)MH_RemoveHook(Native<void*>(hook));
    championClashPopulation.reset();
    {std::lock_guard lock(viewMutex);ClearEventAutomap();view={};receivedAt=0;survivorView={};survivorReceived=0;}
    // DLL unloading during a live run would invalidate native temporary lists.
    // The launcher unloads only after game shutdown; process teardown lets the
    // operating system reclaim the registered population allocation.
}
}
