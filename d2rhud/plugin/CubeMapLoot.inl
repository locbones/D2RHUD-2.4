// Scoped root/all-node TC rules. The original wrapper chooses the normal
// difficulty/monster-level upgrade unless an event explicitly overrides it.
// Surge changes root picks; Plentiful adds one root pick and clears NoDrop
// on private root/child copies, removing Blank weights without adding nested picks.
using SelectedTreasure=void(__fastcall*)(Game*,Unit*,Unit*,void*,int,int,int,Unit**,int*,int);
std::unique_ptr<MonsterInitializer> lootSelector;
std::unique_ptr<MonsterInitializer> plentifulLootWalker;
thread_local int additionalTreasurePicks=0;
thread_local bool plentifulTreasure=false;
thread_local int overriddenTreasureClass=-1;
thread_local void(*treasureCreatedObserver)(Unit*)=nullptr;
void ObserveTreasure(const std::vector<Unit*>& items,int count){
    if(!treasureCreatedObserver)return;std::set<Unit*> observed;
    for(int n=0;n<count;++n)if(items[n] && observed.insert(items[n]).second)treasureCreatedObserver(items[n]);
}

struct PrivateTreasure {
    std::array<uint8_t,LootNativeData::RecordSize> record{};
    std::array<std::array<uint8_t,0x1c>,2> wrapper{};
    std::vector<std::array<uint8_t,0x1c>> entries;
};
struct PrivateTreasureGraph {
    uint8_t* table=nullptr;uint64_t count=0;int synthetic=65534;
    std::map<int,std::unique_ptr<PrivateTreasure>> records;
    std::unique_ptr<PrivateTreasure> root;
    bool valid=true;int blank=-1;
    PrivateTreasureGraph():table(Field<uint8_t*>(Native<void*>(0x1c9f6b0),0)),count(Field<uint64_t>(Native<void*>(0x1c9f6b8),0)){
        valid=table && count>0 && count<65534;
        if(valid){const int id=LookupName(0x1c9f6a8,"Blank");if(id>0 && uint64_t(id)<count)blank=id;}
    }
    PrivateTreasure* Raw(const void* source){
        if(!valid || uint64_t(synthetic)<count){valid=false;return nullptr;}
        auto node=std::make_unique<PrivateTreasure>();memcpy(node->record.data(),source,node->record.size());
        Set(node->record.data(),0x10,int(0));auto result=node.get();records.emplace(synthetic--,std::move(node));return result;
    }
    bool RemoveBlank(PrivateTreasure& node){
        if(blank<0)return true;
        const auto source=Field<uint8_t*>(node.record.data(),0x28);const auto size=Field<uint64_t>(node.record.data(),0x30);
        if(size>1000000 || (size && !source))return valid=false;
        auto isBlank=[&](const void* entry){return (Field<uint8_t>(entry,0xc)&4) && Field<uint16_t>(entry,8)==blank;};
        bool found=false;for(uint64_t n=0;n<size;++n)if(isBlank(source+n*0x1c)){found=true;break;}
        if(!found)return true;
        // Treat the named empty TC like NoDrop=0: remove its probability
        // interval instead of rerolling. Preserve both native distributions
        // and all real entries' relative weights in private copies only.
        std::array<int,2> totals{};
        for(uint64_t n=0;n<size;++n){
            const auto entry=source+n*0x1c;std::array<int,2> weights{};
            for(size_t distribution=0;distribution<2;++distribution){
                const auto offset=distribution*4;
                const int begin=Field<int>(entry,offset);
                const int end=n+1<size ? Field<int>(entry+0x1c,offset) : Field<int>(node.record.data(),4+offset);
                if(begin<0 || end<begin)return valid=false;weights[distribution]=end-begin;
            }
            if(isBlank(entry))continue;
            node.entries.emplace_back();memcpy(node.entries.back().data(),entry,0x1c);
            for(size_t distribution=0;distribution<2;++distribution){
                Set(node.entries.back().data(),distribution*4,totals[distribution]);
                totals[distribution]+=weights[distribution];
            }
        }
        for(size_t distribution=0;distribution<2;++distribution)Set(node.record.data(),4+distribution*4,totals[distribution]);
        Set(node.record.data(),0x28,node.entries.empty() ? static_cast<void*>(nullptr) : node.entries.data());
        Set(node.record.data(),0x30,uint64_t(node.entries.size()));return true;
    }
    bool Transform(PrivateTreasure& node,const void* source,int bonus){
        memcpy(node.record.data(),source,node.record.size());Set(node.record.data(),0x10,int(0));
        if(!RemoveBlank(node))return false;
        const auto filtered=node.record;source=filtered.data();
        const int picks=Field<int>(source,0xc);
        if(picks==INT_MIN || picks>INT_MAX-bonus)return valid=false;
        if(picks>=0){if(bonus)Set(node.record.data(),0xc,picks+bonus);}
        else if(bonus){
            // A two-pick deterministic wrapper retains every vanilla negative
            // pick and then performs only the added ROOT weighted picks.
            // Descendants retain native Picks; only their Blank weights are removed.
            const int originalId=synthetic;auto original=Raw(source);if(!original)return false;
            const int bonusId=synthetic;auto extra=Raw(source);if(!extra)return false;
            Set(extra->record.data(),0xc,bonus);
            Set(node.record.data(),4,int(2));Set(node.record.data(),8,int(2));Set(node.record.data(),0xc,int(-2));
            for(int n=0;n<2;++n){Set(node.wrapper[n].data(),0,n);Set(node.wrapper[n].data(),4,n);Set(node.wrapper[n].data(),8,uint16_t(n ? bonusId : originalId));Set(node.wrapper[n].data(),0xc,uint8_t(4));}
            Set(node.record.data(),0x28,node.wrapper.data());Set(node.record.data(),0x30,uint64_t(2));
        }
        const auto info=Field<uint8_t*>(source,0x28);const auto entries=Field<uint64_t>(source,0x30);
        if(entries>1000000 || (entries && !info))return valid=false;
        for(uint64_t n=0;n<entries;++n)if(Field<uint8_t>(info+n*0x1c,0xc)&4){
            const auto id=Field<uint16_t>(info+n*0x1c,8);
            if(!id || id>=count)return valid=false;
            if(!records.contains(id)){
                auto next=std::make_unique<PrivateTreasure>();auto pointer=next.get();records.emplace(id,std::move(next));
                if(!Transform(*pointer,table+size_t(id)*LootNativeData::RecordSize,0))return false;
            }
        }
        return true;
    }
    void* Build(const void* source,int rootBonus){
        if(!valid)return nullptr;root=std::make_unique<PrivateTreasure>();
        return Transform(*root,source,1+rootBonus) ? root->record.data() : nullptr;
    }
};
thread_local PrivateTreasureGraph* activeTreasureGraph=nullptr;
void* __fastcall PrivateTreasureLookup(int id){
    if(activeTreasureGraph){auto found=activeTreasureGraph->records.find(id);return found==activeTreasureGraph->records.end() ? nullptr : found->second->record.data();}
    return nullptr;
}

