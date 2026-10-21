#include "QuestKillEvents.h"
#include "QuestJournal.h"
#include "LegacyTransmogData.h"
#include "../Pattern.h"
#include "../d2/Reclass.h"
#include <MinHook.h>
#include <array>
#include <atomic>

namespace QuestKillEvents {
namespace {
using Unit=D2UnitStrc;using Game=D2GameStrc;
using Kill=void(__fastcall*)(Game*,Unit*,Unit*,int);
Kill original=nullptr;
std::atomic<bool> installed{false};
std::atomic<const char*> status{"Quest kill tracking is not installed."};
constexpr std::array<LegacyTransmog::Data::Signature,3> signatures{{
    {0x328000,0x268,0xf1a3b1e2eb88c3b1ULL},
    {0x2c49c0,0x20,0x04d772b7be349296ULL},
    {0x1e5620,0x72,0xa1e6f0ef87a796f0ULL}
}};
template<class T>T Native(uint32_t rva){return reinterpret_cast<T>(Pattern::Address(rva));}
template<class T>bool Read(const void* p,T& out){SIZE_T count=0;return p&&ReadProcessMemory(GetCurrentProcess(),p,&out,sizeof(out),&count)&&count==sizeof(out);}
template<class T>T Field(const void* p,size_t off){T out{};if(p)Read(static_cast<const uint8_t*>(p)+off,out);return out;}
Unit* Lookup(Game* game,int type,uint32_t id){return Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0)(game,type,id);}
bool Supported(){
    std::array<uint8_t,0x268> bytes{};
    for(const auto& signature:signatures){SIZE_T count=0;if(!ReadProcessMemory(GetCurrentProcess(),Native<void*>(signature.rva),bytes.data(),signature.size,&count)||count!=signature.size||LegacyTransmog::Data::Hash({bytes.data(),signature.size})!=signature.hash)return false;}
    return true;
}
Unit* Owner(Game* game,Unit* attacker){
    std::array<Unit*,8> seen{};size_t count=0;
    while(attacker&&count<seen.size()){
        for(size_t n=0;n<count;++n)if(seen[n]==attacker)return nullptr;
        seen[count++]=attacker;Unit unit{};if(!Read(attacker,unit))return nullptr;
        if(Lookup(game,int(unit.dwUnitType),unit.dwUnitId)!=attacker)return nullptr;
        if(unit.dwUnitType==0)return attacker;
        if(unit.dwUnitType!=1||!unit.pMonsterData)return nullptr;
        // Same AI owner chain read by the native kill/experience machinery.
        const auto ai=Field<void*>(unit.pMonsterData,0x30);
        if(!ai||Field<Game*>(ai,0x58)!=game)return nullptr;
        const int type=Field<int>(ai,0x64);const auto id=Field<uint32_t>(ai,0x60);
        if(type!=0&&type!=1)return nullptr;
        attacker=Lookup(game,type,id);
    }
    return nullptr;
}
struct Victim {
    uint32_t id=0,seed=0;int monster=-1,unique=-1;bool valid=false;
};
Victim Capture(Game* game,Unit* victim){
    Victim result;Unit unit{};D2MonsterDataStrc monster{};
    if(!game||!Read(victim,unit)||unit.dwUnitType!=1||unit.dwAnimMode==0||unit.dwAnimMode==12||
       (unit.dwFlags&0x80010000u)||!Read(unit.pMonsterData,monster)||!monster.pMonstatsTxt||Lookup(game,1,unit.dwUnitId)!=victim)return result;
    if(!(Field<uint32_t>(monster.pMonstatsTxt,0x3c)&0x8000u)||Native<int(__fastcall*)(Unit*,uint32_t,uint16_t)>(0x1e5620)(victim,172,0)!=0)return result;
    result.id=unit.dwUnitId;result.seed=uint32_t(unit.pSeed.dwSeed[0]);result.monster=int(unit.dwClassId);
    if(monster.nTypeFlag&2)result.unique=int(int16_t(monster.wBossHcIdx));result.valid=true;return result;
}
void __fastcall Hook(Game* game,Unit* victim,Unit* attacker,int petKill){
    if(!installed.load()||!game){original(game,victim,attacker,petKill);return;}
    const auto before=Capture(game,victim);Unit* owner=before.valid?Owner(game,attacker):nullptr;
    const auto ownerId=owner?Field<uint32_t>(owner,8):0;
    original(game,victim,attacker,petKill);
    if(!before.valid||!owner||Lookup(game,0,ownerId)!=owner)return;
    // Native on-death scripts can delete the allocation synchronously. A
    // removed accepted victim is final; a still-live/failed transition is not.
    const auto after=Lookup(game,1,before.id);Unit state{};
    const bool dead=!after||(after==victim&&Read(after,state)&&(state.dwAnimMode==0||state.dwAnimMode==12||(state.dwFlags&0x10000u)));
    if(dead)QuestJournal::Killed(game,owner,before.id,before.seed,before.monster,before.unique);
}
}
bool Install(){
    if(installed.load())return true;
    if(!Supported()){status.store("Quest kill tracking unavailable: native signature mismatch.");return false;}
    const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED){status.store("Quest kill tracking unavailable: hook initialization failed.");return false;}
    auto target=Native<void*>(0x328000);
    if(MH_CreateHook(target,reinterpret_cast<void*>(Hook),reinterpret_cast<void**>(&original))!=MH_OK){status.store("Quest kill tracking unavailable: kill hook creation failed.");return false;}
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);status.store("Quest kill tracking unavailable: kill hook enable failed.");return false;}
    installed.store(true);status.store("Quest kills track server-confirmed deaths and their player or summon owner.");return true;
}
void Uninstall(){if(!installed.exchange(false))return;auto target=Native<void*>(0x328000);if(MH_DisableHook(target)==MH_OK)MH_RemoveHook(target);status.store("Quest kill tracking is not installed.");}
const char* Status(){return status.load();}
}
