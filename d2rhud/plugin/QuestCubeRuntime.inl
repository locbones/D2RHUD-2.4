// Included in the cube hook owner's namespace; it owns the single native
// recipe/create/add detours rather than attaching competing quest detours.
struct QuestOutputScope {
    struct Before {uint32_t id;Unit* pointer;D2InventoryStrc* parent;uint8_t page;int quantity;};
    Game* game;Unit* player;QuestJournal::RecipeGate gate;size_t expected=0;
    std::vector<uint32_t> inputCodes;std::vector<Before> before;std::set<uint32_t> placed;
    bool attempted=false,failed=false,finalSeen=false;
    QuestOutputScope(Game* g,Unit* p,const QuestJournal::RecipeGate& q,const Recipe& recipe):game(g),player(p),gate(q){
        for(const auto& out:recipe.outputs)if(out.type)expected+=(std::max)(1,int(out.quantity));
        std::set<Unit*> seen;
        for(auto item=player->pInventory->pFirstItem;item&&seen.size()<4096;){
            if(!seen.insert(item).second||!item->pItemData)break;auto data=item->pItemData;
            if(data->nPage==3){
                before.push_back({item->dwUnitId,item,data->pExtraData.pParentInv,data->nPage,GetStat(item,70)});
                const auto table=RuneField<const uint8_t*>(Native<void*>(0x1ca0390));const auto rows=RuneField<uint64_t>(Native<void*>(0x1ca0398));
                if(table&&rows<=100000&&item->dwClassId<rows)inputCodes.push_back(RuneField<uint32_t>(table+size_t(item->dwClassId)*0x1b4,0x80));
            }
            item=data->pExtraData.pNextItem;
        }
    }
    QuestJournal::Outcome Outcome()const{
        // The final native Add returns unused input quantities to the cube.
        // It also runs on successful recipes with stacked gems/removers; only
        // a failed return or missing output makes that transaction incomplete.
        if(!failed&&expected&&placed.size()==expected)return QuestJournal::Outcome::Committed;
        if(attempted||!placed.empty())return QuestJournal::Outcome::Ambiguous;
        for(const auto& old:before){auto item=Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0)(game,4,old.id);
            if(item!=old.pointer||!item->pItemData||item->pItemData->pExtraData.pParentInv!=old.parent||item->pItemData->nPage!=old.page||GetStat(item,70)!=old.quantity)return QuestJournal::Outcome::Ambiguous;
        }return QuestJournal::Outcome::Rejected;
    }
};
thread_local QuestOutputScope* questOutput=nullptr;
void QuestAdded(bool success,Unit* player,const uint32_t* ids,size_t count,uintptr_t caller){
    if(!questOutput||questOutput->player!=player)return;
    auto& q=*questOutput;
    if(caller==Pattern::Address(0x3085d2)){q.finalSeen=true;if(!success)q.failed=true;return;}
    if(caller!=Pattern::Address(0x30812a)&&caller!=Pattern::Address(0x30850e))return;
    q.attempted=true;if(!success||!ids||count!=1){q.failed=true;return;}
    if(!q.placed.insert(*ids).second)q.failed=true;
}
std::optional<QuestJournal::RecipeGate> QuestGate(Recipe* recipe){
    const auto rules=QuestJournal::Bindings();if(!rules||rules->gates.empty())return {};
    const auto base=RuneField<Recipe*>(Native<void*>(0x1ca0b18));const auto count=RuneField<uint64_t>(Native<void*>(0x1ca0b20));
    const auto address=reinterpret_cast<uintptr_t>(recipe),first=reinterpret_cast<uintptr_t>(base);
    if(!base||address<first||(address-first)%sizeof(Recipe)||(address-first)/sizeof(Recipe)>=count)return {};
    const auto row=(address-first)/sizeof(Recipe);
    for(const auto& gate:rules->gates)if(gate.row==row)return gate;
    return {};
}
bool CreateQuest(Game* game,Unit* player,Recipe* recipe,Input* inputs){
    const auto gate=QuestGate(recipe);if(!gate)return false;
    if(!game||!player||!player->pInventory||!QuestJournal::Begin(game,player,*gate)){QuestJournal::Refused(game,player,"Quest recipe refused: check the active objective in your Quest Journal.");return true;}
    Recipe native=*recipe;
    const auto rules=QuestJournal::Bindings();
    if(rules&&rules->events){
        if(!QuestJournal::RecipeObjective(*rules,*gate)){
            if(!gate->turnIn||!rules->quests[gate->quest-1].milestone||gate->inputs!=4){QuestJournal::Finish(game,player,*gate,QuestJournal::Outcome::Rejected);return true;}
            memmove(native.header.data()+0x1c,native.header.data()+0x24,5*8);memset(native.header.data()+0x44,0,8);native.header[0x10]=3;
        }
    }
    for(const auto& out:native.outputs)if(out.type&&((out.type!=252&&out.type!=254)||out.quantity>1||out.Visual())){
        QuestJournal::Finish(game,player,*gate,QuestJournal::Outcome::Rejected);QuestJournal::Refused(game,player,"Quest recipe has unsupported output semantics; no inputs were consumed.");return true;
    }
    const int property=RuneName(0x1c9ebd8,"rng-generator");
    if(property<0){QuestJournal::Finish(game,player,*gate,QuestJournal::Outcome::Rejected);QuestJournal::Refused(game,player,"Quest progress property is missing; no inputs were consumed.");return true;}
    // Progress is owned by the journal. Keep every actual reward/bag balance
    // modifier, but stop adding tutorial stage numbers to movable items.
    for(auto& out:native.outputs)for(auto& mod:out.mods)if(mod.property==property)mod=Modifier{};
    QuestOutputScope transaction(game,player,*gate,native);
    struct Scope {QuestOutputScope* previous;Scope(QuestOutputScope* q):previous(questOutput){questOutput=q;}~Scope(){questOutput=previous;}} scope(&transaction);
    originalCreate(game,player,&native,inputs);
    QuestJournal::Finish(game,player,*gate,transaction.Outcome());return true;
}
bool RecoverQuestProof(Game* game,Unit* player,int quest){
    if(!installed.load()||!game||!player||!player->pInventory||quest<1||quest>99)return false;
    const auto code="Bt"+(quest<10?std::string("0"):std::string{})+std::to_string(quest);
    const auto target=Resolve(code,0,0);uint32_t base=0;if(!target.Valid()||target.kind||!Base(target,base))return false;
    LegacyTransmog::Data::Drop drop;drop.game=reinterpret_cast<uintptr_t>(game);drop.seed=reinterpret_cast<uintptr_t>(&player->pSeed);drop.level=(std::max)(1,GetStat(player,12));drop.format=uint16_t(game->dwGameVersion);drop.id=int(base);drop.quality=2;
    auto item=Native<Unit*(__fastcall*)(Game*,LegacyTransmog::Data::Drop*,int)>(0x2f58c0)(game,&drop,0);
    if(!item||!item->pItemData)return false;item->pItemData->dwItemFlags|=0x10;
    struct Placement {Location location{};uint8_t valid=0;std::array<uint8_t,7> padding{};} placement;
    Native<void(__fastcall*)(void*,Game*,Unit*,Unit*)>(0x2d5890)(&placement,game,player,item);
    const auto id=item->dwUnitId;
    if(!placement.valid||!originalAdd(player,&placement.location,&id,1,false)){Native<void(__fastcall*)(Game*,Unit*)>(0x2f70a0)(game,item);return false;}
    const auto& p=placement.location;originalNotify(game,player,item,1,2,p.mode,p.page,p.body,uint32_t(p.x)|(uint32_t(p.y)<<16));return true;
}

