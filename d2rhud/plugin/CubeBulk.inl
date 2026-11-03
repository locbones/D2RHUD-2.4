// Generate complete native outputs without consuming or publishing ingredients.
// Reuse those prepared items at commit; repeat only non-stackable quantities.
struct BulkItem {Unit* unit;uint32_t id;};
struct BulkTransaction {
    Game* game;Unit* player;std::vector<BulkItem> owned;
    std::vector<Unit*> placedItems;std::vector<Location> positions;
    std::vector<uint32_t> extras;std::vector<Location> extraPositions;
    std::array<uint8_t,512> uniqueFlags{};std::set<uint32_t> added;bool committed=false,failed=false;
    BulkTransaction(Game* g,Unit* p):game(g),player(p){memcpy(uniqueFlags.data(),reinterpret_cast<uint8_t*>(game)+0x3640,uniqueFlags.size());}
    void Own(Unit* item){if(item&&std::none_of(owned.begin(),owned.end(),[&](const auto& old){return old.id==item->dwUnitId;}))owned.push_back({item,item->dwUnitId});}
    ~BulkTransaction(){
        for(const auto& old:owned){auto item=Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0)(game,4,old.id);
            if(item==old.unit&&item->pItemData&&!item->pItemData->pExtraData.pParentInv)Native<void(__fastcall*)(Game*,Unit*)>(0x2f70a0)(game,item);}
        if(!committed&&added.empty())memcpy(reinterpret_cast<uint8_t*>(game)+0x3640,uniqueFlags.data(),uniqueFlags.size());
    }
};
thread_local BulkTransaction* bulkTransaction=nullptr;
bool BulkDefinition(Unit* item,std::array<uint8_t,0x1b4>& out){
    const auto table=RuneField<const uint8_t*>(Native<void*>(0x1ca0390));const auto count=RuneField<uint64_t>(Native<void*>(0x1ca0398));
    return item&&item->dwUnitType==4&&item->pItemData&&table&&count<=100000&&item->dwClassId<count&&Read(reinterpret_cast<uintptr_t>(table)+size_t(item->dwClassId)*out.size(),out);
}
bool BulkCubeSize(Unit* player,int& width,int& height){
    uintptr_t grids=0;uint64_t count=0;
    const auto inventory=reinterpret_cast<uintptr_t>(player->pInventory);
    std::array<uint8_t,2> dimensions{};
    if(!Read(inventory+0x20,grids)||!grids||!Read(inventory+0x28,count)||count<6||count>64||
       !Read(grids+5*0x20+0x10,dimensions))return false;
    width=dimensions[0];height=dimensions[1];return width>0&&height>0&&width<=32&&height<=32;
}
bool BulkStage(BulkTransaction& bulk,const WideRecipe& recipe,const Input* inputs,std::array<Unit*,7>& outputs){
    WideRecipe stage=recipe;UseTransaction copies{bulk.game,bulk.player};std::copy_n(inputs,7,copies.inputs.begin());
    for(size_t i=0;i<7;++i){auto& out=stage.outputs[i];
        if(out.type<252){out=Output{};continue;}
        if(out.type==254||(out.flags&1)){
            auto source=inputs[i].item;if(!source)return false;
            auto copy=CopyUseItem(bulk.game,source);if(!copy)return false;
            bulk.Own(copy);copies.inputs[i].item=copy;copies.copies.push_back({copy,copy->dwUnitId,source});
        }
    }
    UseScope scope(copies);UseCreateInvocation invocation;
    if(!invocation.Build(true,true))return false;
    struct Capture {std::array<Unit*,7>* old=bulkStageOutputs;Capture(std::array<Unit*,7>& value){bulkStageOutputs=&value;}~Capture(){bulkStageOutputs=old;}} capture(outputs);
    struct SevenScope {bool old=nativeSevenActive;SevenScope(){nativeSevenActive=true;}~SevenScope(){nativeSevenActive=old;}} seven;
    reinterpret_cast<Create>(invocation.memory)(bulk.game,bulk.player,reinterpret_cast<Recipe*>(&stage),copies.inputs.data());
    for(auto item:outputs)bulk.Own(item);
    // Ownership now belongs to BulkTransaction, including unused source copies.
    copies.copies.clear();
    for(size_t i=0;i<7;++i)if(stage.outputs[i].type&&!outputs[i])return false;
    return true;
}
int __fastcall BulkPlacement(void* inventory,Unit* item,int record,int* x,int* y,uint8_t page){
    if(bulkTransaction){for(size_t i=0;i<bulkTransaction->placedItems.size();++i)if(bulkTransaction->placedItems[i]==item){
        *x=bulkTransaction->positions[i].x;*y=bulkTransaction->positions[i].y;return 1;}return 0;}
    return Native<int(__fastcall*)(void*,Unit*,int,int*,int*,uint8_t)>(0x210200)(inventory,item,record,x,y,page);
}
void BulkAdded(bool success,Unit* player,const uint32_t* ids,size_t count,uintptr_t caller){
    auto bulk=bulkTransaction;if(!bulk||bulk->player!=player)return;
    if(caller!=Pattern::Address(0x30812a)&&caller!=Pattern::Address(0x30850e))return;
    if(!success||!ids||count!=1){bulk->failed=true;return;}
    if(!bulk->added.insert(*ids).second)bulk->failed=true;
}
bool CommitBulkExtras(BulkTransaction& bulk){
    const bool result=bulk.extras.empty()||originalAdd(bulk.player,bulk.extraPositions.data(),bulk.extras.data(),bulk.extras.size(),false);
    bulk.committed=result;
    if(result)for(size_t i=0;i<bulk.extras.size();++i){const auto id=bulk.extras[i];
        auto item=Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0)(bulk.game,4,id);const auto& p=bulk.extraPositions[i];
        if(item&&item->pItemData){
            // Match the native primary-output path: identify after placement,
            // before the full item description is sent to the client.
            item->pItemData->dwItemFlags|=0x10u;
            originalNotify(bulk.game,bulk.player,item,1,2,p.mode,p.page,p.body,uint32_t(p.x)|(uint32_t(p.y)<<16));
        }
        if(questOutput&&questOutput->player==bulk.player)questOutput->placed.insert(id);
    }
    if(!result)status.store("Bulk cube outputs: native placement failed while adding prepared copies.");
    return result;
}
bool TryBulkCreate(Game* game,Unit* player,Recipe* recipe,Input* inputs){
    if(!bulkOutputs.load()||bulkStageOutputs||bulkTransaction||!recipe||!inputs)return false;
    const auto expanded=RuntimeRecipe(*recipe);std::array<Input,7> selected{};
    if(useTransaction)selected=useTransaction->inputs;else std::copy_n(inputs,3,selected.begin());
    // Resolve the real generated definition first: set/unique selectors and
    // usetype/upgrades need not have the same base as the compiled selector.
    const bool requested=std::any_of(expanded.outputs.begin(),expanded.outputs.end(),[](const Output& out){
        return out.quantity>=2&&out.type>=252&&!(out.flags&2);
    });
    if(!requested)return false;
    if(!game||!QuestNative::SafeInventory(player)||!useCreateReady.load()||!useCopyReady.load()){
        status.store("Bulk cube outputs refused: native item preparation or cube inventory is unavailable.");return true;}
    // Native unsocket outputs create additional items outside its three
    // primary slots. Refuse that combination until their footprints can also
    // participate in the all-output space check.
    if(std::any_of(expanded.outputs.begin(),expanded.outputs.end(),[](const Output& out){return (out.flags&0x20)!=0;})){
        status.store("Bulk cube outputs refused: socket-content removal cannot be combined with bulk quantity. No ingredients were consumed.");return true;
    }
    size_t cubeItems=0;for(auto item=player->pInventory->pFirstItem;item;item=item->pItemData->pExtraData.pNextItem)if(item->pItemData->nPage==3)++cubeItems;
    if(cubeItems+7+(useTransaction?useTransaction->copies.size():0)>256){status.store("Bulk cube outputs refused: native cube item bookkeeping is full. No ingredients were consumed.");return true;}
    int width=0,height=0;if(!BulkCubeSize(player,width,height)){status.store("Bulk cube outputs refused: cube dimensions could not be read.");return true;}
    BulkTransaction bulk(game,player);std::array<Unit*,7> first{};
    if(!BulkStage(bulk,expanded,selected.data(),first)){status.store("Bulk cube outputs refused: an output could not be prepared. No ingredients were consumed.");return true;}
    std::array<unsigned,7> quantities{};size_t total=0;
    for(size_t i=0;i<7;++i)if(first[i]){std::array<uint8_t,0x1b4> definition{};
        if(!BulkDefinition(first[i],definition)){status.store("Bulk cube outputs refused: invalid output item definition.");return true;}
        quantities[i]=!definition[0x13a]&&!(expanded.outputs[i].flags&2)?(std::max)(1u,unsigned(expanded.outputs[i].quantity)):1;
        total+=quantities[i];
    }
    if(total>256||total>size_t(width)*height){status.store("Bulk cube outputs refused: the complete output batch will not fit in the cube. No ingredients were consumed.");return true;}
    WideRecipe commit=expanded;UseTransaction delivery{game,player};delivery.inputs=selected;
    if(useTransaction)delivery.copies=useTransaction->copies;
    std::vector<BulkData::Rect> rectangles;std::vector<Unit*> all;std::set<uint32_t> reused;
    auto append=[&](Unit* prepared,Unit* placed){std::array<uint8_t,0x1b4> definition{};if(!BulkDefinition(prepared,definition))return false;
        rectangles.push_back({definition[0x116],definition[0x117]});all.push_back(placed);return true;};
    for(size_t i=0;i<7;++i)if(first[i]){
        auto& output=commit.outputs[i];const bool preserving=output.type==254||(output.flags&1);
        Unit* primary=preserving?selected[i].item:first[i];
        if(preserving&&(!primary||!reused.insert(primary->dwUnitId).second)){status.store("Bulk cube outputs refused: enable Useitem Multiple Outputs for repeated source items.");return true;}
        if(!append(first[i],primary))return true;
        bulk.placedItems.push_back(primary);
        if(!preserving){output=Output{};output.type=254;delivery.inputs[i]={primary,-1,int(primary->pItemData->dwItemLevel)};
            Unit* origin=nullptr;size_t scanned=0;
            for(auto item=player->pInventory->pFirstItem;item&&scanned++<4096;item=item->pItemData->pExtraData.pNextItem){
                if(!item->pItemData)break;if(item->pItemData->nPage==3){origin=item;break;}
            }
            if(!origin)return true;delivery.copies.push_back({primary,primary->dwUnitId,origin});
        }else if(quantities[i]>1)output.quantity=0;
        for(unsigned n=1;n<quantities[i];++n){WideRecipe repeated=expanded;repeated.outputs={};repeated.outputs[i]=expanded.outputs[i];repeated.outputs[i].quantity=1;
            std::array<Unit*,7> next{};if(!BulkStage(bulk,repeated,selected.data(),next)||!next[i]||!append(next[i],next[i])){status.store("Bulk cube outputs refused: an additional copy could not be prepared. No ingredients were consumed.");return true;}
            bulk.extras.push_back(next[i]->dwUnitId);
        }
    }
    if(!BulkData::Pack(width,height,rectangles)){status.store("Bulk cube outputs refused: the complete output batch will not fit in the cube. No ingredients were consumed.");return true;}
    for(size_t i=0;i<all.size();++i){Location p{0,3,uint16_t(rectangles[i].x),uint16_t(rectangles[i].y),0};
        const auto primary=std::find(bulk.placedItems.begin(),bulk.placedItems.end(),all[i]);
        if(primary!=bulk.placedItems.end()){const size_t slot=size_t(primary-bulk.placedItems.begin());if(bulk.positions.size()<=slot)bulk.positions.resize(slot+1);bulk.positions[slot]=p;}
        else bulk.extraPositions.push_back(p);
    }
    if(questOutput&&questOutput->player==player)questOutput->expected=total;
    struct Scope {BulkTransaction* old=bulkTransaction;bool placement=bulkPlacementActive;Scope(BulkTransaction& next){bulkTransaction=&next;bulkPlacementActive=true;}~Scope(){bulkTransaction=old;bulkPlacementActive=placement;}} scope(bulk);
    struct WideScope {WideRecipe* old=activeWideRecipe;WideScope(WideRecipe& next){activeWideRecipe=&next;}~WideScope(){activeWideRecipe=old;}} wide(commit);
    auto native=commit.Native();UseScope use(delivery);DispatchNativeCreate(game,player,&native,delivery.inputs.data());
    const bool placed=!bulk.failed&&bulk.added.size()==bulk.placedItems.size()&&std::all_of(bulk.placedItems.begin(),bulk.placedItems.end(),[&](Unit* item){
        auto live=Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0)(game,4,item->dwUnitId);
        return live==item&&item->pItemData&&item->pItemData->pExtraData.pParentInv==player->pInventory&&item->pItemData->nPage==3;
    });
    if(placed)CommitBulkExtras(bulk);
    else status.store("Bulk cube outputs were not committed; native preflight or placement rejected the recipe.");
    return true;
}
