namespace LootFilterConfig::Preview::UI {
inline ImVec4 QualityColor(int quality) {
    switch(quality){case 1:return {0.6f,0.6f,0.6f,1};case 3:return {0.9f,0.9f,0.9f,1};case 4:return {0.42f,0.55f,1,1};case 5:return {0.2f,0.95f,0.3f,1};case 6:return {1,0.95f,0.25f,1};case 7:return {0.78f,0.66f,0.4f,1};case 8:return {1,0.6f,0.2f,1};case 9:return {0.8f,0.4f,1,1};default:return {0.9f,0.9f,0.9f,1};}
}
inline ImVec4 TextColor(char code) {
    switch(code){case '1':return {1,0.2f,0.2f,1};case '2':return {0.2f,1,0.2f,1};case '3':return {0.35f,0.5f,1,1};case '4':return {0.78f,0.66f,0.4f,1};case '5':return {0.6f,0.6f,0.6f,1};case '6':return {0.05f,0.05f,0.05f,1};case '7':return {0.8f,0.7f,0.5f,1};case '8':return {1,0.6f,0.2f,1};case '9':return {1,1,0.2f,1};case ';':return {0.7f,0.35f,1,1};case 'A':return {0.15f,0.6f,0.2f,1};case 'N':return {0.25f,0.95f,1,1};case 'O':return {1,0.4f,0.7f,1};case 'P':return {0.8f,0.6f,1,1};default:return {0.95f,0.95f,0.95f,1};}
}
inline size_t ColorToken(const std::string& text,size_t offset,char& color) {
    if(offset+3<text.size()&&static_cast<unsigned char>(text[offset])==0xc3&&static_cast<unsigned char>(text[offset+1])==0xbf&&text[offset+2]=='c'){color=text[offset+3];return 4;}
    if(offset+2<text.size()&&static_cast<unsigned char>(text[offset])==0xff&&text[offset+1]=='c'){color=text[offset+2];return 3;}
    return 0;
}
inline std::string Plain(const std::string& text){std::string result;for(size_t i=0;i<text.size();){char code;size_t count=ColorToken(text,i,code);if(count)i+=count;else result+=text[i++];}return result;}
inline void ColoredName(ImDrawList* draw,ImVec2 position,const std::string& text,ImVec4 initial) {
    ImVec4 color=initial;float x=position.x,y=position.y;
    for(size_t i=0;i<text.size();){char code;size_t count=ColorToken(text,i,code);if(count){color=TextColor(code);i+=count;continue;}
        size_t end=i;while(end<text.size()&&text[end]!='\n'&&!ColorToken(text,end,code))++end;
        if(end>i){draw->AddText(ImVec2(x,y),ImGui::ColorConvertFloat4ToU32(color),text.data()+i,text.data()+end);x+=ImGui::CalcTextSize(text.data()+i,text.data()+end).x;}
        if(end<text.size()&&text[end]=='\n'){x=position.x;y+=ImGui::GetFontSize();++end;}i=end;
    }
}
inline void Card(size_t id,const Sample& sample,const Result& result,float width,const Catalog& catalog) {
    ImGui::PushID(int(id));const ImVec2 top=ImGui::GetCursorScreenPos();const float line=ImGui::GetTextLineHeight(),scale=line/18.f;
    const int nameLines=result.hidden?1:std::clamp(1+int(std::count(result.name.begin(),result.name.end(),'\n')),1,3);
    const float height=line*(5+nameLines)+32*scale;
    ImGui::InvisibleButton("sample",ImVec2(width,height));
    const bool hover=ImGui::IsItemHovered();auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(top,ImVec2(top.x+width,top.y+height),IM_COL32(23,22,20,250),4);
    draw->AddRect(top,ImVec2(top.x+width,top.y+height),hover?IM_COL32(170,135,70,255):IM_COL32(70,63,48,255),4);
    const auto quality=ImGui::ColorConvertFloat4ToU32(QualityColor(sample.quality));
    draw->AddRectFilled({top.x,top.y+8*scale},{top.x+3*scale,top.y+height-8},quality);
    draw->PushClipRect(ImVec2(top.x+10*scale,top.y+6*scale),ImVec2(top.x+width-10,top.y+height-6),true);
    auto symbol=Builder::UI::Symbol::Gem;std::string base=sample.name;
    std::transform(base.begin(),base.end(),base.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(sample.code=="rin"||base.find("ring")!=base.npos)symbol=Builder::UI::Symbol::Ring;
    else if(sample.code=="amu"||base.find("amulet")!=base.npos)symbol=Builder::UI::Symbol::Amulet;
    else if(base.find("potion")!=base.npos)symbol=Builder::UI::Symbol::Potion;
    else if(sample.maximum>0)symbol=Builder::UI::Symbol::Weapon;
    Builder::UI::Icon(symbol,{top.x+12*scale,top.y+10*scale},36*scale,quality);
    std::string heading=Qualities[std::clamp(sample.quality,0,9)];if(sample.ethereal)heading+=" / Ethereal";if(sample.runeword)heading+=" / Runeword";
    draw->AddText(ImVec2(top.x+60*scale,top.y+9*scale),quality,heading.c_str());
    const std::string details="Level "+std::to_string(sample.level)+" / "+std::to_string(sample.sockets)+" sockets";
    draw->AddText({top.x+60*scale,top.y+10*scale+line},IM_COL32(145,141,129,255),details.c_str());
    const float beforeY=top.y+18*scale+line*2;
    draw->AddText({top.x+12*scale,beforeY},IM_COL32(133,129,118,255),"Before");
    draw->AddText({top.x+90*scale,beforeY},quality,sample.name.c_str());
    draw->AddText({top.x+12*scale,beforeY+line+12*scale},IM_COL32(213,185,123,255),"After");
    const float nameY=beforeY+line+8*scale;const ImVec2 labelMin(top.x+90*scale,nameY),labelMax(top.x+width-12,nameY+line*nameLines+8*scale);
    ImVec4 background(result.background[0],result.background[1],result.background[2],result.background[3]);
    draw->AddRectFilled(labelMin,labelMax,ImGui::ColorConvertFloat4ToU32(background));
    if(result.border[3]>0&&result.thickness>0)draw->AddRect(labelMin,labelMax,ImGui::ColorConvertFloat4ToU32(ImVec4(result.border[0],result.border[1],result.border[2],result.border[3])),0,0,result.thickness);
    draw->PushClipRect(labelMin,labelMax,true);
    if(result.hidden)draw->AddText(ImVec2(labelMin.x+4,labelMin.y+4),IM_COL32(170,160,155,255),"Hidden");
    else ColoredName(draw,ImVec2(labelMin.x+4,labelMin.y+4),result.name,QualityColor(sample.quality));
    draw->PopClipRect();
    const char* status=result.hidden?(result.matched?"Hidden by this rule":"Hidden on mismatch"):result.matched?"Matches this rule":"Unchanged / no match";
    ImU32 statusColor=result.hidden?IM_COL32(230,125,105,255):result.matched?IM_COL32(115,210,135,255):IM_COL32(145,145,140,255);
    draw->AddCircleFilled({top.x+17*scale,top.y+height-line*.5f-10*scale},3,statusColor);
    draw->AddText(ImVec2(top.x+28*scale,top.y+height-line-10*scale),statusColor,status);draw->PopClipRect();
    if(hover){
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*30);
        ImGui::TextUnformatted(sample.name.c_str());ImGui::Text("%s | item level %d | %d sockets (max %d)",Qualities[std::clamp(sample.quality,0,9)],sample.level,sample.sockets,sample.maximum);
        ImGui::Text("%s | %s | %s",sample.tier==2?"Elite":sample.tier==1?"Exceptional":"Normal base",sample.identified?"Identified":"Unidentified",sample.collected?"Collected for grail":"Not collected for grail");
        for(const auto& stat:sample.stats){auto label=Builder::UI::Label(catalog.stats,std::to_string(stat.first.first));if(stat.first.second)ImGui::Text("%s (parameter %d): %d",label.c_str(),stat.first.second,stat.second);else ImGui::Text("%s: %d",label.c_str(),stat.second);}
        ImGui::Separator();if(!result.matched){auto* spec=Builder::FindSpec(result.reason);ImGui::TextWrapped("%s",spec?(std::string(spec->label)+" condition does not match").c_str():result.reason.c_str());}
        if(!result.hidden)ImGui::TextWrapped("After: %s",Plain(result.name).c_str());
        ImGui::TextWrapped("%s",Plain(result.description).c_str());
        if(!result.messages.empty())ImGui::TextWrapped("Chat/message preview: %s",Plain(result.messages).c_str());
        if(result.audio)ImGui::TextUnformatted("Speech/sound is configured (not played in preview).");
        ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
    ImGui::PopID();
}
inline void Render(const std::string& raw,const std::unordered_map<std::string,std::string>& globals,const Catalog& catalog,const std::string& engine) {
    static Context context;static Session session;static std::string observed;static bool matchesOnly=false,allRolls=false;static double lastAnimation=-1;
    Builder::UI::Banner(Builder::UI::Symbol::Preview,"Live item preview","Selected rule / simulated items. Hover for details.");
    bool refresh=false;
    if(ImGui::Button("Sample context..."))ImGui::OpenPopup("PreviewContext");
    ImGui::SetNextWindowSizeConstraints({320,0},{480,700});
    if(ImGui::BeginPopup("PreviewContext")){
        ImGui::TextUnformatted("Difficulty");ImGui::SetNextItemWidth(-1);refresh|=ImGui::Combo("##difficulty",&context.difficulty,"Normal\0Nightmare\0Hell\0");
        ImGui::TextUnformatted("Filter level");ImGui::SetNextItemWidth(-1);refresh|=ImGui::InputInt("##filter_level",&context.filterLevel);context.filterLevel=(std::max)(1,context.filterLevel);
        ImGui::TextUnformatted("Player level");ImGui::SetNextItemWidth(-1);refresh|=ImGui::InputInt("##player_level",&context.playerLevel);context.playerLevel=(std::max)(1,context.playerLevel);
        ImGui::TextUnformatted("Area");std::string area=std::to_string(context.area);if(Builder::UI::Choose("##area",area,catalog.areas)){Builder::Integer(area,context.area);refresh=true;}
        int location=context.location==-1?3:context.location==3?0:context.location==0?1:2;
        ImGui::TextUnformatted("Location");ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##location",&location,"Ground\0Inventory / storage\0Equipped\0Vendor\0")){context.location=location==0?3:location==1?0:location==2?1:-1;refresh=true;}
        ImGui::TextUnformatted("Sample base");auto choices=catalog.items;choices.insert(choices.begin(),{"Suggested items",""});refresh|=Builder::UI::Choose("##sample_base",context.itemCode,choices);
        ImGui::EndPopup();
    }
    if(ImGui::GetContentRegionAvail().x>ImGui::CalcTextSize("Matches only    All rolls").x+100)ImGui::SameLine();
    ImGui::Checkbox("Matches only",&matchesOnly);
    ImGui::SameLine();ImGui::Checkbox("All rolls",&allRolls);
    std::string key=raw;
    for(const char* name:{"language","styles"}){auto value=globals.find(name);if(value!=globals.end())key+='\n'+std::string(name)+'='+value->second;}
    key+='\n'+std::to_string(catalog.items.size())+":"+std::to_string(catalog.namedItems.size())+":"+std::to_string(catalog.bases.size())+":"+std::to_string(std::hash<std::string>{}(engine));
    if(refresh||observed!=key){observed=key;session.Build(engine,raw,globals,catalog,context);lastAnimation=-1;}
    if(!session.error.empty()){ImGui::TextWrapped("Preview unavailable: %s",session.error.c_str());return;}
    const double now=ImGui::GetTime();if(now-lastAnimation>=0.1||lastAnimation<0){session.Animate(unsigned(now*1000));lastAnimation=now;}
    if(!session.error.empty()){ImGui::TextWrapped("Preview unavailable: %s",session.error.c_str());return;}
    std::set<std::string> seen;
    std::vector<size_t> order;
    for(size_t i=0;i<session.results.size();++i){const auto& result=session.results[i];const auto& sample=session.samples[i];
        const auto signature=sample.code+":"+std::to_string(sample.quality)+":"+std::to_string(sample.runeword)+":"+std::to_string(result.matched)+":"+std::to_string(result.hidden);
        if((!matchesOnly||result.matched)&&(allRolls||seen.insert(signature).second))order.push_back(i);
    }
    auto rank=[&](size_t i){return session.results[i].matched?0:session.samples[i].focused?2:1;};
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return rank(a)<rank(b);});
    int matching=0,hidden=0;for(size_t i:order){matching+=session.results[i].matched;hidden+=session.results[i].hidden;}
    ImGui::TextDisabled("%d matching variants   /   %d hidden",matching,hidden);ImGui::Spacing();
    if(order.empty()){ImGui::TextDisabled("No matching samples. Try a different sample context.");return;}
    const float minWidth=(std::max)(330.f,ImGui::GetFontSize()*17.5f);
    const int columns=std::clamp(int(ImGui::GetContentRegionAvail().x/(minWidth+12)),1,3);
    if(ImGui::BeginTable("SampleCards",columns,ImGuiTableFlags_SizingStretchSame)){
        for(size_t index:order){ImGui::TableNextColumn();Card(index,session.samples[index],session.results[index],ImGui::GetContentRegionAvail().x,catalog);}
        ImGui::EndTable();
    }
}
}
