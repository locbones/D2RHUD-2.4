#define CUBEMAP_UI_TEST
#define main MapNativeTestsMain
#include "cube-map-test.cpp"
#undef main
#include "../d2rhud/plugin/MapEventFonts.h"
#include <imgui_internal.h>
CubeMap::Text::FontBank eventFonts;
ImFont* TestEventFont(float size){return eventFonts.ForSize(size);}
Room* __fastcall EventRoom(Unit*){return reinterpret_cast<Room*>(fixture.r1[0].data());}
uint64_t TestArtwork(){return 2;}
int commands=0;std::vector<Survivors::RequestBody> attributeRequests;
void __fastcall CaptureEventCommand(void* bytes,int size){assert(size==sizeof(CommandPacket));if(static_cast<uint8_t*>(bytes)[9]==Survivors::Operation){auto p=static_cast<Survivors::Request*>(bytes);assert(Survivors::Valid(p->data));attributeRequests.push_back(p->data);return;}const auto packet=static_cast<CommandPacket*>(bytes);assert(packet->opcode==3 && packet->run==123 && packet->version==5);++commands;}
int main(int argc,char** argv){
    assert(!SurvivorsEnabled());
    assert(argc==3);Reset();Put(fixture.level.data(),0x1f8,int(109));
    uint32_t selector=0;labelPlayer=&fixture.objects[0];labelPlayer->dwUnitType=0;labelPlayer->dwUnitId=55;
    bindings[0x1d442d8]=reinterpret_cast<uintptr_t>(&selector);bindings[0x6b050]=reinterpret_cast<uintptr_t>(LabelPlayer);bindings[0x209530]=reinterpret_cast<uintptr_t>(EventRoom);bindings[0x10dce0]=reinterpret_cast<uintptr_t>(CaptureEventCommand);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DeltaTime=1.0f/60;
    for(float size:{12.0f,15.0f,18.0f,24.0f,36.0f,48.0f})assert(io.Fonts->AddFontFromFileTTF(argv[2],size));
    eventFonts.Load(*io.Fonts,argv[2]);SetFontProvider(TestEventFont);
    unsigned char* pixels=nullptr;int tw=0,th=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&tw,&th);io.Fonts->SetTexID(ImTextureID(1));
    view={};view.run=123;view.recipient=55;view.die=6;view.event=Event::Trial;view.eventPhase=EventPhase::WaitingTown;receivedAt=GetTickCount64();io.DisplaySize={1280,720};
    auto frame=[&](bool ready){receivedAt=GetTickCount64();ImGui::NewFrame();DrawCelebration(1.0f,ready);ImGui::Render();};
    Put(fixture.level.data(),0x1f8,int(2));frame(true);assert(!commands && !readySince);
    Put(fixture.level.data(),0x1f8,int(109));frame(false);assert(!commands && !readySince);frame(true);assert(!commands && readySince);
    readySince=GetTickCount64()-2990;frame(true);assert(!commands);readySince=GetTickCount64()-3000;frame(true);assert(commands==1);frame(true);assert(commands==1);
    std::filesystem::create_directories(argv[1]);std::ofstream atlas(std::filesystem::path(argv[1])/"atlas.rgba",std::ios::binary);atlas.write(reinterpret_cast<char*>(pixels),size_t(tw)*th*4);atlas.close();
    SetArtworkProvider(TestArtwork);
    int scenario=0;
    for(const auto display:{ImVec2{1280,720},ImVec2{3840,2160}})for(auto event:{Event::GoldenArmada,Event::ForsakenFestival,Event::FountainOfYouth,Event::SpeedDemon,Event::MidasMeddling,Event::GoblinGrotto,Event::Trial,Event::TreasureTrove,Event::GoldenGoose,Event::ChampionClash,Event::PlentifulPirating,Event::SoulSurprise})for(auto phase:{EventPhase::Ceremony,EventPhase::Briefing,EventPhase::Finished})for(uint32_t elapsed:{0u,1500u,2900u,4200u,7000u,9999u,10000u}){
        io.DisplaySize=display;ImGui::NewFrame();view.event=event;view.eventPhase=phase;view.eventElapsed=elapsed;view.success=1;view.reward=event==Event::Trial && phase==EventPhase::Finished ? 2 : 0;receivedAt=GetTickCount64();
        DrawCelebration(display.y/1080);ImGui::Render();auto data=ImGui::GetDrawData();
        if(phase==EventPhase::Finished && elapsed>6000){assert(!data->TotalVtxCount);continue;}assert(data->TotalVtxCount>0);
        for(int l=0;l<data->CmdListsCount;++l)for(const auto& v:data->CmdLists[l]->VtxBuffer){assert(std::isfinite(v.pos.x) && std::isfinite(v.pos.y));assert(v.pos.x>=-1 && v.pos.x<=display.x+1 && v.pos.y>=-1 && v.pos.y<=display.y+1);}
        if(phase==EventPhase::Briefing){
            bool die=false;for(int l=0;l<data->CmdListsCount;++l)for(const auto& v:data->CmdLists[l]->VtxBuffer)die|=v.col==IM_COL32(250,222,171,255);
            assert(die); // Includes the final countdown frame, for every event.
        }
        if(event==Event::Trial && (elapsed==1500 || elapsed==7000)){
            std::ofstream out(std::filesystem::path(argv[1])/("frame"+std::to_string(scenario++)+".bin"),std::ios::binary);
            const uint32_t header[]{uint32_t(display.x),uint32_t(display.y),uint32_t(tw),uint32_t(th),uint32_t(data->TotalVtxCount),uint32_t(data->TotalIdxCount)};out.write(reinterpret_cast<const char*>(header),sizeof(header));
            for(int l=0;l<data->CmdListsCount;++l)out.write(reinterpret_cast<const char*>(data->CmdLists[l]->VtxBuffer.Data),size_t(data->CmdLists[l]->VtxBuffer.Size)*sizeof(ImDrawVert));
            uint32_t offset=0;for(int l=0;l<data->CmdListsCount;++l){auto list=data->CmdLists[l];for(auto i:list->IdxBuffer){const uint32_t index=i+offset;out.write(reinterpret_cast<const char*>(&index),4);}offset+=list->VtxBuffer.Size;}
            uint32_t commands=0;for(int l=0;l<data->CmdListsCount;++l)commands+=data->CmdLists[l]->CmdBuffer.Size;out.write(reinterpret_cast<const char*>(&commands),4);
            uint32_t first=0;for(int l=0;l<data->CmdListsCount;++l){auto list=data->CmdLists[l];for(const auto& cmd:list->CmdBuffer){const uint32_t start=first+cmd.IdxOffset,count=cmd.ElemCount;const uint64_t texture=cmd.GetTexID();out.write(reinterpret_cast<const char*>(&start),4);out.write(reinterpret_cast<const char*>(&count),4);out.write(reinterpret_cast<const char*>(&texture),8);}first+=list->IdxBuffer.Size;}

        }
    }
    // Idle animation must continue after the initial three-second roll settles.
    std::vector<ImVec2> resting;
    for(float age:{4.0f,5.0f}){
        ImGui::NewFrame();auto draw=ImGui::GetForegroundDrawList();DrawDie(draw,{640,346},76,6,age,true);
        if(resting.empty())for(const auto& v:draw->VtxBuffer)resting.push_back(v.pos);
        else {assert(int(resting.size())==draw->VtxBuffer.Size);bool moved=false;for(int i=0;i<draw->VtxBuffer.Size;++i)moved|=resting[i].x!=draw->VtxBuffer[i].pos.x || resting[i].y!=draw->VtxBuffer[i].pos.y;assert(moved);}
        ImGui::Render();
    }
    for(auto phase:{EventPhase::Entering,EventPhase::Active}){
        view.eventPhase=phase;view.level=39;receivedAt=GetTickCount64();ImGui::NewFrame();DrawCelebration(1);ImGui::Render();assert(!ImGui::GetDrawData()->TotalVtxCount);
    }
    view.eventPhase=EventPhase::Cancelled;view.reward=0;view.eventElapsed=1000;
    for(const auto display:{ImVec2{1280,720},ImVec2{3840,2160}})for(unsigned failure=0;failure<=unsigned(EventFailure::ChampionTreasureClass);++failure){
        io.DisplaySize=display;view.failure=EventFailure(failure);receivedAt=GetTickCount64();ImGui::NewFrame();DrawCelebration(display.y/1080);ImGui::Render();auto data=ImGui::GetDrawData();assert(data->TotalVtxCount>0);
        for(int l=0;l<data->CmdListsCount;++l)for(const auto& v:data->CmdLists[l]->VtxBuffer)assert(v.pos.x>=-1 && v.pos.x<=display.x+1 && v.pos.y>=-1 && v.pos.y<=display.y+1);
    }
    view.failure=EventFailure::None;
    SetArtworkProvider(nullptr);
    Put(fixture.level.data(),0x1f8,int(39));view.active=1;view.level=39;view.eventPhase=EventPhase::Active;view.event=Event::Trial;
    for(const auto display:{ImVec2{1280,720},ImVec2{3840,2160}})for(uint32_t seconds:{11u,10u,1u,0u}){
        io.DisplaySize=display;view.eventElapsed=(120-seconds)*1000;receivedAt=GetTickCount64();ImGui::NewFrame();Draw(display.y/1080,true);ImGui::Render();auto data=ImGui::GetDrawData();assert(data->TotalVtxCount>0);
        bool red=false;for(int l=0;l<data->CmdListsCount;++l)for(const auto& v:data->CmdLists[l]->VtxBuffer){assert(v.pos.x>=0 && v.pos.x<=display.x && v.pos.y>=0 && v.pos.y<=display.y);red|=v.col==IM_COL32(255,65,45,255);}assert(red==(seconds<=10));
    }
    // Keep a 12px default selected, reproducing the old timer's blurry input.
    // Its actual foreground vertices must reference the 32px/64px glyph atlas.
    for(const auto display:{ImVec2{1280,720},ImVec2{3840,2160}}){
        const float scale=display.x>=3840?2.0f:1.0f;io.DisplaySize=display;view.eventElapsed=0;receivedAt=GetTickCount64();
        ImGui::NewFrame();assert(ImGui::GetFont()->FontSize==12);Draw(scale,true);
        const auto font=eventFonts.ForSize(32*scale);assert(font && font->FontSize==32*scale);
        const auto glyph=font->FindGlyph('2');const auto& vertices=ImGui::GetForegroundDrawList()->VtxBuffer;
        assert(vertices.Size>=4 && glyph && vertices[0].uv.x==glyph->U0 && vertices[0].uv.y==glyph->V0);
        assert(vertices[2].pos.x-vertices[0].pos.x==glyph->X1-glyph->X0);
        ImGui::Render();
    }
    // Location-free bonuses must still draw with no current map snapshot,
    // and Champion Clash must never display a zero-second countdown.
    for(const auto display:{ImVec2{1280,720},ImVec2{3840,2160}}){
        io.DisplaySize=display;clientLayoutSessionKnown=true;clientLayoutSession=91;clientLayoutEpoch=10;RewardsData::Wire rewards;rewards.session=91;rewards.epoch=10;rewards.sequence=1;rewards.recipient=55;rewards.remaining={600,600,600};
        ClearClientRewards();assert(ReceiveRewards(reinterpret_cast<const uint8_t*>(&rewards),sizeof(rewards)));receivedAt=0;ImGui::NewFrame();Draw(display.y/1080,true);ImGui::Render();auto draw=ImGui::GetDrawData();assert(draw->TotalVtxCount>0);
        for(int l=0;l<draw->CmdListsCount;++l)for(const auto& v:draw->CmdLists[l]->VtxBuffer)assert(v.pos.x>=0 && v.pos.x<=display.x && v.pos.y>=0 && v.pos.y<=display.y);
        ClearClientRewards();view.event=Event::ChampionClash;view.level=108;view.eventPhase=EventPhase::Active;view.active=1;view.eventElapsed=100000000;Put(fixture.level.data(),0x1f8,108);receivedAt=GetTickCount64();ImGui::NewFrame();Draw(display.y/1080,true);ImGui::Render();assert(ImGui::GetForegroundDrawList()->VtxBuffer.empty());
    }
    clientLayoutSessionKnown=false;ClearClientRewards();view.event=Event::Trial;view.level=39;view.eventElapsed=0;Put(fixture.level.data(),0x1f8,39);
    auto saveScene=[&](const std::string& name){
        auto data=ImGui::GetDrawData();assert(data->TotalVtxCount>0);
        std::ofstream out(std::filesystem::path(argv[1])/(name+".bin"),std::ios::binary);
        const uint32_t header[]{uint32_t(io.DisplaySize.x),uint32_t(io.DisplaySize.y),uint32_t(tw),uint32_t(th),uint32_t(data->TotalVtxCount),uint32_t(data->TotalIdxCount)};out.write(reinterpret_cast<const char*>(header),sizeof(header));
        for(int l=0;l<data->CmdListsCount;++l)for(const auto& v:data->CmdLists[l]->VtxBuffer){assert(std::isfinite(v.pos.x) && std::isfinite(v.pos.y));assert(v.pos.x>=-1 && v.pos.x<=io.DisplaySize.x+1 && v.pos.y>=-1 && v.pos.y<=io.DisplaySize.y+1);out.write(reinterpret_cast<const char*>(&v),sizeof(v));}
        uint32_t offset=0;for(int l=0;l<data->CmdListsCount;++l){auto list=data->CmdLists[l];for(auto i:list->IdxBuffer){const uint32_t index=i+offset;out.write(reinterpret_cast<const char*>(&index),4);}offset+=list->VtxBuffer.Size;}
        uint32_t count=0;for(int l=0;l<data->CmdListsCount;++l)count+=data->CmdLists[l]->CmdBuffer.Size;out.write(reinterpret_cast<const char*>(&count),4);
        uint32_t first=0;for(int l=0;l<data->CmdListsCount;++l){auto list=data->CmdLists[l];for(const auto& cmd:list->CmdBuffer){const uint32_t start=first+cmd.IdxOffset,n=cmd.ElemCount;const uint64_t texture=cmd.GetTexID();out.write(reinterpret_cast<const char*>(&start),4);out.write(reinterpret_cast<const char*>(&n),4);out.write(reinterpret_cast<const char*>(&texture),8);}first+=list->IdxBuffer.Size;}
    };
    for(const auto display:{ImVec2{1280,720},ImVec2{3840,2160}}){
        const float scale=display.x>=3840?2.0f:1.0f;io.DisplaySize=display;view.event=Event::Survivors;view.eventPhase=EventPhase::Active;view.eventElapsed=90000;
        survivorView={};survivorView.recipient=55;survivorView.run=view.run;survivorView.session=view.session;survivorView.phase=EventPhase::Active;
        survivorView.level=12;survivorView.wave=23;survivorView.kills=312;survivorView.xp=35;survivorView.requiredXP=Survivors::RequiredXP(12);survivorView.ranks={3,2,1,3,1,2,0,0,0};survivorView.choices={0,1,7};survivorView.offer=4;survivorView.choiceSeconds=12;
        survivorView.totalAttributes={30,25,35,40};survivorView.paused=1;assert(Survivors::Valid(survivorView));receivedAt=survivorReceived=GetTickCount64();
        for(int frame=0;frame<2;++frame){ImGui::NewFrame();Draw(scale,true);ImGui::Render();}
        saveScene("survivor-upgrades-"+std::to_string(int(display.x)));
        auto plusPosition=[&](){
            auto window=ImGui::FindWindowByName("Survivor character stats");assert(window);
            const auto glyph=TestEventFont(18*scale)->FindGlyph('+');assert(glyph);
            const auto& vertices=window->DrawList->VtxBuffer;
            for(int n=0;n+3<vertices.Size;++n)if(vertices[n].uv.x==glyph->U0 && vertices[n].uv.y==glyph->V0 && vertices[n+2].uv.x==glyph->U1 && vertices[n+2].uv.y==glyph->V1 && vertices[n].pos.x>window->Pos.x+window->Size.x-60*scale)
                return ImVec2{(vertices[n].pos.x+vertices[n+2].pos.x)*0.5f,(vertices[n].pos.y+vertices[n+2].pos.y)*0.5f};
            assert(false);return ImVec2{};
        };
        auto clickPlus=[&](){
            const auto position=plusPosition();io.AddMousePosEvent(position.x,position.y);
            for(int phase=0;phase<3;++phase){if(phase)io.AddMouseButtonEvent(0,phase==1);ImGui::NewFrame();Draw(scale,true);ImGui::Render();}
        };
        attributeRequests.clear();clickPlus();assert(attributeRequests.empty()); // Skill must be chosen first.

        survivorView.choices.fill(Survivors::NoUpgrade);survivorView.allocating=1;
        for(int frame=0;frame<2;++frame){ImGui::NewFrame();Draw(scale,true);ImGui::Render();}
        saveScene("survivor-stats-"+std::to_string(int(display.x)));
        const auto& background=ImGui::GetBackgroundDrawList()->VtxBuffer;
        for(int n=0;n+3<background.Size;++n)assert(!(background[n].pos.x<=0 && background[n].pos.y<=0 && background[n+2].pos.x>=display.x && background[n+2].pos.y>=display.y)); // No full-screen dim layer.
        clickPlus();assert(attributeRequests.size()==1 && attributeRequests.back().action==Survivors::Action::Allocate && !attributeRequests.back().slot && attributeRequests.back().points==1 && !attributeRequests.back().spent && attributeRequests.back().offer==4);
        io.AddKeyEvent(ImGuiMod_Ctrl,true);clickPlus();io.AddKeyEvent(ImGuiMod_Ctrl,false);assert(attributeRequests.size()==2 && attributeRequests.back().points==5);
        io.AddKeyEvent(ImGuiMod_Shift,true);clickPlus();io.AddKeyEvent(ImGuiMod_Shift,false);assert(attributeRequests.size()==3 && attributeRequests.back().points==55);
        survivorView.attributes={55,0,0,0};for(int n=0;n<2;++n){ImGui::NewFrame();Draw(scale,true);ImGui::Render();}
        clickPlus();assert(attributeRequests.size()==3); // No overspending request at zero remaining.
        io.AddMousePosEvent(-100,-100);

        survivorView.allocating=0;
        Put(fixture.level.data(),0x1f8,int(1));view.eventPhase=EventPhase::Briefing;view.active=0;view.eventElapsed=7000;receivedAt=GetTickCount64();
        SetArtworkProvider(TestArtwork);ImGui::NewFrame();DrawCelebration(scale);ImGui::Render();saveScene("survivor-briefing-"+std::to_string(int(display.x)));SetArtworkProvider(nullptr);
        Put(fixture.level.data(),0x1f8,int(39));view.active=1;
    }
    survivorRoster=std::make_unique<Survivors::Roster>(std::filesystem::path(argv[1])/"selector.roster");
    {Survivors::Profile saved;saved.level=3;saved.ranks={2,0,0,1};saved.xp=10;assert(survivorRoster->Store(3,saved));}
    for(uint8_t cls=0;cls<Survivors::Classes.size();++cls){auto saved=survivorRoster->Get(cls);saved.xp=unsigned(cls+1);assert(survivorRoster->Store(cls,saved));}
    // Class selection must send that class's saved profile, independently of
    // the normal hero's class and of whichever class was selected last.
    attributeRequests.clear();
    SetSurvivorsEnabled(false);
    SurvivorRequest(Survivors::Action::Start,{},0,0,0);
    assert(attributeRequests.empty());
    SetSurvivorsEnabled(true);
    for(uint8_t cls:{uint8_t(1),uint8_t(3),uint8_t(1),uint8_t(3),uint8_t(0),uint8_t(2),uint8_t(4),uint8_t(5),uint8_t(6)}){
        SurvivorRequest(Survivors::Action::Start,{},0,0,cls);
        assert(attributeRequests.back().action==Survivors::Action::Start && attributeRequests.back().classId==cls && attributeRequests.back().profile==survivorRoster->Get(cls));
        assert(attributeRequests.back().profile.xp==unsigned(cls+1));
    }
    std::cout<<"Class entry sends only the selected class's distinct saved XP/ranks/attributes, including repeated Sorceress/Paladin switches.\n";
    survivorsAvailable=true;survivorRosterError=false;receivedAt=0;Put(fixture.level.data(),0x1f8,int(1));
    eventPlayers={};survivorMonsters={};labelPlayer=&eventPlayers[0];labelPlayer->dwUnitId=55;labelPlayer->dwAnimMode=1;labelPlayer->pDynamicPath=&survivorPaths[0];
    auto& npc=survivorMonsters[0];npc.dwUnitType=1;npc.dwClassId=2;npc.dwAnimMode=1;npc.pDynamicPath=&survivorPaths[1];
    Put(fixture.r1[0].data(),0xa8,&npc);encounterNames={{"charsi",2}};void* linker=reinterpret_cast<void*>(1);
    bindings[0x1c9f890]=reinterpret_cast<uintptr_t>(&linker);bindings[0x6d0470]=reinterpret_cast<uintptr_t>(EncounterName);bindings[0x2232c0]=reinterpret_cast<uintptr_t>(SurvivorNearby);
    SetSurvivorsEnabled(false);
    ImGui::NewFrame();DrawSurvivors(1.0f,true);ImGui::Render();assert(!ImGui::GetDrawData()->TotalVtxCount);
    SetSurvivorsEnabled(true);
    for(const auto display:{ImVec2{1280,720},ImVec2{3840,2160}}){io.DisplaySize=display;for(int n=0;n<2;++n){ImGui::NewFrame();DrawSurvivors(display.x>=3840?2.0f:1.0f,true);ImGui::Render();}saveScene("survivor-classes-"+std::to_string(int(display.x)));}
    SetFontProvider(nullptr);ImGui::DestroyContext();populationCode=nullptr;std::cout<<"Client readiness, all new dice events, independent bonus timers, untimed Champion Clash, native-size timers, Survivors briefing and three-choice upgrade HUD pass at 720p and 4K.\n";
}
