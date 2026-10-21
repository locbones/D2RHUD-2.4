// Included in CubeMap's namespace: server arena logic and scoped client pause.
bool SurvivorMobileSkill(const void* row){
    // These start callbacks prepare damage/skill parameters without owning
    // player locomotion. Movement/sequence skills require another loadout.
    const int start=Field<int16_t>(row,0x4c),effect=Field<int16_t>(row,0x4e);
    const bool safeStart=start==0 || start==4 || start==6 || start==28 || start==32 || start==39;
    const bool safeEffect=effect==0 || effect==2 || effect==8 || effect==11 || effect==18 || effect==22 || effect==28 || effect==43 || effect==68 || effect==73 || effect==80 || effect==117 || effect==118 || effect==123 || effect==125 || effect==144;
    return safeStart && safeEffect;
}
int SurvivorSkillId(uint8_t cls,size_t slot){
    if(cls>=Survivors::Classes.size() || slot>=3)return -1;
    // RMD moves Fire Ball from vanilla's slot 47 to 41. Resolve its actual
    // compiled-table name so learning, selection and casting use one skill.
    if(cls==1 && !slot && Field<void*>(Native<void*>(0x1c9fae8),0))return LookupName(0x1c9fae8,"Fire Ball");
    return Survivors::Classes[cls].ids[slot];
}
#include "CubeMapSurvivorsAvatar.inl"
using SurvivorClientTick=bool(__fastcall*)(uint32_t);
SurvivorClientTick originalSurvivorClientTick=nullptr;
// These are the RIP-relative targets read by 70012/70019, not the
// neighboring menu flags. HD selects the second flag when 1d64170 > 0.
constexpr uint32_t SurvivorClassicPause=0x1840a0c,SurvivorHdPause=0x1d47325;
struct SurvivorClientClock {
    void* clock=nullptr;
    uint64_t source=0;
    uint32_t offset=0,held=0;
    bool paused=false;
    uint32_t Time(void* current,uint64_t accumulated,uint32_t now,bool pause){
        if(current!=clock || accumulated<source)*this={};
        clock=current;source=accumulated;
        if(pause){
            if(!paused)held=Field<uint32_t>(current,8);
            paused=true;
            return held;
        }
        if(paused){
            // The local server's pump keeps its native 40-ms tick boundaries
            // while servicing paused input. Its reconciliation flag is set on
            // those same boundaries after Resume. A fractional offset shifts
            // client physics away from every subsequent server copy, producing
            // recurring position corrections even after returning to town.
            // Exclude only whole ticks; resume can advance by at most one tick
            // beyond the held render time, with the original shared phase.
            offset=((now-held)/40u)*40u;
        }
        paused=pause;
        return now-offset;
    }
} survivorClientClock;
bool SurvivorClientPaused(){
    if(!installed.load() || !survivorsAvailable)return false;
    Packet packet;Survivors::SnapshotBody snapshot;uint64_t stamp=0,worldStamp=0;
    {std::lock_guard lock(viewMutex);packet=view;snapshot=survivorView;stamp=survivorReceived;worldStamp=receivedAt;}
    const auto now=GetTickCount64();
    if(!stamp || !worldStamp || now-stamp>4000 || now-worldStamp>4000 || !snapshot.paused || snapshot.phase!=EventPhase::Active || packet.event!=Event::Survivors || !packet.active || packet.eventPhase!=EventPhase::Active ||
       snapshot.run!=packet.run || snapshot.session!=packet.session || snapshot.recipient!=packet.recipient)return false;
    auto player=LocalPlayer();return player && player->dwUnitId==packet.recipient && Level(player)==packet.level;
}
bool __fastcall SurvivorClientTickHook(uint32_t now){
    const bool paused=SurvivorClientPaused();
    auto clock=Field<void*>(Native<void*>(0x1c9b3f0),0);
    if(!clock){survivorClientClock={};return originalSurvivorClientTick(now);}
    // Freeze simulation time, preserving the previous 25-Hz fractional phase.
    // The input/main-loop clock must keep running to service network commands
    // while players choose upgrades. Exclude the paused interval from world
    // updates thereafter, including in town, until the native clock resets.
    now=survivorClientClock.Time(clock,Field<uint64_t>(clock,0),now,paused);
    if(!paused)return originalSurvivorClientTick(now);
    // Native paused rendering still snapshots units and interpolates at zero
    // delta. It must not run the normal branch and advance animation frames.
    struct PauseScope {
        uint8_t classic=Field<uint8_t>(Native<void*>(SurvivorClassicPause),0),hd=Field<uint8_t>(Native<void*>(SurvivorHdPause),0);
        PauseScope(){Set(Native<void*>(SurvivorClassicPause),0,uint8_t(1));Set(Native<void*>(SurvivorHdPause),0,uint8_t(1));}
        ~PauseScope(){Set(Native<void*>(SurvivorClassicPause),0,classic);Set(Native<void*>(SurvivorHdPause),0,hd);}
    } scope;
    return originalSurvivorClientTick(now);
}
struct SurvivorSignatureFailure {
    uint32_t rva=0,size=0;
    uintptr_t address=0;
    uint64_t expected=0;
    DWORD error=0;
    bool complete=false;
    std::vector<uint8_t> bytes;
};
bool SurvivorSignaturesSupported(const auto& signatures,uint32_t* failed,SurvivorSignatureFailure* failure=nullptr){
    if(failed)*failed=0;
    if(failure)*failure={};
    for(const auto& signature:signatures){
        // Save-buffer expansion changes its prologue and seven coordinated
        // body operands. Check the entire native routine against two exact
        // variants so partial edits and unrelated changes remain rejected.
        const bool saveDispatch=signature.rva==SurvivorsNative::SaveDispatchOriginal.rva;
        auto checked=saveDispatch ? SurvivorsNative::SaveDispatchOriginal : signature;
        std::vector<uint8_t> bytes(checked.size);SIZE_T n=0;
        const auto address=Native<void*>(signature.rva);
        const bool read=ReadProcessMemory(GetCurrentProcess(),address,bytes.data(),bytes.size(),&n)!=FALSE;
        const DWORD error=read ? (n==bytes.size() ? ERROR_SUCCESS : ERROR_PARTIAL_COPY) : GetLastError();
        const bool complete=read && n==bytes.size();
        const auto digest=complete ? LegacyTransmog::Data::Hash(bytes) : 0;
        const bool expanded=saveDispatch && complete && digest==SurvivorsNative::SaveDispatch32K.hash;
        if(!complete || (digest!=checked.hash && !expanded)){
            if(failed)*failed=signature.rva;
            if(failure){
                failure->rva=signature.rva;failure->size=checked.size;
                failure->address=reinterpret_cast<uintptr_t>(address);
                failure->expected=saveDispatch && n>7 && bytes[7]==0x80 ? SurvivorsNative::SaveDispatch32K.hash : checked.hash;
                failure->error=error;
                failure->complete=complete;
                bytes.resize((std::min)(size_t(n),bytes.size()));
                failure->bytes=std::move(bytes);
            }
            return false;
        }
    }return true;
}
bool SaveLoadSupported(uint32_t* failed=nullptr){return SurvivorSignaturesSupported(SurvivorsNative::SaveLoadSignatures,failed);}
bool SurvivorsSupported(uint32_t* failed=nullptr,SurvivorSignatureFailure* failure=nullptr){return SurvivorSignaturesSupported(SurvivorsNative::Signatures,failed,failure);}

