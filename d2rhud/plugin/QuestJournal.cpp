#include "QuestJournal.h"
#include "QuestJournalData.h"
#include "QuestJournalStorage.h"
#include "CubeMap.h"
#include "CompiledNames.h"
#include "../Pattern.h"
#include "../d2/Reclass.h"
#include <atomic>
#include <mutex>
#include <map>

namespace QuestJournal {
namespace {
using Game=D2GameStrc;using Unit=D2UnitStrc;using Client=D2ClientStrc;
template<class T>T Native(uint32_t rva){return reinterpret_cast<T>(Pattern::Address(rva));}
template<class T>T Field(const void* p,size_t offset=0){T value{};SIZE_T read=0;if(p)ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const uint8_t*>(p)+offset,&value,sizeof(value),&read);return read==sizeof(value)?value:T{};}
std::atomic<std::shared_ptr<const Rules>> definitions;
std::atomic<bool> available{false};std::atomic<ProofRecovery> recover{nullptr};std::atomic<RewardClaim> claimReward{nullptr};
using Key=std::pair<Game*,uint32_t>;
struct Loaded {Data::Identity identity;Data::Checkpoint point;};
struct Session {
    Unit* player=nullptr;Data::History history;Data::State state;std::filesystem::path path;
    uint32_t sequence=1,epoch=0;bool blocked=false,dirty=false,busy=false,awaiting=true,storageBlocked=false;RecipeGate transaction;
    uint64_t acknowledged=0;bool hasAcknowledgment=false;Data::State published,acknowledgedState;std::optional<Data::State> legacy;
    std::string message;std::set<uint32_t> weapons;
};
std::mutex mutex,viewMutex,storageMutex;
std::unique_ptr<Data::Store> clientStore;uint32_t storageSession=0,storageRecipient=0;std::map<Key,Session> sessions;std::map<Client*,Loaded> loading;
std::map<Game*,std::set<uint32_t>> killed;
std::map<Game*,uint64_t> sent;std::filesystem::path directory;View clientView;uint64_t received=0;
std::string configurationStatus="Quest journal has not been configured.";
std::atomic<uint32_t> epochSerial{uint32_t(GetTickCount64())};
Unit* LocalPlayer(){const auto selector=Field<uint32_t>(Native<void*>(0x1d442d8));return selector<8?Native<Unit*(__fastcall*)(uint32_t)>(0x6b050)(selector):nullptr;}
Client* Owner(Game* game,Unit* player){
    if(!game||!player||player->dwUnitType||!player->pPlayerData)return nullptr;
    auto client=Field<Client*>(player->pPlayerData,0xf0);
    return client&&client->pGame==game&&client->dwUnitGUID==player->dwUnitId&&client->dwClientState==4?client:nullptr;
}
void Message(Session& s,std::string value){s.message=std::move(value);++s.sequence;}
bool Safe(Game* game,Unit* player){return Owner(game,player)&&!(player->dwFlags&0x10000)&&player->dwAnimMode!=0&&player->dwAnimMode!=17;}
int ItemStat(Unit* item,int stat){return Native<int(__fastcall*)(Unit*,uint32_t,uint16_t)>(0x1e5620)(item,stat,0);}
uint32_t Code(Unit* item){auto table=Field<const uint8_t*>(Native<void*>(0x1ca0390));auto count=Field<uint64_t>(Native<void*>(0x1ca0398));return table&&count<1000000&&item&&item->dwClassId<count?Field<uint32_t>(table+item->dwClassId*0x1b4,0x80):0;}
int LegacyCompleted(Unit* player,int count){
    if(!player||!player->pInventory)return 0;
    const auto uniqueCount=Field<uint64_t>(Native<void*>(0x1c9fc78));if(!uniqueCount || uniqueCount>65536)return 0;
    std::array<int,4> ids{};for(int n=0;n<4;++n){
        const auto name="Barrier"+std::to_string(n+1);ids[n]=CompiledNames::Resolve(0x1c9fc68,name.c_str());
        if(ids[n]<0 || uint64_t(ids[n])>=uniqueCount)ids[n]=-1;
    }
    int result=0;std::set<Unit*> seen;
    for(auto item=player->pInventory->pFirstItem;item&&seen.size()<4096;){
        if(!seen.insert(item).second||item->dwUnitType!=4||!item->pItemData)break;
        const auto data=item->pItemData;
        if(data->pExtraData.pParentInv==player->pInventory&&data->dwQualityNo==7&&int(data->dwFileIndex)>=0&&std::find(ids.begin(),ids.end(),int(data->dwFileIndex))!=ids.end()){
            const int stage=ItemStat(item,398);if(stage>=1&&stage<=count+1)result=(std::max)(result,stage-1);
        }
        item=data->pExtraData.pNextItem;
    }return result;
}
bool HasProof(Unit* player,int quest){
    if(quest<1||quest>99||!player||!player->pInventory)return true;
    const auto name="Bt"+(quest<10?std::string("0"):std::string{})+std::to_string(quest);uint32_t code=0;memcpy(&code,name.data(),4);
    std::set<Unit*> seen;for(auto item=player->pInventory->pFirstItem;item&&seen.size()<4096;){if(!seen.insert(item).second||item->dwUnitType!=4||!item->pItemData)return true;auto data=item->pItemData;if(data->pExtraData.pParentInv==player->pInventory&&Code(item)==code)return true;item=data->pExtraData.pNextItem;}return false;
}
#include "QuestJournalEvents.inl"
bool Durable(const Session& s){
    // New kills/evidence need not invalidate an already saved claim boundary.
    // A change to the active objective or claimed stage always needs a new ACK.
    const auto& saved=s.acknowledgedState;
    return !s.awaiting&&!s.storageBlocked&&s.hasAcknowledgment&&saved.completed==s.state.completed&&
        saved.objectiveReady==s.state.objectiveReady&&saved.legacyClosed==s.state.legacyClosed&&saved.uncertain==s.state.uncertain;
}
void Send(Game* game,Session& s,const Rules& rules){
    auto client=Owner(game,s.player);if(!client)return;
    if(!s.awaiting&&s.published!=s.state){
        if(s.history.revision==UINT64_MAX){s.storageBlocked=true;Message(s,"Quest revision limit reached.");}
        else {++s.history.revision;s.published=s.state;Data::Append(s.history,{0,0,s.state});}
    }
    Data::Packet packet;packet.recipient=s.player->dwUnitId;packet.session=s.epoch;packet.sequence=s.sequence;packet.definition=rules.digest;
    packet.count=uint8_t((std::max)(int(rules.quests.size()),s.state.completed));packet.completed=uint8_t(s.state.completed);packet.ready=s.state.objectiveReady;packet.kills=s.state.kills;packet.evidence=s.state.evidence;
    packet.flags=uint8_t((s.blocked||s.storageBlocked||s.awaiting||s.state.uncertain?1:0)|(!s.state.legacyClosed&&!s.state.completed&&!s.state.objectiveReady&&!s.awaiting?2:0)|(s.dirty?4:0)|(s.state.uncertain?8:0)|(s.awaiting?16:0));
    if(packet.flags&2)packet.legacyCompleted=uint8_t(LegacyCompleted(s.player,packet.count));
    packet.revision=s.history.revision;packet.menus=s.state.menus;packet.classId=uint8_t(s.history.identity.classId);packet.characterFlags=uint8_t(s.history.identity.flags&0x24);
    packet.events=uint8_t((s.state.countess?1:0)|(s.state.thul?2:0)|(s.state.legacyClosed?4:0));
    strncpy_s(packet.character,s.history.identity.name.c_str(),_TRUNCATE);strncpy_s(packet.message,s.message.c_str(),_TRUNCATE);
    void* range[]{&packet,reinterpret_cast<uint8_t*>(&packet)+sizeof(packet)};
    Native<void(__fastcall*)(void*,uint32_t,void**)>(0x422ac0)(Native<void*>(0x18682b0),client->dwClientId,range);
}
void Submit(uint8_t action,const View& view,uint8_t detail=0){
    auto player=LocalPlayer();if(!player||player->dwUnitId!=view.recipient)return;
    Data::CommandPacket command;command.recipient=view.recipient;command.session=view.session;command.sequence=view.sequence;command.action=action;command.detail=detail;command.proposal=uint8_t(action>=4?view.completed+1:view.legacyCompleted);
    Native<void(__fastcall*)(void*,int)>(0x10dce0)(&command,sizeof(command));
}
}
void Configure(const std::filesystem::path& install,const std::string& mod,const std::string& source,const std::string& config){
    if(!SupportsMod(mod)){
        std::lock_guard lock(mutex);definitions.store(nullptr);available=false;directory.clear();sessions.clear();loading.clear();killed.clear();sent.clear();
        configurationStatus="Quest journal is restricted to the RMD-MP mod.";return;
    }
    auto rules=Data::ParseRules(source,config);std::lock_guard lock(mutex);
    (void)install;wchar_t local[32768]{};const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,DWORD(std::size(local)));
    directory=length&&length<std::size(local)?std::filesystem::path(local)/L"D2RHUD"/L"Quests":std::filesystem::path{};
    configurationStatus=rules->error.empty()?"Per-character quests configured ("+std::to_string(rules->quests.size())+" quests).":rules->error;
    definitions.store(std::move(rules));
}
std::shared_ptr<const Rules> Bindings(){return definitions.load();}
void SetAvailable(bool enabled){const auto rules=definitions.load();available=enabled&&rules&&rules->error.empty();}
void SetProofRecovery(ProofRecovery value){recover=value;}
void SetRewardClaim(RewardClaim value){claimReward=value;}
std::string Status(){
    std::lock_guard lock(mutex);const auto path=directory.u8string();const auto rules=definitions.load();
    const bool valid=rules&&rules->error.empty();
    return configurationStatus+(valid&&!rules->events?" Install version 2 D2RHUD/quests.json.":"")+
        (valid&&!available.load()?" Quest hooks are unavailable.":"")+
        (directory.empty()?" Quest storage path is unavailable.":" Companion folder: "+std::string(path.begin(),path.end()));
}
Progress Inspect(Game* game,Unit* player){std::lock_guard lock(mutex);auto rules=definitions.load();if(!available||!rules||!rules->error.empty()||!player)return {};auto it=sessions.find({game,player->dwUnitId});if(it==sessions.end()||it->second.player!=player)return {};const auto& s=it->second;return {!s.awaiting&&!s.blocked&&!s.storageBlocked&&!s.busy&&!s.state.uncertain,s.state.completed,s.state.objectiveReady};}
void Refused(Game* game,Unit* player,const std::string& message){std::lock_guard lock(mutex);if(!player)return;if(auto it=sessions.find({game,player->dwUnitId});it!=sessions.end()){Message(it->second,message);sent.erase(game);}}
bool Begin(Game* game,Unit* player,const RecipeGate& gate){
    std::lock_guard lock(mutex);const auto rules=definitions.load();if(!available||!rules||!rules->error.empty()||!Safe(game,player))return false;
    auto it=sessions.find({game,player->dwUnitId});if(it==sessions.end())return false;auto& s=it->second;
    if(s.player!=player||s.blocked||s.busy||!Durable(s)||!Data::Allows(s.state,gate,int(rules->quests.size())))return false;
    const auto binding=std::find_if(rules->gates.begin(),rules->gates.end(),[&](const auto& g){return g.row==gate.row&&g.quest==gate.quest&&g.turnIn==gate.turnIn;});if(binding==rules->gates.end())return false;
    s.busy=true;s.transaction=gate;return true;
}
void Finish(Game* game,Unit* player,const RecipeGate& gate,Outcome outcome){
    std::lock_guard lock(mutex);auto rules=definitions.load();if(!rules||!player)return;auto it=sessions.find({game,player->dwUnitId});if(it==sessions.end())return;auto& s=it->second;
    if(s.player!=player||!s.busy||s.transaction.row!=gate.row)return;s.busy=false;
    if(outcome==Outcome::Committed&&RecipeObjective(*rules,gate)){
        Evidence(s,gate.quest);AdvanceEvents(s,*rules);
    }
    else if(outcome==Outcome::Committed&&Data::Advance(s.state,gate,int(rules->quests.size()))){s.dirty=true;Message(s,gate.turnIn?"Quest "+std::to_string(gate.quest)+" reward claimed.":"Objective complete.");}
    else if(outcome==Outcome::Ambiguous){s.state.uncertain=true;s.state.legacyClosed=true;s.dirty=true;s.blocked=true;Message(s,"Quest output incomplete. Progress locked to prevent a duplicate reward; restore a matching backup.");}
    else Message(s,"Quest recipe did not complete. Free storage space and try again.");
    Send(game,s,*rules);sent.erase(game);
}
void BeforeLoad(Game*,Client* client){
    if(!definitions.load()||!client)return;const auto block=client->pSaveHeaders;const auto bytes=Field<const uint8_t*>(block);const auto size=Field<uint64_t>(block,8);Loaded value;
    const bool valid=Data::Header(bytes,size,value.identity,value.point);std::lock_guard lock(mutex);loading.erase(client);if(valid)loading.emplace(client,std::move(value));
}
void AfterLoad(Game* game,Client* client,Unit* player,bool success){
    std::lock_guard lock(mutex);auto loaded=loading.find(client);if(loaded==loading.end())return;auto value=std::move(loaded->second);loading.erase(loaded);
    auto rules=definitions.load();if(!success||!game||!player||player->dwUnitType||!rules||!rules->error.empty())return;
    Session s;s.player=player;s.epoch=++epochSerial;if(!s.epoch)s.epoch=++epochSerial;
    s.history.identity=value.identity;s.history.digest=rules->digest;
    // Older builds kept the guest's recent progress on the host. Recover that
    // history read-only, then transfer it back to its owner during the handshake.
    try{bool invalid=false;if(!directory.empty())s.legacy=Data::Store::LegacyProgress(directory,value.identity,invalid);}catch(...){}
    s.message="Loading this character's journal from the player's computer...";
    // The server owns gameplay validation, never a remote player's disk journal.
    sessions[{game,player->dwUnitId}]=std::move(s);sent.erase(game);
}
void Saved(Game* game,Unit* player,const uint8_t* bytes,size_t size){
    if(!game||!player)return;Data::Identity identity;Data::Checkpoint point;if(!Data::Header(bytes,size,identity,point))return;
    std::lock_guard lock(mutex);auto rules=definitions.load();auto it=sessions.find({game,player->dwUnitId});if(!rules||it==sessions.end()||it->second.player!=player)return;auto& s=it->second;
    if(!Data::SameCharacter(s.history.identity,identity))return;
    // Queue the durable journal snapshot before the native character-save packets.
    // The receiver flushes it synchronously before processing later game packets.
    Send(game,s,*rules);
}
void Removed(Game* game,Unit* player){if(!player||player->dwUnitType)return;std::lock_guard lock(mutex);auto it=sessions.find({game,player->dwUnitId});if(it!=sessions.end()&&it->second.player==player)sessions.erase(it);}
void Destroyed(Game* game){std::lock_guard lock(mutex);std::erase_if(sessions,[&](const auto& e){return e.first.first==game;});sent.erase(game);killed.erase(game);}
void Tick(Game* game){
    if(!available||!game)return;auto rules=definitions.load();if(!rules||!rules->error.empty())return;
    std::lock_guard lock(mutex);auto now=GetTickCount64();auto& last=sent[game];if(last&&now-last<750)return;last=now;
    for(auto& [key,s]:sessions)if(key.first==game){Observe(game,s,*rules);Send(game,s,*rules);}
}
bool Command(Game* game,Unit* player,const uint8_t* bytes,size_t size){
    if(!definitions.load())return false;
    if(!bytes||size<10||bytes[0]!=3||Field<uint64_t>(bytes,1)!=Data::Magic||bytes[9]!=Data::Operation)return false;
    if(size!=sizeof(Data::CommandPacket))return true;Data::CommandPacket command;memcpy(&command,bytes,size);
    const size_t payload=command.action==7?sizeof(Data::Transfer):command.action==8?sizeof(uint64_t):0;
    if(command.version!=Data::Version||command.action>9||command.detail>6||!std::all_of(command.padding.begin()+payload,command.padding.end(),[](uint8_t v){return !v;})||!available||!Safe(game,player)||command.recipient!=player->dwUnitId)return true;
    std::lock_guard lock(mutex);auto rules=definitions.load();auto it=sessions.find({game,player->dwUnitId});if(!rules||it==sessions.end())return true;auto& s=it->second;
    if(command.session!=s.epoch||s.player!=player)return true;
    if(command.action==7){
        if(!s.awaiting)return true;Data::Transfer transfer;memcpy(&transfer,command.padding.data(),sizeof(transfer));auto state=Data::Unpack(transfer);
        if(!state||transfer.revision==UINT64_MAX)return true;
        s.state=*state;s.published=s.state;s.acknowledgedState=s.state;s.hasAcknowledgment=true;
        s.history.revision=transfer.revision;s.acknowledged=transfer.revision;s.awaiting=false;
        if(s.legacy){
            // A legacy copy can restore missed progress, never reopen a claim.
            s.state=Data::Merge(s.state,*s.legacy);s.legacy.reset();
        }
        s.blocked=s.state.uncertain;Data::Append(s.history,{0,0,s.state});
        Message(s,s.blocked?"An incomplete reward transaction needs recovery; other character data is unaffected.":"Quest journal loaded from this character's computer.");
        AdvanceEvents(s,*rules);Send(game,s,*rules);sent.erase(game);return true;
    }
    if(command.action==8){
        uint64_t revision=0;memcpy(&revision,command.padding.data(),sizeof(revision));
        if(!s.awaiting&&revision==s.history.revision){
            if(s.storageBlocked){Message(s,"Quest journal saved. Rewards are available again.");sent.erase(game);}
            s.acknowledged=revision;s.acknowledgedState=s.published;s.hasAcknowledgment=true;s.storageBlocked=false;s.dirty=false;
        }return true;
    }
    if(command.action==9){if(command.sequence==s.sequence){s.storageBlocked=true;Message(s,"Quest progress could not be written on your computer. Rewards pause until it saves.");}return true;}
    if(command.action==0){Send(game,s,*rules);return true;}
    if(s.blocked||s.busy||s.state.uncertain||!Durable(s)||command.sequence>s.sequence)return true;
    if(command.action==1){
        const int proposal=LegacyCompleted(player,int(rules->quests.size()));
        if(command.proposal!=proposal||!Data::Import(s.state,proposal,int(rules->quests.size()))){Message(s,"Legacy import refused: carried statue or journal changed.");}
        else{s.dirty=true;Message(s,"Legacy progress imported. Earlier rewards are marked claimed; nothing was granted.");}
    }else if(command.action==2){
        if(!s.state.legacyClosed&&!s.state.completed&&!s.state.objectiveReady){s.state.legacyClosed=true;s.dirty=true;Message(s,"New per-character quest journal started.");AdvanceEvents(s,*rules);}
    }else if(command.action==3&&!rules->events&&s.state.objectiveReady&&s.state.completed<int(rules->quests.size())){
        if(HasProof(player,s.state.completed+1))Message(s,"This character already carries the proof; check inventory, cube and stash.");
        else if(auto callback=recover.load();callback&&callback(game,player,s.state.completed+1))Message(s,"Lost proof replaced. Quest progress and rewards are unchanged.");
        else Message(s,"Proof recovery failed. Free an inventory slot and try again.");
    }
    if(command.action==6&&rules->events&&s.state.legacyClosed){
        const auto bit=uint16_t(1<<command.detail);if(!(s.state.menus&bit)){s.state.menus|=bit;s.dirty=true;++s.sequence;}
        for(const auto& q:rules->quests)if(q.trigger=="acknowledge"){
            if((q.id==10&&(s.state.menus&3)==3)||(q.id==17&&(s.state.menus&4))||(q.id==24&&(s.state.menus&8))||(q.id==28&&(s.state.menus&16))||(q.id==31&&(s.state.menus&32))||(q.id==33&&(s.state.menus&64)))Evidence(s,q.id);
        }AdvanceEvents(s,*rules);
    }
    if(rules->events&&s.state.legacyClosed&&s.state.completed<int(rules->quests.size())&&command.proposal==s.state.completed+1){
        const auto& q=rules->quests[s.state.completed];
        if(command.action==4&&q.trigger=="acknowledge"&&(q.id==28||q.id==34)){Evidence(s,q.id);AdvanceEvents(s,*rules);}
        else if(command.action==5&&q.bonus&&s.state.objectiveReady){
            auto callback=claimReward.load();
            s.busy=true;const bool success=callback&&callback(game,player,q.id);s.busy=false;
            if(success){++s.state.completed;s.state.objectiveReady=false;s.dirty=true;Message(s,"Reward claimed: "+q.title);AdvanceEvents(s,*rules);}
            else Message(s,"Reward could not be applied. Carry exactly one appropriate Evil Barrier charm in inventory and try again.");
        }
    }
    Send(game,s,*rules);sent.erase(game);return true;
}
bool Receive(const uint8_t* bytes,size_t size){
    if(!definitions.load()||!bytes||size!=sizeof(Data::Packet))return false;Data::Packet packet;memcpy(&packet,bytes,size);
    if(packet.opcode==0x2b&&packet.magic==Data::Magic&&packet.operation==Data::Operation&&packet.version!=Data::Version){
        auto player=LocalPlayer();if(player&&player->dwUnitId==packet.recipient){
            std::lock_guard lock(viewMutex);clientView={};clientView.connected=clientView.blocked=true;clientView.recipient=packet.recipient;
            clientView.message="Quest synchronization requires the updated HUD on both the host and client.";received=GetTickCount64();
        }return true;
    }
    if(!Data::Valid(packet))return false;
    auto player=LocalPlayer();
    std::lock_guard storageLock(storageMutex);
    Data::Identity identity{packet.character,0,packet.classId,packet.characterFlags};
    const bool live=player&&player->dwUnitId==packet.recipient&&player->dwClassId==packet.classId;
    // Save-and-exit may destroy the client unit before its final save packets
    // arrive. The bound session may still flush its final journal update.
    const bool final=!player&&!(packet.flags&16)&&clientStore&&storageSession==packet.session&&storageRecipient==packet.recipient&&
        Data::SameCharacter(clientStore->history.identity,identity);
    if(!live&&!final)return true;
    auto reply=[&](uint8_t action,const void* payload=nullptr,size_t length=0){
        if(!live)return;
        Data::CommandPacket command;command.recipient=packet.recipient;command.session=packet.session;command.sequence=packet.sequence;command.action=action;
        if(payload&&length<=command.padding.size())memcpy(command.padding.data(),payload,length);
        Native<void(__fastcall*)(void*,int)>(0x10dce0)(&command,sizeof(command));
    };
    std::string error;bool stored=false;
    if(packet.flags&16){
        if(storageSession!=packet.session||storageRecipient!=packet.recipient||!clientStore){
            clientStore.reset();auto store=std::make_unique<Data::Store>();
            if(store->Open(directory,identity,error)){clientStore=std::move(store);storageSession=packet.session;storageRecipient=packet.recipient;}
        }
        if(clientStore){const auto transfer=Data::Pack(Data::Current(clientStore->history),clientStore->history.revision);reply(7,&transfer,sizeof(transfer));stored=true;}
    }else{
        if(storageSession!=packet.session||storageRecipient!=packet.recipient||!clientStore||!Data::SameCharacter(clientStore->history.identity,identity))return true;
        Data::State state{packet.completed,packet.ready!=0,(packet.events&4)!=0,(packet.flags&8)!=0,packet.kills,packet.evidence,(packet.events&1)!=0,(packet.events&2)!=0,packet.menus};
        if(packet.revision<clientStore->history.revision)return true;
        stored=clientStore->Commit(state,packet.revision,error);
        if(stored)reply(8,&packet.revision,sizeof(packet.revision));
    }
    if(!stored)reply(9);
    std::lock_guard lock(viewMutex);
    if(clientView.connected&&clientView.recipient==packet.recipient&&clientView.session==packet.session&&packet.sequence<clientView.sequence)return true;
    View view;view.connected=true;view.recipient=packet.recipient;view.session=packet.session;view.sequence=packet.sequence;view.completed=packet.completed;view.objectiveReady=packet.ready;
    view.blocked=(packet.flags&1)!=0||!stored;view.legacyAvailable=(packet.flags&2)!=0;view.dirty=(packet.flags&4)!=0;view.legacyCompleted=packet.legacyCompleted;
    view.character=packet.character;view.message=stored?packet.message:error;view.kills=packet.kills;view.evidence=packet.evidence;
    auto rules=definitions.load();if(!rules||!rules->error.empty()){view.blocked=true;view.message="Quest definitions are unavailable on this computer.";}
    clientView=std::move(view);received=GetTickCount64();return true;
}
View CurrentView(){
    auto player=LocalPlayer();if(!player){
        std::lock_guard storageLock(storageMutex);std::lock_guard lock(viewMutex);
        // Retain the binding briefly for ordered save-and-exit packets.
        if(!received||GetTickCount64()-received>6000){clientStore.reset();storageSession=storageRecipient=0;clientView={};received=0;}return {};
    }
    std::lock_guard lock(viewMutex);if(!received||GetTickCount64()-received>6000||player->dwUnitId!=clientView.recipient)return {};return clientView;
}
void Killed(Game* game,Unit* player,uint32_t victimId,uint32_t,int,int unique){
    if(!available||!game||!player)return;std::lock_guard lock(mutex);auto rules=definitions.load();auto it=sessions.find({game,player->dwUnitId});
    if(!rules||!rules->events||it==sessions.end()||it->second.player!=player||it->second.blocked||it->second.awaiting||!Owner(game,player))return;
    auto& s=it->second;if(!killed[game].insert(victimId).second)return;const auto before=s.state;
    if(s.state.kills<UINT32_MAX)++s.state.kills;s.dirty=true;++s.sequence;
    for(const auto& q:rules->quests){
        if(q.trigger=="kills"&&s.state.kills>=uint32_t(q.target))Evidence(s,q.id);
        if(q.trigger=="countessThul"&&unique==q.boss){s.state.countess=true;if(s.state.thul)Evidence(s,q.id);}
    }AdvanceEvents(s,*rules);
    // Ordinary kill counters share the periodic snapshot. Flush an objective or
    // claim boundary immediately, instead of doing a disk flush for every kill.
    if(before.completed!=s.state.completed||before.objectiveReady!=s.state.objectiveReady){Send(game,s,*rules);sent.erase(game);}
}
void Crafted(Game* game,Unit* player,size_t row,const std::vector<uint32_t>& inputCodes,const std::vector<Unit*>& outputs,Outcome outcome){
    if(!available||!game||!player)return;std::lock_guard lock(mutex);auto rules=definitions.load();auto it=sessions.find({game,player->dwUnitId});
    if(!rules||!rules->events)return;
    const bool session=it!=sessions.end()&&it->second.player==player;
    const bool safe=Safe(game,player);
    // One bounded local diagnostic per cube operation, never per render/tick.
    // This also explains native refusals and journal/save mismatches which
    // otherwise look like the operation was never observed.
    if(!directory.empty())try{
        const auto path=directory/"quest-events.log";
        std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
        std::ofstream log(path,std::ios::binary|(!ec&&size>1024*1024?std::ios::trunc:std::ios::app));
        log<<"tick="<<GetTickCount64()<<" player="<<player->dwUnitId<<" recipe="<<row
           <<" result="<<(outcome==Outcome::Committed?"committed":outcome==Outcome::Rejected?"rejected":"incomplete")
           <<" session="<<session<<" safe="<<safe;
        if(session){log<<" blocked="<<it->second.blocked<<" started="<<it->second.state.legacyClosed<<" completed="<<it->second.state.completed;if(it->second.blocked)log<<" reason="<<it->second.message;}
        log<<" inputs=";for(auto code:inputCodes){char text[5]{};memcpy(text,&code,4);log<<text<<',';}log<<" outputs="<<outputs.size()<<'\n';
    }catch(...){}
    if(outcome!=Outcome::Committed||!session||it->second.blocked||it->second.awaiting||!safe)return;auto& s=it->second;
    const auto before=s.state.evidence;
    for(const auto& q:rules->quests){
        const bool recipe=std::find(q.recipes.begin(),q.recipes.end(),row)!=q.recipes.end();
        if(q.trigger=="recipe"&&recipe)Evidence(s,q.id);
        else if(q.trigger=="depositSkull"&&recipe&&std::any_of(inputCodes.begin(),inputCodes.end(),[](uint32_t code){return (code&65535)==0x6b73;}))Evidence(s,q.id);
        else if(q.trigger=="countessThul"&&recipe){s.state.thul=true;s.dirty=true;if(s.state.countess)Evidence(s,q.id);}
        else if(q.trigger=="craft"&&(q.recipes.empty()||recipe)&&std::any_of(outputs.begin(),outputs.end(),[&](Unit* item){return item&&item->pItemData&&item->pItemData->pExtraData.pParentInv==player->pInventory&&Matches(item,q);}))Evidence(s,q.id);
    }
    if(s.state.evidence!=before&&!s.state.legacyClosed)Message(s,"Activity recorded. Choose Start at Quest 1 or import legacy progress to continue.");
    AdvanceEvents(s,*rules);Send(game,s,*rules);sent.erase(game);
}
void DepositedRune(Game* game,Unit* player,uint32_t code){
    if(code!=ItemCode("r10")||!available||!game||!player)return;std::lock_guard lock(mutex);auto rules=definitions.load();auto it=sessions.find({game,player->dwUnitId});
    if(!rules||!rules->events||it==sessions.end()||it->second.player!=player||it->second.blocked||it->second.awaiting||!Safe(game,player))return;auto& s=it->second;
    for(const auto& q:rules->quests)if(q.trigger=="countessThul"){s.state.thul=true;s.dirty=true;if(s.state.countess)Evidence(s,q.id);}
    AdvanceEvents(s,*rules);Send(game,s,*rules);sent.erase(game);
}
void RequestAcknowledge(const View& view){Submit(4,view);}
void RequestReward(const View& view){Submit(5,view);}
void Viewed(const char* panel){
    auto view=CurrentView();auto rules=Bindings();if(!panel||!view.connected||view.blocked||view.legacyAvailable||!rules||!rules->events)return;
    constexpr const char* names[]{"CubeRecipesPanel","RemoddedShopPanel","MercInfoPanel","RemoddedInfoPanel","CreditsModal","GrailTracker","LootFilter"};
    for(uint8_t i=0;i<std::size(names);++i)if(!strcmp(panel,names[i])){
        static std::array<uint64_t,7> last{};const auto now=GetTickCount64();if(now-last[i]<1500)return;last[i]=now;Submit(6,view,i);return;
    }
}
void PollMenus(){
    // Observe the attached widget tree on the client render thread. FindPanel
    // is deliberately avoided: its native lookup creates absent panels.
    auto view=CurrentView();if(!view.connected||view.blocked||view.legacyAvailable)return;
    static uint64_t last=0;const auto now=GetTickCount64();if(now-last<300)return;last=now;
    auto manager=Field<void*>(Native<void*>(0x1d7c4e8));if(!manager)return;
    std::vector<std::pair<void*,bool>> pending{{manager,true}};std::set<void*> seen;
    while(!pending.empty()&&seen.size()<512){
        const auto [widget,root]=pending.back();pending.pop_back();if(!widget||!seen.insert(widget).second)continue;
        if(!root&&(!Field<uint8_t>(widget,0x50)||!Field<uint8_t>(widget,0x51)))continue;
        if(!root){
            const auto name=Field<const char*>(widget,8);char text[81]{};
            for(size_t n=0;name&&n<80;++n){text[n]=Field<char>(name,n);if(!text[n])break;}
            Viewed(text);
        }
        const auto children=Field<void**>(widget,0x58);const auto count=Field<uint64_t>(widget,0x60);
        if(!children||count>512)continue;
        for(size_t n=0;n<count;++n)pending.emplace_back(Field<void*>(children,n*sizeof(void*)),false);
    }
}
void RequestLegacyImport(const View& view){Submit(1,view);}
void RequestFreshStart(const View& view){Submit(2,view);}
void RequestProofRecovery(const View& view){Submit(3,view);}
void RequestRefresh(){auto view=CurrentView();if(view.connected)Submit(0,view);}
}
