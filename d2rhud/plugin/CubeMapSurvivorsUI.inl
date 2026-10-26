void SurvivorRequest(Survivors::Action action,const Packet& packet,uint32_t offer=0,uint8_t slot=0,uint8_t classId=0,uint16_t points=0,uint16_t spent=0){
    Survivors::Request request;request.data.action=action;request.data.run=packet.run;request.data.session=packet.session;request.data.offer=offer;request.data.slot=slot;
    request.data.points=points;request.data.spent=spent;
    if(action==Survivors::Action::Start){if(!SurvivorsEnabled())return;std::lock_guard lock(survivorRosterMutex);auto& roster=SurvivorRoster();if(!roster.good || classId>=Survivors::Classes.size() || !roster.Store(classId,roster.Get(classId))){survivorRosterError=true;return;}request.data.classId=classId;request.data.profile=roster.Get(classId);}
    Native<void(__fastcall*)(void*,int)>(0x10dce0)(&request,sizeof(request));
}
void DrawSurvivors(float scale,bool clientReady){
    if(!clientReady || !installed.load())return;
    auto player=LocalPlayer();if(!player)return;
    Packet packet;Survivors::SnapshotBody data;uint64_t received,survivalTime;
    {std::lock_guard lock(viewMutex);packet=view;received=receivedAt;data=survivorView;survivalTime=survivorReceived;}
    const auto now=GetTickCount64();
    const bool own=received && now-received<=4000 && packet.event==Event::Survivors && packet.recipient==player->dwUnitId;
    const bool active=own && packet.active && packet.eventPhase==EventPhase::Active && Level(player)==packet.level;
    auto& io=ImGui::GetIO();scale=std::clamp(scale,0.5f,2.0f);
    const float size=18*scale;auto font=EventFont(size);
    auto begin=[&](const char* title,ImVec2 position,ImVec2 width){
        ImGui::PushStyleColor(ImGuiCol_WindowBg,{0.025f,0.016f,0.011f,0.95f});
        ImGui::PushStyleColor(ImGuiCol_TitleBg,{0.10f,0.06f,0.03f,0.98f});ImGui::PushStyleColor(ImGuiCol_TitleBgActive,{0.16f,0.10f,0.04f,0.98f});
        ImGui::PushStyleColor(ImGuiCol_Border,{0.45f,0.33f,0.17f,0.9f});ImGui::PushStyleColor(ImGuiCol_Button,{0.23f,0.13f,0.055f,1});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0.37f,0.23f,0.09f,1});ImGui::PushStyleColor(ImGuiCol_ButtonActive,{0.49f,0.31f,0.12f,1});ImGui::PushStyleColor(ImGuiCol_Text,{0.88f,0.79f,0.62f,1});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{8*scale,8*scale});ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{6*scale,4*scale});ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{8*scale,5*scale});
        ImGui::SetNextWindowPos(position,ImGuiCond_Always);ImGui::SetNextWindowSize(width,ImGuiCond_Always);
        ImGui::PushFont(font);HudText::Begin(title,nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoCollapse);
        ImGui::SetWindowFontScale(size/font->FontSize/io.FontGlobalScale);
    };
    auto end=[](){ImGui::End();ImGui::PopFont();ImGui::PopStyleVar(3);ImGui::PopStyleColor(8);};
    static uint64_t lastNpcCheck=0;static Unit* npcPlayer=nullptr;static bool nearCharsi=false;
    if(!own && SurvivorsEnabled() && survivorsAvailable && (!lastNpcCheck || now-lastNpcCheck>=250 || npcPlayer!=player)){
        lastNpcCheck=now;npcPlayer=player;nearCharsi=NearCharsi(player);
    }
    if(!own && SurvivorsEnabled() && survivorsAvailable && Level(player)==1 && nearCharsi){
        const float width=(std::min)(380*scale,io.DisplaySize.x-32);
        begin("Charsi - Survivor Arena",{io.DisplaySize.x-width-16,io.DisplaySize.y*0.18f},{width,0});
        HudText::TextWrapped("Survive ten minutes. Your weapons attack automatically while you move.");
        HudText::TextWrapped("Choose a Survivors class. Each class has separate saved progress, shared across your normal characters.");
        std::lock_guard lock(survivorRosterMutex);auto& roster=SurvivorRoster();static int selected=0;
        for(int i=0;i<int(Survivors::Classes.size());++i){
            char label[80];snprintf(label,sizeof(label),"%s - Level %u",Survivors::Classes[i].name,unsigned(roster.Get(uint8_t(i)).level));
            if(HudText::Selectable(label,selected==i))selected=i;
        }
        HudText::TextWrapped("Starting skill: %s",SurvivorUpgradeName(uint8_t(selected),0));
        HudText::TextWrapped("Your normal hero returns with their original equipment, stats and skills.");
        static uint64_t requestedAt=0;
        if(roster.good && !survivorRosterError){if(HudText::Button("Enter Sanctuary Survivors",{-1,36*scale}) && (!requestedAt || now-requestedAt>2000)){requestedAt=now;SurvivorRequest(Survivors::Action::Start,{},0,0,uint8_t(selected));}}
        else HudText::TextWrapped("%s",Survivors::Message(Survivors::Failure::Profile));
        if(survivalTime && now-survivalTime<15000 && data.recipient==player->dwUnitId && data.failure!=Survivors::Failure::None)HudText::TextWrapped("%s",Survivors::Message(data.failure));
        end();return;
    }
    if(!active || !survivalTime || now-survivalTime>4000 || data.run!=packet.run || data.session!=packet.session || data.recipient!=packet.recipient)return;
    char heading[160];snprintf(heading,sizeof(heading),"%s - Level %u  |  Wave %u  |  Kills %u",Survivors::Classes[data.classId].name,unsigned(data.level),unsigned(data.wave),data.kills);
    const float textSize=22*scale;auto hudFont=EventFont(textSize);auto draw=ImGui::GetForegroundDrawList();
    const auto extent=HudText::CalcTextSizeA(hudFont,textSize,FLT_MAX,0,heading);
    const ImVec2 p{std::round((io.DisplaySize.x-extent.x)*0.5f),std::round(24*scale)};
    draw->AddRectFilled({p.x-10*scale,p.y-6*scale},{p.x+extent.x+10*scale,p.y+extent.y+16*scale},IM_COL32(0,0,0,190),5*scale);
    HudText::AddText(draw,hudFont,textSize,p,IM_COL32(240,215,165,255),heading);
    const float barWidth=320*scale,x=(io.DisplaySize.x-barWidth)*0.5f,y=p.y+extent.y+4*scale;
    draw->AddRectFilled({x,y},{x+barWidth,y+5*scale},IM_COL32(40,35,30,255));
    const float progress=data.requiredXP ? std::clamp(float(data.xp)/data.requiredXP,0.0f,1.0f):0;
    draw->AddRectFilled({x,y},{x+barWidth*progress,y+5*scale},IM_COL32(100,165,210,255));
    const bool choosing=data.choices[0]!=Survivors::NoUpgrade;
    if(choosing || data.allocating){
        unsigned spent=0;for(auto n:data.attributes)spent+=n;
        const auto available=unsigned(data.level-1)*5-spent;
        const float statsWidth=(std::min)(360*scale,io.DisplaySize.x*0.45f);
        begin("Survivor character stats",{16,io.DisplaySize.y*0.18f},{statsWidth,0});
        HudText::Text("%s - Level %u",Survivors::Classes[data.classId].name,unsigned(data.level));
        HudText::Text("Stat points available: %u",available);ImGui::Spacing();
        constexpr const char* names[]{"Strength","Energy","Dexterity","Vitality"};
        for(uint8_t stat:{uint8_t(0),uint8_t(2),uint8_t(3),uint8_t(1)}){
            ImGui::PushID(stat);
            HudText::Text("%s: %d (+%u)",names[stat],data.totalAttributes[stat],unsigned(data.attributes[stat]));
            ImGui::SameLine(statsWidth-52*scale);ImGui::BeginDisabled(!data.allocating || !available);
            if(HudText::Button("+",{32*scale,28*scale})){
                const auto points=io.KeyShift?available:(std::min)(available,io.KeyCtrl?5u:1u);
                SurvivorRequest(Survivors::Action::Allocate,packet,data.offer,stat,0,uint16_t(points),uint16_t(spent));
            }
            ImGui::EndDisabled();ImGui::PopID();ImGui::Spacing();
        }
        HudText::TextWrapped(choosing ? "Choose your skill upgrade to receive five stat points. The wave stays paused." : "Click + to spend one point. Ctrl: five. Shift: all remaining points.");
        ImGui::Separator();HudText::TextWrapped("Strength/Dexterity: combat. Vitality: life. Energy: mana and +1 skill level per 10 allocated points.");
        end();
        const float width=(std::min)(410*scale,io.DisplaySize.x-32);
        const float left=(std::min)(io.DisplaySize.x-width-16,(std::max)((io.DisplaySize.x-width)*0.5f,io.DisplaySize.x*0.52f));
        begin("Paused - level up",{left,io.DisplaySize.y*0.18f},{width,0});
        HudText::TextWrapped(choosing ? "Choose 1, 2 or 3 to gain a level and 5 stat points. The wave stays paused while you allocate them." : "Spend your stat points with the + buttons on the left, then resume when ready.");
        for(uint8_t slot=0;slot<3;++slot){
            const auto choice=data.choices[slot];if(choice>=Survivors::Count)continue;
            const auto& upgrade=Survivors::Upgrades[choice];
            ImGui::PushID(slot);char label[120];snprintf(label,sizeof(label),"%u. %s - Rank %u",unsigned(slot+1),SurvivorUpgradeName(data.classId,choice),unsigned(data.ranks[choice]+1));
            const bool clicked=HudText::Button(label,{-1,34*scale});
            const bool key=!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey(int(ImGuiKey_1)+slot),false);
            if(clicked || key)SurvivorRequest(Survivors::Action::Choose,packet,data.offer,slot);
            HudText::TextWrapped("%s",choice<3 ? "Unlock or strengthen this class skill. Its spell effects play automatically while you move." : upgrade.description);ImGui::Spacing();ImGui::PopID();
        }
        if(data.allocating){
            HudText::Text("Stat points available: %u",available);
            if(HudText::Button("Resume wave",{-1,36*scale}))SurvivorRequest(Survivors::Action::Ready,packet,data.offer);
            HudText::TextWrapped("Unspent points can be kept for later.");
        }
        if(HudText::Button("Return to town",{-1,32*scale}))SurvivorRequest(Survivors::Action::Leave,packet);
        end();return;
    }
    // A small retreat control does not take mouse input outside its own window.
    begin("Survivors controls",{16,io.DisplaySize.y*0.55f},{300*scale,0});
    if(data.paused && data.choices[0]==Survivors::NoUpgrade)HudText::TextWrapped("Paused while another survivor chooses an upgrade.");
    unsigned spent=0;for(auto n:data.attributes)spent+=n;
    HudText::Text("Stat points available: %u",unsigned(data.level-1)*5-spent);
    HudText::TextWrapped("STR/DEX: combat. VIT: life. Each 10 spent Energy: +1 skill level.");
    if(HudText::Button("Return to town",{-1,32*scale}))SurvivorRequest(Survivors::Action::Leave,packet);
    HudText::TextWrapped("Class progress and stats save separately. Exit restores your normal hero.");
    if(survivorRosterError)HudText::TextWrapped("Progress could not be saved. Check the Survivors roster file.");end();
}
