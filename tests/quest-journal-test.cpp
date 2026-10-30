#define NOMINMAX
#include "../d2rhud/plugin/QuestJournal.cpp"
#include <iostream>
#include <deque>

std::map<uint32_t,uintptr_t> bindings;
DWORD64 Pattern::Address(uint32_t rva){return bindings[rva];}
using namespace QuestJournal;
int checks=0,awards=0;
void Check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
template<class T>void Put(void* p,size_t offset,T value){memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
std::string ReadText(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
std::string Source(){return "description\tenabled\tnuminputs\top\tparam\tvalue\tinput 1\tinput 2\toutput\toutput b\nQuest 1a\t1\t2\t6\t398\t1\tZ01\tgemz\tuseitem\tBt01\nQuest 1b\t1\t2\t18\t398\t1\tBarrier1\tBt01\tuseitem\t\nQuest 2a\t1\t2\t18\t398\t1\tZ01\tZ08\tuseitem\tBt02\nQuest 2b\t1\t2\t18\t398\t2\tBarrier1\tBt02\tuseitem\t\n";}
Game game{};std::array<Unit,3> heroes{};std::array<D2PlayerDataStrc,3> playerData{};std::array<Client,3> clients{};std::array<D2InventoryStrc,3> inventories{};
Unit* local=nullptr;uint32_t selector=0;
struct Peer {
    std::filesystem::path root;std::unique_ptr<Data::Store> store;View view;uint64_t received=0;uint32_t session=0,recipient=0;
};
std::array<Peer,3> peers;
struct PeerScope {
    Peer& peer;Unit* previous;std::filesystem::path previousDirectory;
    PeerScope(size_t index):peer(peers[index]),previous(local),previousDirectory(directory){
        local=&heroes[index];directory=peer.root;
        clientStore.swap(peer.store);std::swap(clientView,peer.view);std::swap(received,peer.received);std::swap(storageSession,peer.session);std::swap(storageRecipient,peer.recipient);
    }
    ~PeerScope(){clientStore.swap(peer.store);std::swap(clientView,peer.view);std::swap(received,peer.received);std::swap(storageSession,peer.session);std::swap(storageRecipient,peer.recipient);local=previous;directory=previousDirectory;}
};
std::deque<Data::Packet> downstream;
struct Upstream {uint32_t owner;Data::CommandPacket command;};
std::deque<Upstream> upstream;
void __fastcall CaptureSend(void*,uint32_t,void** range){
    Check(static_cast<uint8_t*>(range[1])-static_cast<uint8_t*>(range[0])==sizeof(Data::Packet),"wrong snapshot size");
    Data::Packet packet;memcpy(&packet,range[0],sizeof(packet));Check(Data::Valid(packet),"invalid snapshot");downstream.push_back(packet);
}
void __fastcall CaptureCommand(void* bytes,int size){
    Check(local&&size==sizeof(Data::CommandPacket),"bad client command");Data::CommandPacket packet;memcpy(&packet,bytes,sizeof(packet));upstream.push_back({local->dwUnitId,packet});
}
void Pump(){
    for(unsigned n=0;!downstream.empty()||!upstream.empty();++n){
        Check(n<1000,"network handshake loop");
        if(!downstream.empty()){
            const auto packet=downstream.front();downstream.pop_front();Check(packet.recipient>=1&&packet.recipient<=3,"wrong recipient");
            PeerScope peer(packet.recipient-1);Check(Receive(reinterpret_cast<const uint8_t*>(&packet),sizeof(packet)),"snapshot not recognized");
        }else{
            const auto message=upstream.front();upstream.pop_front();auto& player=heroes[message.owner-1];
            Check(Command(&game,&player,reinterpret_cast<const uint8_t*>(&message.command),sizeof(message.command)),"command not recognized");
        }
    }
}
std::array<uint8_t,0x200> Save(const char* name,uint32_t checksum=100){
    std::array<uint8_t,0x200> bytes{};Put(bytes.data(),0,uint32_t(0xaa55aa55));Put(bytes.data(),4,uint32_t(98));Put(bytes.data(),8,uint32_t(bytes.size()));Put(bytes.data(),12,checksum);
    Put(bytes.data(),0x2c,uint32_t(12345));Put(bytes.data(),0x24,uint32_t(0x20));strcpy_s(reinterpret_cast<char*>(bytes.data()+0x10b),61,name);return bytes;
}
void Load(size_t index,std::array<uint8_t,0x200>& bytes){
    auto& player=heroes[index];auto& client=clients[index];std::array<uint64_t,2> block{reinterpret_cast<uint64_t>(bytes.data()),bytes.size()};client.pSaveHeaders=reinterpret_cast<D2SaveHeadersStrc*>(block.data());
    BeforeLoad(&game,&client);AfterLoad(&game,&client,&player,true);client.pSaveHeaders=nullptr;sent.erase(&game);Tick(&game);Pump();
}
Data::CommandPacket CommandFor(size_t index,uint8_t action){
    const auto& view=peers[index].view;Data::CommandPacket command;command.recipient=view.recipient;command.session=view.session;command.sequence=view.sequence;command.action=action;command.proposal=uint8_t(action>=4?view.completed+1:view.legacyCompleted);return command;
}
void Action(size_t index,uint8_t action){Pump();auto command=CommandFor(index,action);Command(&game,&heroes[index],reinterpret_cast<uint8_t*>(&command),sizeof(command));Pump();}
void Reset(size_t index){Removed(&game,&heroes[index]);peers[index].store.reset();peers[index].session=peers[index].recipient=0;peers[index].view={};downstream.clear();upstream.clear();}
nlohmann::json Legacy(const Data::Identity& identity,const std::vector<Data::State>& states){
    nlohmann::json points=nlohmann::json::array();uint32_t checksum=20;
    for(const auto& state:states){auto value=Data::EncodeState(state);value["checksum"]=checksum++;value["size"]=uint32_t(512);points.push_back(value);}
    return {{"version",1},{"definition","123456789abcdef"},{"identity",{{"name",identity.name},{"created",identity.created},{"classId",identity.classId},{"flags",identity.flags}}},{"checkpoints",points}};
}
void Write(const std::filesystem::path& path,const std::string& text){std::filesystem::create_directories(path.parent_path());std::ofstream(path,std::ios::binary)<<text;}

int main(int argc,char** argv){try{
    const auto root=std::filesystem::temp_directory_path()/("d2rhud-quest-tests-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(root);const auto hostRoot=root/"host";SetEnvironmentVariableW(L"LOCALAPPDATA",hostRoot.c_str());
    for(size_t n=0;n<heroes.size();++n){auto& player=heroes[n];auto& client=clients[n];player.dwUnitId=uint32_t(n+1);player.dwAnimMode=1;player.pPlayerData=&playerData[n];player.pInventory=&inventories[n];
        playerData[n].pClient=&client;client.pGame=&game;client.pPlayer=&player;client.dwUnitGUID=player.dwUnitId;client.dwClientId=player.dwUnitId;client.dwClientState=4;inventories[n].pOwner=&player;
        peers[n].root=root/("client-"+std::to_string(n));}
    bindings[0x1d442d8]=uintptr_t(&selector);bindings[0x6b050]=uintptr_t(+[](uint32_t)->Unit*{return local;});bindings[0x18682b0]=1;bindings[0x422ac0]=uintptr_t(CaptureSend);bindings[0x10dce0]=uintptr_t(CaptureCommand);
    uint64_t uniqueCount=0;bindings[0x1c9fc78]=uintptr_t(&uniqueCount);
    Configure(root,"RMD-MP",Source(),"");SetAvailable(true);auto rules=Bindings();Check(rules->error.empty()&&rules->quests.size()==2,"fixture rules failed");
    Data::State past;past.completed=1;past.legacyClosed=true;past.kills=987;past.evidence=uint64_t(1)<<40;past.menus=65;past.countess=true;past.thul=true;
    auto old=Legacy({"Guest",9999,0,0x60},{{},past,{}});const auto legacyPath=peers[1].root/"old-install-checksum"/"mod"/(SummonPersistence::Data::Key({"Guest",9999,0,0x60})+".json");Write(legacyPath,old.dump(2));const auto original=ReadText(legacyPath);
    auto hostSave=Save("Host"),guestSave=Save("Guest",999);Load(0,hostSave);Load(1,guestSave);
    Check(Inspect(&game,&heroes[0]).completed==0&&Inspect(&game,&heroes[1]).completed==1,"remote progress leaked or old progress was reset");
    Check(Inspect(&game,&heroes[1]).available&&peers[1].store,"legacy journal was rejected due to save/definition drift");
    const auto guestPath=peers[1].store->path;const auto guestState=Data::Current(peers[1].store->history);
    Check(guestState.kills==987&&guestState.evidence==past.evidence&&guestState.menus==65&&guestState.thul&&guestState.countess,"legacy observations lost");
    Check(ReadText(legacyPath)==original,"migration modified an existing journal");Check(!std::filesystem::exists(directory/"RMD-MP"),"host wrote a local journal for a remote guest");
    auto document=nlohmann::json::parse(ReadText(guestPath));Check(document["version"]==2&&!document.contains("checkpoints")&&!document.contains("definition")&&ReadText(guestPath).find("checksum")==std::string::npos,"new format retained checksum history");
    // Immediate persistence, followed by mandatory durable acknowledgement.
    auto gate=rules->gates[0],claim=rules->gates[1];Check(Begin(&game,&heroes[0],gate),"initial objective refused");Finish(&game,&heroes[0],gate,Outcome::Committed);
    Check(!Begin(&game,&heroes[0],claim),"reward advanced before the client's write acknowledgement");Pump();
    Check(Data::Current(*Data::Store::Read(peers[0].store->path)).objectiveReady,"objective waited for a character save to persist");
    ++sessions.at({&game,1}).state.kills;
    Check(!Begin(&game,&heroes[1],claim),"another character's proof replayed a previous reward");Check(Begin(&game,&heroes[0],claim),"durable objective did not unlock claim");Finish(&game,&heroes[0],claim,Outcome::Committed);Pump();
    Check(!Begin(&game,&heroes[0],claim)&&Data::Current(*Data::Store::Read(peers[0].store->path)).completed==1,"reward replay or missing immediate write");
    {
        auto& session=sessions.at({&game,1});session.acknowledged=0;session.hasAcknowledgment=false;
        auto forged=CommandFor(0,8);const uint64_t future=session.history.revision+10;memcpy(forged.padding.data(),&future,sizeof(future));
        Command(&game,&heroes[0],reinterpret_cast<uint8_t*>(&forged),sizeof(forged));Check(!Durable(session),"forged future ACK bypassed a write");
        sent.erase(&game);Tick(&game);Pump();Check(Durable(session),"valid write ACK did not recover");
    }
    // Same character, different save checksum/size/timestamp/ladder/definition and installation.
    const auto beforePath=peers[0].store->path;const auto beforeFiles=std::distance(std::filesystem::directory_iterator(beforePath.parent_path()),std::filesystem::directory_iterator{});
    for(unsigned n=0;n<40;++n){Put(hostSave.data(),12,9000+n);Put(hostSave.data(),8,uint32_t(500+n%12));Put(hostSave.data(),0x2c,uint32_t(n));Put(hostSave.data(),0x24,uint32_t(n%2?0x60:0x20));Reset(0);Load(0,hostSave);Check(Inspect(&game,&heroes[0]).completed==1&&peers[0].store->path==beforePath,"ordinary saves rekeyed or reset progress");}
    Check(std::distance(std::filesystem::directory_iterator(beforePath.parent_path()),std::filesystem::directory_iterator{})==beforeFiles,"restarts created extra files");
    auto changedSource=Source();changedSource.replace(changedSource.find("gemz"),4,"gem");Configure(root/"different-install","RMD-MP",changedSource,"");SetAvailable(true);Reset(0);Load(0,hostSave);Check(Inspect(&game,&heroes[0]).completed==1,"recipe changes invalidated saved progress");rules=Bindings();
    // The guest can later host locally and still reads its own latest progress.
    Reset(1);Load(1,guestSave);Check(Inspect(&game,&heroes[1]).completed==1&&peers[1].store->path==guestPath,"guest could not resume its journal on another game");
    auto secondGate=rules->gates[2];Check(Begin(&game,&heroes[1],secondGate),"guest second objective refused");Finish(&game,&heroes[1],secondGate,Outcome::Committed);Pump();
    Check(Data::Current(*Data::Store::Read(guestPath)).objectiveReady,"guest progress saved on the host instead of its owner");
    // Stale packets, forged uploads/ACKs and simultaneous writers do not bypass claims.
    auto& guestSession=sessions.at({&game,2});auto forged=CommandFor(1,7);const auto fake=Data::Pack({},10000);memcpy(forged.padding.data(),&fake,sizeof(fake));Command(&game,&heroes[1],reinterpret_cast<uint8_t*>(&forged),sizeof(forged));Check(guestSession.state.completed==1,"post-join upload reset host progress");
    Data::Store competing;std::string error;Check(!competing.Open(peers[1].root,{"Guest",0,0,0x20},error),"second writer acquired the same journal");
    auto savedHistory=peers[1].store->history;Check(!peers[1].store->Commit({},savedHistory.revision+1,error),"journal rolled back completed rewards");
    // A failed local disk write pauses rewards, and retry recovers without a new journal.
    auto backup=guestPath;backup+=L".bak";if(std::filesystem::exists(backup))std::filesystem::remove(backup);std::filesystem::create_directory(backup);
    Check(Begin(&game,&heroes[1],rules->gates[3]),"guest reward refused before write failure");Finish(&game,&heroes[1],rules->gates[3],Outcome::Committed);Pump();Check(!Inspect(&game,&heroes[1]).available,"storage failure did not pause rewards");
    std::filesystem::remove(backup);sent.erase(&game);Tick(&game);Pump();Check(Inspect(&game,&heroes[1]).available&&Data::Current(*Data::Store::Read(guestPath)).completed==2,"storage retry reset or lost progress");
    // Genuine malformed files are preserved, never replaced with fresh claims.
    Reset(2);Data::Identity broken{"Broken",0,0,0x20};const auto brokenPath=peers[2].root/"RMD-MP"/(Data::Key(broken)+".json");Write(brokenPath,"broken");auto brokenSave=Save("Broken");Load(2,brokenSave);Check(!Inspect(&game,&heroes[2]).available&&ReadText(brokenPath)=="broken","corrupt local journal was overwritten");
    // One-checkpoint, timestamp-named and two-install journals all migrate;
    // earned claims from earlier checkpoints survive a later empty checkpoint.
    Data::Store migration;const auto migrationRoot=root/"migration";Data::State later=past;later.completed=2;
    const auto a=migrationRoot/"install-a"/(SummonPersistence::Data::Key({"Migrator",123,0,0x20})+".json");const auto b=migrationRoot/"install-b"/(SummonPersistence::Data::Key({"Migrator",456,0,0x60})+".json");
    const auto textA=Legacy({"Migrator",123,0,0x20},{past}).dump(),textB=Legacy({"Migrator",456,0,0x60},{later,{}}).dump();Write(a,textA);Write(b,textB);
    Check(migration.Open(migrationRoot,{"Migrator",0,0,0x20},error)&&Data::Current(migration.history).completed==2,"multiple old journals rejected or lost furthest reward");Check(ReadText(a)==textA&&ReadText(b)==textB,"migration changed originals");
    // A v1 file already in the canonical location is compacted with an exact backup.
    Data::Store inPlace;const auto oldCanonical=migrationRoot/"RMD-MP"/(Data::Key({"InPlace",0,0,0x20})+".json");const auto oldText=Legacy({"InPlace",0,0,0x20},{past}).dump();Write(oldCanonical,oldText);
    Check(inPlace.Open(migrationRoot,{"InPlace",0,0,0x20},error)&&Data::Current(inPlace.history).completed==1,"canonical v1 migration failed");auto oldBackup=oldCanonical;oldBackup+=L".bak";Check(ReadText(oldBackup)==oldText,"canonical original backup differs");
    // A pre-update host may have the only recent guest checkpoint. Transfer it
    // back to the client's durable journal without creating a new host journal.
    Reset(2);const auto oldHost=directory/"old-host-install"/(SummonPersistence::Data::Key({"Returning",123,0,0x20})+".json");
    const auto hostText=Legacy({"Returning",123,0,0x20},{past}).dump();Write(oldHost,hostText);auto returning=Save("Returning",654);Load(2,returning);
    Check(Inspect(&game,&heroes[2]).available&&Data::Current(peers[2].store->history).completed==1,"old host-owned guest journal was not recovered on the client");Check(ReadText(oldHost)==hostText,"guest recovery changed the old host journal");
    {
        auto& session=sessions.at({&game,3});session.state.kills=12345;Saved(&game,&heroes[2],returning.data(),returning.size());
        Check(downstream.size()==1,"final journal was not queued before the native character save");auto packet=downstream.front();downstream.pop_front();
        PeerScope peer(2);local=nullptr;Check(!CurrentView().connected&&clientStore,"leaving the game released the writer before the final save");
        Receive(reinterpret_cast<uint8_t*>(&packet),sizeof(packet));Check(Data::Current(clientStore->history).kills==12345,"late save-and-exit journal packet was discarded");
    }
    Reset(2);auto uncertainSave=Save("UncertainPeer");Load(2,uncertainSave);
    Check(Begin(&game,&heroes[2],Bindings()->gates[0]),"uncertain fixture objective refused");Finish(&game,&heroes[2],Bindings()->gates[0],Outcome::Rejected);Pump();
    Check(!Inspect(&game,&heroes[2]).objectiveReady,"rejected transaction advanced the objective");
    Check(Begin(&game,&heroes[2],Bindings()->gates[0]),"uncertain fixture retry refused");Finish(&game,&heroes[2],Bindings()->gates[0],Outcome::Ambiguous);Pump();
    Check(!Inspect(&game,&heroes[2]).available&&Data::Current(peers[2].store->history).uncertain,"ambiguous native outputs were not protected");
    Reset(2);Load(2,uncertainSave);Check(!Inspect(&game,&heroes[2]).available,"rejoining cleared an unresolved output transaction");
    {
        PeerScope peer(2);const auto before=ReadText(clientStore->path);Data::Packet oldVersion;oldVersion.version=2;oldVersion.recipient=3;
        Receive(reinterpret_cast<uint8_t*>(&oldVersion),sizeof(oldVersion));Check(CurrentView().blocked&&CurrentView().message.find("updated HUD")!=std::string::npos&&ReadText(clientStore->path)==before,"mixed HUD versions changed player progress or had no explanation");
    }
    auto obsoleteMetadata=Legacy({"OldMetadata",123,0,0x20},{past});obsoleteMetadata["definition"]=nullptr;obsoleteMetadata["identity"]["created"]="obsolete";
    Check(Data::Decode(obsoleteMetadata.dump(),2).has_value(),"obsolete hash/timestamp metadata still gated progress");
    // Replay actual installed journals through isolated copies. Original player
    // files are read twice to verify they are unchanged, never opened for write.
    if(argc>3){
        size_t profiles=0;
        for(const auto& file:std::filesystem::recursive_directory_iterator(argv[3])){
            if(!file.is_regular_file()||file.path().extension()!=L".json")continue;
            const auto profileText=ReadText(file.path());const auto history=Data::Decode(profileText,Data::MaxQuests);Check(history.has_value(),"existing player journal could not be decoded");
            const auto copyRoot=root/("real-profile-"+std::to_string(profiles++));const auto copy=copyRoot/"old-install"/(SummonPersistence::Data::Key(history->identity)+".json");Write(copy,profileText);
            Data::Store replay;Check(replay.Open(copyRoot,history->identity,error),("existing player journal migration failed: "+error).c_str());Check(Data::Current(replay.history)==Data::Current(*history),"existing player progress changed during migration");Check(ReadText(file.path())==profileText,"existing player file changed");
        }
        Check(profiles>0,"no actual journal profiles were replayed");std::cout<<"Read-only migration replay passed for "<<profiles<<" existing player journals.\n";
    }
    // Definition parsing and sequential gates still prevent repeated rewards.
    if(argc>2){
        Configure(root,"RMD-MP",ReadText(argv[1]),ReadText(argv[2]));SetAvailable(true);auto actual=Bindings();Check(actual->error.empty()&&actual->quests.size()==45&&actual->events,"actual quest rules failed");
        for(int n=1;n<=45;++n){Data::State state;state.completed=n-1;auto objective=std::find_if(actual->gates.begin(),actual->gates.end(),[&](const auto& g){return g.quest==n&&!g.turnIn;});auto reward=std::find_if(actual->gates.begin(),actual->gates.end(),[&](const auto& g){return g.quest==n&&g.turnIn;});Check(Data::Advance(state,*objective,45)&&Data::Advance(state,*reward,45)&&!Data::Advance(state,*reward,45),"actual quest chain can repeat or cannot advance");}
        // Kill observations are server confirmed; bonus writes go to the owning peer.
        Reset(0);auto eventSave=Save("EventPeer");Data::History seed;seed.identity={"EventPeer",0,0,0x20};seed.revision=20;Data::State state;state.completed=4;state.legacyClosed=true;Data::Append(seed,{0,0,state});Check(Data::AtomicWrite(peers[0].root/"RMD-MP"/(Data::Key(seed.identity)+".json"),seed,error),"event seed failed");Load(0,eventSave);
        const auto target=actual->quests[4].target;for(int n=0;n<target;++n)Killed(&game,&heroes[0],uint32_t(n+1),1,0,-1);Pump();Check(Inspect(&game,&heroes[0]).objectiveReady,"kill objective not observed");
        SetRewardClaim(+[](Game*,Unit*,int quest){Check(quest==5,"wrong bonus quest");++awards;return true;});auto replay=CommandFor(0,5);Action(0,5);Command(&game,&heroes[0],reinterpret_cast<uint8_t*>(&replay),sizeof(replay));Pump();Check(awards==1&&Data::Current(peers[0].store->history).completed==5,"bonus was duplicated or not persisted");
    }
    Destroyed(&game);for(auto& peer:peers)peer.store.reset();clientStore.reset();
    // Store leases above intentionally remain live; cleanup after their destructors.
    std::cout<<"PASS: "<<checks<<" quest checks; existing journals, two-client TCP/IP sync, no checksum gating, durable writes, writer leases, replay and failure recovery. Fixtures: "<<root<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
