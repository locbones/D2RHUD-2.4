std::unique_ptr<MonsterInitializer> BuildQuality(bool unique,bool both=false){
    namespace N=QualityNativeData;auto result=std::make_unique<MonsterInitializer>();
    SYSTEM_INFO info{};GetSystemInfo(&info);const auto base=Pattern::Address(0),step=uintptr_t(info.dwAllocationGranularity);
    for(uintptr_t delta=step;delta<0x40000000 && !result->code;delta+=step)for(int direction:{1,-1}){
        const auto candidate=(direction>0 ? base+delta : base>delta ? base-delta : 0)&~(step-1);
        if(candidate)result->code=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(candidate),N::Allocation,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(result->code)break;
    }
    auto code=result->code;if(!code)return nullptr;memcpy(code,Native<void*>(N::Source),N::Size);
    for(const auto fix:N::Fixups){
        const int64_t difference=int64_t(Pattern::Address(fix.target))-int64_t(reinterpret_cast<uintptr_t>(code)+fix.next);
        if(difference<INT32_MIN || difference>INT32_MAX)return nullptr;const int32_t value=int32_t(difference);memcpy(code+fix.offset,&value,4);
    }
    const uint32_t threshold=768;memcpy(code+(unique ? 0x274 : 0x31f),&threshold,4);
    if(both){memcpy(code+0x274,&threshold,4);memcpy(code+0x31f,&threshold,4);}
    for(const auto& block:N::UnwindBlocks)memcpy(code+block.offset,block.bytes.data(),block.size);
    auto runtime=reinterpret_cast<RUNTIME_FUNCTION*>(code+0x2800);
    for(size_t i=0;i<N::RuntimeFunctions.size();++i){const auto& r=N::RuntimeFunctions[i];runtime[i]={r.begin,r.end,r.unwind};}
    DWORD old=0;if(!VirtualProtect(code,N::Allocation,PAGE_EXECUTE_READ,&old))return nullptr;
    FlushInstructionCache(GetCurrentProcess(),code,N::Allocation);
    if(!RtlAddFunctionTable(runtime,DWORD(N::RuntimeFunctions.size()),reinterpret_cast<uintptr_t>(code)))return nullptr;
    return result;
}
bool BuildEventQuality(){if(!uniqueQuality)uniqueQuality=BuildQuality(true);if(!setQuality)setQuality=BuildQuality(false);if(!treasureQuality)treasureQuality=BuildQuality(true,true);return uniqueQuality && setQuality && treasureQuality;}
int __fastcall QualityHook(void* unused,Unit* monster,Unit* player,int level,int item,void* ratios){
    std::lock_guard lock(serverMutex);
    for(const auto& [game,state]:games)if(state.seed==game->dwInitSeed && monster && monster->dwUnitType<6 &&
        Lookup(game,monster->dwUnitType,monster->dwUnitId)==monster && HasTimedReward(game,player,Event::TreasureTrove) && treasureQuality)
        return reinterpret_cast<Quality>(treasureQuality->code)(unused,monster,player,level,item,ratios);
    for(const auto& [game,state]:games)if(state.run && state.seed==game->dwInitSeed){
        const auto& run=*state.run;
        if(run.eventPhase!=EventPhase::Active || run.level!=Level(monster) || !monster || Lookup(game,1,monster->dwUnitId)!=monster)continue;
        auto code=run.event==Event::GoldenArmada ? uniqueQuality.get() : run.event==Event::ForsakenFestival ? setQuality.get() : nullptr;
        if(code)return reinterpret_cast<Quality>(code->code)(unused,monster,player,level,item,ratios);
    }
    return originalQuality(unused,monster,player,level,item,ratios);
}
bool FountainPlayer(Unit* unit){
    if(!unit || unit->dwUnitType!=0)return false;
    {
        std::lock_guard lock(serverMutex);
        for(const auto& [game,state]:games)if(state.run && (state.run->event==Event::FountainOfYouth || (state.run->event==Event::Survivors && AvatarFor(game,unit))) && state.run->eventPhase==EventPhase::Active &&
            Level(unit)==state.run->level && Lookup(game,0,unit->dwUnitId)==unit)return true;
    }
    std::lock_guard lock(viewMutex);
    return LocalPlayer()==unit && view.active && (view.event==Event::FountainOfYouth || view.event==Event::Survivors) && view.eventPhase==EventPhase::Active && view.recipient==unit->dwUnitId &&
        receivedAt && GetTickCount64()-receivedAt<4000 && Level(unit)==view.level;
}
int __fastcall ManaConsumeHook(Unit* unit,int cost){return FountainPlayer(unit) ? 1 : originalManaConsume(unit,cost);}
int __fastcall ManaCheckHook(Unit* unit){
    // Charged-item skills still require and consume charges. Only a spell's
    // mana requirement is waived, on both the client and authoritative server.
    if(FountainPlayer(unit)){
        const auto skills=Field<void*>(unit,0x100);const auto used=skills ? Field<void*>(skills,0x18) : nullptr;
        if(used && Field<int>(used,0x44)==-1)return 1;
    }
    return originalManaCheck(unit);
}

void __fastcall ManaSetHook(Unit* unit,int stat,int value,uint16_t layer){
    if(stat==8 && !layer && FountainPlayer(unit)){const int maximum=Stat(unit,9);if(maximum>0)value=maximum;}
    originalManaSet(unit,stat,value,layer);
}
void __fastcall ManaAddHook(Unit* unit,int stat,int value,uint16_t layer){
    if(stat==8 && !layer && FountainPlayer(unit)){const int maximum=Stat(unit,9);if(maximum>0){originalManaSet(unit,8,maximum,0);return;}}
    originalManaAdd(unit,stat,value,layer);
}
