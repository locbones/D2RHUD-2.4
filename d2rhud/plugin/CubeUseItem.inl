// Scoped native useitem routing. Original items stay native useitem outputs;
// only repeated sources are copied. Copies are registered server items and are
// never inserted into the live ingredient inventory before the native commit.
std::array<uint8_t,UseNativeData::Size> useCreateSource{};
std::atomic<bool> useCreateReady{false},useCopyReady{false};
bool InitializeUseNative(){
    useCreateReady=Read(Pattern::Address(UseNativeData::Begin),useCreateSource) &&
        LegacyTransmog::Data::Hash(useCreateSource)==0xac545ecdc36232fcULL;
    bool copies=true;std::array<uint8_t,32> bytes{};
    for(const auto sig:UseNativeData::CopySignatures)if(!Read(Pattern::Address(sig.rva),bytes)||LegacyTransmog::Data::Hash(bytes)!=sig.hash)copies=false;
    useCopyReady=copies;return useCreateReady.load();
}
struct UseCopy {Unit* unit=nullptr;uint32_t id=0;Unit* source=nullptr;};
struct UseTransaction {
    Game* game;Unit* player;std::array<Input,3> inputs{};std::vector<UseCopy> copies;
    ~UseTransaction(){
        // A failed preflight leaves detached copies to reclaim. A successful
        // native placement owns its copy; a failed placement already destroyed it.
        for(const auto& copy:copies){
            auto item=Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0)(game,4,copy.id);
            if(item==copy.unit && item->pItemData && !item->pItemData->pExtraData.pParentInv)
                Native<void(__fastcall*)(Game*,Unit*)>(0x2f70a0)(game,item);
        }
    }
};
thread_local UseTransaction* useTransaction=nullptr;
bool ExtendedUseActive(){return useTransaction!=nullptr;}
struct UseScope {UseTransaction* previous;UseScope(UseTransaction& next):previous(useTransaction){useTransaction=&next;}~UseScope(){useTransaction=previous;}};
struct UseItemBits {uint8_t* data;uint64_t bits,bytes=0,bit=0;uint32_t error=0,padding=0;};
static_assert(sizeof(UseItemBits)==0x28);
Unit* CopyUseItem(Game* game,Unit* source){
    if(!useCopyReady.load())return nullptr;
    std::vector<uint8_t> storage(128*1024);UseItemBits bits{storage.data(),storage.size()*8};
    Native<int(__fastcall*)(Unit*,UseItemBits*,int,int,int)>(0x207610)(source,&bits,1,1,0);
    const auto length=bits.bytes+(bits.bit?1:0);if(bits.error||!length||length>storage.size())return nullptr;
    using ParseHeader=int(__fastcall*)(const void*,size_t,int,void*,int);
    using Load=Unit*(__fastcall*)(Game*,D2ActiveRoomStrc*,const void*,int,int,void*,int*,int);
    std::array<uint8_t,0x28> header{};
    if(!Native<ParseHeader>(0x204dc0)(storage.data(),size_t(length),1,header.data(),98))return nullptr;
    const int children=QuestNative::Field<int32_t>(header.data(),0x14);if(children<0||children>64)return nullptr;
    int used=0;auto copy=Native<Load>(0x2f5770)(game,nullptr,storage.data(),int(length),0,header.data(),&used,98);
    if(!copy)return nullptr;
    auto destroy=[&](){Native<void(__fastcall*)(Game*,Unit*)>(0x2f70a0)(game,copy);return static_cast<Unit*>(nullptr);};
    if(used<=0||size_t(used)>length||copy->dwUnitType!=4||copy->dwClassId!=source->dwClassId||copy->dwUnitId==source->dwUnitId||
       !copy->pItemData||copy->pItemData->pExtraData.pParentInv)return destroy();
    size_t offset=size_t(used);
    for(int n=0;n<children;++n){
        header={};if(offset>=length || !Native<ParseHeader>(0x204dc0)(storage.data()+offset,size_t(length-offset),1,header.data(),98))return destroy();
        used=0;auto child=Native<Load>(0x2f5770)(game,nullptr,storage.data()+offset,int(length-offset),0,header.data(),&used,98);
        if(!child)return destroy();
        const auto discardChild=[&](){Native<void(__fastcall*)(Game*,Unit*)>(0x2f70a0)(game,child);};
        if(used<=0||size_t(used)>length-offset||child->dwUnitType!=4||!child->pItemData||child->pItemData->pExtraData.pParentInv){discardChild();return destroy();}
        Native<void(__fastcall*)(Unit*,int)>(0x209c60)(child,4);
        if(!Native<int(__fastcall*)(Game*,Unit*,uint32_t,uint32_t)>(0x2d7480)(game,nullptr,child->dwUnitId,copy->dwUnitId)){
            if(!child->pItemData->pExtraData.pParentInv || child->pItemData->pExtraData.pParentInv!=copy->pInventory)discardChild();
            return destroy();
        }
        child->pItemData->dwItemFlags=(child->pItemData->dwItemFlags|0x80000u)&~0x2000u;offset+=size_t(used);
    }
    if(offset!=length)return destroy();
    copy->pItemData->dwItemFlags=(copy->pItemData->dwItemFlags|0x80000u)&~0x2000u;
    Native<void(__fastcall*)(Unit*,int)>(0x209c60)(copy,4);copy->pItemData->nPage=255;
    Native<void(__fastcall*)(Game*,Unit*)>(0x2f8660)(game,copy);Native<void(__fastcall*)(Game*,Unit*)>(0x2f8740)(game,copy);
    return copy;
}
uint64_t __fastcall AppendUseCopies(uint32_t* ids,Location* locations,uint64_t count){
    if(!useTransaction || count+useTransaction->copies.size()>256)return count;
    // Only the private invocation's keep/consume bookkeeping is extended. The
    // real player inventory and its input descriptors retain their original form.
    for(const auto& copy:useTransaction->copies){
        ids[count]=copy.id;Native<Location*(__fastcall*)(Location*,Unit*)>(0x213a20)(&locations[count],copy.source);++count;
    }
    return count;
}
struct UseCreateInvocation {
    uint8_t* memory=nullptr;std::array<RUNTIME_FUNCTION,2> unwind{};bool registered=false;
    ~UseCreateInvocation(){if(registered)RtlDeleteFunctionTable(unwind.data());if(memory)VirtualFree(memory,0,MEM_RELEASE);}
    bool Build(){
        if(!useCreateReady.load())return false;
        SYSTEM_INFO system{};GetSystemInfo(&system);const auto gran=uintptr_t(system.dwAllocationGranularity),native=Pattern::Address(UseNativeData::Begin);
        const auto low=(std::max)(uintptr_t(system.lpMinimumApplicationAddress),native>0x20000000?native-0x20000000:uintptr_t(0));
        const auto high=(std::min)(uintptr_t(system.lpMaximumApplicationAddress),native+0x20000000);
        for(uintptr_t at=(low+gran-1)&~(gran-1);at<high;){
            MEMORY_BASIC_INFORMATION region{};if(!VirtualQuery(reinterpret_cast<void*>(at),&region,sizeof(region)))break;
            const auto end=reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize;if(end<=at)break;
            if(region.State==MEM_FREE&&at+0x4000<=end){memory=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(at),0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(memory)break;}
            if(end>UINTPTR_MAX-gran)break;at=(end+gran-1)&~(gran-1);
        }
        if(!memory)return false;memcpy(memory,useCreateSource.data(),useCreateSource.size());
        for(const auto fix:UseNativeData::Fixups){
            const auto target=fix.target==0xffffffffu?reinterpret_cast<uintptr_t>(memory)-UseNativeData::Begin:
                fix.target>=UseNativeData::Begin&&fix.target<UseNativeData::Begin+UseNativeData::Size?reinterpret_cast<uintptr_t>(memory)+fix.target-UseNativeData::Begin:Pattern::Address(fix.target);
            const auto delta=int64_t(target)-int64_t(reinterpret_cast<uintptr_t>(memory)+fix.instruction+fix.length);
            if(delta<INT32_MIN||delta>INT32_MAX)return false;const int32_t relative=int32_t(delta);memcpy(memory+fix.instruction+fix.displacement,&relative,4);
        }
        memcpy(memory+0x2000,UseNativeData::Unwind.data(),UseNativeData::Unwind.size());
        const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(memory+0x3100,jump,6);auto handler=Pattern::Address(UseNativeData::Handler);memcpy(memory+0x3106,&handler,8);
        // This callback follows a normal native call, so no SIMD values are
        // live across it. Preserve all volatile GPRs and the aligned shadow space.
        const uint8_t prefix[]{0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53,0x48,0x83,0xec,0x28,
            0x48,0x8d,0x8d,0x10,0x02,0,0,0x48,0x8d,0x95,0x10,0x06,0,0,0x49,0x89,0xf8,0x48,0xb8};
        size_t at=0x3000;memcpy(memory+at,prefix,sizeof(prefix));at+=sizeof(prefix);auto callback=reinterpret_cast<uintptr_t>(AppendUseCopies);memcpy(memory+at,&callback,8);at+=8;
        const uint8_t suffix[]{0xff,0xd0,0x48,0x89,0xc7,0x48,0x89,0x45,0x98,0x48,0x8d,0x64,0x24,0x28,
            0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x5a,0x59,0x58};memcpy(memory+at,suffix,sizeof(suffix));at+=sizeof(suffix);
        memcpy(memory+at,useCreateSource.data()+UseNativeData::Append,5);at+=5;
        memory[at++]=0xe9;int32_t delta=int32_t(UseNativeData::Append+5-(at+4));memcpy(memory+at,&delta,4);at+=4;
        memory[UseNativeData::Append]=0xe9;delta=0x3000-(UseNativeData::Append+5);memcpy(memory+UseNativeData::Append+1,&delta,4);
        const uint8_t chain[]{0x21,15,1,0,15,0xb2,0,0};memcpy(memory+0x2080,chain,sizeof(chain));
        const RUNTIME_FUNCTION parent{0,UseNativeData::Size,0x2000};memcpy(memory+0x2088,&parent,sizeof(parent));
        unwind={parent,RUNTIME_FUNCTION{0x3000,uint32_t(at),0x2080}};
        if(!RtlAddFunctionTable(unwind.data(),DWORD(unwind.size()),reinterpret_cast<DWORD64>(memory)))return false;registered=true;
        DWORD old=0;if(!VirtualProtect(memory,0x4000,PAGE_EXECUTE_READ,&old))return false;FlushInstructionCache(GetCurrentProcess(),memory,0x4000);return true;
    }
};
void DispatchCreate(Game* game,Unit* player,Recipe* recipe,Input* inputs){
    if(!useTransaction || useTransaction->game!=game || useTransaction->player!=player){originalCreate(game,player,recipe,inputs);return;}
    Recipe clean=*recipe;for(auto& out:clean.outputs)if(IndexedUse(out))out.special=0;
    UseCreateInvocation invocation;if(!invocation.Build()){status.store("Cube useitem recipe refused: isolated native output processing is unavailable.");return;}
    reinterpret_cast<Create>(invocation.memory)(game,player,&clean,inputs);
}
std::optional<Input> ResolveUseInput(Game* game,Unit* player,const Recipe& recipe,size_t output,int input,Unit* source){
    // Reuse the native source-record calculation too (upgrade base and item
    // level), rather than treating every selected input as a plain base=-1.
    Unit owner=*player,item=*source;D2ItemDataStrc data=*source->pItemData;D2InventoryStrc inventory=*player->pInventory;
    owner.pInventory=&inventory;inventory.pOwner=&owner;inventory.pFirstItem=&item;item.pItemData=&data;
    data.pExtraData.pParentInv=&inventory;data.pExtraData.pNextItem=nullptr;
    Recipe probe=recipe;probe.header[4]=0;probe.header[0x10]=1;
    memcpy(probe.header.data()+0x14,recipe.header.data()+0x14+(input-1)*8,8);probe.header[0x1b]=1;
    memset(probe.header.data()+0x1c,0,6*8);probe.outputs={};probe.outputs[0]=recipe.outputs[output];
    if(IndexedUse(probe.outputs[0]))probe.outputs[0].special=0;
    QuestNative::Invocation scanner;if(!scanner.Build(&probe,1))return {};
    RuneMatch result;Input selected[3]{};reinterpret_cast<RuneMatcher>(scanner.memory)(&result,game,&owner,selected);
    if(!result.matched||selected[0].item!=&item)return {};
    selected[0].item=source;return selected[0];
}
void __fastcall CreateHook(Game* game,Unit* player,Recipe* recipe,Input* inputs){
    if(!installed.load()||!recipe||!inputs){originalCreate(game,player,recipe,inputs);return;}
    const auto sources=UseSources(recipe);const auto table=RuneField<Recipe*>(Native<void*>(0x1ca0b18));const auto rows=RuneField<uint64_t>(Native<void*>(0x1ca0b20));
    const auto start=reinterpret_cast<uintptr_t>(table),at=reinterpret_cast<uintptr_t>(recipe);
    const auto row=table&&at>=start&&(at-start)%sizeof(Recipe)==0&&(at-start)/sizeof(Recipe)<rows?size_t((at-start)/sizeof(Recipe)):SIZE_MAX;
    const auto source=sources.find(row);const auto plan=PlanUseItems(*recipe,source==sources.end()?nullptr:&source->second);
    if(!plan.managed){CreateBody(game,player,recipe,inputs);return;}
    if(!game||!player||!plan.valid||!useCreateReady.load()||!QuestNative::SafeInventory(player)||useMatch.game!=game||useMatch.player!=player->dwUnitId||useMatch.row!=row){
        status.store("Cube useitem recipe refused: enable the required useitem options and match valid native input slots.");return;
    }
    UseTransaction transaction{game,player};std::copy_n(inputs,3,transaction.inputs.begin());std::set<uint32_t> used;
    for(size_t i=0;i<3;++i){
        Unit* item=plan.inputs[i]?useMatch.inputs[plan.inputs[i]-1]:inputs[i].item;
        const bool preserving=plan.inputs[i] || (recipe->outputs[i].flags&1);
        if(!preserving)continue;
        const auto id=plan.inputs[i]?useMatch.ids[plan.inputs[i]-1]:(item?item->dwUnitId:0);
        auto real=Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0)(game,4,id);
        if(!item||real!=item||!item->pItemData||item->pItemData->pExtraData.pParentInv!=player->pInventory||item->pItemData->nPage!=3){status.store("Cube useitem recipe refused: a selected ingredient changed before commit.");return;}
        Input selected=inputs[i];
        if(plan.inputs[i] && (plan.inputs[i]!=1||selected.item!=item)){
            const auto resolved=ResolveUseInput(game,player,*recipe,i,plan.inputs[i],item);
            if(!resolved){status.store("Cube useitem recipe refused: its native source metadata could not be resolved.");return;}selected=*resolved;
        }
        if(!used.insert(id).second){
            auto copy=CopyUseItem(game,item);if(!copy){status.store("Cube useitem recipe refused: an independent saved-item copy could not be prepared. No inputs were consumed.");return;}
            transaction.copies.push_back({copy,copy->dwUnitId,item});item=copy;
        }
        selected.item=item;transaction.inputs[i]=selected;
    }
    UseScope scope(transaction);CreateBody(game,player,recipe,transaction.inputs.data());
}
