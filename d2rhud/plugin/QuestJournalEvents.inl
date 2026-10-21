// Server-side evidence is retained even if the observed item later changes
// hands. Ordinary possessions never import another character's quest stage.
uint32_t ItemCode(std::string_view text){uint32_t code=0x20202020;for(size_t n=0;n<(std::min)(size_t(4),text.size());++n)reinterpret_cast<char*>(&code)[n]=text[n];return code;}
bool IsType(Unit* item,std::string_view name){
    const auto items=Field<const uint8_t*>(Native<void*>(0x1ca0390));const auto count=Field<uint64_t>(Native<void*>(0x1ca0398));
    const auto types=Field<const uint8_t*>(Native<void*>(0x1c9fbf0));const auto typeCount=Field<uint64_t>(Native<void*>(0x1c9fbf8));
    if(!item||!items||item->dwClassId>=count||count>100000||!types||!typeCount||typeCount>8192)return false;
    const auto row=items+size_t(item->dwClassId)*0x1b4;
    std::vector<int> pending{Field<int16_t>(row,0x126),Field<int16_t>(row,0x128)};std::set<int> seen;
    while(!pending.empty()){
        const int type=pending.back();pending.pop_back();if(type<=0||uint64_t(type)>=typeCount||!seen.insert(type).second)continue;
        const auto entry=types+size_t(type)*0xe4;if(Field<uint32_t>(entry)==ItemCode(name))return true;
        pending.push_back(Field<int16_t>(entry,4));pending.push_back(Field<int16_t>(entry,6));
    }return false;
}
bool Matches(Unit* item,const Definition::Item& predicate){
    if(!item||item->dwUnitType!=4||!item->pItemData)return false;const auto data=item->pItemData;
    if(predicate.quality&&int(data->dwQualityNo)!=predicate.quality)return false;
    if(!predicate.code.empty()&&Code(item)!=ItemCode(predicate.code))return false;
    if(!predicate.type.empty()&&!IsType(item,predicate.type))return false;
    if(!predicate.unique.empty()||!predicate.set.empty()){
        const bool unique=!predicate.unique.empty();const auto link=Field<void*>(Native<void*>(unique?0x1c9fc68:0x1c9fc48));
        if(!link||int(data->dwQualityNo)!=(unique?7:5))return false;
        const int id=Native<int(__fastcall*)(void*,const char*)>(0x6d0470)(link,(unique?predicate.unique:predicate.set).c_str());
        if(id<0||int(data->dwFileIndex)!=id)return false;
    }
    if(predicate.stat>=0){const int value=ItemStat(item,predicate.stat);if(value<predicate.min||value>predicate.max)return false;}
    return true;
}
bool Matches(Unit* item,const Definition& quest){return std::any_of(quest.items.begin(),quest.items.end(),[&](const auto& p){return Matches(item,p);});}
void Evidence(Session& s,int quest){if(quest<1||quest>64)return;const auto bit=uint64_t(1)<<(quest-1);if(!(s.state.evidence&bit)){s.state.evidence|=bit;s.dirty=true;++s.sequence;}}
void AdvanceEvents(Session& s,const Rules& rules){
    if(!rules.events||!s.state.legacyClosed||s.blocked||s.busy||s.state.uncertain)return;
    while(s.state.completed<int(rules.quests.size())){
        const auto& quest=rules.quests[s.state.completed];if(!(s.state.evidence&(uint64_t(1)<<(quest.id-1))))break;
        if(!s.state.objectiveReady){s.state.objectiveReady=true;s.dirty=true;Message(s,"Objective complete: "+quest.title);}
        if(quest.bonus||quest.milestone)break;
        ++s.state.completed;s.state.objectiveReady=false;s.dirty=true;++s.sequence;
    }
}
void Observe(Game* game,Session& s,const Rules& rules){
    if(!rules.events||s.blocked||s.busy||s.state.uncertain||!s.state.legacyClosed||!Safe(game,s.player)||!s.player->pInventory)return;
    std::set<uint32_t> equipped;std::set<Unit*> seen;
    for(auto item=s.player->pInventory->pFirstItem;item&&seen.size()<4096;){
        if(!seen.insert(item).second||item->dwUnitType!=4||!item->pItemData)break;const auto data=item->pItemData;
        if(data->pExtraData.pParentInv==s.player->pInventory){
            if(item->dwAnimMode==1&&IsType(item,"weap"))equipped.insert(item->dwUnitId);
            for(const auto& q:rules.quests){
                if(q.trigger=="item"&&Matches(item,q))Evidence(s,q.id);
                else if(q.trigger=="statueCube"&&data->nPage==3&&Matches(item,q))Evidence(s,q.id);
            }
        }item=data->pExtraData.pNextItem;
    }
    for(auto id:s.weapons)if(!equipped.contains(id)){
        const auto item=Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0)(game,4,id);
        if(!item||item->dwUnitType!=4||!item->pItemData)continue;
        const auto data=item->pItemData;
        // Dragging equipment passes through cursor mode, potentially for many
        // observations. Keep that provenance until the actual ground drop.
        // Putting it away or transferring it ends this equipped-item action.
        if(item->dwAnimMode==4&&(data->pExtraData.pParentInv==s.player->pInventory||
            (!data->pExtraData.pParentInv&&data->dwOwnerGUID==s.player->dwUnitId)))equipped.insert(id);
        else if(item->dwAnimMode==3&&!data->pExtraData.pParentInv)
            for(const auto& q:rules.quests)if(q.trigger=="dropWeapon")Evidence(s,q.id);
    }
    s.weapons=std::move(equipped);AdvanceEvents(s,rules);
}
