// Champion Clash reuses native room density and native elite packs. Its private
// population copy replaces the ordinary/elite decision; the executable and
// shared MonStats/Levels records remain untouched.
constexpr uint16_t ChampionClashPackLimit=512;
struct ChampionClashPopulation {
    uint8_t* code=nullptr;
    ~ChampionClashPopulation(){if(code){RtlDeleteFunctionTable(reinterpret_cast<RUNTIME_FUNCTION*>(code+0xc00));VirtualFree(code,0,MEM_RELEASE);}}
};
std::unique_ptr<ChampionClashPopulation> championClashPopulation;
struct ChampionClashPopulationContext {
    Game* game=nullptr;Run* run=nullptr;
    uint16_t packs=0;bool overflow=false,invalid=false;
    std::map<uint32_t,Unit*> before;
};
thread_local ChampionClashPopulationContext* championClashContext=nullptr;
Unit* __fastcall ChampionClashLeader(Game* game,Room* room,void* coords,int x,int y,int guid,int monster,int flags){
    auto context=championClashContext;
    if(!context || context->game!=game || !context->run || context->run->event!=Event::ChampionClash || !MapPackCandidate(monster)){
        if(context)context->invalid=true;return nullptr;
    }
    if(context->packs>=ChampionClashPackLimit){context->overflow=true;return nullptr;}
    context->before=MapPackUnits(*context->run);
    return Native<Unit*(__fastcall*)(Game*,Room*,void*,int,int,int,int,int)>(0x2fad40)(game,room,coords,x,y,guid,monster,flags);
}
void __fastcall ChampionClashGroup(Game* game,Unit* leader,void* coords,int monster){
    auto context=championClashContext;
    if(!context || context->game!=game || !context->run){if(context)context->invalid=true;return;}
    Native<void(__fastcall*)(Game*,Unit*,void*,int)>(0x2dce30)(game,leader,coords,monster);
    const auto after=MapPackUnits(*context->run);
    const auto flags=leader && leader->pMonsterData ? leader->pMonsterData->nTypeFlag : uint8_t(0);
    size_t members=0;
    for(const auto& [id,unit]:after)if(!context->before.contains(id) && !Dead(unit))++members;
    if(leader && (flags&(4|8)) && members>=((flags&4) ? 2u : 4u)){++context->packs;return;}
    // Native collision can leave only part of a pack. Discard that group and
    // continue the room's remaining native density attempts.
    for(const auto& [id,unit]:after)if(!context->before.contains(id))originalRemove(game,unit);
}
bool BuildChampionClashPopulation(){
    if(championClashPopulation && championClashPopulation->code)return true;
    auto result=std::make_unique<ChampionClashPopulation>();
    SYSTEM_INFO info{};GetSystemInfo(&info);const auto base=Pattern::Address(0),step=uintptr_t(info.dwAllocationGranularity);
    for(uintptr_t delta=step;delta<0x40000000 && !result->code;delta+=step)for(int direction:{1,-1}){
        const auto candidate=(direction>0 ? base+delta : base>delta ? base-delta : 0)&~(step-1);
        if(candidate)result->code=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(candidate),NativeData::Allocation,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(result->code)break;
    }
    auto code=result->code;if(!code)return false;
    memcpy(code,Native<void*>(NativeData::Source),NativeData::Size);
    for(const auto& fixup:NativeData::Fixups){
        uintptr_t target=Pattern::Address(fixup.target);
        if(fixup.target==0x2fad40)target=reinterpret_cast<uintptr_t>(code+0xe00);
        if(fixup.target==0x2dce30)target=reinterpret_cast<uintptr_t>(code+0xe20);
        const auto difference=int64_t(target)-int64_t(reinterpret_cast<uintptr_t>(code)+fixup.next);
        if(difference<INT32_MIN || difference>INT32_MAX)return false;
        const int32_t value=int32_t(difference);memcpy(code+fixup.offset,&value,4);
    }
    // At 2dddc3 the native room generator chooses elite versus ordinary packs.
    // Always enter the complete native elite path at 2dde76 in this copy.
    constexpr uint32_t decision=0x283,elite=0x336;
    const int32_t jump=int32_t(elite-decision-5);code[decision]=0xe9;memcpy(code+decision+1,&jump,4);memset(code+decision+5,0x90,3);
    code[0xd8]=0xeb; // Retain the existing map-only quest-cleared bypass.
    for(const auto& block:NativeData::UnwindBlocks)memcpy(code+block.offset,block.bytes.data(),block.size);
    auto runtime=reinterpret_cast<RUNTIME_FUNCTION*>(code+0xc00);
    for(size_t i=0;i<NativeData::RuntimeFunctions.size();++i){const auto& r=NativeData::RuntimeFunctions[i];runtime[i]={r.begin,r.end,r.unwind};}
    const uint8_t absoluteJump[]{0xff,0x25,0,0,0,0};
    memcpy(code+0xe00,absoluteJump,6);const auto create=reinterpret_cast<uintptr_t>(ChampionClashLeader);memcpy(code+0xe06,&create,8);
    memcpy(code+0xe20,absoluteJump,6);const auto complete=reinterpret_cast<uintptr_t>(ChampionClashGroup);memcpy(code+0xe26,&complete,8);
    DWORD old=0;if(!VirtualProtect(code,NativeData::Allocation,PAGE_EXECUTE_READ,&old))return false;
    FlushInstructionCache(GetCurrentProcess(),code,NativeData::Allocation);
    if(!RtlAddFunctionTable(runtime,DWORD(NativeData::RuntimeFunctions.size()),reinterpret_cast<uintptr_t>(code)))return false;
    championClashPopulation=std::move(result);return true;
}
using ChampionClashPopulate=void(__fastcall*)(Game*,Room*);
bool PopulateChampionClashWith(Game* game,Run& run,Position entrance,ChampionClashPopulate populate){
    if(run.event!=Event::ChampionClash)return true;
    if(!game || run.level!=108 || !populate || run.pins.empty())return false;
    auto roster=MapPackRoster(run);if(roster.empty())return false;
    run.roster=std::move(roster);run.spawnedPacks=run.targetPacks=0;
    // Old scripted combat actors must never survive into this private event.
    // Objects and dropped items are preserved by the ordinary reload path.
    for(const auto& [id,unit]:MapPackUnits(run))if(Monster(unit,false))originalRemove(game,unit);
    const auto existing=MapPackUnits(run);
    ChampionClashPopulationContext context{game,&run};
    struct Scope {
        ChampionClashPopulationContext* previous;
        Scope(ChampionClashPopulationContext* current):previous(championClashContext){championClashContext=current;}
        ~Scope(){championClashContext=previous;}
    } scope(&context);
    for(const auto pin:run.pins){
        const auto room=Field<Room*>(pin.room,0x58);
        if(!room){context.invalid=true;break;}
        populate(game,room);
        if(context.overflow || context.invalid)break;
    }
    const auto generated=MapPackUnits(run);
    for(const auto& [id,unit]:generated)if(!existing.contains(id) && !Dead(unit)){
        const auto flags=unit->pMonsterData ? unit->pMonsterData->nTypeFlag : uint8_t(0);
        if(!(flags&(4|8|0x10)))context.invalid=true;
    }
    if(!context.packs || context.overflow || context.invalid){
        for(const auto& [id,unit]:generated)if(!existing.contains(id))originalRemove(game,unit);
        return false;
    }
    run.spawnedPacks=context.packs;return true;
}
bool PopulateChampionClash(Game* game,Run& run,Position entrance){
    if(run.event!=Event::ChampionClash)return true;
    return BuildChampionClashPopulation() && PopulateChampionClashWith(game,run,entrance,reinterpret_cast<ChampionClashPopulate>(championClashPopulation->code));
}
