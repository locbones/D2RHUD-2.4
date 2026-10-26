// Native server avatars. The normal unit, inventory, skills and stat lists are
// parked intact; none are transplanted into, or owned by, the temporary unit.
using SurvivorLoad=int(__fastcall*)(Game*,Client*,Unit**);
using SurvivorSave=int(__fastcall*)(Game*,Unit*,uint8_t*,uint32_t*,uint32_t);
using SurvivorSendSave=void(__fastcall*)(Game*,Unit*,const char*,Client*);
using SurvivorDisconnect=void(__fastcall*)(Game*,uint32_t);
using SurvivorPreview=void(__fastcall*)(Client*,void*);
using SurvivorStarter=void(__fastcall*)(Unit*,Game*);
SurvivorLoad originalSurvivorLoad=nullptr;
SurvivorSave originalSurvivorSave=nullptr;
SurvivorSendSave originalSurvivorSendSave=nullptr;
SurvivorDisconnect originalSurvivorDisconnect=nullptr;
SurvivorPreview originalSurvivorPreview=nullptr;
SurvivorStarter originalSurvivorStarter=nullptr;
thread_local SurvivorAvatar* survivorHandoff=nullptr;
struct SurvivorStarterContext {Game* game;Client* client;bool ready=false;};
thread_local SurvivorStarterContext* survivorStarter=nullptr;

void __fastcall SurvivorStarterHook(Unit* player,Game* game){
    auto context=survivorStarter;
    if(!context || context->game!=game || !player || !player->pPlayerData || Field<Client*>(player->pPlayerData,0xf0)!=context->client){
        originalSurvivorStarter(player,game);return;
    }
    // The mod's charstats loadout can contain rare affixes and powerful utility
    // charms. Build plain equipment only for this temporary avatar instead.
    constexpr std::array<uint32_t,7> weapons{0x2076616a,0x20747373,0x20646e77,0x20706373,0x20786168,0x20626c63,0x2072746b};
    const auto cls=player->dwClassId;if(cls>=weapons.size())return;
    std::array<uint8_t,0xd0> row{};Set(row.data(),0xb0,int16_t(-1)); // No starter staffmod.
    for(int slot=0;slot<(cls==1?1:2);++slot){
        const uint32_t code=slot?0x20637562:weapons[cls];
        const auto linker=Field<void*>(Native<void*>(0x1c9eba0),0);
        const int id=linker?Native<int(__fastcall*)(void*,uint32_t)>(0x6d01a0)(linker,code):-1;
        if(id<0)return;
        std::array<uint8_t,8> entry{};Set(entry.data(),0,code);entry[4]=uint8_t(slot?5:4);entry[5]=1;entry[6]=2;
        auto item=Native<Unit*(__fastcall*)(Game*,Unit*,void*,void*,int,int)>(0x28c520)(game,player,row.data(),entry.data(),slot,id);
        if(!item || item->dwUnitType!=4 || !item->pItemData)return;
        Set(item->pItemData,0x18,Field<uint32_t>(item->pItemData,0x18)|0x20000);
        const int maximum=Stat(item,73);if(maximum>0)UnitStat(item,72,maximum);
        struct Placement {LegacyTransmog::Data::Location location{};uint8_t valid=0;std::array<uint8_t,7> padding{};} placement;
        Native<void(__fastcall*)(void*,Game*,Unit*,Unit*)>(0x2d5890)(&placement,game,player,item);
        auto guid=item->dwUnitId;
        if(!placement.valid || !Native<bool(__fastcall*)(Unit*,void*,uint32_t*,int,bool)>(0x2d5e10)(player,&placement,&guid,1,false)){
            Native<void(__fastcall*)(Game*,Unit*)>(0x2f70a0)(game,item);return;
        }
    }
    context->ready=true;
}

