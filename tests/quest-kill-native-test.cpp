#include "../d2rhud/plugin/QuestKillEvents.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
uintptr_t imageBase=0;
DWORD64 Pattern::Address(uint32_t rva){return imageBase+rva;}
struct Credit {D2UnitStrc* owner;uint32_t id,seed;int monster,unique;};
std::vector<Credit> credits;
namespace QuestJournal {void Killed(D2GameStrc*,D2UnitStrc* owner,uint32_t id,uint32_t seed,int monster,int unique){credits.push_back({owner,id,seed,monster,unique});}}
namespace Fixture {
using namespace QuestKillEvents;
Unit player{},pet{},victim{},stranger{};Game game{},otherGame{};
D2MonsterDataStrc monster{},petMonster{};
std::array<uint8_t,0x1dc> definition{};std::array<uint8_t,0x70> ai{},victimAi{};
bool exists=true,transition=true,removeOnDeath=false;int xp=0,quests=0;bool friendly=false;
template<class T>void Put(void* p,size_t off,T value){memcpy(static_cast<uint8_t*>(p)+off,&value,sizeof(value));}
Unit* __fastcall Find(Game* g,int type,uint32_t id){assert(g==&game);for(auto unit:{&player,&pet,&stranger})if(unit->dwUnitType==uint32_t(type)&&unit->dwUnitId==id)return unit;if(type==1&&id==victim.dwUnitId&&exists)return &victim;return nullptr;}
void Reset(){
    player={};pet={};victim={};stranger={};monster={};petMonster={};definition={};ai={};victimAi={};game={};otherGame={};credits.clear();exists=transition=true;removeOnDeath=friendly=false;xp=quests=0;
    player.dwUnitType=0;player.dwUnitId=10;player.dwAnimMode=1;
    stranger.dwUnitType=0;stranger.dwUnitId=11;stranger.dwAnimMode=1;
    pet.dwUnitType=1;pet.dwUnitId=20;pet.dwAnimMode=1;pet.pMonsterData=&petMonster;
    Put(&petMonster,0x30,ai.data());Put(ai.data(),0x58,&game);Put(ai.data(),0x60,player.dwUnitId);Put(ai.data(),0x64,int(0));
    victim.dwUnitType=1;victim.dwUnitId=30;victim.dwAnimMode=1;victim.dwClassId=0;victim.pMonsterData=&monster;victim.pSeed.dwSeed[0]=1234;
    monster.pMonstatsTxt=reinterpret_cast<D2MonStatsTxt*>(definition.data());monster.nTypeFlag=2;monster.wBossHcIdx=42;
    Put(definition.data(),0x3c,uint32_t(0x8000));
}
void Run(const std::filesystem::path& executable){
    std::ifstream input(executable,std::ios::binary|std::ios::ate);assert(input);std::vector<uint8_t> bytes(size_t(input.tellg()));input.seekg(0);assert(input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(bytes.data()+dos->e_lfanew);
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
    const auto sections=IMAGE_FIRST_SECTION(nt);for(unsigned n=0;n<nt->FileHeader.NumberOfSections;++n)memcpy(image+sections[n].VirtualAddress,bytes.data()+sections[n].PointerToRawData,sections[n].SizeOfRawData);
    imageBase=reinterpret_cast<uintptr_t>(image);assert(Supported());for(const auto& sig:signatures){image[sig.rva]^=1;assert(!Supported());image[sig.rva]^=1;}
    assert(Install());Uninstall();assert(Supported());
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);const auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    Put(image,0x1c9f878,definition.data());Put(image,0x1c9f880,uint64_t(1));
    stub(0x2c49c0,Find);stub(0x1e5620,+[](Unit*,uint32_t stat,uint16_t)->int{assert(stat==172);return friendly?1:0;});
    stub(0x32bba0,+[](Game* g,Unit* attacker,Unit* target){assert(g==&game&&attacker&&target==&victim);++xp;});
    stub(0x32d880,+[]()->int{return 0;});stub(0x20b9b0,+[]()->int{return 0;});
    stub(0x32fd20,+[](Game* g,void* mode,int)->int{assert(g==&game);auto target=Field<Unit*>(mode,8);assert(target==&victim);if(transition)target->dwAnimMode=0;if(removeOnDeath)exists=false;return transition?1:0;});
    stub(0x2a1a80,+[](Game*,Unit* target,Unit*){assert(target==&victim);++quests;});stub(0x664a0,+[]()->int{return 0;});
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);original=reinterpret_cast<Kill>(image+0x328000);installed=true;
    Reset();Hook(&game,&victim,&player,1);assert(credits.size()==1&&credits[0].owner==&player&&credits[0].id==30&&credits[0].seed==1234&&credits[0].monster==0&&credits[0].unique==42&&xp==1&&quests==1);
    Hook(&game,&victim,&player,1);assert(credits.size()==1&&xp==1); // Native mode gate rejects duplicate calls.
    Reset();Hook(&game,&victim,&pet,1);assert(credits.size()==1&&credits[0].owner==&player&&xp==1);
    Reset();Put(ai.data(),0x58,&otherGame);Hook(&game,&victim,&pet,1);assert(credits.empty());
    Reset();Put(ai.data(),0x60,pet.dwUnitId);Put(ai.data(),0x64,int(1));Hook(&game,&victim,&pet,1);assert(credits.empty()); // owner cycle
    Reset();transition=false;Hook(&game,&victim,&player,1);assert(credits.empty()&&victim.dwAnimMode==1);
    Reset();removeOnDeath=true;Hook(&game,&victim,&player,1);assert(credits.size()==1&&!exists);
    Reset();friendly=true;Hook(&game,&victim,&player,1);assert(credits.empty());
    Reset();Put(definition.data(),0x3c,uint32_t(0));Hook(&game,&victim,&player,1);assert(credits.empty()&&!xp&&!quests);
    Reset();victim.dwFlags=0x80000000u;Hook(&game,&victim,&player,1);assert(credits.empty()&&!quests);
    Reset();victim.dwAnimMode=12;Hook(&game,&victim,&player,1);assert(credits.empty()&&!xp);
    Reset();Hook(&game,&victim,nullptr,1);assert(credits.empty()&&!xp);
    installed=false;original=nullptr;imageBase=0;VirtualFree(image,0,MEM_RELEASE);
    std::cout<<"Native kill transition, duplicate/dead/nonkillable/revive/friendly exclusion, player/pet owner attribution, cyclic/foreign ownership, failed transition and synchronous corpse removal passed.\n";
}
}
int main(int argc,char** argv){assert(argc==2);Fixture::Run(argv[1]);return 0;}
