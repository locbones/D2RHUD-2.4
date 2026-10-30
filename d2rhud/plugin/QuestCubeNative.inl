// The native recipe scanner keeps every ordinary header/op/input predicate.
// Each invocation owns its recipe copies, code, count and pointer cells: no
// shared table pointer, recipe record, or live item stat is temporarily changed.
int __fastcall ConditionalIngredient(Game*,D2InventoryStrc*,Recipe*,int,Input*,int*);
bool __fastcall CandidateConditions(Game*,Unit*,Recipe*);
void __fastcall CaptureUseInput(int,Unit*,Recipe*);
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
std::array<uint8_t,0xb4c> originalIngredient{};
std::atomic<bool> ready{false};
template<class T>T Field(const void* address,size_t offset){T value{};memcpy(&value,static_cast<const uint8_t*>(address)+offset,sizeof(value));return value;}
struct Invocation {
    uint8_t* memory=nullptr;
    std::vector<RUNTIME_FUNCTION> unwind;
    bool registered=false;
    Invocation()=default;
    Invocation(const Invocation&)=delete;
    ~Invocation(){if(registered)RtlDeleteFunctionTable(unwind.data());if(memory)VirtualFree(memory,0,MEM_RELEASE);}
    bool Build(Recipe* recipes,uint64_t rows,bool conditional=false){
        // All original calls/globals and their rel32 operands remain in range.
        SYSTEM_INFO system{};GetSystemInfo(&system);const auto gran=uintptr_t(system.dwAllocationGranularity);
        const auto native=Pattern::Address(MatcherRva);
        const auto low=(std::max)(uintptr_t(system.lpMinimumApplicationAddress),native>0x20000000 ? native-0x20000000 : uintptr_t(0));
        const auto high=(std::min)(uintptr_t(system.lpMaximumApplicationAddress),native+0x20000000);
        for(uintptr_t at=(low+gran-1)&~(gran-1);at<high;){
            MEMORY_BASIC_INFORMATION region{};if(!VirtualQuery(reinterpret_cast<void*>(at),&region,sizeof(region)))break;
            const auto end=reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize;
            if(end<=at)break;
            if(region.State==MEM_FREE && at+0x4000<=end){
                memory=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(at),0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
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
            else if(conditional && fix.target==0x305d00)destination=reinterpret_cast<uintptr_t>(memory+0x3100);
            else destination=Pattern::Address(fix.target);
            const int64_t delta=int64_t(destination)-int64_t(reinterpret_cast<uintptr_t>(memory)+fix.instruction+fix.length);
            if(delta<INT32_MIN || delta>INT32_MAX)return false;
            const int32_t relative=int32_t(delta);memcpy(memory+fix.instruction+fix.displacement,&relative,4);
        }
        // Exact unwind description of the signature-checked prologue: eight
        // nonvolatile pushes followed by sub rsp,0x188. Register the clone so
        // Windows stack walks/unwinding retain the original caller correctly.
        constexpr uint8_t info[]{1,0x14,10,0,0x14,1,0x31,0,0xd,0xf0,0xb,0xe0,9,0xd0,7,0xc0,5,0x70,4,0x60,3,0x50,2,0x30};
        memcpy(memory+0x900,info,sizeof(info));unwind.push_back({0,0x7f3,0x900});
        if(conditional){
            namespace C=ConditionNativeData;
            memcpy(memory+0x2000,originalIngredient.data(),originalIngredient.size());
            for(const auto fix:C::Fixups){
                const auto target=fix.target?Pattern::Address(fix.target):reinterpret_cast<uintptr_t>(memory+0x2000)-0x305d00;
                const int64_t delta=int64_t(target)-int64_t(reinterpret_cast<uintptr_t>(memory+0x2000)+fix.instruction+fix.length);
                if(delta<INT32_MIN || delta>INT32_MAX)return false;
                const int32_t relative=int32_t(delta);memcpy(memory+0x2000+fix.instruction+fix.displacement,&relative,4);
            }
            for(const auto& block:C::UnwindBlocks)memcpy(memory+block.offset,block.bytes.data(),block.size);
            for(const auto r:C::RuntimeFunctions)unwind.push_back({r.begin,r.end,r.unwind});
            // Filter the exact native first-input candidate, after its ordinary
            // ingredient checks and before it is selected or any stat is read
            // for a single native item op. Preserve all live volatile GPRs.
            const uint8_t callback[]{0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53,
                0x48,0x83,0xec,0x28,0x4c,0x89,0xe9,0x48,0x89,0xea,0x4d,0x89,0xf8,0x48,0xb8};
            size_t at=0x3000;memcpy(memory+at,callback,sizeof(callback));at+=sizeof(callback);
            const auto function=reinterpret_cast<uintptr_t>(CandidateConditions);memcpy(memory+at,&function,8);at+=8;
            const uint8_t restore[]{0xff,0xd0,0x84,0xc0,0x48,0x8d,0x64,0x24,0x28,0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x5a,0x59,0x58};
            memcpy(memory+at,restore,sizeof(restore));at+=sizeof(restore);
            memory[at++]=0x0f;memory[at++]=0x85;int32_t delta=int32_t(0x2000+0x4f3-(at+4));memcpy(memory+at,&delta,4);at+=4;
            memory[at++]=0xe9;delta=int32_t(0x2000+0x976-(at+4));memcpy(memory+at,&delta,4);at+=4;
            memory[0x246e]=0xe9;delta=0x3000-(0x246e+5);memcpy(memory+0x246f,&delta,4);
            const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(memory+0x3100,jump,6);
            const auto dispatch=reinterpret_cast<uintptr_t>(ConditionalIngredient);memcpy(memory+0x3106,&dispatch,8);
            // The callback's 96-byte temporary frame chains into the native
            // ingredient matcher's full nonvolatile-save description.
            const uint8_t chain[]{0x21,15,1,0,15,0xb2,0,0};memcpy(memory+0x1280,chain,sizeof(chain));
            const auto parent=C::CallbackParent;memcpy(memory+0x1288,&parent,sizeof(parent));
            unwind.push_back({0x3000,uint32_t(at),0x1280});
            // Record the native ingredient slot only after every predicate has
            // accepted this exact item. The saved original input index remains
            // at +0xb8 in the native frame (+96 while our GPRs are saved).
            const uint8_t capture[]{0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53,
                0x48,0x83,0xec,0x28,0x8b,0x8c,0x24,0x18,0x01,0x00,0x00,
                0x48,0x89,0xea,0x4d,0x89,0xf8,0x48,0xb8};
            at=0x3200;memcpy(memory+at,capture,sizeof(capture));at+=sizeof(capture);
            const auto captureFn=reinterpret_cast<uintptr_t>(CaptureUseInput);memcpy(memory+at,&captureFn,8);at+=8;
            // call; restore the complete volatile frame (the result is void).
            memcpy(memory+at,restore,sizeof(restore));at+=sizeof(restore);
            const uint8_t compare[]{0x48,0x83,0xf9,0x30,0x0f,0x8d};memcpy(memory+at,compare,sizeof(compare));at+=sizeof(compare);
            delta=int32_t(0x2000+0xa4d-(at+4));memcpy(memory+at,&delta,4);at+=4;
            memory[at++]=0xe9;delta=int32_t(0x2000+0xa40-(at+4));memcpy(memory+at,&delta,4);at+=4;
            memory[0x2a3a]=0xe9;delta=0x3200-(0x2a3a+5);memcpy(memory+0x2a3b,&delta,4);
            unwind.push_back({0x3200,uint32_t(at),0x1280});
        }
        if(!RtlAddFunctionTable(unwind.data(),DWORD(unwind.size()),reinterpret_cast<DWORD64>(memory)))return false;
        registered=true;
        DWORD previous=0;if(!VirtualProtect(memory,conditional?0x4000:0x1000,PAGE_EXECUTE_READ,&previous))return false;
        FlushInstructionCache(GetCurrentProcess(),memory,conditional?0x4000:0x1000);return true;
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
    QuestNative::originalIngredient=lower;
    QuestNative::ready.store(true);return true;
}
void ShutdownQuestMatcher(){QuestNative::ready.store(false);}
// Evaluate the game's own op implementations against the real inventory.
// One private scanner is reused for the row's three comparisons. It cannot
// consume inputs, produce outputs, or alter shared recipes/player stats.
std::optional<bool> ConditionsPass(Game* game,Unit* player,const Recipe& recipe,const Conditions& conditions){
    if(!conditions.valid || !QuestNative::ready.load() || !QuestNative::SafeInventory(player))return {};
    Recipe probe=recipe;
    QuestNative::Invocation scanner;if(!scanner.Build(&probe,1))return {};
    bool active=false;
    for(size_t i=0;i<conditions.tests.size();++i){
        auto test=conditions.tests[i];if(!test.op)continue;active=true;
        probe.header[4]=uint8_t(test.op);memcpy(probe.header.data()+8,&test.param,4);memcpy(probe.header.data()+12,&test.value,4);
        RuneMatch result;Input inputs[3]{};reinterpret_cast<RuneMatcher>(scanner.memory)(&result,game,player,inputs);
        if(result.matched==conditions.any)return result.matched;
    }
    return !active || !conditions.any;
}
struct ConditionContext {
    Recipe* recipes=nullptr;size_t count=0;Unit* player=nullptr;uint8_t* ingredient=nullptr;
    std::map<size_t,Conditions> options;
    std::set<size_t> reuseRows,retiredProofRows;std::map<size_t,std::array<Unit*,7>> captured;
};
thread_local ConditionContext* conditionContext=nullptr;
struct ConditionScope {
    ConditionContext* previous;ConditionScope(ConditionContext& next):previous(conditionContext){conditionContext=&next;}
    ~ConditionScope(){conditionContext=previous;}
};
std::optional<size_t> ConditionalRow(const Recipe* recipe){
    const auto context=conditionContext;if(!context || !recipe)return {};
    const auto start=reinterpret_cast<uintptr_t>(context->recipes),pointer=reinterpret_cast<uintptr_t>(recipe);
    if(pointer<start || (pointer-start)%sizeof(Recipe) || (pointer-start)/sizeof(Recipe)>=context->count)return {};
    const size_t row=(pointer-start)/sizeof(Recipe);return (context->options.contains(row)||context->reuseRows.contains(row))?std::optional<size_t>(row):std::nullopt;
}
int __fastcall ConditionalIngredient(Game* game,D2InventoryStrc* inventory,Recipe* recipe,int index,Input* inputs,int* used){
    using Matcher=int(__fastcall*)(Game*,D2InventoryStrc*,Recipe*,int,Input*,int*);
    const auto row=ConditionalRow(recipe);
    const auto matcher=row?reinterpret_cast<Matcher>(conditionContext->ingredient):Native<Matcher>(0x305d00);
    return matcher(game,inventory,recipe,index,inputs,used);
}
bool __fastcall CandidateConditions(Game* game,Unit* candidate,Recipe* recipe){
    const auto row=ConditionalRow(recipe);if(!row)return false;
    if(!conditionContext->options.contains(*row))return true;
    Unit owner=*conditionContext->player;D2InventoryStrc inventory=*owner.pInventory;
    Unit item=*candidate;D2ItemDataStrc data=*candidate->pItemData;
    owner.pInventory=&inventory;inventory.pOwner=&owner;inventory.pFirstItem=&item;
    item.pItemData=&data;data.pExtraData.pParentInv=&inventory;data.pExtraData.pNextItem=nullptr;
    Recipe probe=*recipe;probe.header[0x10]=1;probe.header[0x1b]=1;
    memset(probe.header.data()+0x1c,0,6*8);
    return ConditionsPass(game,&owner,probe,conditionContext->options.at(*row)).value_or(false);
}
void __fastcall CaptureUseInput(int index,Unit* item,Recipe* recipe){
    const auto row=ConditionalRow(recipe);
    if(row && conditionContext->retiredProofRows.contains(*row) && index>=1)++index;
    if(row && index>=0 && index<7 && conditionContext->reuseRows.contains(*row))conditionContext->captured[*row][index]=item;
}
bool ExtendedRuneAllowed(Game* game,Unit* player,Recipe* table,size_t row,uint64_t count,Unit* bag,Unit* rune){
    const auto conditions=ConditionSources(table);const auto it=conditions.find(row);
    if(it==conditions.end())return true;
    if(row>=count || !it->second.Matches(table[row]))return false;
    // Mixed deposits are a HUD aggregate. Test each contributing native
    // two-input recipe against private views of the real bag/rune and their
    // real stats, without moving items or editing the player's inventory.
    Unit owner=*player;D2InventoryStrc inventory=*player->pInventory;
    std::array<Unit,2> items{*bag,*rune};std::array<D2ItemDataStrc,2> data{*bag->pItemData,*rune->pItemData};
    owner.pInventory=&inventory;inventory.pOwner=&owner;inventory.pFirstItem=&items[0];
    for(size_t i=0;i<2;++i){items[i].pItemData=&data[i];data[i].pExtraData.pParentInv=&inventory;data[i].pExtraData.pNextItem=i?nullptr:&items[1];}
    return ConditionsPass(game,&owner,table[row],it->second).value_or(false);
}
bool TryQuestMatch(RuneMatch* result,Game* game,Unit* player,Input* inputs){
    const auto rules=QuestJournal::Bindings();
    const bool journal=rules&&!rules->gates.empty();
    if(!result || !inputs)return false;
    *result={};std::fill_n(inputs,3,Input{});
    auto refuse=[&](const char* message){if(journal)QuestJournal::Refused(game,player,message);return journal||extendedConditions.load()||multipleUseOutputs.load()||multipleUseInputs.load();};
    if(!QuestNative::ready.load())return refuse("Quest recipes unavailable: native matcher signature mismatch.");
    if(!game || !QuestNative::SafeInventory(player))return refuse("Quest recipes unavailable: unsafe cube inventory.");
    Recipe* source=nullptr;uint64_t count=0;
    if(!Read(Pattern::Address(0x1ca0b18),source) || !source || !Read(Pattern::Address(0x1ca0b20),count) || !count || count>100000)
        return refuse("Quest recipes unavailable: loaded cube table cannot be read.");
    std::vector<Recipe> copies(size_t(count),Recipe{});SIZE_T got=0;
    if(!ReadProcessMemory(GetCurrentProcess(),source,copies.data(),copies.size()*sizeof(Recipe),&got) || got!=copies.size()*sizeof(Recipe))
        return refuse("Quest recipes unavailable: cube table cannot be read.");
    const auto conditions=ConditionSources(source);
    const auto useSources=UseSources(source);
    const auto originals=copies;
    const auto progress=QuestJournal::Inspect(game,player);std::vector<bool> seen(copies.size());std::set<size_t> retiredProofRows;
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
                retiredProofRows.insert(gate.row);
                memmove(header.data()+0x1c,header.data()+0x24,5*8);
                memset(header.data()+0x44,0,8);header[0x10]=3;
            }
        }
        else {header[0]=0;header[0x10]=255;} // Debug-enabled recipes cannot bypass the journal.
    }
    ConditionContext context;context.recipes=copies.data();context.count=copies.size();context.player=player;context.retiredProofRows=retiredProofRows;
    for(const auto& [row,options]:conditions){
        if(row>=copies.size())continue;
        auto& recipe=copies[row];if(!recipe.header[0] || recipe.header[0x10]==255)continue;
        if(!options.valid || !options.Matches(originals[row])){
            recipe.header[0]=0;recipe.header[0x10]=255;
            status.store("Extended cube conditions: skipped a recipe with invalid columns or mismatched source fields.");continue;
        }
        auto tests=options;
        // The journal has already satisfied this original proof comparison.
        if(tests.tests[0].op && !recipe.header[4]){
            if(tests.any)tests.tests={};else tests.tests[0]={};
        }
        if(!(recipe.header[0x14]&3)){
            // No first-input candidate exists for an item-op callback. Keep
            // native behavior for such rows while enforcing player/time ops.
            const auto passed=ConditionsPass(game,player,recipe,tests);
            if(!passed || !*passed){recipe.header[0]=0;recipe.header[0x10]=255;}
        }else context.options.emplace(row,tests);
        recipe.header[4]=0;
    }
    bool useRows=false;
    for(size_t row=0;row<copies.size();++row){
        auto& recipe=copies[row];const auto found=useSources.find(row);
        const auto plan=PlanUseItems(originals[row],found==useSources.end()?nullptr:&found->second);
        if(!plan.managed)continue;useRows=true;
        if(!plan.valid || (retiredProofRows.contains(row)&&std::find(plan.inputs.begin(),plan.inputs.end(),2)!=plan.inputs.end())){recipe.header[0]=0;recipe.header[0x10]=255;continue;}
        if(!recipe.header[0]||recipe.header[0x10]==255)continue;
        context.reuseRows.insert(row);
        if(!context.options.contains(row)){
            Conditions ordinary;ordinary.tests[0]={recipe.header[4],QuestNative::Field<int32_t>(recipe.header.data(),8),QuestNative::Field<int32_t>(recipe.header.data(),12)};
            context.options.emplace(row,ordinary);recipe.header[4]=0;
        }
        // Capture uses the actual compiled ingredient descriptors. Indexed
        // commands are ordinary native useitem during matching/execution.
        for(size_t out=0;out<3;++out)if(plan.inputs[out])recipe.outputs[out].special=0;
    }
    bool skippedMap=false;
    for(auto& recipe:copies)if(UnrestrictedMapRecipe(recipe)){
        recipe.header[0]=0;recipe.header[0x10]=255;skippedMap=true;
    }
    if(!journal&&!skippedMap&&conditions.empty()&&!useRows)return false;
    if(skippedMap)status.store("Skipped a map recipe with an empty compiled item requirement. Check the event's map item definition.");
    QuestNative::Invocation scanner;
    if(!scanner.Build(copies.data(),count,!context.options.empty()))return refuse("Quest recipes unavailable: cannot prepare an isolated native matcher.");
    context.ingredient=scanner.memory+0x2000;ConditionScope scope(context);
    reinterpret_cast<RuneMatcher>(scanner.memory)(result,game,player,inputs);
    if(result->matched && (result->row<0 || uint64_t(result->row)>=count)){
        *result={};std::fill_n(inputs,3,Input{});return refuse("Quest recipe matcher returned an invalid row.");
    }
    if(result->matched && context.reuseRows.contains(size_t(result->row))){
        const auto captured=context.captured.find(size_t(result->row));
        if(captured!=context.captured.end()){
            useMatch={game,player->dwUnitId,result->row,captured->second};
            for(size_t i=0;i<7;++i)if(useMatch.inputs[i])useMatch.ids[i]=useMatch.inputs[i]->dwUnitId;
        }
    }
    return true;
}