SurvivorAvatar* AvatarFor(Game* game,Unit* unit){
    for(auto& [client,a]:survivorAvatars)if(a->game==game && a->avatar==unit)return a.get();
    return nullptr;
}
void SurvivorParkedRoom(SurvivorAvatar& a,bool parked){
    // Owned pets still resolve their original player during inactive-room
    // relocation (38d1a0). A detached player with no room crashes that native
    // path at 38d21b. Retain only the logical owner room: the hero stays out of
    // collision and room update lists, and the existing pin owns its lifetime.
    if(a.hero && a.hero->pDynamicPath)Set(a.hero->pDynamicPath,0x20,parked?a.townRoom:nullptr);
}
// The native loader is bypassed ONLY during our synchronous re-assignment.
// The normal save-file loader and its error handling remain the default.
int __fastcall SurvivorLoadHook(Game* game,Client* client,Unit** output){
    if(survivorHandoff && survivorHandoff->game==game && survivorHandoff->client==client){*output=client->pPlayer;return *output?0:14;}
    SummonPersistence::BeforeLoad(game,client);
    QuestJournal::BeforeLoad(game,client);
    int result=0;
    {
        // Companion checkpoints above identify the ORIGINAL save. Only the
        // synchronous leaf reader borrows a converted copy; native descriptors
        // retain their allocation and ownership through success/failure.
        CandyCornSave::LoadScope conversion(client,output);
        result=originalSurvivorLoad(game,client,output);
        conversion.Complete(result==0);
    }
    SummonPersistence::AfterLoad(game,client,output?*output:nullptr,result==0);
    QuestJournal::AfterLoad(game,client,output?*output:nullptr,result==0);
    return result;
}
struct SurvivorSaveScope {
    SurvivorAvatar& a;Client* previous;void* slot;
    explicit SurvivorSaveScope(SurvivorAvatar& avatar):a(avatar),previous(Field<Client*>(a.hero->pPlayerData,0xf0)),slot(Field<void*>(a.game,0x21e8+a.slot*8)){
        a.saved.dwFlags|=1;a.saved.pPlayer=a.hero;a.saved.dwUnitGUID=a.hero->dwUnitId;
        Set(a.hero->pPlayerData,0xf0,&a.saved);Set(a.game,0x21e8+a.slot*8,a.heroSlot);
    }
    ~SurvivorSaveScope(){Set(a.hero->pPlayerData,0xf0,previous);Set(a.game,0x21e8+a.slot*8,slot);}
};
int __fastcall SurvivorSaveHook(Game* game,Unit* unit,uint8_t* bytes,uint32_t* size,uint32_t capacity){
    std::lock_guard lock(serverMutex);
    auto save=[&](Unit* hero){
        const int result=originalSurvivorSave(game,hero,bytes,size,capacity);
        if(!result && size && *size<=capacity)SummonPersistence::Saved(game,hero,bytes,*size);
        if(!result && size && *size<=capacity)QuestJournal::Saved(game,hero,bytes,*size);
        return result;
    };
    if(auto a=AvatarFor(game,unit)){SurvivorSaveScope scope(*a);return save(a->hero);}
    return save(unit);
}
void __fastcall SurvivorSendSaveHook(Game* game,Unit* unit,const char* name,Client* client){
    std::lock_guard lock(serverMutex);
    // Includes native shared-stash/golem sections, not just the .d2s header.
    if(auto a=AvatarFor(game,unit)){SurvivorSaveScope scope(*a);originalSurvivorSendSave(game,a->hero,name,client);return;}
    originalSurvivorSendSave(game,unit,name,client);
}
void __fastcall SurvivorPreviewHook(Client* client,void* preview){
    std::lock_guard lock(serverMutex);
    if(auto it=survivorAvatars.find(client);it!=survivorAvatars.end()){SurvivorSaveScope scope(*it->second);originalSurvivorPreview(&it->second->saved,preview);return;}
    originalSurvivorPreview(client,preview);
}
void __fastcall SurvivorDisconnectHook(Game* game,uint32_t clientId){
    {std::lock_guard lock(serverMutex);for(auto& [client,a]:survivorAvatars)if(a->game==game && client->dwClientId==clientId){RestoreSurvivor(game,a->avatar,false);break;}}
    originalSurvivorDisconnect(game,clientId);
}
void SurvivorRekey(Run* run,uint32_t from,uint32_t to){
    if(!run)return;
    if(auto node=run->returns.extract(from);!node.empty()){node.key()=to;run->returns.insert(std::move(node));}
    if(auto node=run->survival.builds.extract(from);!node.empty()){node.key()=to;run->survival.builds.insert(std::move(node));}
    for(auto set:{&run->returning,&run->ready})if(set->erase(from))set->insert(to);
    if(run->creator==from)run->creator=to;
    run->automapSent.clear();
}
bool SurvivorBind(SurvivorAvatar& a,Unit* target,bool notify){
    const auto client=a.client;
    Native<void(__fastcall*)(Client*,Unit*)>(0x29d890)(client,target);
    Native<void(__fastcall*)(Unit*,Client*)>(0x2c52e0)(target,client);
    if(!notify)return true;
    // Native player assignment, complete inventory/stat/skill synchronization.
    // No live client structures are copied or constructed by the HUD.
    Native<void(__fastcall*)(Game*,Unit*,Client*)>(0x341670)(a.game,target,client);
    // AddPlayer normally consumes/frees the incoming save-buffer vector. This
    // handoff already has a live unit; park that vector, including its inline
    // storage, so normal-character/shared-stash buffers remain owned intact.
    std::array<uint8_t,0x78> savedBuffers{};
    memcpy(savedBuffers.data(),reinterpret_cast<uint8_t*>(client)+0x1a0,savedBuffers.size());
    Set(client,0x1a0,reinterpret_cast<uint8_t*>(client)+0x1b8);Set(client,0x1a8,uint64_t(0));Set(client,0x1b0,uint64_t(0x8000000000000004ULL));
    auto previous=survivorHandoff;survivorHandoff=&a;
    const int error=Native<int(__fastcall*)(Client*,Game*)>(0x29de20)(client,a.game);
    survivorHandoff=previous;
    memcpy(reinterpret_cast<uint8_t*>(client)+0x1a0,savedBuffers.data(),savedBuffers.size());
    return error==0;
}
int SurvivorBaseStat(Unit* unit,int stat){
    auto root=unit->pStatListEx;auto values=Field<uint8_t*>(root,0x30);const auto count=Field<uint64_t>(root,0x38);
    if(!values || count>4096)return 0;
    for(size_t n=0;n<count;++n)if(Field<uint32_t>(values+n*8,0)==uint32_t(stat)<<16)return Field<int>(values+n*8,4);
    return 0;
}
bool SurvivorCaptureAttributes(SurvivorAvatar& a,Survivors::Build& build){
    auto next=build.Save();
    for(size_t n=0;n<4;++n){const int delta=SurvivorBaseStat(a.avatar,int(n))-a.baseAttributes[n];if(delta<0 || delta>145)return false;next.attributes[n]=uint16_t(delta);}
    if(!Survivors::Valid(next) || next.attributes==build.attributes)return false;
    build.attributes=next.attributes;return true;
}
int SurvivorSkillLevel(const Survivors::Build& build,int rank){return rank?std::clamp(rank+int(build.ranks[size_t(Survivors::Upgrade::Power)])*2+(build.level-1)/3+int(build.attributes[1]/10),1,40):0;}
void SurvivorApplyLevel(SurvivorAvatar& a,const Survivors::Build& build,bool heal){
    auto unit=a.avatar;const int level=build.level;
    UnitStat(unit,12,level);UnitStat(unit,4,int(build.Unspent()));UnitStat(unit,5,0);UnitStat(unit,13,0);
    for(size_t n=0;n<4;++n)UnitStat(unit,int(n),a.baseAttributes[n]+build.attributes[n]);
    const auto rows=Field<uint8_t*>(Native<void*>(0x1c9fb60),0);
    const auto count=Field<uint64_t>(Native<void*>(0x1c9fb68),0);
    std::array<int,3> attributeBonus{};
    if(rows && build.classId<count){const auto row=rows+build.classId*0xd0;
        attributeBonus={int(build.attributes[3])*int(Field<uint8_t>(row,0x42))*64,int(build.attributes[1])*int(Field<uint8_t>(row,0x44))*64,int(build.attributes[3])*int(Field<uint8_t>(row,0x43))*64};
    }
    for(size_t i=0;i<3;++i){
        const int maximum=a.baseMaximum[i]+(level-1)*(i==0?8:4)*256+attributeBonus[i];
        UnitStat(unit,int(7+i*2),maximum);
        if(heal)UnitStat(unit,int(6+i*2),maximum);
    }
    for(size_t i=0;i<3;++i){
        const int rank=build.ranks[i];const int skill=SurvivorSkillId(build.classId,i);
        Native<void(__fastcall*)(Unit*,int,int,int)>(0x2192d0)(unit,skill,SurvivorSkillLevel(build,rank),0);
    }
    // SetSkillLevel changes only server ownership. Send the native skill-level
    // packet as well; the vitals synchronizer does not publish learned skills.
    for(size_t i=0;i<3;++i){
        const int rank=build.ranks[i];
        const int level=SurvivorSkillLevel(build,rank);
        Native<void(__fastcall*)(Client*,Unit*,uint16_t,uint8_t,uint8_t)>(0x2cce40)(a.client,unit,uint16_t(SurvivorSkillId(build.classId,i)),uint8_t(level),0);
    }
    Native<void(__fastcall*)(Unit*,int,int,int)>(0x29a2a0)(unit,0,SurvivorSkillId(build.classId,0),-1);
}
Unit* CreateSurvivor(Game* game,Unit* hero,const Survivors::Build& build){
    const auto client=Field<Client*>(hero->pPlayerData,0xf0);
    if(!client || survivorAvatars.contains(client) || !Town(Level(hero)) || Dead(hero) || build.classId>=Survivors::Classes.size())return nullptr;
    const auto skillRows=Field<uint8_t*>(Native<void*>(0x1c9fad0),0);const auto skills=Field<uint64_t>(Native<void*>(0x1c9fad8),0);
    // Refuse a mismatched mod skill table before touching the normal player.
    for(size_t slot=0;slot<3;++slot)if(const int id=SurvivorSkillId(build.classId,slot);id<0 || !skillRows || uint64_t(id)>=skills || Field<int16_t>(skillRows+id*0x284,0)!=id || Field<uint8_t>(skillRows+id*0x284,0x2c)!=build.classId || !SurvivorMobileSkill(skillRows+id*0x284))return nullptr;
    auto a=std::make_unique<SurvivorAvatar>();a->game=game;a->client=client;a->hero=hero;
    // Reclass's historical client declaration is larger than this build's
    // actual 0x888-byte allocation. Never read beyond the native allocation.
    memcpy(&a->saved,client,0x888);
    // Expiration callbacks may still visit the parked hero. Give them a valid
    // non-network client context so their stat packets cannot reach the avatar.
    a->saved.dwClientId=UINT32_MAX;a->saved.pNext=nullptr;a->saved.dwFlags|=1;a->saved.pPlayer=hero;
    a->town=Location(hero);a->townRoom=GetRoom(hero);a->slot=Field<int>(hero,0x12c);
    if(a->slot<0 || a->slot>=8)return nullptr;
    a->heroSlot=Field<void*>(game,0x21e8+a->slot*8);
    if(!a->heroSlot || Field<Unit*>(a->heroSlot,0)!=hero)return nullptr;
    // Fresh non-network client context: no borrowed packet pools, connection
    // objects, save buffers, cached stats or other owning pointers.
    a->creation.dwClientId=UINT32_MAX;a->creation.pGame=game;a->creation.dwClientState=3;a->creation.dwFlags=1;
    memcpy(reinterpret_cast<uint8_t*>(&a->creation)+0x2c,reinterpret_cast<uint8_t*>(client)+0x2c,0x3d);
    Set(&a->creation,0x1a0,reinterpret_cast<uint8_t*>(&a->creation)+0x1b8);Set(&a->creation,0x1b0,uint64_t(0x8000000000000004ULL));
    for(size_t i=0;i<16;++i){Set(&a->creation,0x468+i*8,int16_t(-1));Set(&a->creation,0x46c+i*8,int(-1));}
    Set(&a->creation,8,build.classId);Set(&a->creation,0xa,uint16_t(Field<int>(game,0x108)?0x21:1));Set(&a->creation,0x238,uint8_t(0));
    Set(game,0x21e8+a->slot*8,static_cast<void*>(nullptr));
    SurvivorStarterContext starter{game,&a->creation};auto previousStarter=survivorStarter;survivorStarter=&starter;
    Unit* unit=nullptr;const int error=Native<SurvivorLoad>(0x31a280)(game,&a->creation,&unit);
    survivorStarter=previousStarter;
    if(error || !starter.ready || !unit || !unit->pPlayerData || !unit->pStatListEx || !unit->pInventory){
        if(unit)originalRemove(game,unit);
        Set(game,0x21e8+a->slot*8,a->heroSlot);return nullptr;
    }
    a->avatar=unit;
    a->townPin.room=Field<void*>(a->townRoom,0x18);a->townPin.original=(Field<uint32_t>(a->townPin.room,0x50)&Pinned)!=0;
    Set(a->townPin.room,0x50,Field<uint32_t>(a->townPin.room,0x50)|Pinned);
    for(size_t i=0;i<3;++i)a->baseMaximum[i]=Stat(unit,int(7+i*2));
    for(size_t i=0;i<4;++i)a->baseAttributes[i]=SurvivorBaseStat(unit,int(i));
    // Remove from collision and room update lists, retaining all hero data.
    Native<void(__fastcall*)(Room*,int,uint32_t)>(0x1d1280)(a->townRoom,0,hero->dwUnitId);
    Native<void(__fastcall*)(Unit*,int)>(0x1f73e0)(hero,1);
    Native<void(__fastcall*)(Unit*)>(0x22e8a0)(hero);
    SurvivorParkedRoom(*a,true);
    Set(hero->pPlayerData,0xf0,&a->saved);
    Set(client,8,build.classId);Set(client,0xa,Field<uint16_t>(&a->creation,0xa));
    // Clear only skill hotkeys; networking, save buffers and account identity
    // remain owned by the real client. The original hotkeys are restored later.
    for(size_t i=0;i<16;++i){Set(client,0x468+i*8,int16_t(-1));Set(client,0x46c+i*8,int(-1));}
    auto ptr=a.get();survivorAvatars.emplace(client,std::move(a));
    Native<void(__fastcall*)(Client*,Unit*)>(0x29d890)(client,unit);
    Native<void(__fastcall*)(Unit*,Client*)>(0x2c52e0)(unit,client);
    Native<void(__fastcall*)(Client*,Unit*,Game*,Room*,int,int)>(0x29d8f0)(client,unit,game,ptr->townRoom,ptr->town.x,ptr->town.y);
    SurvivorApplyLevel(*ptr,build,true);
    if(!SurvivorBind(*ptr,unit,true)){RestoreSurvivor(game,unit);return nullptr;}
    // Publish after local-player assignment too, so fresh client units retain
    // their starting skill and selection across the native handoff.
    SurvivorApplyLevel(*ptr,build,false);
    SurvivorRekey(Active(game),hero->dwUnitId,unit->dwUnitId);return unit;
}
bool RestoreSurvivor(Game* game,Unit* unit,bool notify){
    auto a=AvatarFor(game,unit);if(!a)return false;
    const auto client=a->client;const auto originalId=a->hero->dwUnitId;const auto avatarId=unit->dwUnitId;
    if(auto run=Active(game)){if(auto it=run->players.find(avatarId);it!=run->players.end()){Detach(unit,it->second,run->state);run->players.erase(it);}SurvivorRekey(run,avatarId,originalId);}
    // Disconnect and destruction use the same restoration before native save
    // or removal can consume the currently assigned player.
    Set(client,8,Field<uint8_t>(&a->saved,8));Set(client,0xa,Field<uint16_t>(&a->saved,0xa));
    Set(client,0x238,Field<uint8_t>(&a->saved,0x238));
    memcpy(reinterpret_cast<uint8_t*>(client)+0x468,reinterpret_cast<uint8_t*>(&a->saved)+0x468,0x80);
    Native<void(__fastcall*)(Client*,Unit*)>(0x29d890)(client,a->hero);
    Native<void(__fastcall*)(Unit*,Client*)>(0x2c52e0)(a->hero,client);
    // Remove the logical owner reference before native teleport reattaches
    // the hero to room/update/collision lists. It was never a list membership.
    SurvivorParkedRoom(*a,false);
    // Teleport supports a detached room with an existing dynamic path.
    Native<int(__fastcall*)(Game*,Unit*,Room*,int,int,int)>(0x2c7a90)(game,a->hero,a->townRoom,a->town.x,a->town.y,1);
    a->creation.pPlayer=unit;a->creation.dwUnitGUID=avatarId;
    Set(unit->pPlayerData,0xf0,&a->creation);
    if(auto room=GetRoom(unit))Native<void(__fastcall*)(Room*,int,uint32_t)>(0x1d1280)(room,0,avatarId);
    originalRemove(game,unit);a->avatar=nullptr;
    Set(game,0x21e8+a->slot*8,a->heroSlot);
    SurvivorBind(*a,a->hero,notify);
    if(a->townPin.room && !a->townPin.original)Set(a->townPin.room,0x50,Field<uint32_t>(a->townPin.room,0x50)&~Pinned);
    survivorAvatars.erase(client);return true;
}