bool DropPlentifulTreasure(Game* game,Unit* monster,Unit* player,void* record,int quality,int level,int a7,Unit** items,int* count,int maximum,int bonus){
    if(!plentifulLootWalker || !record || bonus<0 || bonus>3 || (items && maximum<=0))return false;
    const int baseMaximum=maximum>0 ? maximum : 6;if(baseMaximum>INT_MAX-bonus-1)return false;
    PrivateTreasureGraph graph;const auto root=graph.Build(record,bonus);if(!root)return false;
    const int expanded=baseMaximum+bonus+1;
    const bool capture=items || treasureCreatedObserver;
    std::vector<Unit*> captured(capture ? size_t(expanded) : 0);int dropped=0;
    struct Scope {PrivateTreasureGraph* previous;Scope(PrivateTreasureGraph* graph):previous(activeTreasureGraph){activeTreasureGraph=graph;}~Scope(){activeTreasureGraph=previous;}} scope(&graph);
    reinterpret_cast<SelectedTreasure>(plentifulLootWalker->code)(game,monster,player,root,quality,level,a7,capture ? captured.data() : nullptr,&dropped,expanded);
    dropped=(std::clamp)(dropped,0,expanded);
    ObserveTreasure(captured,dropped);
    if(items){const int reported=(std::min)(dropped,maximum);std::copy_n(captured.begin(),reported,items);if(count)*count=reported;}
    else if(count)*count=dropped;
    return true;
}