// Recognize common unconditional x64 detours without executing their targets.
// A destination in private memory can be a relay; the bounded trace follows it
// until an image-owned function, an unrecognized instruction, or an unreadable
// address is reached. An owner is evidence of the destination, not proof of who
// installed the patch.
bool SurvivorEntryJump(uintptr_t address,std::span<const uint8_t> bytes,uintptr_t& target){
    if(bytes.size()>=5 && bytes[0]==0xe9){
        int32_t displacement=0;memcpy(&displacement,bytes.data()+1,4);
        target=address+5+displacement;return true;
    }
    if(bytes.size()>=2 && bytes[0]==0xeb){target=address+2+int8_t(bytes[1]);return true;}
    if(bytes.size()>=6 && bytes[0]==0xff && bytes[1]==0x25){
        int32_t displacement=0;memcpy(&displacement,bytes.data()+2,4);
        return Read(address+6+displacement,target);
    }
    if(bytes.size()>=12 && bytes[0]==0x48 && bytes[1]==0xb8 && bytes[10]==0xff && bytes[11]==0xe0){
        memcpy(&target,bytes.data()+2,8);return true;
    }
    if(bytes.size()>=13 && bytes[0]==0x49 && bytes[1]==0xbb && bytes[10]==0x41 && bytes[11]==0xff && bytes[12]==0xe3){
        memcpy(&target,bytes.data()+2,8);return true;
    }
    return false;
}
void DescribeSurvivorAddress(std::ostream& report,uintptr_t address){
    report<<"0x"<<std::hex<<address;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info))){report<<" (VirtualQuery error "<<std::dec<<GetLastError()<<')';return;}
    report<<" [allocation=0x"<<reinterpret_cast<uintptr_t>(info.AllocationBase)<<", type=0x"<<info.Type<<", protection=0x"<<info.Protect<<"]";
    if(info.Type==MEM_IMAGE){
        wchar_t path[32768]{};
        if(GetModuleFileNameW(static_cast<HMODULE>(info.AllocationBase),path,32768)){
            const auto utf8=std::filesystem::path(path).u8string();
            report<<" "<<std::string(reinterpret_cast<const char*>(utf8.data()),utf8.size())
                <<"+0x"<<(address-reinterpret_cast<uintptr_t>(info.AllocationBase));
        }
    }else report<<" (no loaded image owns this address)";
}
std::string DescribeSurvivorFailure(const SurvivorSignatureFailure& failure){
    std::ostringstream report;
    report<<"Captured before Survivors hooks were installed.\nSignature: D2R.exe+0x"<<std::hex<<failure.rva<<" at ";
    DescribeSurvivorAddress(report,failure.address);
    report<<"\nRead: "<<std::dec<<failure.bytes.size()<<'/'<<failure.size<<" bytes; Win32 error "<<failure.error<<".\nExpected FNV-1a64: "
        <<std::hex<<std::setfill('0')<<std::setw(16)<<failure.expected;
    if(failure.complete)report<<"\nObserved FNV-1a64: "<<std::setw(16)<<LegacyTransmog::Data::Hash(failure.bytes);
    report<<"\nCaptured bytes:";
    for(const auto byte:failure.bytes)report<<' '<<std::setw(2)<<unsigned(byte);
    report<<'\n';
    uintptr_t address=failure.address;auto bytes=failure.bytes;std::set<uintptr_t> visited{address};
    for(int hop=0;hop<4;++hop){
        uintptr_t target=0;
        if(!SurvivorEntryJump(address,bytes,target)){
            report<<(hop ? "End of resolved entry-jump chain.\n" : "No standard entry jump could be resolved from the captured bytes.\n");break;
        }
        report<<"Entry jump "<<std::dec<<(hop+1)<<": ";DescribeSurvivorAddress(report,target);report<<'\n';
        if(!visited.insert(target).second){report<<"Stopped at repeated jump target.\n";break;}
        std::array<uint8_t,16> next{};
        if(!Read(target,next)){report<<"Target bytes could not be read.\n";break;}
        report<<"Target bytes:"<<std::hex;
        for(const auto byte:next)report<<' '<<std::setw(2)<<unsigned(byte);
        report<<'\n';address=target;bytes.assign(next.begin(),next.end());
        if(hop==3)report<<"Stopped at four entry jumps.\n";
    }
    return report.str();
}
bool NearCharsi(Unit* player){
    if(!player || Dead(player) || Level(player)!=1 || !player->pDynamicPath)return false;
    const int id=LookupName(0x1c9f890,"charsi");if(id<0)return false;
    auto source=GetRoom(player);const auto p=Location(player);std::set<Room*> seen;
    for(int dx:{-12,0,12})for(int dy:{-12,0,12}){
        auto room=Native<Room*(__fastcall*)(Room*,int,int)>(0x2232c0)(source,p.x+dx,p.y+dy);
        if(!room || !seen.insert(room).second)continue;
        for(auto npc:RoomUnits(room))if(npc->dwUnitType==1 && int(npc->dwClassId)==id && !Dead(npc)){
            const auto q=Location(npc);const int64_t x=q.x-p.x,y=q.y-p.y;
            if(q.level==1 && x*x+y*y<=144)return true;
        }
    }return false;
}
void SurvivorSend(Client* client,const Survivors::SnapshotBody& data){
    Survivors::Snapshot packet;packet.data=data;void* range[]{&packet,reinterpret_cast<uint8_t*>(&packet)+sizeof(packet)};
    Native<void(__fastcall*)(void*,uint32_t,void**)>(0x422ac0)(Native<void*>(0x18682b0),client->dwClientId,range);
}
void SurvivorPublishStats(SurvivorAvatar& avatar){
    // The normal world update publishes dirty character stats, but it is
    // intentionally stopped during allocation. Publish the authoritative base
    // values now: the native '+' buttons check CLIENT base statpts before they
    // will send their normal allocation request.
    for(uint16_t stat:{uint16_t(0),uint16_t(1),uint16_t(2),uint16_t(3),uint16_t(4),uint16_t(12)})
        Native<void(__fastcall*)(Client*,uint16_t,uint32_t)>(0x2cb670)(avatar.client,stat,uint32_t(SurvivorBaseStat(avatar.avatar,stat)));
    // Native vitals/cache synchronization, including class-specific life,
    // mana and stamina changes from attributes. Both arguments are units.
    Native<void(__fastcall*)(Unit*,Unit*,int)>(0x2aaa00)(avatar.avatar,avatar.avatar,0);
}
void SendSurvivorState(Game* game,Run& run,Client* client){
    if(run.event!=Event::Survivors || !run.returns.contains(client->dwUnitGUID))return;
    auto& build=run.survival.builds[client->dwUnitGUID];
    if(auto a=AvatarFor(game,client->pPlayer)){
        if(SurvivorCaptureAttributes(*a,build))SurvivorApplyLevel(*a,build,false);
        if(build.allocating && run.eventPhase==EventPhase::Active && Level(a->avatar)==run.level)SurvivorPublishStats(*a);
    }
    D2RHUD_CubeMapDiagnostics.survivorWave=run.survival.wave;D2RHUD_CubeMapDiagnostics.survivorKills=build.kills;
    Survivors::SnapshotBody p;p.run=run.id;p.session=uint32_t(game->dwInitSeed);p.recipient=client->dwUnitGUID;p.sequence=run.sequence;p.phase=run.eventPhase;
    p.xp=build.xp;p.requiredXP=Survivors::RequiredXP(build.level);p.kills=build.kills;p.wave=run.survival.wave;p.level=build.level;p.ranks=build.ranks;p.classId=build.classId;
    p.choices=build.choices;p.offer=build.offer;
    p.attributes=build.attributes;p.allocating=build.allocating;
    if(auto a=AvatarFor(game,client->pPlayer))for(size_t i=0;i<4;++i)p.totalAttributes[i]=SurvivorBaseStat(a->avatar,int(i));
    for(const auto& [id,b]:run.survival.builds)if(b.Paused())if(auto player=Lookup(game,0,id);player && !Dead(player) && Level(player)==run.level){p.paused=1;break;}
    SurvivorSend(client,p);
}
bool SurvivorCommand(Game* game,Unit* player,const uint8_t* bytes,size_t size){
    if(size!=sizeof(Survivors::Request) || !bytes)return false;
    Survivors::Request request;memcpy(&request,bytes,sizeof(request));const auto& p=request.data;
    if(p.opcode!=3 || p.magic!=PacketMagic || p.operation!=Survivors::Operation)return false;
    if(!Survivors::Valid(p) || !installed.load() || !game || !player || player->dwUnitType || !player->pPlayerData || Dead(player))return true;
    std::lock_guard lock(serverMutex);
    auto client=Field<Client*>(player->pPlayerData,0xf0);
    if(!client || client->pGame!=game || client->dwClientState!=4 || client->dwUnitGUID!=player->dwUnitId || Lookup(game,0,player->dwUnitId)!=player)return true;
    auto& state=State(game);
    if(p.action==Survivors::Action::Start){
        Survivors::Failure failure=Survivors::Failure::None;
        const int mapState=MapState();
        const bool joining=state.run && state.run->event==Event::Survivors &&
            (state.run->eventPhase==EventPhase::Briefing || state.run->eventPhase==EventPhase::Entering || state.run->eventPhase==EventPhase::Active);
        if(!SurvivorsEnabled())failure=Survivors::Failure::Disabled;
        else if(state.run && (!joining || !Town(Level(player)) || AvatarFor(game,player)))failure=Survivors::Failure::Busy;
        else if(!survivorsAvailable)failure=Survivors::Failure::Native;
        else if(!NearCharsi(player))failure=Survivors::Failure::Npc;
        else if(mapState<0)failure=Survivors::Failure::State;
        else if(Field<uint8_t>(player,0xb0) || (player->pInventory && Field<Unit*>(player->pInventory,0x40)))failure=Survivors::Failure::Interaction;
        if(failure!=Survivors::Failure::None){Survivors::SnapshotBody reply;reply.recipient=player->dwUnitId;reply.session=uint32_t(game->dwInitSeed);reply.failure=failure;SurvivorSend(client,reply);return true;}
        Survivors::Build build;build.Load(p.profile,p.classId);
        const auto town=Location(player);
        auto avatar=CreateSurvivor(game,player,build);
        if(!avatar){Survivors::SnapshotBody reply;reply.recipient=client->dwUnitGUID;reply.session=uint32_t(game->dwInitSeed);reply.failure=Survivors::Failure::Avatar;SurvivorSend(client,reply);return true;}
        if(joining){
            auto& run=*state.run;run.returns[avatar->dwUnitId]=town;run.survival.builds[avatar->dwUnitId]=build;
            if(run.eventPhase==EventPhase::Active)Native<void(__fastcall*)(Game*,Unit*,int,int)>(0x2e16d0)(game,avatar,run.level,11);
            Sync(game,run,true);return true;
        }
        auto run=std::make_unique<Run>();run->id=++serial;if(!run->id)run->id=++serial;
        run->creator=avatar->dwUnitId;run->characterLevel=build.level;
        run->level=39;run->state=mapState;run->created=run->eventFrame=game->dwGameFrame;run->entrance=town;
        run->returns.emplace(avatar->dwUnitId,town);run->survival.builds.emplace(avatar->dwUnitId,build);
        run->random.seed(uint32_t(game->dwInitSeed)^game->dwGameFrame^(run->id*0x85ebca6b));
        run->event=Event::Survivors;run->eventPhase=EventPhase::Briefing;run->phase=Phase::Closed;
        state.run=std::move(run);Sync(game,*state.run,true);status.store("Sanctuary Survivors: the arena opens after the briefing.");return true;
    }
    auto run=Active(game);
    if(!run || run->event!=Event::Survivors || run->eventPhase!=EventPhase::Active || p.run!=run->id ||
       p.session!=uint32_t(game->dwInitSeed) || !run->returns.contains(player->dwUnitId) || Level(player)!=run->level)return true;
    auto& build=run->survival.builds[player->dwUnitId];
    if(p.action==Survivors::Action::Choose){
        if(auto a=AvatarFor(game,player))SurvivorCaptureAttributes(*a,build);
        if(build.Choose(p.offer,p.slot)){
            build.allocating=true;
            if(auto a=AvatarFor(game,player))SurvivorApplyLevel(*a,build,true);
            if(auto it=run->players.find(player->dwUnitId);it!=run->players.end()){Detach(player,it->second,run->state);run->players.erase(it);}
            build.Offer(run->random,game->dwGameFrame);Sync(game,*run,true);
        }
    }else if(p.action==Survivors::Action::Allocate){
        if(build.allocating && !build.Pending() && p.offer==build.offer){
            if(auto a=AvatarFor(game,player)){
                SurvivorCaptureAttributes(*a,build);
                const unsigned spent=unsigned(build.level-1)*5-build.Unspent();
                if(p.spent==spent && p.points<=build.Unspent() && SurvivorBaseStat(player,4)>=p.points){
                    // The host owns the budget; use the game's normal spending
                    // function for class-derived gains, without a world tick.
                    for(unsigned i=0;i<p.points;++i)if(!Native<int(__fastcall*)(Unit*,int)>(0x320fa0)(player,p.slot))break;
                    if(SurvivorCaptureAttributes(*a,build))SurvivorApplyLevel(*a,build,false);
                }
            }
            Sync(game,*run,true);
        }
    }else if(p.action==Survivors::Action::Ready){
        if(build.allocating && p.offer==build.offer && !build.Pending()){
            if(auto a=AvatarFor(game,player);a && SurvivorCaptureAttributes(*a,build))SurvivorApplyLevel(*a,build,false);
            build.allocating=false;build.Offer(run->random,game->dwGameFrame);Sync(game,*run,true);
        }
    }else if(p.action==Survivors::Action::Leave){
        // A participant can leave without ending the shared run for the party.
        if(auto it=run->players.find(player->dwUnitId);it!=run->players.end()){Detach(player,it->second,run->state);run->players.erase(it);}
        if(Move(game,player,run->returns.at(player->dwUnitId))){Sync(game,*run,true);RestoreSurvivor(game,player);}
        Sync(game,*run,true);
    }return true;
}
bool ReceiveSurvivor(const uint8_t* bytes,size_t size){
    if(size!=sizeof(Survivors::Snapshot))return false;Survivors::Snapshot packet;memcpy(&packet,bytes,size);
    if(!Survivors::Valid(packet.data))return false;
    const auto& p=packet.data;bool force=false;
    {
    std::lock_guard lock(viewMutex);
    if(p.failure==Survivors::Failure::None && (p.run!=view.run || p.session!=view.session || p.recipient!=view.recipient || view.event!=Event::Survivors))return false;
    if(survivorReceived && p.failure==Survivors::Failure::None && p.run==survivorView.run && p.session==survivorView.session && int32_t(p.sequence-survivorView.sequence)<=0)return false;
    survivorView=p;survivorReceived=GetTickCount64();
    force=p.phase!=EventPhase::Active || !view.active;
    }
    SaveSurvivorProfile(p,force);
    return true;
}
void SurvivorRefreshBuff(Game* game,Run& run,Unit* player){
    if(auto it=run.players.find(player->dwUnitId);it!=run.players.end()){Detach(player,it->second,run.state);run.players.erase(it);}
}
void SurvivorNotifyEffect(Game* game,Unit* player,Unit* target,int skill,int level){
    const auto origin=Location(player);
    auto client=game->pClientList;
    for(size_t n=0;client && n<64;++n,client=client->pNext){
        if(client->pGame!=game || client->dwClientState!=4)continue;
        auto recipient=Lookup(game,0,client->dwUnitGUID);
        if(!recipient || Level(recipient)!=origin.level)continue;
        // Native triggered-effect packet 0x99 (0x9a when the target is outside
        // the recipient's loaded rooms). It runs skill VFX without entering a
        // player cast mode, including on the owning client.
        Native<void(__fastcall*)(Client*,uint8_t,uint32_t,uint8_t,uint32_t,uint16_t,uint16_t,uint16_t,uint16_t,uint8_t,int)>(0x2ce6c0)(
            client,0,player->dwUnitId,uint8_t(target->dwUnitType),target->dwUnitId,uint16_t(skill),uint16_t(origin.x),uint16_t(origin.y),0,uint8_t(level),1);
    }
}
bool SurvivorCast(Game* game,Unit* player,Unit* monster,const Survivors::Build& build,size_t weapon){
    if(!monster || !player->pDynamicPath || build.classId>=Survivors::Classes.size() || weapon>=3 || (player->dwAnimMode!=1 && player->dwAnimMode!=2 && player->dwAnimMode!=3))return false;
    const int id=SurvivorSkillId(build.classId,weapon);
    auto skill=Native<void*(__fastcall*)(Unit*,int)>(0x218ce0)(player,id);if(!skill)return false;
    if(!SurvivorMobileSkill(Field<void*>(skill,0)))return false;
    const int level=Field<int>(skill,0x38);if(level<=0)return false;
    UnitStat(player,8,Stat(player,9));
    // Throwing stacks and durability belong only to this temporary avatar.
    // Keep class weapons usable for the full run, without touching hero gear.
    if(AvatarFor(game,player) && player->pInventory){
        size_t count=0;for(auto item=player->pInventory->pFirstItem;item && count++<256 && item->dwUnitType==4 && item->pItemData;item=item->pItemData->pExtraData.pNextItem){
            if(item->dwItemMode!=1)continue;
            const int quantity=Stat(item,70),durability=Stat(item,72),maximum=Stat(item,73);
            if(quantity>0 && quantity<50)UnitStat(item,70,50);
            if(maximum>0 && durability<maximum)UnitStat(item,72,maximum);
        }
    }
    const auto list=Field<void*>(player,0x100);
    if(!list)return false;
    ++D2RHUD_CubeMapDiagnostics.survivorAttempts;
    int result=0;
    {
        struct TargetScope {
            Unit* unit;void* list;void* used;uint32_t didEffect;
            std::array<uint8_t,12> aim;std::array<uint8_t,16> target;
            TargetScope(Unit* u,void* l):unit(u),list(l),used(Field<void*>(l,0x18)),didEffect(u->dwFlags&0x40){
                memcpy(aim.data(),reinterpret_cast<uint8_t*>(u->pDynamicPath)+0x10,aim.size());
                memcpy(target.data(),reinterpret_cast<uint8_t*>(u->pDynamicPath)+0x70,target.size());
            }
            ~TargetScope(){
                memcpy(reinterpret_cast<uint8_t*>(unit->pDynamicPath)+0x10,aim.data(),aim.size());
                memcpy(reinterpret_cast<uint8_t*>(unit->pDynamicPath)+0x70,target.data(),target.size());
                Set(list,0x18,used);unit->dwFlags=(unit->dwFlags&~0x40u)|didEffect;
            }
        } scope(player,list);
        Set(list,0x18,skill);
        Native<void(__fastcall*)(Unit*,Unit*)>(0x2095a0)(player,monster);
        const auto destination=Location(monster);
        Set(player->pDynamicPath,0x10,uint16_t(destination.x));Set(player->pDynamicPath,0x12,uint16_t(destination.y));
        // Prepare native damage, then execute its action immediately. Neither
        // entry point starts a player animation or replaces the movement path.
        // Do not use ItemEffect overrides: mod skills may omit that column or
        // map it to a different spell than their normal skill effect.
        if(Native<int(__fastcall*)(Game*,Unit*,int,int,int)>(0x298550)(game,player,id,level,1))
            result=Native<int(__fastcall*)(Game*,Unit*,int,int,int,int,int)>(0x298810)(game,player,id,level,0,1,0);
    }
    if(result){++D2RHUD_CubeMapDiagnostics.survivorEffects;SurvivorNotifyEffect(game,player,monster,id,level);}
    return result!=0;
}
Room* SurvivorPosition(Run& run,const Position& position){
    for(const auto pin:run.pins)if(auto room=Field<Room*>(pin.room,0x58)){
        const int x=Field<int>(room,0x80),y=Field<int>(room,0x84),w=Field<int>(room,0x88),h=Field<int>(room,0x8c);
        if(position.x>=x && position.x<x+w && position.y>=y && position.y<y+h)return room;
    }return nullptr;
}
Unit* SurvivorSpawn(Game* game,Run& run,Unit* player,int monster,bool elite){
    const auto origin=Location(player);
    for(int attempt=0;attempt<12;++attempt){
        const float angle=std::uniform_real_distribution<float>(0,6.2831853f)(run.random);
        const int radius=std::uniform_int_distribution<int>(14,24)(run.random);
        Position p{run.level,origin.x+int(cosf(angle)*radius),origin.y+int(sinf(angle)*radius)};
        if(auto room=SurvivorPosition(run,p))if(auto unit=SpawnEncounter(game,run,room,p,{monster,-1})){
            run.objectives.insert(unit->dwUnitId);EnsureMonsterBuff(unit,run);
            run.census.Observe(unit->dwUnitId,false);
            // Run XP is separate from the character's saved experience.
            UnitStat(unit,13,0);UnitStat(unit,74,0);UnitStat(unit,12,run.monsterLevel);
            run.levelsFinalized.insert(unit->dwUnitId);
            // Hordes start with shorter fights and grow tougher over time.
            // Immunities cannot make an early single-weapon run unwinnable.
            const uint32_t seconds=(game->dwGameFrame-run.eventStarted)/25;
            const int maximum=(std::max)(256,int(std::min<int64_t>(INT_MAX,int64_t(Stat(unit,7))*(25+(std::min)(seconds,540u)/6)*(elite?4:1)/100)));
            UnitStat(unit,7,maximum);UnitStat(unit,6,maximum);
            run.survival.life[unit->dwUnitId]=maximum;
            if(elite)run.survival.elites.insert(unit->dwUnitId);
            return unit;
        }
    }return nullptr;
}
void SurvivorReturn(Game* game,Run& run,bool success){
    run.success=success;run.timedOut=!success;run.phase=Phase::Returning;run.eventPhase=EventPhase::Returning;run.eventFrame=game->dwGameFrame;
    for(const auto& [id,p]:run.returns)run.returning.insert(id);
}
void ProcessSurvivors(Game* game,Run& run,const std::vector<Unit*>& connected){
    using namespace Survivors;
    const uint32_t frame=game->dwGameFrame,seconds=(frame-run.eventStarted)/25;
    std::vector<Unit*> players;for(auto p:connected)if(!Dead(p) && Level(p)==run.level && run.ready.contains(p->dwUnitId))players.push_back(p);
    if(players.empty() || seconds>=Duration){SurvivorReturn(game,run,false);return;}
    // Bound work and network churn independently of render rate.
    if(frame%5)return;
    std::vector<uint32_t> living;
    for(const auto pin:run.pins)for(auto unit:RoomUnits(Field<Room*>(pin.room,0x58))){
        if(!RunMonster(unit,run))continue;
        const auto id=unit->dwUnitId;
        const int life=(std::max)(0,Stat(unit,6));
        if(auto old=run.survival.life.find(id);old!=run.survival.life.end()){
            if(life<old->second)D2RHUD_CubeMapDiagnostics.survivorDamage.fetch_add(uint32_t(old->second-life));
            old->second=life;
        }
        run.census.Observe(id,Dead(unit));
        if(Dead(unit)){
            if(run.survival.credited.insert(id).second){
                const unsigned xp=id==run.survival.boss ? 100 : run.survival.elites.contains(id) || (unit->pMonsterData->nTypeFlag&0x1e) ? 10 : 3;
                for(auto p:players){auto& b=run.survival.builds[p->dwUnitId];b.xp=(std::min)(1000000u,b.xp+xp);++b.kills;}
            }
            run.survival.corpses.try_emplace(id,frame);
        }else living.push_back(id);
    }
    // On-death callbacks can remove a corpse immediately; RemoveHook records
    // the observed death in the census before that native allocation is freed.
    for(const auto [id,dead]:run.census.dead)if(dead && run.survival.credited.insert(id).second)
        for(auto p:players){auto& b=run.survival.builds[p->dwUnitId];b.xp=(std::min)(1000000u,b.xp+(id==run.survival.boss?100u:run.survival.elites.contains(id)?10u:3u));++b.kills;}
    if(run.survival.bossSpawned && run.survival.credited.contains(run.survival.boss) && living.empty() && run.census.Complete()){
        if(!run.clearedAt)run.clearedAt=frame;
        if(frame-*run.clearedAt>=50){SurvivorReturn(game,run,true);return;}
    }else run.clearedAt.reset();
    if(seconds>=FinalBossAt && !run.survival.bossSpawned){
        const int boss=LookupName(0x1c9f890,"diablo");
        if(EncounterCandidate(boss))if(auto unit=SurvivorSpawn(game,run,players.front(),boss,true)){run.survival.boss=unit->dwUnitId;run.survival.bossSpawned=true;}
    }else if(seconds<FinalBossAt && frame>=run.survival.nextWave && !run.roster.empty()){
        run.survival.nextWave=frame+100;++run.survival.wave;
        const auto count=std::min<size_t>(WaveSize(seconds,unsigned(players.size())),living.size()<MaxLiving ? MaxLiving-living.size() : 0);
        for(size_t n=0;n<count;++n)SurvivorSpawn(game,run,players[n%players.size()],RandomMonster(run),run.survival.wave%8==0 && n==0);
    }
    for(auto player:players){
        auto& b=run.survival.builds[player->dwUnitId];b.Offer(run.random,frame);
    }
    // Freeze before any more automatic effects after a level offer appears.
    for(auto player:players)if(run.survival.builds[player->dwUnitId].Paused())return;
    for(auto player:players){
        auto& b=run.survival.builds[player->dwUnitId];
        const auto origin=Location(player);std::vector<std::pair<int64_t,uint32_t>> targets;
        for(auto id:living)if(auto unit=Lookup(game,1,id);unit && !Dead(unit) && RunMonster(unit,run)){
            const auto p=Location(unit);const int64_t x=p.x-origin.x,y=p.y-origin.y;targets.push_back({x*x+y*y,id});
        }
        std::sort(targets.begin(),targets.end());
        for(size_t weapon=0;weapon<3 && !Dead(player);++weapon)if(b.ranks[weapon] && frame>=b.nextAttack[weapon]){
            int range=Range(b,weapon);
            if(auto skill=Native<void*(__fastcall*)(Unit*,int)>(0x218ce0)(player,SurvivorSkillId(b.classId,weapon))){
                const int effect=Field<int16_t>(Field<void*>(skill,0),0x4e);
                if(effect==117)range=8+2*b.ranks[size_t(Upgrade::Reach)];
                else if(effect==22)range=10+2*b.ranks[size_t(Upgrade::Reach)];
            }
            for(const auto [distance,id]:targets){
                if(distance>range*range || Dead(player))break;
                if(auto unit=Lookup(game,1,id);unit && !Dead(unit) && Level(unit)==run.level && SurvivorCast(game,player,unit,b,weapon)){
                    b.nextAttack[weapon]=frame+Cooldown(b,weapon);break;
                }
            }
        }
        if(frame%25==0){const int rank=b.ranks[size_t(Upgrade::Mending)];if(rank){const int life=Stat(player,6),maximum=Stat(player,7);if(life>0 && maximum>life)UnitStat(player,6,int(std::min<int64_t>(maximum,int64_t(life)+rank*256)));}}
    }
    // Expired corpses cannot accumulate for all ten minutes. Loot is a separate
    // unit and is retained; live/revived monsters are never removed here.
    for(auto it=run.survival.corpses.begin();it!=run.survival.corpses.end();)if(frame-it->second>=125){
        const auto id=it->first;if(auto unit=Lookup(game,1,id);unit && Dead(unit) && Level(unit)==run.level){run.census.Observe(id,true);run.monsters.erase(id);originalRemove(game,unit);}
        run.survival.life.erase(id);
        it=run.survival.corpses.erase(it);
    }else ++it;
}
