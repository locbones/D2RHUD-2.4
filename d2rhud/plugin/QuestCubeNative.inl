// The native recipe scanner keeps every ordinary header/op/input predicate.
// Each invocation owns its recipe copies, code, count and pointer cells: no
// shared table pointer, recipe record, or live item stat is temporarily changed.
namespace QuestNative {
constexpr uint32_t MatcherRva=0x308840;
constexpr size_t MatcherSize=0x82c;
struct Fixup {uint16_t instruction;uint8_t displacement,length;uint32_t target;};
// Complete relative-address inventory for the signature below, including both
// switch-base LEAs. Switch entries themselves remain native RVA values.
constexpr Fixup Fixups[]{
    {0x14,3,7,0x17f6178},{0x26,3,7,0x1ca0b20},{0x3e,1,5,0x208330},
    {0xd6,2,6,0x1baa468},{0xec,1,5,0xae3a10},{0x10f,1,5,0xae4920},
    {0x14c,3,7,0x1c9fb80},{0x153,3,7,0},{0x15a,3,7,0x1c9fb78},
    {0x172,3,7,0x1ca0b20},{0x186,3,7,0x1ca0b18},{0x198,2,7,0x1bfa310},
    {0x2b1,1,5,0x1e2e70},{0x31e,1,5,0x1e2e70},{0x38b,1,5,0x1e2e70},
    {0x3f8,1,5,0x1e2e70},{0x471,1,5,0x1e2e00},{0x4dd,1,5,0x1e2e00},
    {0x549,1,5,0x1e2e00},{0x5b5,1,5,0x1e2e00},{0x5f6,1,5,0x1e56a0},
    {0x603,3,7,0x1c9fb80},{0x60a,3,7,0x1c9fb78},{0x611,3,7,0},
    {0x67b,1,5,0x1e56a0},{0x6c8,1,5,0x1e56a0},{0x70e,1,5,0x1e56a0},
    {0x732,1,5,0x208330},{0x753,1,5,0x1438100},{0x77e,1,5,0x305d00},
    {0x7b7,1,5,0x1434c50},{0x7d0,1,5,0x6bc3b0},{0x7e3,1,5,0x6bc0f0},
    {0x7ed,1,5,0x143d4e4}
};
std::array<uint8_t,MatcherSize> original{};
std::atomic<bool> ready{false};
template<class T>T Field(const void* address,size_t offset){T value{};memcpy(&value,static_cast<const uint8_t*>(address)+offset,sizeof(value));return value;}
struct Invocation {
    uint8_t* memory=nullptr;
    RUNTIME_FUNCTION unwind{};
    bool registered=false;
    Invocation()=default;
    Invocation(const Invocation&)=delete;
    ~Invocation(){if(registered)RtlDeleteFunctionTable(&unwind);if(memory)VirtualFree(memory,0,MEM_RELEASE);}
    bool Build(Recipe* recipes,uint64_t rows){
        // All original calls/globals and their rel32 operands remain in range.
        SYSTEM_INFO system{};GetSystemInfo(&system);const auto gran=uintptr_t(system.dwAllocationGranularity);
        const auto native=Pattern::Address(MatcherRva);
        const auto low=(std::max)(uintptr_t(system.lpMinimumApplicationAddress),native>0x20000000 ? native-0x20000000 : uintptr_t(0));
        const auto high=(std::min)(uintptr_t(system.lpMaximumApplicationAddress),native+0x20000000);
        for(uintptr_t at=(low+gran-1)&~(gran-1);at<high;){
            MEMORY_BASIC_INFORMATION region{};if(!VirtualQuery(reinterpret_cast<void*>(at),&region,sizeof(region)))break;
            const auto end=reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize;
            if(end<=at)break;
            if(region.State==MEM_FREE && at+0x2000<=end){
                memory=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(at),0x2000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
                if(memory)break;
            }
            if(end>UINTPTR_MAX-gran)break;at=(end+gran-1)&~(gran-1);
        }
        if(!memory)return false;
        memcpy(memory,original.data(),original.size());
        const auto tableCell=reinterpret_cast<uintptr_t>(memory+0x1000),countCell=tableCell+8;
        memcpy(memory+0x1000,&recipes,8);memcpy(memory+0x1008,&rows,8);
        for(const auto& fix:Fixups){
            uintptr_t destination=0;
            if(fix.target==0)destination=reinterpret_cast<uintptr_t>(memory)-MatcherRva;
            else if(fix.target==0x1ca0b18)destination=tableCell;
            else if(fix.target==0x1ca0b20)destination=countCell;
            else destination=Pattern::Address(fix.target);
            const int64_t delta=int64_t(destination)-int64_t(reinterpret_cast<uintptr_t>(memory)+fix.instruction+fix.length);
            if(delta<INT32_MIN || delta>INT32_MAX)return false;
            const int32_t relative=int32_t(delta);memcpy(memory+fix.instruction+fix.displacement,&relative,4);
        }
        // Exact unwind description of the signature-checked prologue: eight
        // nonvolatile pushes followed by sub rsp,0x188. Register the clone so
        // Windows stack walks/unwinding retain the original caller correctly.
        constexpr uint8_t info[]{1,0x14,10,0,0x14,1,0x31,0,0xd,0xf0,0xb,0xe0,9,0xd0,7,0xc0,5,0x70,4,0x60,3,0x50,2,0x30};
        memcpy(memory+0x900,info,sizeof(info));unwind={0,0x7f3,0x900};
        if(!RtlAddFunctionTable(&unwind,1,reinterpret_cast<DWORD64>(memory)))return false;
        registered=true;
        DWORD previous=0;if(!VirtualProtect(memory,0x1000,PAGE_EXECUTE_READ,&previous))return false;
        FlushInstructionCache(GetCurrentProcess(),memory,0x1000);return true;
    }
};
bool SafeInventory(Unit* player){
    if(!player || player->dwUnitType!=0 || !player->pInventory || player->pInventory->dwSignature!=0x1020304 || player->pInventory->pOwner!=player)return false;
    std::vector<Unit*> seen;size_t cube=0;
    for(auto item=player->pInventory->pFirstItem;item;item=item->pItemData->pExtraData.pNextItem){
        if(seen.size()>=4096 || std::find(seen.begin(),seen.end(),item)!=seen.end() || item->dwUnitType!=4 || !item->pItemData)return false;
        seen.push_back(item);if(item->pItemData->nPage==3 && ++cube>=48)return false;
    }
    return true;
}
}
bool InitQuestMatcher(){
    QuestNative::ready.store(false);
    std::array<uint8_t,0xb4c> lower{};
    if(!Read(Pattern::Address(QuestNative::MatcherRva),QuestNative::original) ||
       LegacyTransmog::Data::Hash(QuestNative::original)!=0xb01c7cc2c185785fULL ||
       !Read(Pattern::Address(0x305d00),lower) || LegacyTransmog::Data::Hash(lower)!=0x0cd622517e6e82e9ULL)return false;
    QuestNative::ready.store(true);return true;
}
void ShutdownQuestMatcher(){QuestNative::ready.store(false);}
bool TryQuestMatch(RuneMatch* result,Game* game,Unit* player,Input* inputs){
    const auto rules=QuestJournal::Bindings();
    const bool journal=rules&&!rules->gates.empty();
    if(!result || !inputs)return false;
    *result={};std::fill_n(inputs,3,Input{});
    auto refuse=[&](const char* message){if(journal)QuestJournal::Refused(game,player,message);return journal;};
    if(!QuestNative::ready.load())return refuse("Quest recipes unavailable: native matcher signature mismatch.");
    if(!game || !QuestNative::SafeInventory(player))return refuse("Quest recipes unavailable: unsafe cube inventory.");
    Recipe* source=nullptr;uint64_t count=0;
    if(!Read(Pattern::Address(0x1ca0b18),source) || !source || !Read(Pattern::Address(0x1ca0b20),count) || !count || count>100000)
        return refuse("Quest recipes unavailable: loaded cube table cannot be read.");
    std::vector<Recipe> copies(size_t(count),Recipe{});SIZE_T got=0;
    if(!ReadProcessMemory(GetCurrentProcess(),source,copies.data(),copies.size()*sizeof(Recipe),&got) || got!=copies.size()*sizeof(Recipe))
        return refuse("Quest recipes unavailable: cube table cannot be read.");
    const auto progress=QuestJournal::Inspect(game,player);std::vector<bool> seen(copies.size());
    if(journal)for(const auto& gate:rules->gates){
        if(gate.row>=copies.size() || seen[gate.row])return refuse("Quest recipes unavailable: invalid or duplicate quest recipe binding.");
        seen[gate.row]=true;auto& recipe=copies[gate.row];auto& header=recipe.header;
        if(header[0]!=gate.enabled || header[0x10]!=gate.inputs || header[4]!=gate.op ||
           QuestNative::Field<int32_t>(header.data(),8)!=gate.param || QuestNative::Field<int32_t>(header.data(),12)!=gate.value ||
           gate.quest<=0 || gate.op<0 || gate.op>28)
            return refuse("Quest recipes unavailable: a loaded quest recipe differs from its source definition.");
        const bool eligible=progress.available && gate.quest==progress.completed+1 && gate.turnIn==progress.objectiveReady &&
            (!rules->events||(gate.turnIn&&rules->quests[gate.quest-1].milestone)||QuestJournal::RecipeObjective(*rules,gate));
        if(eligible){
            if(gate.op && gate.param==398)header[4]=0;
            if(rules->events&&!QuestJournal::RecipeObjective(*rules,gate)){
                if(gate.inputs!=4)return refuse("Quest upgrade recipe has unexpected ingredients.");
                // Keep the charm, Quill and chosen reward ingredient. Remove
                // only the obsolete proof slot in this invocation's copy.
                memmove(header.data()+0x1c,header.data()+0x24,5*8);
                memset(header.data()+0x44,0,8);header[0x10]=3;
            }
        }
        else {header[0]=0;header[0x10]=255;} // Debug-enabled recipes cannot bypass the journal.
    }
    bool skippedMap=false;
    for(auto& recipe:copies)if(UnrestrictedMapRecipe(recipe)){
        recipe.header[0]=0;recipe.header[0x10]=255;skippedMap=true;
    }
    if(!journal&&!skippedMap)return false;
    if(skippedMap)status.store("Skipped a map recipe with an empty compiled item requirement. Check the event's map item definition.");
    QuestNative::Invocation scanner;
    if(!scanner.Build(copies.data(),count))return refuse("Quest recipes unavailable: cannot prepare an isolated native matcher.");
    reinterpret_cast<RuneMatcher>(scanner.memory)(result,game,player,inputs);
    if(result->matched && (result->row<0 || uint64_t(result->row)>=count)){
        *result={};std::fill_n(inputs,3,Input{});return refuse("Quest recipe matcher returned an invalid row.");
    }
    return true;
}