void __fastcall DropSelectedTreasure(Game* game,Unit* monster,Unit* player,void* record,int quality,int level,int a7,Unit** items,int* count,int maximum){
    namespace N=LootNativeData;const auto native=Native<SelectedTreasure>(N::Walker);
    // Explicit event names select that exact loaded record. The vanilla
    // wrapper may have upgraded the incoming ID by group/monster level, so
    // substitute only here, after its normal selection has completed.
    if(overriddenTreasureClass>=0){const auto table=Field<uint8_t*>(Native<void*>(0x1c9f6b0),0);const auto rows=Field<uint64_t>(Native<void*>(0x1c9f6b8),0);if(table && uint64_t(overriddenTreasureClass)<rows)record=table+size_t(overriddenTreasureClass)*N::RecordSize;}
    const int bonus=additionalTreasurePicks;
    if(plentifulTreasure && DropPlentifulTreasure(game,monster,player,record,quality,level,a7,items,count,maximum,bonus))return;
    const int baseMaximum=maximum>0 ? maximum : 6;
    if(!record || bonus<0 || bonus>3 || (!bonus && !treasureCreatedObserver) || (items && maximum<=0) || baseMaximum>INT_MAX-bonus){
        native(game,monster,player,record,quality,level,a7,items,count,maximum);return;
    }
    std::array<uint8_t,N::RecordSize> copy{};memcpy(copy.data(),record,copy.size());
    const int picks=Field<int>(record,N::Picks);
    if(picks==INT_MIN || picks>INT_MAX-bonus){native(game,monster,player,record,quality,level,a7,items,count,maximum);return;}
    // The default native six-item cap becomes seven to nine. A private output
    // buffer prevents writing beyond a caller's original array capacity. All
    // extra items are still created on the ground by the real native walker.
    const int expanded=baseMaximum+bonus;
    const bool capture=items || treasureCreatedObserver;
    std::vector<Unit*> captured(capture ? size_t(expanded) : 0);
    Unit** output=capture ? captured.data() : nullptr;int dropped=0;
    if(!bonus){native(game,monster,player,record,quality,level,a7,output,&dropped,baseMaximum);}
    else if(picks>=0){
        Set(copy.data(),N::Picks,(std::max)(picks,1)+bonus);
        native(game,monster,player,copy.data(),quality,level,a7,output,&dropped,expanded);
    }else{
        // Negative Picks enumerate deterministic entries. Increasing their
        // magnitude changes the offset calculation and cannot represent extra
        // weighted picks safely. Preserve the complete vanilla result first,
        // then draw only the bonus picks from the same root's weighted entries.
        native(game,monster,player,record,quality,level,a7,output,&dropped,baseMaximum);
        dropped=(std::clamp)(dropped,0,baseMaximum);int extra=0;
        Set(copy.data(),N::Picks,bonus);
        native(game,monster,player,copy.data(),quality,level,a7,output ? output+dropped : nullptr,&extra,expanded-dropped);
        dropped+=(std::clamp)(extra,0,expanded-dropped);
    }
    dropped=(std::clamp)(dropped,0,expanded);
    ObserveTreasure(captured,dropped);
    if(items){const int reported=(std::min)(dropped,maximum);std::copy_n(captured.begin(),reported,items);if(count)*count=reported;}
    else if(count)*count=dropped;
}

bool BuildPlentifulLootWalker(){
    if(plentifulLootWalker)return true;
    namespace N=LootWalkerNativeData;auto result=std::make_unique<MonsterInitializer>();
    SYSTEM_INFO info{};GetSystemInfo(&info);const auto base=Pattern::Address(0),step=uintptr_t(info.dwAllocationGranularity);
    for(uintptr_t delta=step;delta<0x40000000 && !result->code;delta+=step)for(int direction:{1,-1}){
        const auto candidate=(direction>0 ? base+delta : base>delta ? base-delta : 0)&~(step-1);
        if(candidate)result->code=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(candidate),N::Allocation,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(result->code)break;
    }
    auto code=result->code;if(!code)return false;memcpy(code,Native<void*>(N::Source),N::Size);
    for(const auto fix:N::Fixups){
        const int64_t difference=int64_t(Pattern::Address(fix.target))-int64_t(reinterpret_cast<uintptr_t>(code)+fix.next);
        if(difference<INT32_MIN || difference>INT32_MAX)return false;const int32_t value=int32_t(difference);memcpy(code+fix.offset,&value,4);
    }
    memset(code+N::Lookup,0x90,N::LookupSize);code[N::Lookup]=0xe8;
    const int32_t lookupRelative=int32_t(N::Adapter)-int32_t(N::Lookup+5);memcpy(code+N::Lookup+1,&lookupRelative,4);
    memcpy(code+N::Adapter,N::AdapterBytes.data(),N::AdapterBytes.size());
    const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(code+N::LookupJump,jump,6);
    const auto lookup=reinterpret_cast<uintptr_t>(PrivateTreasureLookup);memcpy(code+N::LookupJump+6,&lookup,8);
    for(const auto& block:N::UnwindBlocks)memcpy(code+block.offset,block.bytes.data(),block.size);
    auto runtime=reinterpret_cast<RUNTIME_FUNCTION*>(code+0x2800);
    for(size_t i=0;i<N::RuntimeFunctions.size();++i){const auto& r=N::RuntimeFunctions[i];runtime[i]={r.begin,r.end,r.unwind};}
    memcpy(code+0x2d00,jump,6);const auto handler=Pattern::Address(0x14359a4);memcpy(code+0x2d06,&handler,8);
    DWORD old=0;if(!VirtualProtect(code,N::Allocation,PAGE_EXECUTE_READ,&old))return false;
    FlushInstructionCache(GetCurrentProcess(),code,N::Allocation);
    if(!RtlAddFunctionTable(runtime,DWORD(N::RuntimeFunctions.size()),reinterpret_cast<uintptr_t>(code)))return false;
    plentifulLootWalker=std::move(result);return true;
}

