#define NOMINMAX
#include <Windows.h>
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include "../d2rhud/Pattern.h"

static unsigned checks=0;
static void Check(bool value,const char* name){++checks;if(!value){std::cerr<<"FAIL: "<<name<<'\n';std::abort();}}
static uint8_t* Memory(){static auto memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));return memory;}
static std::atomic<int> nativeHits=0,queuedHits=0;
static DWORD executedOn=0;
static std::vector<std::string> commands;
static void __fastcall DebugCommand(const char* text){commands.emplace_back(text);executedOn=GetCurrentThreadId();}
DWORD64 Pattern::Address(uint32_t rva){if(rva==0x76bc0)return reinterpret_cast<DWORD64>(&DebugCommand);return reinterpret_cast<DWORD64>(Memory()+rva);}

// Compile the production modules, exposing internal state only to this native
// regression fixture. Native game calls are replaced; Win32 and ImGui are real.
#include "../d2rhud/plugin/D2RHUD/HudCommands.cpp"
#include "../d2rhud/plugin/D2RHUD/CombatFeedback.cpp"
#include "../d2rhud/plugin/D2RHUD/MonsterStatsOverlay.cpp"

static std::atomic<bool> feedback=false;
namespace FloatingDamage {
bool IsEnabled(){return feedback.load();}
void QueueGameDamage(int,float,float,uint32_t,uint32_t,Kind,Element,uint64_t){++queuedHits;}
}
namespace SunderSystem {int EffectiveResistance(D2GameStrc*,D2UnitStrc*,uint32_t,int value){return value;}}
static D2UnitStrc* localPlayer=nullptr;
D2UnitStrc* GetClientPlayerUnit(){return localPlayer;}
static void __fastcall NativeDamage(void*,D2UnitStrc*,D2UnitStrc*,int,int,void*,int,void*,char,void*){++nativeHits;}
static int32_t __fastcall UnitStat(D2UnitStrc*,uint32_t stat,uint16_t){return stat==STAT_HITPOINTS?25600:stat==STAT_MAXHP?0:stat==361?1:25;}
static unsigned serverLookups=0,clientLookups=0;
static D2UnitStrc hovered{};
static D2UnitStrc* __fastcall ServerLookup(D2GameStrc* game,D2C_UnitTypes,uint32_t){Check(game!=nullptr,"no null server lookup");++serverLookups;return &hovered;}
static D2UnitStrc* __fastcall ClientLookup(uint64_t,uint32_t,uint32_t,uint32_t){++clientLookups;return &hovered;}