void LoadSurvivorSkillNames();
Survivors::Roster& SurvivorRoster(){
    if(!survivorRoster){
        LoadSurvivorSkillNames();
        wchar_t local[32768]{};char mod[128]{};Read(reinterpret_cast<uintptr_t>(Native<void*>(0x1bf084f)),mod);mod[127]=0;
        const std::string name=mod;const bool safe=!name.empty() && name.find_first_of("/\\:")==name.npos && name!="." && name!="..";
        const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
        const auto path=std::filesystem::path(local)/L"D2RHUD"/L"Survivors"/(safe?name+".roster":"base.roster");
        survivorRoster=std::make_unique<Survivors::Roster>(path);
        if(!length || length>=32768 || (!name.empty() && !safe))survivorRoster->good=false;
    }
    return *survivorRoster;
}
void SaveSurvivorProfile(const Survivors::SnapshotBody& p,bool force=false){
    const Survivors::Profile profile{p.xp,p.level,p.ranks,p.attributes};
    if(!p.run || p.failure!=Survivors::Failure::None || p.classId>=Survivors::Classes.size() || !Survivors::Valid(profile))return;
    std::lock_guard lock(survivorRosterMutex);const auto now=GetTickCount64();
    static uint64_t savedAt=0;auto& roster=SurvivorRoster();
    if(force || roster.Get(p.classId).level!=p.level || !savedAt || now-savedAt>=2000){
        survivorRosterError=!roster.Store(p.classId,profile);savedAt=now;
    }
}
std::array<std::array<std::string,3>,7> survivorSkillNames{};
const char* SurvivorUpgradeName(uint8_t cls,size_t upgrade){
    if(cls<Survivors::Classes.size() && upgrade<3 && !survivorSkillNames[cls][upgrade].empty())return survivorSkillNames[cls][upgrade].c_str();
    return Survivors::UpgradeName(cls,upgrade);
}
void LoadSurvivorSkillNames(){
    wchar_t exe[32768]{};char mod[128]{};
    if(!GetModuleFileNameW(nullptr,exe,32768) || !Read(reinterpret_cast<uintptr_t>(Native<void*>(0x1bf084f)),mod))return;
    mod[127]=0;const std::string name=mod;
    if(name.find_first_of("/\\:")!=name.npos || name=="." || name=="..")return;
    const auto root=std::filesystem::path(exe).parent_path();
    const auto data=name.empty()?root/"data":root/"Mods"/name/(name+".mpq")/"data";
    auto excel=data/"global/excel";
    if(!name.empty() && !std::filesystem::is_regular_file(excel/"skills.txt") && !std::filesystem::is_regular_file(excel/"skills.bin"))excel=root/"data/global/excel";
    auto set=[&](int number,const std::string& title){
        for(size_t cls=0;cls<Survivors::Classes.size();++cls)for(size_t i=0;i<3;++i)
            if(!(cls==1 && !i) && Survivors::Classes[cls].ids[i]==number && !title.empty())survivorSkillNames[cls][i]=title;
    };
    std::ifstream input(excel/"skills.txt");std::string line;
    if(!input){
        // skills.bin retains both the numeric skill ID and its 32-byte name.
        // TXT row positions and their *Id comment column are not required.
        std::ifstream binary(excel/"skills.bin",std::ios::binary|std::ios::ate);
        if(!binary || binary.tellg()<4 || binary.tellg()>64*1024*1024)return;
        const auto size=binary.tellg();binary.seekg(0);uint32_t count=0;
        if(!binary.read(reinterpret_cast<char*>(&count),4) || !count || count>65536 || size!=std::streamoff(4+size_t(count)*0x284))return;
        for(size_t i=0;i<count;++i){
            std::array<uint8_t,0x284> record{};if(!binary.read(reinterpret_cast<char*>(record.data()),record.size()))return;
            int16_t id=-1;memcpy(&id,record.data(),2);const auto begin=reinterpret_cast<const char*>(record.data()+2);
            const auto end=static_cast<const char*>(memchr(begin,0,32));set(id,std::string(begin,end?size_t(end-begin):32));
        }
        return;
    }
    auto fields=[](const std::string& row){std::vector<std::string> result;std::istringstream values(row);std::string cell;while(std::getline(values,cell,'\t'))result.push_back(CubeVisual::Data::Trim(cell));return result;};
    if(!std::getline(input,line))return;const auto header=fields(line);int id=-1,title=-1;
    for(size_t i=0;i<header.size();++i){if(header[i]=="*Id" || header[i]=="Id" || header[i]=="id")id=int(i);if(header[i]=="skill")title=int(i);}
    if(id<0 || title<0)return;
    while(std::getline(input,line)){
        const auto row=fields(line);if(row.size()<=size_t((std::max)(id,title)))continue;
        int number=-1;const auto& value=row[id];const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
        if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size())continue;
        set(number,row[title]);
    }
}
