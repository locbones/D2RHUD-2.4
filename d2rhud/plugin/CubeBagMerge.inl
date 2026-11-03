// RMD-MP storage balances only: never add quest/RNG state or arbitrary item
// modifiers when combining bags. Resolve symbols against the loaded TXT/BIN
// metadata, so stat IDs may move without changing this implementation.
constexpr std::array BagBalanceNames{
    "GB_Total","RB_Low","RB_Mid","RB_High","RB_Ultra",
    "JB_Magic","JB_Rare","PB_Rejuv","KB_Terror","KB_Hate","KB_Dest",
    "OB_Diablo","OB_Baal","OB_Meph","score_tracker","ucore_tracker"
};
struct BagBalance {int stat=0,old=0,total=0;};
struct BagMergeTransaction {
    Game* game;Unit* player;Unit* bag;void* saved;uint32_t id;
    std::vector<BagBalance> balances;bool committed=false;
    ~BagMergeTransaction(){
        if(!committed && Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0)(game,4,id)==bag && SavedList(bag)==saved)
            for(const auto& balance:balances)SetSavedStat(saved,balance.stat,balance.old);
    }
};
thread_local BagMergeTransaction* bagMergeTransaction=nullptr;
void BagMergeAdded(bool result,Unit* player,const uint32_t* ids,size_t count){
    // A native rejection can restore ALL ingredients through this same hook.
    // Only placement of the single retained output commits the new balances.
    if(result && ids && count==1 && bagMergeTransaction && bagMergeTransaction->player==player &&
       ids[0]==bagMergeTransaction->id)bagMergeTransaction->committed=true;
}
bool BagStoredValues(Unit* item,const std::vector<int>& stats,std::vector<int>& values){
    values.assign(stats.size(),0);auto list=SavedList(item);
    if(list){
        uintptr_t entries=0;uint64_t count=0;
        if(!Read(uintptr_t(list)+0x30,entries) || !Read(uintptr_t(list)+0x38,count) || count>65536 || (count&&!entries))return false;
        std::set<uint32_t> seen;
        for(size_t n=0;n<count;++n){
            struct Entry {uint32_t key;int value;} entry{};
            if(!Read(entries+n*sizeof(entry),entry) || !seen.insert(entry.key).second)return false;
            const auto found=std::find(stats.begin(),stats.end(),int(entry.key>>16));
            if(found==stats.end())continue;
            if((entry.key&0xffff) || entry.value<0)return false;
            values[found-stats.begin()]=entry.value;
        }
    }
    // Do not silently discard balances carried on an unexpected stat list.
    for(size_t n=0;n<stats.size();++n)if(GetStat(item,stats[n])!=values[n])return false;
    return true;
}
bool MergeStorageBags(Game* game,Unit* player,Recipe* recipe,Input* inputs){
    if(!Equal(sourceMod,"RMD-MP") || !game || !player || !recipe || !inputs || recipe->header[0x10]<2)return false;
    const auto items=RuneField<uint8_t*>(Native<void*>(0x1ca0390));const auto rows=RuneField<uint64_t>(Native<void*>(0x1ca0398));
    if(!items || !rows || rows>65536)return false;
    // An explicit bag-only recipe opts in. Leave other recipes that happen to
    // accept a bag (e.g. generic item recipes) with their authored behavior.
    size_t descriptors=0;
    for(size_t n=0;n<7;++n){
        const auto input=recipe->header.data()+0x14+n*8;const auto flags=RuneField<uint16_t>(input);
        if(!flags)continue;
        const auto base=RuneField<uint16_t>(input,2);
        if(!(flags&1) || base>=rows || RuneField<uint32_t>(items+size_t(base)*0x1b4,0x80)!=0x2031305au)return false;
        ++descriptors;
    }
    if(!descriptors)return false;
    auto refuse=[](const char* message){status.store(message);return true;};
    auto wide=RuntimeRecipe(*recipe);auto output=wide.outputs[0];if(IndexedUse(output))output.special=0;
    if(output.type!=254)return false;
    // lvl/plvl/ilvl are ordinary native useitem behavior, not additional
    // outputs or balance modifiers. Leave their interpretation to native useitem.
    Output plain;plain.type=254;plain.level=output.level;plain.playerLevel=output.playerLevel;plain.itemLevel=output.itemLevel;
    bool extra=false;for(size_t n=1;n<wide.outputs.size();++n)extra|=wide.outputs[n].type!=0;
    if(extra || memcmp(&output,&plain,sizeof(Output)))return refuse("Bag merge refused: use one useitem output without property, quantity or quality modifiers. Level fields are supported.");
    if(!QuestNative::SafeInventory(player))return refuse("Bag merge refused: unsafe cube inventory. Nothing was consumed.");
    std::vector<Unit*> bags;
    for(auto item=player->pInventory->pFirstItem;item;item=item->pItemData->pExtraData.pNextItem){
        if(item->pItemData->nPage!=3)continue;
        if(item->dwClassId>=rows || RuneField<uint32_t>(items+size_t(item->dwClassId)*0x1b4,0x80)!=0x2031305au ||
           item->dwAnimMode!=0 || item->pItemData->pExtraData.pParentInv!=player->pInventory ||
           (item->pInventory&&item->pInventory->pFirstItem) ||
           Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0)(game,4,item->dwUnitId)!=item)
            return refuse("Bag merge refused: cube ingredients changed. Nothing was consumed.");
        bags.push_back(item);
    }
    auto retained=inputs[0].item;
    if(bags.size()<2 || bags.size()!=recipe->header[0x10] || std::find(bags.begin(),bags.end(),retained)==bags.end())
        return refuse("Bag merge refused: the recipe must match all bags and retain one of them. Nothing was consumed.");
    const auto costs=RuneField<uint8_t*>(Native<void*>(0x1c9fb78));const auto count=RuneField<uint64_t>(Native<void*>(0x1c9fb80));
    if(!costs || !count || count>512)return refuse("Bag merge refused: storage stat definitions are unavailable. Nothing was consumed.");
    std::vector<int> stats;std::vector<int64_t> capacities,totals(BagBalanceNames.size());
    for(const auto name:BagBalanceNames){
        const int stat=RuneName(0x1c9fb90,name);
        if(stat<0 || uint64_t(stat)>=count || std::find(stats.begin(),stats.end(),stat)!=stats.end())
            return refuse("Bag merge refused: missing storage symbols. Load itemstatcost TXT or update D2RHUD/table-metadata.json. Nothing was consumed.");
        const auto cost=costs+size_t(stat)*0x144;const int bits=cost[0x15],send=cost[8];
        if(bits<1 || bits>32 || send<1 || send>32 || cost[0x14])return refuse("Bag merge refused: unsupported storage stat encoding. Nothing was consumed.");
        int64_t capacity=(std::min)((int64_t(1)<<bits)-1-RuneField<uint32_t>(cost,0x18),int64_t(INT_MAX));
        if(send<32)capacity=(std::min)(capacity,(int64_t(1)<<(send-1))-1);
        stats.push_back(stat);capacities.push_back(capacity);
    }
    std::vector<int> old,values;
    for(auto bag:bags){
        if(!BagStoredValues(bag,stats,values))return refuse("Bag merge refused: invalid saved storage balances. Nothing was consumed.");
        if(bag==retained)old=values;
        for(size_t n=0;n<stats.size();++n){
            totals[n]+=values[n];
            if(totals[n]>capacities[n])return refuse("Bag merge refused: combined contents exceed a storage counter's save capacity. Nothing was consumed.");
        }
    }
    auto saved=SavedList(retained,true);
    if(!saved)return refuse("Bag merge refused: saved bag modifiers are unavailable. Nothing was consumed.");
    BagMergeTransaction transaction{game,player,retained,saved,retained->dwUnitId};
    for(size_t n=0;n<stats.size();++n)if(totals[n]!=old[n])transaction.balances.push_back({stats[n],old[n],int(totals[n])});
    // Verify every persistent balance before native preflight/consumption.
    // Native useitem retains the original GUID and handles ingredient removal
    // and placement; its normal refusal path restores the retained balances.
    for(const auto& balance:transaction.balances){
        SetSavedStat(saved,balance.stat,balance.total);
        if(SavedStat(saved,balance.stat)!=balance.total)return refuse("Bag merge refused: could not save the combined balances. Nothing was consumed.");
    }
    struct Scope {BagMergeTransaction* old=bagMergeTransaction;Scope(BagMergeTransaction* next){bagMergeTransaction=next;}~Scope(){bagMergeTransaction=old;}} scope(&transaction);
    DispatchCreate(game,player,recipe,inputs);
    status.store(transaction.committed ? "Storage bags merged into one bag." : "Bag merge refused: native useitem did not complete.");return true;
}
