#include "QuestJournal.h"
#include "QuestJournalData.h"
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
    uint32_t sequence=1,epoch=0;bool blocked=false,dirty=false,busy=false;RecipeGate transaction;
    std::string message;std::set<uint32_t> weapons;
};
std::mutex mutex,viewMutex;std::map<Key,Session> sessions;std::map<Client*,Loaded> loading;
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
void BackupUnusedJournal(const std::filesystem::path& path,const std::string& original,uint64_t digest){
    auto backup=path;backup+=".unused-"+Data::Hex(digest)+"-"+Data::Hex(Data::Hash(original))+".bak";
    std::error_code error;
    if(!std::filesystem::exists(backup))std::filesystem::copy_file(path,backup,std::filesystem::copy_options::none,error);
    if(error)throw std::runtime_error("Could not back up the unused quest journal: "+error.message());
    std::ifstream input(backup,std::ios::binary|std::ios::ate);
    if(!input||input.tellg()!=std::streamoff(original.size()))throw std::runtime_error("Unused quest journal backup could not be verified.");
    std::string text(original.size(),'\0');input.seekg(0);if(!input.read(text.data(),text.size())||text!=original)throw std::runtime_error("Unused quest journal backup differs from the original.");
}
std::optional<Data::History> LegacyTimestampJournal(const Data::Identity& identity,const Rules& rules,const Data::Checkpoint& current){
    if(!std::filesystem::exists(directory))return {};
    std::optional<Data::History> candidate;
    auto prefixIdentity=identity;prefixIdentity.created=0;
    const auto prefix=SummonPersistence::Data::Key(prefixIdentity).substr(0,identity.name.size()*2+1);
    for(const auto& file:std::filesystem::directory_iterator(directory)){
        if(!file.is_regular_file()||file.path().extension()!=L".json"||!file.path().filename().string().starts_with(prefix))continue;
        if(file.file_size()>Data::MaxBytes)throw std::runtime_error("Legacy quest journal is oversized.");
        std::ifstream input(file.path(),std::ios::binary);const std::string text(std::istreambuf_iterator<char>(input),{});
        auto history=Data::Decode(text,int(rules.quests.size()));if(!history)throw std::runtime_error("Legacy quest journal is unreadable.");
        if(!history->identity.created||history->identity.name!=identity.name||history->identity.classId!=identity.classId||history->identity.flags!=identity.flags)continue;
        if(candidate)throw std::runtime_error("Multiple legacy quest journals match. Restore the correct matching backup.");
        if(history->digest!=rules.digest)throw std::runtime_error("Legacy quest definitions do not match.");
        const auto state=Data::Find(*history,current.checksum,current.saveSize);
        // Only an entirely unused journal can accept an unmatched first save.
        // A prior reward/evidence checkpoint still requires its matching save.
        if(!state&&!Data::Pristine(*history))throw std::runtime_error("Legacy quest journal does not match this character save. Restore matching backups.");
        auto point=current;point.state=state.value_or(Data::State{});history->identity=identity;Data::Append(*history,point);candidate=std::move(history);
    }return candidate;
}
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
void Send(Game* game,Session& s,const Rules& rules){
    auto client=Owner(game,s.player);if(!client)return;
    Data::Packet packet;packet.recipient=s.player->dwUnitId;packet.session=s.epoch;packet.sequence=s.sequence;packet.definition=rules.digest;
    packet.count=uint8_t(rules.quests.size());packet.completed=uint8_t(s.state.completed);packet.ready=s.state.objectiveReady;packet.kills=s.state.kills;packet.evidence=s.state.evidence;
    packet.flags=uint8_t((s.blocked||s.state.uncertain?1:0)|(!s.state.legacyClosed&&!s.state.completed&&!s.state.objectiveReady?2:0)|(s.dirty?4:0));
    if(packet.flags&2)packet.legacyCompleted=uint8_t(LegacyCompleted(s.player,packet.count));
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
    wchar_t local[32768]{};const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,DWORD(std::size(local)));
    if(length&&length<std::size(local)){
        auto folder=install.lexically_normal().wstring();std::transform(folder.begin(),folder.end(),folder.begin(),[](wchar_t c){return wchar_t(towlower(c));});
        const auto installKey=Data::Hex(Data::Hash({reinterpret_cast<const char*>(folder.data()),folder.size()*sizeof(wchar_t)}));
        Data::Identity name;name.name="RMD-MP";
        directory=std::filesystem::path(local)/L"D2RHUD"/L"Quests"/installKey/SummonPersistence::Data::Key(name);
    }
    configurationStatus=rules->error.empty()?"Per-character quests configured ("+std::to_string(rules->quests.size())+" quests).":rules->error;
    definitions.store(std::move(rules));
}
std::shared_ptr<const Rules> Bindings(){return definitions.load();}
void SetAvailable(bool enabled){available=enabled&&bool(definitions.load());}
void SetProofRecovery(ProofRecovery value){recover=value;}
void SetRewardClaim(RewardClaim value){claimReward=value;}
std::string Status(){std::lock_guard lock(mutex);const auto path=directory.u8string();return configurationStatus+((definitions.load()&&!definitions.load()->events)?" Install version 2 D2RHUD/quests.json.":"")+(available.load()?"":" Quest hooks or definitions are unavailable.")+(directory.empty()?" Quest storage path is unavailable.":" Companion folder: "+std::string(path.begin(),path.end()));}
Progress Inspect(Game* game,Unit* player){std::lock_guard lock(mutex);auto rules=definitions.load();if(!available||!rules||!rules->error.empty()||!player)return {};auto it=sessions.find({game,player->dwUnitId});if(it==sessions.end()||it->second.player!=player)return {};const auto& s=it->second;return {!s.blocked&&!s.busy&&!s.state.uncertain,s.state.completed,s.state.objectiveReady};}
void Refused(Game* game,Unit* player,const std::string& message){std::lock_guard lock(mutex);if(!player)return;if(auto it=sessions.find({game,player->dwUnitId});it!=sessions.end()){Message(it->second,message);sent.erase(game);}}
bool Begin(Game* game,Unit* player,const RecipeGate& gate){
    std::lock_guard lock(mutex);const auto rules=definitions.load();if(!available||!rules||!rules->error.empty()||!Safe(game,player))return false;
    auto it=sessions.find({game,player->dwUnitId});if(it==sessions.end())return false;auto& s=it->second;
    if(s.player!=player||s.blocked||s.busy||!Data::Allows(s.state,gate,int(rules->quests.size())))return false;
    const auto binding=std::find_if(rules->gates.begin(),rules->gates.end(),[&](const auto& g){return g.row==gate.row&&g.quest==gate.quest&&g.turnIn==gate.turnIn;});if(binding==rules->gates.end())return false;
    s.busy=true;s.transaction=gate;return true;
}
void Finish(Game* game,Unit* player,const RecipeGate& gate,Outcome outcome){
    std::lock_guard lock(mutex);auto rules=definitions.load();if(!rules||!player)return;auto it=sessions.find({game,player->dwUnitId});if(it==sessions.end())return;auto& s=it->second;
    if(s.player!=player||!s.busy||s.transaction.row!=gate.row)return;s.busy=false;
    if(outcome==Outcome::Committed&&Data::Advance(s.state,gate,int(rules->quests.size()))){s.dirty=true;Message(s,gate.turnIn?"Quest "+std::to_string(gate.quest)+" reward claimed.":"Objective complete.");}
    else if(outcome==Outcome::Ambiguous){s.state.uncertain=true;s.state.legacyClosed=true;s.dirty=true;s.blocked=true;Message(s,"Quest output incomplete. Progress locked to prevent a duplicate reward; restore a matching backup.");}
    else Message(s,"Quest recipe did not complete. Free storage space and try again.");
    sent.erase(game);
}
void BeforeLoad(Game*,Client* client){
    if(!definitions.load()||!client)return;const auto block=client->pSaveHeaders;const auto bytes=Field<const uint8_t*>(block);const auto size=Field<uint64_t>(block,8);Loaded value;
    const bool valid=Data::Header(bytes,size,value.identity,value.point);std::lock_guard lock(mutex);loading.erase(client);if(valid)loading.emplace(client,std::move(value));
}
void AfterLoad(Game* game,Client* client,Unit* player,bool success){
    std::lock_guard lock(mutex);auto loaded=loading.find(client);if(loaded==loading.end())return;auto value=std::move(loaded->second);loading.erase(loaded);
    auto rules=definitions.load();if(!success||!game||!player||player->dwUnitType||!rules||!rules->error.empty())return;
    Session s;s.player=player;s.epoch=++epochSerial;if(!s.epoch)s.epoch=++epochSerial;s.history.identity=value.identity;s.history.digest=rules->digest;s.path=directory/(SummonPersistence::Data::Key(value.identity)+".json");
    try{
        if(directory.empty())throw std::runtime_error("Quest storage path is unavailable.");
        if(!std::filesystem::exists(s.path))if(auto legacy=LegacyTimestampJournal(value.identity,*rules,value.point)){
            std::string error;if(!Data::AtomicWrite(s.path,*legacy,error))throw std::runtime_error(error);
            // Keep the original timestamp-named companion intact as a backup.
        }
        if(std::filesystem::exists(s.path)){
            std::ifstream input(s.path,std::ios::binary|std::ios::ate);const auto length=input.tellg();if(!input||length<=0||length>std::streamoff(Data::MaxBytes))throw std::runtime_error("Quest file is unreadable or oversized.");
            std::string text(size_t(length),'\0');input.seekg(0);if(!input.read(text.data(),text.size()))throw std::runtime_error("Quest file could not be read.");input.close();
            auto history=Data::Decode(text,int(rules->quests.size()));
            if(!history||!SummonPersistence::Data::SameIdentity(history->identity,value.identity))throw std::runtime_error("Quest file character identity does not match.");
            auto state=Data::Find(*history,value.point.checksum,value.point.saveSize);
            const bool recovering=history->digest!=rules->digest||!state;
            if(recovering){
                // A never-started journal has no observations, choices or
                // claims to lose/replay. Compilation can change definitions
                // while intervening native saves leave its old checkpoint
                // behind. Recover only when EVERY historical state is empty.
                if(!Data::Pristine(*history))throw std::runtime_error(history->digest!=rules->digest?"Quest definitions changed for a journal with existing progress. Restore matching quest data and journal backups.":"Quest file does not match this character save. Restore matching backups.");
                BackupUnusedJournal(s.path,text,history->digest);
                history->digest=rules->digest;Data::Append(*history,value.point);state=value.point.state;
                std::string error;if(!Data::AtomicWrite(s.path,*history,error))throw std::runtime_error("Could not update the unused quest journal: "+error);
            }
            s.history=std::move(*history);s.state=*state;s.blocked=s.state.uncertain;
            s.message=s.blocked?"An incomplete quest transaction requires a matching backup.":recovering?"Unused journal updated; original backed up. Choose Start at Quest 1 or import legacy progress.":"Quest journal loaded for this character.";
        }else{
            s.history.checkpoints.push_back(value.point);std::string error;if(!Data::AtomicWrite(s.path,s.history,error))throw std::runtime_error(error);
            s.message="New journal. Existing characters may explicitly import legacy progress once.";
        }
    }catch(const std::exception& e){s.blocked=true;s.message=e.what();}
    sessions[{game,player->dwUnitId}]=std::move(s);sent.erase(game);
}
void Saved(Game* game,Unit* player,const uint8_t* bytes,size_t size){
    if(!game||!player)return;Data::Identity identity;Data::Checkpoint point;if(!Data::Header(bytes,size,identity,point))return;
    std::lock_guard lock(mutex);auto it=sessions.find({game,player->dwUnitId});if(it==sessions.end()||it->second.player!=player)return;auto& s=it->second;
    if(!SummonPersistence::Data::SameIdentity(s.history.identity,identity)||s.busy){s.blocked=true;Message(s,"Quest save identity/transaction mismatch. Further quest recipes are locked.");return;}
    // Never overwrite a corrupt/mismatched journal with an invented fresh state.
    if(s.blocked&&!s.state.uncertain)return;
    auto history=s.history;point.state=s.state;Data::Append(history,point);std::string error;
    if(Data::AtomicWrite(s.path,history,error)){s.history=std::move(history);s.dirty=false;}
    else{s.blocked=true;Message(s,"Could not save quest journal: "+error);}
    sent.erase(game);
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
    if(command.version!=Data::Version||command.action>6||command.detail>6||!std::all_of(command.padding.begin(),command.padding.end(),[](uint8_t v){return !v;})||!available||!Safe(game,player)||command.recipient!=player->dwUnitId)return true;
    std::lock_guard lock(mutex);auto rules=definitions.load();auto it=sessions.find({game,player->dwUnitId});if(!rules||it==sessions.end())return true;auto& s=it->second;
    if(command.session!=s.epoch)return true;
    if(command.action==0){Send(game,s,*rules);return true;}
    if(s.blocked||s.busy||s.state.uncertain||command.sequence!=s.sequence)return true;
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
    sent.erase(game);return true;
}
bool Receive(const uint8_t* bytes,size_t size){
    if(!definitions.load())return false;
    if(!bytes||size!=sizeof(Data::Packet))return false;Data::Packet packet;memcpy(&packet,bytes,size);if(!Data::Valid(packet))return false;
    auto player=LocalPlayer();if(!player||player->dwUnitId!=packet.recipient)return true;
    std::lock_guard lock(viewMutex);if(clientView.connected&&clientView.recipient==packet.recipient&&clientView.session==packet.session&&packet.sequence<clientView.sequence)return true;
    View view;view.connected=true;view.recipient=packet.recipient;view.session=packet.session;view.sequence=packet.sequence;view.completed=packet.completed;view.objectiveReady=packet.ready;view.blocked=(packet.flags&1)!=0;view.legacyAvailable=(packet.flags&2)!=0;view.dirty=(packet.flags&4)!=0;view.legacyCompleted=packet.legacyCompleted;view.character=packet.character;view.message=packet.message;view.kills=packet.kills;view.evidence=packet.evidence;
    auto rules=definitions.load();if(!rules||!rules->error.empty()||rules->digest!=packet.definition||rules->quests.size()!=packet.count){view.blocked=true;view.message="Quest definitions differ from the host. Install the host's matching mod files.";}
    clientView=std::move(view);received=GetTickCount64();return true;
}
View CurrentView(){std::lock_guard lock(viewMutex);auto player=LocalPlayer();if(!received||GetTickCount64()-received>6000||!player||player->dwUnitId!=clientView.recipient)return {};return clientView;}
void Killed(Game* game,Unit* player,uint32_t victimId,uint32_t,int,int unique){
    if(!available||!game||!player)return;std::lock_guard lock(mutex);auto rules=definitions.load();auto it=sessions.find({game,player->dwUnitId});
    if(!rules||!rules->events||it==sessions.end()||it->second.player!=player||it->second.blocked||!Owner(game,player))return;
    auto& s=it->second;if(!killed[game].insert(victimId).second)return;
    if(s.state.kills<UINT32_MAX)++s.state.kills;s.dirty=true;++s.sequence;
    for(const auto& q:rules->quests){
        if(q.trigger=="kills"&&s.state.kills>=uint32_t(q.target))Evidence(s,q.id);
        if(q.trigger=="countessThul"&&unique==q.boss){s.state.countess=true;if(s.state.thul)Evidence(s,q.id);}
    }AdvanceEvents(s,*rules);sent.erase(game);
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
        const auto path=directory.parent_path().parent_path().parent_path()/"quest-events.log";
        std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
        std::ofstream log(path,std::ios::binary|(!ec&&size>1024*1024?std::ios::trunc:std::ios::app));
        log<<"tick="<<GetTickCount64()<<" player="<<player->dwUnitId<<" recipe="<<row
           <<" result="<<(outcome==Outcome::Committed?"committed":outcome==Outcome::Rejected?"rejected":"incomplete")
           <<" session="<<session<<" safe="<<safe;
        if(session){log<<" blocked="<<it->second.blocked<<" started="<<it->second.state.legacyClosed<<" completed="<<it->second.state.completed;if(it->second.blocked)log<<" reason="<<it->second.message;}
        log<<" inputs=";for(auto code:inputCodes){char text[5]{};memcpy(text,&code,4);log<<text<<',';}log<<" outputs="<<outputs.size()<<'\n';
    }catch(...){}
    if(outcome!=Outcome::Committed||!session||it->second.blocked||!safe)return;auto& s=it->second;
    const auto before=s.state.evidence;
    for(const auto& q:rules->quests){
        const bool recipe=std::find(q.recipes.begin(),q.recipes.end(),row)!=q.recipes.end();
        if(q.trigger=="recipe"&&recipe)Evidence(s,q.id);
        else if(q.trigger=="depositSkull"&&recipe&&std::any_of(inputCodes.begin(),inputCodes.end(),[](uint32_t code){return (code&65535)==0x6b73;}))Evidence(s,q.id);
        else if(q.trigger=="countessThul"&&recipe){s.state.thul=true;s.dirty=true;if(s.state.countess)Evidence(s,q.id);}
        else if(q.trigger=="craft"&&(q.recipes.empty()||recipe)&&std::any_of(outputs.begin(),outputs.end(),[&](Unit* item){return item&&item->pItemData&&item->pItemData->pExtraData.pParentInv==player->pInventory&&Matches(item,q);}))Evidence(s,q.id);
    }
    if(s.state.evidence!=before&&!s.state.legacyClosed)Message(s,"Activity recorded. Choose Start at Quest 1 or import legacy progress to continue.");
    AdvanceEvents(s,*rules);sent.erase(game);
}
void DepositedRune(Game* game,Unit* player,uint32_t code){
    if(code!=ItemCode("r10")||!available||!game||!player)return;std::lock_guard lock(mutex);auto rules=definitions.load();auto it=sessions.find({game,player->dwUnitId});
    if(!rules||!rules->events||it==sessions.end()||it->second.player!=player||it->second.blocked||!Safe(game,player))return;auto& s=it->second;
    for(const auto& q:rules->quests)if(q.trigger=="countessThul"){s.state.thul=true;s.dirty=true;if(s.state.countess)Evidence(s,q.id);}
    AdvanceEvents(s,*rules);sent.erase(game);
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
