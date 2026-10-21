// Z01 mixed-rune deposits. All definitions come from the currently loaded
// native tables; no mod table, recipe record or player item code is rewritten.
struct RuneMatch {int row=-1;bool matched=false;uint8_t padding[3]{};};
static_assert(sizeof(RuneMatch)==8);
using RuneMatcher=RuneMatch*(__fastcall*)(RuneMatch*,Game*,Unit*,Input*);
RuneMatcher originalRuneMatch=nullptr;
template<class T>T RuneField(const void* p,size_t offset=0){T result{};Read(reinterpret_cast<uintptr_t>(p)+offset,result);return result;}
struct RunePlan {
    bool applicable=false,valid=false;
    Unit* bag=nullptr;Recipe* anchor=nullptr;int row=-1;
    std::array<int,4> stats{},properties{},values{};
    size_t runes=0;std::vector<uint32_t> codes;
};
struct RuneTransaction {
    Game* game;Unit* player;Unit* bag;uint32_t id;void* saved;
    std::array<int,4> stats,old;
    bool committed=false;
    ~RuneTransaction(){
        // Native output failure can destroy an item. Never write a freed or
        // replaced bag/list while unwinding an unsuccessful native operation.
        if(!committed && Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0)(game,4,id)==bag && SavedList(bag)==saved)
            for(size_t i=0;i<4;++i)SetSavedStat(saved,stats[i],old[i]);
    }
};
thread_local RuneTransaction* runeTransaction=nullptr;
void RuneAdded(bool result,Unit* player,const uint32_t* ids,size_t count){
    if(result && ids && count && runeTransaction && runeTransaction->player==player &&
       std::find(ids,ids+count,runeTransaction->id)!=ids+count)runeTransaction->committed=true;
}
int RuneName(uint32_t address,const char* name){
    return CompiledNames::Resolve(address,name);
}
bool RuneType(int type,const uint8_t* types,size_t count,std::vector<bool>& visited){
    std::vector<int> pending{type};
    while(!pending.empty()){
        const int current=pending.back();pending.pop_back();
        if(current<=0 || size_t(current)>=count || visited[current])continue;visited[current]=true;
        const auto row=types+size_t(current)*0xe4;
        if(RuneField<uint32_t>(row)==0x656e7572u)return true;
        pending.push_back(RuneField<int16_t>(row,4));pending.push_back(RuneField<int16_t>(row,6));
    }
    return false;
}
bool RuneDefinition(const uint8_t* item,const uint8_t* types,size_t count){
    std::vector<bool> visited(count);
    return RuneType(RuneField<int16_t>(item,0x126),types,count,visited) || RuneType(RuneField<int16_t>(item,0x128),types,count,visited);
}
RunePlan PlanRunes(Game* game,Unit* player){
    RunePlan plan;
    if(!game || !player || player->dwUnitType!=0 || !player->pInventory)return plan;
    const auto inventory=player->pInventory;
    if(RuneField<uint32_t>(inventory)!=0x1020304 || inventory->pOwner!=player)return plan;
    const auto table=RuneField<uint8_t*>(Native<void*>(0x1ca0390));
    const auto rows=RuneField<uint64_t>(Native<void*>(0x1ca0398));
    const auto types=RuneField<uint8_t*>(Native<void*>(0x1c9fbf0));
    const auto typeCount=RuneField<uint64_t>(Native<void*>(0x1c9fbf8));
    if(!table || !rows || rows>65536 || !types || !typeCount || typeCount>8192)return plan;
    std::vector<Unit*> runes,seen;bool unsafe=false;
    for(auto item=inventory->pFirstItem;item;){
        if(seen.size()>=4096 || std::find(seen.begin(),seen.end(),item)!=seen.end())return {};
        seen.push_back(item);
        if(item->dwUnitType!=4 || !item->pItemData)return {};
        auto data=item->pItemData;auto next=data->pExtraData.pNextItem;
        if(data->nPage==3){
            if(item->dwClassId>=rows)return {};
            const auto record=table+size_t(item->dwClassId)*0x1b4;
            if(RuneField<uint32_t>(record,0x80)==0x2031305au){if(plan.bag)return {};plan.bag=item;}
            else if(RuneDefinition(record,types,size_t(typeCount)))runes.push_back(item);
            else return {}; // One bag and ONLY runes, never other cube recipes.
            unsafe|=item->dwAnimMode!=0 || data->pExtraData.pParentInv!=inventory;
        }
        item=next;
    }
    if(!plan.bag || runes.empty())return plan;
    plan.applicable=true;
    // Native output preparation has fixed arrays for 256 cube items.
    if(unsafe || runes.size()>=256)return plan;
    const auto costs=RuneField<uint8_t*>(Native<void*>(0x1c9fb78));
    const auto costCount=RuneField<uint64_t>(Native<void*>(0x1c9fb80));
    const auto properties=RuneField<uint8_t*>(Native<void*>(0x1c9ebc0));
    const auto propertyCount=RuneField<uint64_t>(Native<void*>(0x1c9ebc8));
    if(!costs || !costCount || costCount>512 || !properties || propertyCount>65536)return plan;
    const auto recipeTable=RuneField<Recipe*>(Native<void*>(0x1ca0b18));
    const auto recipeCount=RuneField<uint64_t>(Native<void*>(0x1ca0b20));
    if(!recipeTable || !recipeCount || recipeCount>100000)return plan;
    std::vector<Recipe> recipes(size_t(recipeCount),Recipe{});
    SIZE_T read=0;if(!ReadProcessMemory(GetCurrentProcess(),recipeTable,recipes.data(),recipes.size()*sizeof(Recipe),&read) || read!=recipes.size()*sizeof(Recipe))return plan;
    // BIN records retain property -> saved-stat IDs, but their TXT name
    // linkers are empty without -txt. Discover the four bag balances from
    // eligible single-rune deposits instead of relying on those linkers or
    // hardcoding IDs that can move when the mod's tables change.
    struct Balance {int property;int64_t capacity;};
    std::map<int,Balance> balances;
    std::vector<size_t> deposits;
    for(size_t r=0;r<recipes.size();++r){
        const auto& recipe=recipes[r];const auto& h=recipe.header;const auto& out=recipe.outputs[0];
        if(!h[0] || h[1] || h[2]>game->nDifficulty || h[3]!=255 || h[0x10]!=2 || (h[4]!=0 && h[4]!=16) ||
           (RuneField<uint16_t>(h.data(),0x12)>=100 && !game->bExpansion))continue;
        bool extra=false;for(size_t n=2;n<7;++n)extra|=RuneField<uint16_t>(h.data(),0x14+n*8)!=0;
        if(extra || RuneField<uint16_t>(h.data(),0x14)!=1 || RuneField<uint16_t>(h.data(),0x16)!=plan.bag->dwClassId || h[0x1a] || h[0x1b]>1 ||
           RuneField<uint16_t>(h.data(),0x1c)!=1 || h[0x22] || h[0x23]>1 || out.type!=254 || out.flags || out.base || out.special || out.quality || out.quantity || out.level || out.playerLevel || out.itemLevel ||
           recipe.outputs[1].type || recipe.outputs[2].type)continue;
        for(auto prefix:out.prefixes)extra|=prefix!=0;for(auto suffix:out.suffixes)extra|=suffix!=0;if(extra)continue;
        const auto rune=RuneField<uint16_t>(h.data(),0x1e);if(rune>=rows || !RuneDefinition(table+size_t(rune)*0x1b4,types,size_t(typeCount)))continue;
        std::map<int,Balance> rowBalances;bool valid=true;
        for(const auto& mod:out.mods)if(mod.property>=0){
            if(uint64_t(mod.property)>=propertyCount || mod.param || mod.chance || mod.min<=0 || mod.min!=mod.max){valid=false;break;}
            const auto prop=properties+size_t(mod.property)*0x2e;
            const int stat=RuneField<uint16_t>(prop,0x20);
            if(prop[0x18]!=17 || uint64_t(stat)>=costCount){valid=false;break;}
            for(size_t n=1;n<7;++n)if(prop[0x18+n])valid=false;
            const auto cost=costs+size_t(stat)*0x144;const auto bits=cost[0x15];
            if(!valid || !bits || bits>32 || cost[0x14] || !cost[8] || cost[8]>32){valid=false;break;}
            auto capacity=(std::min)((int64_t(1)<<bits)-1-RuneField<uint32_t>(cost,0x18),int64_t(INT_MAX));
            if(cost[8]<32)capacity=(std::min)(capacity,(int64_t(1)<<(cost[8]-1))-1);
            const auto [found,added]=rowBalances.try_emplace(stat,Balance{mod.property,capacity});
            if(!added && found->second.property!=mod.property){valid=false;break;}
        }
        if(!valid || rowBalances.empty())continue;
        // A conditional deposit must cap a balance that this row grants.
        // Ignore quest/other-stat conditions rather than adopting their stats.
        if(h[4]==16){
            const int stat=RuneField<int>(h.data(),8),limit=RuneField<int>(h.data(),12);
            const auto slot=rowBalances.find(stat);
            if(slot==rowBalances.end() || limit<=0)continue;
            slot->second.capacity=(std::min)(slot->second.capacity,int64_t(limit));
        }
        for(const auto& [stat,balance]:rowBalances){
            const auto [found,added]=balances.try_emplace(stat,balance);
            if(!added){
                if(found->second.property!=balance.property)return plan;
                found->second.capacity=(std::min)(found->second.capacity,balance.capacity);
            }
        }
        if(balances.size()>4)return plan; // Ambiguous bag format: never guess.
        deposits.push_back(r);
    }
    if(balances.size()!=4)return plan;
    std::array<int64_t,4> capacity{};size_t slot=0;
    for(const auto& [stat,balance]:balances){
        plan.stats[slot]=stat;plan.properties[slot]=balance.property;capacity[slot]=balance.capacity;++slot;
    }
    struct Value {std::array<int64_t,4> points{};size_t row=0;bool ambiguous=false;};
    std::map<uint16_t,Value> values;
    for(const auto r:deposits){
        const auto& recipe=recipes[r];const auto rune=RuneField<uint16_t>(recipe.header.data(),0x1e);
        Value value;value.row=r;
        for(const auto& mod:recipe.outputs[0].mods)if(mod.property>=0){
            const auto property=std::find(plan.properties.begin(),plan.properties.end(),mod.property);
            if(property==plan.properties.end())return plan;
            value.points[property-plan.properties.begin()]+=mod.min;
        }
        auto [found,added]=values.try_emplace(rune,value);
        if(!added && found->second.points!=value.points)found->second.ambiguous=true;
    }
    std::array<int64_t,4> totals{};
    for(auto rune:runes){
        const auto found=values.find(uint16_t(rune->dwClassId));if(found==values.end() || found->second.ambiguous)return plan;
        const auto record=table+size_t(rune->dwClassId)*0x1b4;
        const int quantity=record[0x13a] ? GetStat(rune,70) : 1;
        if(quantity<=0 || quantity>1000000)return plan;
        plan.runes+=quantity;plan.codes.push_back(RuneField<uint32_t>(record,0x80));
        for(size_t i=0;i<4;++i){totals[i]+=found->second.points[i]*quantity;if(totals[i]>INT_MAX)return plan;}
        if(plan.row<0)plan.row=int(found->second.row);
    }
    for(size_t i=0;i<4;++i){
        const int old=GetStat(plan.bag,plan.stats[i]);
        if(old<0 || int64_t(old)+totals[i]>capacity[i])return plan;
        plan.values[i]=int(totals[i]);
    }
    plan.anchor=recipeTable+plan.row;plan.valid=true;return plan;
}
RuneMatch* __fastcall RuneMatchHook(RuneMatch* result,Game* game,Unit* player,Input* inputs){
    if(!installed.load() || !result || !inputs)return originalRuneMatch(result,game,player,inputs);
    RuneMatch managed;std::array<Input,3> managedInputs{};
    const bool journal=TryQuestMatch(&managed,game,player,managedInputs.data());
    if(journal&&managed.matched){
        const auto rules=QuestJournal::Bindings();
        if(rules&&std::any_of(rules->gates.begin(),rules->gates.end(),[&](const auto& gate){return gate.row==size_t(managed.row);})){
            *result=managed;std::copy(managedInputs.begin(),managedInputs.end(),inputs);return result;
        }
    }
    const auto plan=PlanRunes(game,player);
    if(!plan.applicable){if(journal){*result=managed;std::copy(managedInputs.begin(),managedInputs.end(),inputs);return result;}return originalRuneMatch(result,game,player,inputs);}
    *result={};
    if(!plan.valid){status.store("Rune deposit refused: missing/ambiguous rune values, unsafe inputs or bag capacity exceeded. Nothing was consumed.");return result;}
    std::fill_n(inputs,3,Input{});inputs[0]={plan.bag,int(plan.bag->dwClassId),int(plan.bag->pItemData->dwItemLevel)};
    result->row=plan.row;result->matched=true;return result;
}
bool DepositRunes(Game* game,Unit* player,Recipe* recipe,Input* inputs){
    const auto plan=PlanRunes(game,player);
    if(!plan.applicable)return false;
    if(!plan.valid || plan.anchor!=recipe || inputs[0].item!=plan.bag){status.store("Rune deposit refused before consumption: cube inputs or deposit definition changed.");return true;}
    auto saved=SavedList(plan.bag,true);
    if(!saved){status.store("Rune deposit refused: saved bag modifiers are unavailable.");return true;}
    RuneTransaction transaction{game,player,plan.bag,plan.bag->dwUnitId,saved,plan.stats,{}};
    for(size_t i=0;i<4;++i)transaction.old[i]=SavedStat(saved,plan.stats[i]);
    // Allocate and verify every saved stat before asking native useitem to
    // consume any inputs. An output-placement refusal restores old balances.
    for(size_t i=0;i<4;++i){
        const auto target=int64_t(transaction.old[i])+plan.values[i];
        if(target>INT_MAX)return true;
        SetSavedStat(saved,plan.stats[i],int(target));
        if(SavedStat(saved,plan.stats[i])!=target){status.store("Rune deposit refused: bag stat update failed; nothing was consumed.");return true;}
    }
    Recipe output{};output.header=recipe->header;output.outputs[0].type=254;
    struct Scope {RuneTransaction* previous;Scope(RuneTransaction* value):previous(runeTransaction){runeTransaction=value;}~Scope(){runeTransaction=previous;}} scope(&transaction);
    originalCreate(game,player,&output,inputs);
    if(transaction.committed)for(auto code:plan.codes)QuestJournal::DepositedRune(game,player,code);
    status.store(transaction.committed ? "Runes deposited into the existing storage bag." : "Rune deposit refused: native useitem placement did not complete.");return true;
}