static void ScheduleTests(){
    using Schedule=HudCommands::StartupSchedule;
    Schedule schedule;Schedule::Commands list{"one","two","disabled","","five","six"};
    Check(schedule.Configure(list,0),"configure first schedule");
    Check(!schedule.TakeDue(9000),"no startup commands outside game");
    Check(schedule.SetSession(1,100),"entry creates session");
    Check(!schedule.TakeDue(3099),"entry delay");
    Check(schedule.TakeDue(3100)=="one","first command");
    Check(!schedule.TakeDue(3100),"no same-frame burst");
    Check(schedule.TakeDue(50000)=="two","slow frame takes one command");
    Check(!schedule.TakeDue(50000),"slow frame does not burst remainder");
    Check(!schedule.Configure(list,50000),"same config does not restart");
    schedule.SetSession(0,50001);Check(!schedule.TakeDue(99999),"exit cancels old run");
    schedule.SetSession(2,60000);Check(schedule.TakeDue(63000)=="one","new game starts at first command");
    list[0]="new";schedule.Configure(list,64000);
    Check(!schedule.TakeDue(66999),"edit gets fresh delay");
    Check(schedule.TakeDue(67000)=="new","edit replaces old run");
    schedule.SetSession(3,68000);Check(schedule.TakeDue(71000)=="new","rapid session change resets schedule");
    Check(schedule.TakeDue(72000)=="two","second command interval");
    Check(!schedule.TakeDue(73000),"disabled slot ignored");
    Check(!schedule.TakeDue(74000),"empty slot ignored");
    Check(schedule.TakeDue(75000)=="five","slot timing preserved");
}
static LRESULT CALLBACK TestWindow(HWND window,UINT message,WPARAM w,LPARAM l){
    if(HudCommands::HandleMessage(window,message,w,l))return 0;
    return DefWindowProcW(window,message,w,l);
}
static void Pump(){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
static void CommandTests(){
    WNDCLASSW type{};type.lpfnWndProc=TestWindow;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"D2RHUD command regression";
    Check(RegisterClassW(&type)!=0,"register hidden test window");
    const auto window=CreateWindowExW(0,type.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,type.hInstance,nullptr);
    Check(window!=nullptr,"create message-only test window");
    auto& status=*reinterpret_cast<uint8_t*>(Memory()+0x1dc76f8);
    auto& session=*reinterpret_cast<uint64_t*>(Memory()+0x1d617d0);
    status=1;session=10;HudCommands::Tick(window);
    const DWORD owner=GetCurrentThreadId();
    std::thread producer([]{Check(HudCommands::Queue("save 1"),"queue from worker");});producer.join();
    Check(commands.empty(),"native command does not run on producer thread");Pump();
    Check(commands.size()==1 && executedOn==owner && commands[0]=="save 1","native command runs on window owner");
    HudCommands::Queue("stale");session=11;Pump();
    Check(commands.size()==1,"game change cancels posted native command before next render");
    HudCommands::Tick(window);Check(HudCommands::Queue("fresh"),"new session queue");Pump();
    Check(commands.back()=="fresh","new session survives old message");
    auto& loading=Memory()[0x1cb410a];loading=1;
    HudCommands::Queue("after loading");Pump();Check(commands.size()==2,"loading screen defers native commands");
    loading=0;HudCommands::Tick(window);Pump();Check(commands.back()=="after loading","queued command resumes after loading");
    for(unsigned i=0;i<64;++i)Check(HudCommands::Queue("bounded"),"bounded queue accepts capacity");
    Check(!HudCommands::Queue("overflow"),"bounded queue rejects overflow");
    status=0;Pump();HudCommands::Tick(window);Check(commands.size()==3,"exit cancels entire backlog");
    Check(!HudCommands::Queue("not in game"),"manual command rejected outside game");
    Check(HudCommands::TrimToken(" \t [tab] \r\n")=="[tab]","command trimming");
    HudCommands::Shutdown();DestroyWindow(window);UnregisterClassW(type.lpszClassName,type.hInstance);
}
static LONG failStage=0;static int aborts=0,commits=0,attaches=0;
static LONG WINAPI Begin(){return failStage==1?ERROR_ACCESS_DENIED:NO_ERROR;}
static LONG WINAPI Update(HANDLE){return failStage==2?ERROR_ACCESS_DENIED:NO_ERROR;}
static LONG WINAPI Attach(PVOID*,PVOID){++attaches;return failStage==3?ERROR_ACCESS_DENIED:NO_ERROR;}
static LONG WINAPI Commit(){++commits;return failStage==4?ERROR_ACCESS_DENIED:NO_ERROR;}
static LONG WINAPI Abort(){++aborts;return NO_ERROR;}
static void TransactionTests(){
    for(failStage=0;failStage<=4;++failStage){
        aborts=commits=attaches=0;bool installed=false;
        {HudHooks::Transaction transaction({Begin,Update,Attach,Commit,Abort});auto function=&DebugCommand;
            if(transaction.Attach(function,DebugCommand) && transaction.Commit())installed=true;}
        Check(installed==(failStage==0),"installation success requires complete transaction");
        Check(aborts==((failStage==2 || failStage==3)?1:0),"incomplete transaction aborts exactly once");
        Check(commits==(failStage==0 || failStage==4?1:0),"commit only after update and attach");
    }
}
static void CombatTests(){
    using namespace HudCombat;
    oDamageInfo=NativeDamage;
    auto guard=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);Check(guard!=nullptr,"guard-page fixture");
    const auto before=std::chrono::steady_clock::now();
    for(unsigned i=0;i<100000;++i)Hooked_DamageInfo(guard,reinterpret_cast<D2UnitStrc*>(guard),reinterpret_cast<D2UnitStrc*>(guard),1,1,guard,256,guard,0,guard);
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();
    Check(nativeHits==100000 && queuedHits==0,"disabled damage calls native once and never reads guarded arguments");
    Check(!GetClientUnitByIdAndType(UINT32_MAX,UINT32_MAX),"unset poison target rejected");
    Check(!GetClientUnitByIdAndType(1,5),"unsupported unit type rejected");
    D2UnitStrc unit{};unit.dwUnitType=UNIT_MONSTER;unit.dwUnitId=1;unit.pListNext=&unit;
    auto table=reinterpret_cast<D2UnitStrc**>(Memory()+0x1d446e0);table[1]=&unit;
    Check(GetClientUnitByIdAndType(1,UNIT_MONSTER)==&unit,"typed client lookup");
    table[2]=&unit;Check(!GetClientUnitByIdAndType(2,UNIT_MONSTER),"cyclic bucket is bounded");table[2]=nullptr;
    Check(!IsCommittedReadableMemory(reinterpret_cast<void*>(UINTPTR_MAX-8),32),"overflowing readable span rejected");
    Check(!IsCommittedReadableMemory(guard,1),"guarded memory rejected");
    Check(ParseCombatLogDamageAmount("takes 123 fire damage")==123,"combat amount");
    Check(ParseCombatLogDamageAmount("999999999999999999999 poison damage")==INT_MAX,"reverse number overflow saturates");
    Check(ParseCombatLogDamageAmount("takes 999999999999999999999 fire damage")==INT_MAX,"forward number overflow saturates");
    std::string sender,body;
    Check(ParseChatLine("Player: hello",sender,body) && sender=="Player" && body=="hello","chat parse retains sender/body");
    Check(!ParseChatLine("Out of mana",sender,body),"system line is not player chat");
    g_targetElementCache[1]={FloatingDamage::Element::Poison,std::chrono::steady_clock::now()};
    g_lastNonPoisonDamageOnTarget[1]=std::chrono::steady_clock::now();
    Tick({1920,1080},false);Check(g_targetElementCache.empty() && g_lastNonPoisonDamageOnTarget.empty(),"leaving game clears all trackers");
    g_DamageInfoHookInstalled=g_BroadcastChatHookInstalled=true;STATLIST_GetUnitStatSigned=UnitStat;
    D2DynamicPathStrc path{};unit.pDynamicPath=&path;feedback=true;Tick({1920,1080},true,20);
    std::thread callbacks([&]{for(unsigned i=0;i<2000;++i)Hooked_DamageInfo(nullptr,&unit,&unit,1,1,nullptr,256,nullptr,0,nullptr);});
    for(unsigned i=0;i<2000;++i)Tick({1920,1080},true,20);
    callbacks.join();Check(g_targetElementCache.size()<=1,"concurrent frame/damage tracking remains bounded");
    feedback=false;Tick({1920,1080},true,21);
    Check(g_targetElementCache.empty() && g_poisonWatch.empty(),"new session drops old unit IDs");
    VirtualFree(guard,0,MEM_RELEASE);
    std::cout<<"100000 disabled native damage callbacks: "<<elapsed<<" ms (native forwarding stub; not an FPS benchmark).\n";
}
static void OverlayTests(){
    using namespace HudMonsterStats;
    Check(Data::FontIndex(1080,0)==-1,"empty font atlas");
    Check(Data::FontIndex(4320,1)==0,"single font on 8K display");
    Check(Data::FontIndex(4320,5)==4,"large display uses highest HUD font");
    Check(!Data::HealthPercent(100,0),"zero max HP guarded");
    Check(Data::HealthPercent(1,2)==50,"fractional HP remains precise");
    Check(Data::HealthPercent(INT_MAX,1)==int64_t(INT_MAX)*100,"HP percentage does not overflow");
    const auto context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1920,1080};io.DeltaTime=1.f/60;
    io.Fonts->AddFontDefault();io.Fonts->Build();ImGui::NewFrame();
    D2MouseHoverStruct hover{};hover.IsHovered=1;hover.HoveredUnitType=UNIT_MONSTER;hover.HoveredUnitId=1;gMouseHover=&hover;
    // Client native lookup lives at the fake game RVA and returns an actual fixture unit.
    auto location=Memory()+0x66510;DWORD old=0;VirtualProtect(location,16,PAGE_EXECUTE_READWRITE,&old);
    const uint8_t jump[]{0x48,0xb8};std::memcpy(location,jump,2);auto function=&ClientLookup;std::memcpy(location+2,&function,8);location[10]=0xff;location[11]=0xe0;FlushInstructionCache(GetCurrentProcess(),location,12);
    UNITS_GetServerUnitByTypeAndId=ServerLookup;D2GameStrc game{};localPlayer=&hovered;
    Draw(nullptr,false);Check(clientLookups==0 && serverLookups==0,"disabled overlay avoids lookups");
    Draw(nullptr,true);Check(clientLookups==1 && serverLookups==0,"joiner uses one client lookup with zero maxHP safely");
    Draw(&game,true);Check(serverLookups==1,"host uses one server lookup");
    UNITS_GetServerUnitByTypeAndId=nullptr;Draw(&game,true);
    Check(clientLookups==1,"missing server symbol cannot substitute a client-owned unit");
    UNITS_GetServerUnitByTypeAndId=ServerLookup;
    Check(LocalTerrorStat(&game)==1,"terror permissions read local player");
    Check(ImGui::GetFont()==io.Fonts->Fonts[0],"font restored after overlay");
    ImGui::EndFrame();ImGui::DestroyContext(context);localPlayer=nullptr;gMouseHover=nullptr;
}
int main(){ScheduleTests();CommandTests();TransactionTests();CombatTests();OverlayTests();std::cout<<"PASS: "<<checks<<" HUD runtime regression checks.\n";}
