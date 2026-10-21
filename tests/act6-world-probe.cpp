// Manual smoke-test DLL, ONLY for the isolated startup-live-20261020 process.
// Not linked into the HUD or normal regression tests. Never loads a save.
#include <Windows.h>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <string>
uintptr_t gameBase;
template<class T>T At(unsigned rva){return reinterpret_cast<T>(gameBase+rva);}
template<class T>T F(void* p,size_t off){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+off);}
extern "C" __declspec(dllexport) DWORD WINAPI Run(void*) {
 wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
 if(!wcsstr(path,L"\\d2rhud-act6-native\\startup-live-20261020\\D2R.exe"))return 90;
 gameBase=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
 if(*At<uint8_t*>(0x1dc76f8)!=0 || *At<uint64_t*>(0x1c9ea88)!=6)return 91;
 wchar_t temp[32768]{};GetTempPathW(32768,temp);std::wstring logfile=std::wstring(temp)+L"d2rhud-act6-native\\world-probe.txt";
 std::ofstream log(logfile);log<<std::unitbuf;
 auto automap=At<int(__fastcall*)(int,int,int,int)>(0x228230);
 const int left=automap(26,1,1,0),right=automap(26,2,1,0);
 log<<"Refuge native automap wall frames: "<<left<<","<<right<<"\n";
 if(left!=61 || right!=60)return 96;
 using Allocate=void*(__fastcall*)(uint8_t,uint32_t,int,void*,uint8_t,int,void*,void*);
 for(uint8_t difficulty=0;difficulty<3;++difficulty)for(uint32_t seed:{0x13579u,0x6ace1u,0xdeadbu}) {
 log<<"Difficulty "<<unsigned(difficulty)<<" seed "<<seed<<"\n";
 log<<"Allocating independent server DRLG, Act VI, without a character/game.\n";
 auto act=At<Allocate>(0x1d0740)(5,seed,0,nullptr,difficulty,240,nullptr,nullptr);
 if(!act){log<<"NULL act\n";return 92;}
 auto drlg=F<void*>(act,0x70);
 int entryX=-1,entryY=-1;
 auto entry=At<void*(__fastcall*)(void*,int,int,int*,int*)>(0x25a080)(drlg,240,11,&entryX,&entryY);
 log<<"Entry: "<<entryX<<","<<entryY<<" room "<<entry<<"\n";
 if(!entry || entryX<0 || entryY<0)return 94;
 log<<"Act and town layout created.\n";
 for(int id=240;id<249;++id){
  log<<"Generating level "<<id<<"\n";
  void* level=nullptr;
  for(auto p=F<void*>(drlg,0x868);p;p=F<void*>(p,0x1b8))if(F<int>(p,0x1f8)==id){level=p;break;}
  if(!level)level=At<void*(__fastcall*)(void*,int)>(0x1e7f70)(drlg,id);
  if(!F<void*>(level,0x10))At<void(__fastcall*)(void*)>(0x1e8390)(level);
  unsigned rooms=0,waypoints=0,stashes=0,npcs=0;
  for(auto room=F<void*>(level,0x10);room && rooms<4096;room=F<void*>(room,0x48)){
   auto flags=F<uint32_t>(room,0x50);
   if(!(flags&(1u<<24)))At<void(__fastcall*)(void*)>(0x26f360)(room);
   flags=F<uint32_t>(room,0x50);
   if(!(flags&(1u<<25)) && F<int>(room,0x74)==2)At<void(__fastcall*)(void*)>(0x25d830)(room);
   if(!(F<uint32_t>(room,0x50)&(1u<<20)))At<void(__fastcall*)(void*)>(0x241e50)(room);
   if(id==240)for(auto unit=F<void*>(room,0x98);unit;unit=F<void*>(unit,0x10)){
    const int type=F<int>(unit,0x20),index=F<int>(unit,4);
    log<<"Town preset type="<<type<<" id="<<index<<"\n";
    if(type==2 && index==238)++waypoints;if(type==2 && index==267)++stashes;if(type==1)++npcs;
   }
   ++rooms;
  }
  log<<"Level "<<id<<": "<<rooms<<" rooms activated.\n";
  if(!rooms || rooms>=4096)return 93;
  if(id==240 && (waypoints!=1 || stashes!=1 || npcs!=4))return 95;
 }
 log<<"All nine levels generated with native room tiles. Releasing independent Act.\n";
 At<void(__fastcall*)(void*)>(0x1d0a20)(act);
 }
 log<<"PASS: nine seed/difficulty combinations; no character or save loaded.\n";return 0;
}
BOOL WINAPI DllMain(HINSTANCE,DWORD,void*){return TRUE;}
