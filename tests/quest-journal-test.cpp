#define NOMINMAX
#include "../d2rhud/plugin/QuestJournal.cpp"
#include <iostream>
#include <cassert>

std::map<uint32_t,uintptr_t> bindings;
DWORD64 Pattern::Address(uint32_t rva){return bindings[rva];}
using namespace QuestJournal;
int checks=0;
void Check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
template<class T>void Put(void* p,size_t offset,T value){memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
std::string ReadText(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
std::string Source(){return "description\tenabled\tnuminputs\top\tparam\tvalue\tinput 1\tinput 2\toutput\toutput b\nQuest 1a\t1\t2\t6\t398\t1\tZ01\tgemz\tuseitem\tBt01\nQuest 1b\t1\t2\t18\t398\t1\tBarrier1\tBt01\tuseitem\t\nQuest 2a\t1\t2\t18\t398\t1\tZ01\tZ08\tuseitem\tBt02\nQuest 2b\t1\t2\t18\t398\t2\tBarrier1\tBt02\tuseitem\t\n";}
Game game{};Unit hero{},secondHero{},statue{},proof{};D2PlayerDataStrc playerData{},otherData{};Client client{},otherClient{};D2InventoryStrc inventory{},otherInventory{};D2ItemDataStrc statueData{},proofData{};
std::vector<Data::Packet> packets;Unit* local=&hero;uint32_t selector=0;int legacyStage=2,recovered=0;bool recoveredOkay=true;
void __fastcall CaptureSend(void*,uint32_t,void** range){auto begin=static_cast<uint8_t*>(range[0]);auto end=static_cast<uint8_t*>(range[1]);Check(end-begin==sizeof(Data::Packet),"wrong packet length");Data::Packet p;memcpy(&p,begin,sizeof(p));Check(Data::Valid(p),"invalid server snapshot");packets.push_back(p);}
std::array<uint8_t,0x200> Save(const char* name,uint32_t checksum=100){std::array<uint8_t,0x200> bytes{};Put(bytes.data(),0,uint32_t(0xaa55aa55));Put(bytes.data(),4,uint32_t(98));Put(bytes.data(),8,uint32_t(bytes.size()));Put(bytes.data(),12,checksum);Put(bytes.data(),0x2c,uint32_t(12345));bytes[0x28]=0;Put(bytes.data(),0x24,uint32_t(0x20));strcpy_s(reinterpret_cast<char*>(bytes.data()+0x10b),61,name);return bytes;}
void Load(Unit& player,Client& owner,std::array<uint8_t,0x200>& bytes){std::array<uint64_t,2> block{reinterpret_cast<uint64_t>(bytes.data()),bytes.size()};owner.pSaveHeaders=reinterpret_cast<D2SaveHeadersStrc*>(block.data());BeforeLoad(&game,&owner);AfterLoad(&game,&owner,&player,true);owner.pSaveHeaders=nullptr;}
Data::Packet Snapshot(uint32_t recipient=1){packets.clear();QuestJournal::sent.erase(&game);Tick(&game);auto p=std::find_if(packets.begin(),packets.end(),[&](const auto& packet){return packet.recipient==recipient;});Check(p!=packets.end(),"snapshot missing");return *p;}
void CommandAction(uint8_t action,int proposal=-1){auto p=Snapshot();Data::CommandPacket command;command.recipient=p.recipient;command.session=p.session;command.sequence=p.sequence;command.action=action;command.proposal=uint8_t(proposal<0?(action>=4?p.completed+1:p.legacyCompleted):proposal);Check(Command(&game,&hero,reinterpret_cast<uint8_t*>(&command),sizeof(command)),"command not recognized");}
#include "quest-journal-events-test.inl"
#include "quest-first-save-test.inl"
int main(int argc,char** argv){try{
    const auto root=std::filesystem::temp_directory_path()/("d2rhud-quest-tests-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));std::filesystem::create_directories(root);SetEnvironmentVariableW(L"LOCALAPPDATA",root.c_str());
    auto rules=Data::ParseRules(Source(),"{\"version\":1,\"quests\":[{\"id\":1,\"title\":\"Gem lesson\"}]}");
    Check(rules->error.empty()&&rules->quests.size()==2&&rules->rows==4&&rules->gates.size()==4,"quest source binding failed");Check(rules->quests[0].title=="Gem lesson","text override failed");
    auto bad=Data::ParseRules(Source(),"{\"version\":1,\"quests\":[{\"id\":1},{\"id\":1}]}");Check(!bad->error.empty(),"duplicate descriptions accepted");
    Check(!Data::ParseRules("bad\n","")->error.empty(),"malformed table accepted");
    auto lf=Source();lf.erase(std::remove(lf.begin(),lf.end(),'\r'),lf.end());auto crlf=lf;for(size_t at=0;(at=crlf.find('\n',at))!=crlf.npos;at+=2)crlf.insert(at,"\r");Check(Data::ParseRules(lf,"")->digest==Data::ParseRules(crlf,"")->digest,"line endings changed quest identity");
    Data::State state;Check(!Data::Allows(state,rules->gates[1],2),"proof transfer bypassed objective");Check(Data::Advance(state,rules->gates[0],2),"objective did not advance");Check(!Data::Advance(state,rules->gates[0],2),"objective repeated");Check(Data::Advance(state,rules->gates[1],2),"claim failed");Check(!Data::Advance(state,rules->gates[1],2),"duplicate reward accepted");
    Data::State imported;Check(Data::Import(imported,2,2)&&imported.completed==2,"legacy import failed");Check(!Data::Import(imported,1,2),"second import accepted");Data::State uncertain;uncertain.uncertain=true;Check(!Data::Advance(uncertain,rules->gates[0],2),"ambiguous transaction advanced");
    Data::History history;history.identity={"Hero",12345,0,0x20};history.digest=rules->digest;Data::Append(history,{100,512,{}});Data::Append(history,{200,512,state});auto encoded=Data::Encode(history);auto decoded=Data::Decode(encoded,2);Check(decoded&&decoded->identity.name=="Hero"&&Data::Find(*decoded,100,512)==Data::State{},"checkpoint rollback lost");Check(Data::Find(*decoded,200,512)==state,"checkpoint state lost");Check(!Data::Find(*decoded,999,512),"wrong save accepted");Check(!Data::Decode("{}",2),"empty journal accepted");
    for(unsigned n=0;n<40;++n)Data::Append(history,{n,512,{}});Check(history.checkpoints.size()==32,"unbounded checkpoint history");std::string error;Check(Data::AtomicWrite(root/"history.json",history,error),"atomic history write failed");Check(Data::Decode(ReadText(root/"history.json"),2).has_value(),"written history invalid");
    hero.dwUnitType=secondHero.dwUnitType=0;hero.dwUnitId=1;secondHero.dwUnitId=2;hero.dwAnimMode=secondHero.dwAnimMode=1;hero.pPlayerData=&playerData;secondHero.pPlayerData=&otherData;hero.pInventory=&inventory;secondHero.pInventory=&otherInventory;inventory.dwSignature=otherInventory.dwSignature=0x1020304;inventory.pOwner=&hero;otherInventory.pOwner=&secondHero;
    playerData.pClient=&client;otherData.pClient=&otherClient;client.pGame=otherClient.pGame=&game;client.pPlayer=&hero;otherClient.pPlayer=&secondHero;client.dwUnitGUID=1;otherClient.dwUnitGUID=2;client.dwClientState=otherClient.dwClientState=4;
    bindings[0x1d442d8]=uintptr_t(&selector);bindings[0x6b050]=uintptr_t(+[](uint32_t)->Unit*{return local;});bindings[0x18682b0]=1;bindings[0x422ac0]=uintptr_t(CaptureSend);
    uint64_t uniqueCount=20,setCount=20;bindings[0x1c9fc78]=uintptr_t(&uniqueCount);bindings[0x1c9fc58]=uintptr_t(&setCount);
    void* linker=reinterpret_cast<void*>(1);bindings[0x1c9fc68]=uintptr_t(&linker);bindings[0x6d0470]=uintptr_t(+[](void*,const char* name)->int{return !strcmp(name,"Barrier1")?10:-1;});bindings[0x1e5620]=uintptr_t(+[](Unit* item,uint32_t stat,uint16_t)->int{return item==&statue&&stat==398?legacyStage:0;});
    Configure(root,"RMD-MP",Source(),"");SetAvailable(true);auto bytes=Save("Hero");Load(hero,client,bytes);Check(Inspect(&game,&hero).available,"new journal unavailable");
    auto first=Snapshot();Check(first.completed==0&&!first.ready&&(first.flags&2),"initial snapshot wrong");Check(Receive(reinterpret_cast<uint8_t*>(&first),sizeof(first))&&CurrentView().character=="Hero","client journal missing");
    auto gate=Bindings()->gates[0];Check(Begin(&game,&hero,gate),"objective transaction rejected");Finish(&game,&hero,gate,Outcome::Rejected);Check(!Inspect(&game,&hero).objectiveReady,"refusal advanced objective");Check(Begin(&game,&hero,gate),"retry rejected");Finish(&game,&hero,gate,Outcome::Committed);Check(Inspect(&game,&hero).objectiveReady,"successful objective missing");
    auto otherBytes=Save("Other");Load(secondHero,otherClient,otherBytes);Check(Inspect(&game,&secondHero).completed==0&&!Inspect(&game,&secondHero).objectiveReady,"progress leaked to another character");
    auto claim=Bindings()->gates[1];Check(!Begin(&game,&secondHero,claim),"transferred proof granted reward to another character");Check(Begin(&game,&hero,claim),"turn-in refused");Finish(&game,&hero,claim,Outcome::Committed);Check(Inspect(&game,&hero).completed==1&&!Begin(&game,&hero,claim),"turn-in repeated");
    auto saved=Save("Hero",200);Saved(&game,&hero,saved.data(),saved.size());Removed(&game,&hero);Load(hero,client,saved);Check(Inspect(&game,&hero).available&&Inspect(&game,&hero).completed==1,"saved progress did not reload");
    // Native save commit failure/old backup: original checksum restores original
    // inventory and its original quest checkpoint, rather than replaying claims.
    Removed(&game,&hero);Load(hero,client,bytes);Check(Inspect(&game,&hero).completed==0&&!Inspect(&game,&hero).objectiveReady,"old save matched newer checkpoint");
    statue.dwUnitType=4;statue.dwUnitId=50;statue.pItemData=&statueData;statueData.dwQualityNo=7;statueData.dwFileIndex=10;statueData.pExtraData.pParentInv=&inventory;inventory.pFirstItem=&statue;
    {
        const auto previous=CompiledNames::database.load();auto names=std::make_shared<CompiledNames::Database>();
        (*names)[0x1c9fc68]["barrier1"]=10;(*names)[0x1c9fc48]["test set"]=3;CompiledNames::database=names;linker=nullptr;
        Check(LegacyCompleted(&hero,2)==1,"BIN-only legacy import could not resolve Barrier1");
        Definition::Item condition;condition.unique="Barrier1";Check(Matches(&statue,condition),"BIN-only unique objective failed");
        --uniqueCount;(*names)[0x1c9fc68]["barrier1"]=99;Check(!Matches(&statue,condition)&&!LegacyCompleted(&hero,2),"invalid compiled unique metadata accepted");
        ++uniqueCount;(*names)[0x1c9fc68]["barrier1"]=10;
        condition.unique.clear();condition.set="Test Set";statueData.dwQualityNo=5;statueData.dwFileIndex=3;Check(Matches(&statue,condition),"BIN-only set objective failed");
        statueData.dwQualityNo=7;statueData.dwFileIndex=10;linker=reinterpret_cast<void*>(1);CompiledNames::database=previous;
    }
    auto legacy=Snapshot();Check(legacy.legacyCompleted==1,"legacy statue not detected");CommandAction(1,2);Check(Inspect(&game,&hero).completed==0,"forged legacy proposal accepted");CommandAction(1);Check(Inspect(&game,&hero).completed==1,"explicit legacy import failed");CommandAction(1);Check(Inspect(&game,&hero).completed==1,"legacy import repeated");
    gate=Bindings()->gates[2];Check(Begin(&game,&hero,gate),"second objective refused");Finish(&game,&hero,gate,Outcome::Ambiguous);Check(!Inspect(&game,&hero).available,"partial outputs did not lock progress");auto partial=Save("Hero",300);Saved(&game,&hero,partial.data(),partial.size());Removed(&game,&hero);Load(hero,client,partial);Check(!Inspect(&game,&hero).available&&(Snapshot().flags&1),"ambiguous lock was not persistent");
    // Mismatched/corrupt state is never silently replaced or reset.
    auto mismatch=Save("Hero",999);Removed(&game,&hero);Load(hero,client,mismatch);Check(!Inspect(&game,&hero).available,"mismatched save accepted");
    auto& session=sessions.at({&game,hero.dwUnitId});auto journalPath=session.path;const auto before=ReadText(journalPath);Saved(&game,&hero,mismatch.data(),mismatch.size());Check(ReadText(journalPath)==before,"mismatch overwrote journal");
    std::ofstream corrupt(journalPath);corrupt<<"bad";corrupt.close();Removed(&game,&hero);Load(hero,client,bytes);Check(!Inspect(&game,&hero).available,"corrupt file reset progress");Saved(&game,&hero,bytes.data(),bytes.size());Check(ReadText(journalPath)=="bad","corrupt file overwritten");
    Check(Begin(&game,&secondHero,Bindings()->gates[0]),"secondHero objective rejected");Finish(&game,&secondHero,Bindings()->gates[0],Outcome::Committed);
    std::array<uint8_t,0x1b4> base{};Put(base.data(),0x80,uint32_t(0x31307442));auto table=base.data();uint64_t itemCount=1;bindings[0x1ca0390]=uintptr_t(&table);bindings[0x1ca0398]=uintptr_t(&itemCount);
    SetProofRecovery(+[](Game*,Unit* player,int quest)->bool{++recovered;Check(player==&secondHero&&quest==1,"wrong proof recovery recipient");if(!recoveredOkay)return false;proof.dwClassId=0;proof.dwUnitType=4;proof.pItemData=&proofData;proofData.pExtraData.pParentInv=&otherInventory;otherInventory.pFirstItem=&proof;return true;});
    auto requestProof=[&](){auto packet=Snapshot(2);Data::CommandPacket c;c.recipient=2;c.session=packet.session;c.sequence=packet.sequence;c.action=3;Command(&game,&secondHero,reinterpret_cast<uint8_t*>(&c),sizeof(c));};
    requestProof();Check(recovered==1&&Inspect(&game,&secondHero).objectiveReady&&Inspect(&game,&secondHero).completed==0,"proof recovery changed progress");requestProof();Check(recovered==1,"proof recovery duplicated an existing token");
    Destroyed(&game);Check(!Inspect(&game,&secondHero).available,"destroyed game retained session");
    if(argc>1){auto real=Data::ParseRules(ReadText(argv[1]),argc>2?ReadText(argv[2]):"");Check(real->error.empty()&&real->quests.size()==45&&real->gates.size()==134,"actual mod quest coverage failed");for(int n=1;n<=45;++n){Data::State progress;progress.completed=n-1;auto a=std::find_if(real->gates.begin(),real->gates.end(),[&](const auto& g){return g.quest==n&&!g.turnIn;});auto b=std::find_if(real->gates.begin(),real->gates.end(),[&](const auto& g){return g.quest==n&&g.turnIn;});Check(Data::Advance(progress,*a,45)&&Data::Advance(progress,*b,45)&&progress.completed==n,"actual quest chain cannot advance");Check(!Data::Advance(progress,*b,45),"actual quest reward can repeat");}}
    if(argc>2)EventTests(root,argv[1],argv[2]);
    if(argc>4)FirstSaveProfileTest(root,argv[1],argv[2],argv[3],argv[4]);
    // Deletion stays inside this test's unique, verified temporary directory.
    Check(root.is_absolute()&&std::filesystem::equivalent(root.parent_path(),std::filesystem::temp_directory_path())&&root.filename().wstring().starts_with(L"d2rhud-quest-tests-"),"unsafe test cleanup path");std::filesystem::remove_all(root);
    std::cout<<"PASS: "<<checks<<" journal checks; character separation, lifecycle saves/rollback, explicit migration, duplicate rewards, partial failures, packets and all 45 actual quests\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