void ObserveCraft(Game* game,Unit* player,Recipe* recipe,Input* inputs,size_t row){
    const auto rules=QuestJournal::Bindings();
    if(!rules||!rules->events||!game||!player||!player->pInventory){originalCreate(game,player,recipe,inputs);return;}
    QuestJournal::RecipeGate gate;gate.row=row;QuestOutputScope transaction(game,player,gate,*recipe);
    struct Scope {QuestOutputScope* previous;Scope(QuestOutputScope* q):previous(questOutput){questOutput=q;}~Scope(){questOutput=previous;}} scope(&transaction);
    originalCreate(game,player,recipe,inputs);
    std::vector<Unit*> outputs;for(auto id:transaction.placed)outputs.push_back(Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0)(game,4,id));
    QuestJournal::Crafted(game,player,row,transaction.inputCodes,outputs,transaction.Outcome());
}
bool ClaimQuestBonus(Game* game,Unit* player,int quest){
    const auto rules=QuestJournal::Bindings();if(!installed.load()||!rules||!rules->events||!game||!player||!player->pInventory||quest<1||quest>int(rules->quests.size())||!rules->quests[quest-1].bonus)return false;
    const Recipe* reward=nullptr;const auto recipes=RuneField<Recipe*>(Native<void*>(0x1ca0b18));const auto rows=RuneField<uint64_t>(Native<void*>(0x1ca0b20));
    if(!recipes||!rows||rows>100000)return false;
    for(const auto& gate:rules->gates)if(gate.quest==quest&&gate.turnIn){if(reward||gate.row>=rows)return false;reward=recipes+gate.row;}
    if(!reward||reward->outputs[0].type!=254||reward->outputs[1].type||reward->outputs[2].type)return false;
    // The milestone's ,mod output changes the base code while retaining the
    // original unique ID. Match the actual compiled reward ingredient instead
    // of guessing a new Barrier<N> unique ID from the quest number. This also
    // works with BIN-only tables and preserves exact-unique early requirements.
    const auto flags=RuneField<uint16_t>(reward->header.data(),0x14);
    const auto base=RuneField<uint16_t>(reward->header.data(),0x16);
    const auto unique=RuneField<uint16_t>(reward->header.data(),0x18);
    const auto quality=reward->header[0x1a],quantity=reward->header[0x1b];
    const auto itemCount=RuneField<uint64_t>(Native<void*>(0x1ca0398));
    if(!(flags&1)||(flags&~uint16_t(0x41))||quality!=7||quantity>1||!itemCount||itemCount>100000||base>=itemCount)return false;
    if(flags&0x40){const auto count=RuneField<uint64_t>(Native<void*>(0x1c9fc78));if(!unique||!count||count>65536||unique>count)return false;}
    else if(unique)return false;
    Unit* charm=nullptr;std::set<Unit*> seen;
    for(auto item=player->pInventory->pFirstItem;item&&seen.size()<4096;){
        if(!seen.insert(item).second||!item->pItemData)return false;const auto data=item->pItemData;
        if(item->dwUnitType==4&&item->dwClassId==base&&data->pExtraData.pParentInv==player->pInventory&&data->nPage==0&&item->dwAnimMode==0&&data->dwQualityNo==quality&&(!(flags&0x40)||data->dwFileIndex==uint32_t(unique-1))){
            if(charm)return false;charm=item;
        }item=data->pExtraData.pNextItem;
    }
    if(!charm)return false;
    const int progress=RuneName(0x1c9ebd8,"rng-generator");if(progress<0)return false;
    const auto properties=RuneField<const uint8_t*>(Native<void*>(0x1c9ebc0));const auto propertyCount=RuneField<uint64_t>(Native<void*>(0x1c9ebc8));
    const auto costs=RuneField<const uint8_t*>(Native<void*>(0x1c9fb78));const auto statCount=RuneField<uint64_t>(Native<void*>(0x1c9fb80));
    if(!properties||propertyCount>65536||!costs||statCount>512)return false;
    int stat=-1,amount=0;int64_t capacity=INT_MAX;
    for(const auto& mod:reward->outputs[0].mods)if(mod.property>=0&&mod.property!=progress){
        if(stat>=0||uint64_t(mod.property)>=propertyCount||mod.param||mod.chance||mod.min<=0||mod.min!=mod.max)return false;
        const auto property=properties+size_t(mod.property)*0x2e;
        // Fixed, unparameterized func1 and func8 values use the same additive
        // saved-stat setter. The 'move' reward uses func8 in Properties.bin.
        if(property[0x18]!=1&&property[0x18]!=8)return false;for(size_t n=1;n<7;++n)if(property[0x18+n])return false;
        stat=RuneField<uint16_t>(property,0x20);if(uint64_t(stat)>=statCount)return false;const auto cost=costs+size_t(stat)*0x144;
        const int shift=cost[0x14],bits=cost[0x15];if(shift>8||!bits||bits>32||!cost[8]||cost[8]>32)return false;
        amount=int(mod.min)<<shift;capacity=(std::min)(int64_t(INT_MAX),((int64_t(1)<<bits)-1-RuneField<uint32_t>(cost,0x18))*(int64_t(1)<<shift));
        if(cost[8]<32)capacity=(std::min)(capacity,(int64_t(1)<<(cost[8]-1))-1);
    }
    if(stat<0)return false;const auto saved=SavedList(charm,true);if(!saved)return false;const int old=SavedStat(saved,stat);const auto value=int64_t(old)+amount;
    if(value>capacity||value<INT_MIN)return false;
    SetSavedStat(saved,stat,int(value));if(SavedStat(saved,stat)!=value){SetSavedStat(saved,stat,old);return false;}
    const auto path=charm->pStaticPath;const auto position=path?uint32_t(path->tGameCoord.X)|(uint32_t(path->tGameCoord.Y)<<16):0;
    originalNotify(game,player,charm,1,2,charm->dwAnimMode,charm->pItemData->nPage,charm->pItemData->nBodyLoc,position);return true;
}