bool BuildLootSelector(){
    if(!BuildPlentifulLootWalker())return false;
    if(lootSelector)return true;
    namespace N=LootNativeData;auto result=std::make_unique<MonsterInitializer>();
    SYSTEM_INFO info{};GetSystemInfo(&info);const auto base=Pattern::Address(0),step=uintptr_t(info.dwAllocationGranularity);
    for(uintptr_t delta=step;delta<0x40000000 && !result->code;delta+=step)for(int direction:{1,-1}){
        const auto candidate=(direction>0 ? base+delta : base>delta ? base-delta : 0)&~(step-1);
        if(candidate)result->code=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(candidate),N::Allocation,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(result->code)break;
    }
    auto code=result->code;if(!code)return false;memcpy(code,Native<void*>(N::Source),N::Size);
    for(const auto fix:N::Fixups){
        const int64_t difference=int64_t(Pattern::Address(fix.target))-int64_t(reinterpret_cast<uintptr_t>(code)+fix.next);
        if(difference<INT32_MIN || difference>INT32_MAX)return false;const int32_t value=int32_t(difference);memcpy(code+fix.offset,&value,4);
    }
    // A local absolute-jump island keeps the wrapper's rel32 call valid even
    // when the DLL itself lies more than 2 GiB away from the game executable.
    const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(code+N::Adapter,jump,6);
    const auto adapter=reinterpret_cast<uintptr_t>(DropSelectedTreasure);memcpy(code+N::Adapter+6,&adapter,8);
    const int32_t relative=int32_t(N::Adapter)-int32_t(N::WalkerNext);memcpy(code+N::WalkerCall,&relative,4);
    for(const auto& block:N::UnwindBlocks)memcpy(code+block.offset,block.bytes.data(),block.size);
    auto runtime=reinterpret_cast<RUNTIME_FUNCTION*>(code+0x2800);
    for(size_t i=0;i<N::RuntimeFunctions.size();++i){const auto& r=N::RuntimeFunctions[i];runtime[i]={r.begin,r.end,r.unwind};}
    DWORD old=0;if(!VirtualProtect(code,N::Allocation,PAGE_EXECUTE_READ,&old))return false;
    FlushInstructionCache(GetCurrentProcess(),code,N::Allocation);
    if(!RtlAddFunctionTable(runtime,DWORD(N::RuntimeFunctions.size()),reinterpret_cast<uintptr_t>(code)))return false;
    lootSelector=std::move(result);return true;
}

bool DropWithTreasureRules(Game* game,Unit* source,Unit* player,int tc,int quality,int level,int a7,Unit** items,int* count,int maximum,int tcOverride,int bonus,bool plentiful,void(*created)(Unit*)=nullptr){
    if(!lootSelector || !game || !source || bonus<0 || bonus>3 || (!plentiful && !bonus && tcOverride<0 && !created))return false;
    if(tcOverride>=0){const auto rows=Field<uint64_t>(Native<void*>(0x1c9f6b8),0);if(!tcOverride || tcOverride>65535 || uint64_t(tcOverride)>=rows)return false;tc=tcOverride;}
    struct Scope {int previous;bool previousPlentiful;int previousOverride;void(*previousObserver)(Unit*);Scope(int value,bool plentiful,int overrideTC,void(*observer)(Unit*)):previous(additionalTreasurePicks),previousPlentiful(plentifulTreasure),previousOverride(overriddenTreasureClass),previousObserver(treasureCreatedObserver){additionalTreasurePicks=value;plentifulTreasure=plentiful;overriddenTreasureClass=overrideTC;treasureCreatedObserver=observer;}~Scope(){additionalTreasurePicks=previous;plentifulTreasure=previousPlentiful;overriddenTreasureClass=previousOverride;treasureCreatedObserver=previousObserver;}} scope(bonus,plentiful,tcOverride,created);
    reinterpret_cast<DropFunction>(lootSelector->code)(game,source,player,tc,quality,level,a7,items,count,maximum);return true;
}
bool DropExtraPicks(Game* game,Unit* monster,Unit* player,int tc,int quality,int level,int a7,Unit** items,int* count,int maximum,int bonus){
    if(bonus<1 || bonus>3)return false;
    return DropWithTreasureRules(game,monster,player,tc,quality,level,a7,items,count,maximum,-1,bonus,false);
}
