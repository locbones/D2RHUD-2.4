// Included after ThemeButton; the model is also exercised by headless tests.
namespace LootFilterConfig::Builder::UI {
struct PanelArt { ImTextureID texture=0; float width=0,height=0,frameWidth=0; };
inline PanelArt panelArt;
enum class Symbol { Conditions, Actions, Preview, Rules, Code, Ring, Amulet, Weapon, Armor, Potion, Gem };
// Resolution-independent pictograms complement the game's panel artwork.
inline void Icon(Symbol symbol,ImVec2 p,float size,ImU32 color) {
    auto* d=ImGui::GetWindowDrawList();auto at=[&](float x,float y){return ImVec2(p.x+x*size,p.y+y*size);};
    auto line=[&](float x,float y,float u,float v){d->AddLine(at(x,y),at(u,v),color,(std::max)(1.f,size*.045f));};
    if(symbol==Symbol::Preview){d->AddBezierCubic(at(.08f,.5f),at(.3f,.08f),at(.7f,.08f),at(.92f,.5f),color,2);d->AddBezierCubic(at(.08f,.5f),at(.3f,.92f),at(.7f,.92f),at(.92f,.5f),color,2);d->AddCircle(at(.5f,.5f),size*.14f,color,20,2);}
    else if(symbol==Symbol::Actions){for(int i=0;i<4;++i){float a=float(i)*1.5707963f;line(.5f+cosf(a)*.12f,.5f+sinf(a)*.12f,.5f+cosf(a)*.4f,.5f+sinf(a)*.4f);}d->AddQuad(at(.5f,.28f),at(.72f,.5f),at(.5f,.72f),at(.28f,.5f),color,2);}
    else if(symbol==Symbol::Ring){d->AddCircle(at(.5f,.6f),size*.27f,color,24,2);d->AddQuad(at(.5f,.1f),at(.65f,.25f),at(.5f,.4f),at(.35f,.25f),color,2);}
    else if(symbol==Symbol::Amulet){d->AddBezierCubic(at(.16f,.15f),at(.12f,.75f),at(.88f,.75f),at(.84f,.15f),color,2);d->AddQuad(at(.5f,.5f),at(.65f,.68f),at(.5f,.88f),at(.35f,.68f),color,2);}
    else if(symbol==Symbol::Weapon){line(.25f,.8f,.76f,.15f);line(.3f,.85f,.82f,.2f);line(.16f,.6f,.5f,.86f);line(.17f,.9f,.31f,.72f);line(.76f,.15f,.82f,.2f);}
    else if(symbol==Symbol::Armor){line(.2f,.15f,.8f,.15f);line(.2f,.15f,.26f,.67f);line(.8f,.15f,.74f,.67f);line(.26f,.67f,.5f,.9f);line(.74f,.67f,.5f,.9f);line(.5f,.22f,.5f,.72f);line(.3f,.4f,.7f,.4f);}
    else if(symbol==Symbol::Potion){d->AddRect(at(.37f,.12f),at(.63f,.3f),color,2,0,2);d->AddCircle(at(.5f,.6f),size*.27f,color,24,2);line(.27f,.6f,.73f,.6f);}
    else if(symbol==Symbol::Rules||symbol==Symbol::Code){d->AddRect(at(.18f,.12f),at(.82f,.88f),color,2,0,2);for(float y:{.32f,.5f,.68f})line(.32f,y,.68f,y);}
    else {d->AddQuad(at(.5f,.08f),at(.85f,.42f),at(.5f,.92f),at(.15f,.42f),color,2);line(.15f,.42f,.85f,.42f);line(.5f,.08f,.35f,.42f);line(.35f,.42f,.5f,.92f);line(.5f,.08f,.65f,.42f);line(.65f,.42f,.5f,.92f);}
}
inline void Banner(Symbol symbol,const char* title,const char* subtitle=nullptr) {
    const auto p=ImGui::GetCursorScreenPos();const float w=ImGui::GetContentRegionAvail().x;
    const float h=ImGui::GetTextLineHeight()*(subtitle?2.f:1.f)+26.f;auto* d=ImGui::GetWindowDrawList();
    d->AddRectFilled(p,{p.x+w,p.y+h},IM_COL32(28,25,20,255),3);
    if(panelArt.texture&&panelArt.frameWidth>=120&&panelArt.height>=146){
        const float x[]{p.x,p.x+10,p.x+w-10,p.x+w},y[]{p.y,p.y+14,p.y+h-9,p.y+h};
        const float u[]{0,60/panelArt.width,(panelArt.frameWidth-60)/panelArt.width,panelArt.frameWidth/panelArt.width};
        const float v[]{0,96/panelArt.height,1-50/panelArt.height,1};
        for(int row=0;row<3;++row)for(int col=0;col<3;++col)d->AddImage(panelArt.texture,{x[col],y[row]},{x[col+1],y[row+1]},{u[col],v[row]},{u[col+1],v[row+1]});
    }else d->AddRect(p,{p.x+w,p.y+h},IM_COL32(111,88,47,255),3);
    Icon(symbol,{p.x+12,p.y+(h-32)*.5f},32,IM_COL32(211,183,121,255));
    d->PushClipRect({p.x+54,p.y+8},{p.x+w-10,p.y+h-6},true);
    HudText::AddText(d,{p.x+54,p.y+11},IM_COL32(232,212,169,255),title);
    if(subtitle)HudText::AddText(d,{p.x+54,p.y+12+ImGui::GetTextLineHeight()},IM_COL32(160,153,136,255),subtitle);
    d->PopClipRect();ImGui::Dummy({w,h});ImGui::Spacing();
}
inline std::string Label(const std::vector<Option>& options,const std::string& value) {
    for(const auto& option:options)if(option.value==value)return option.label;
    return value=="allitems"?"All items":value;
}
inline bool Choose(const char* id,std::string& value,const std::vector<Option>& options) {
    bool changed=false;static std::map<ImGuiID,std::string> searches;
    ImGui::SetNextItemWidth(-1);
    if(HudText::BeginCombo(id,Label(options,value).c_str())){
        auto& query=searches[ImGui::GetID("search")];InputTextWithHint("##search","Search...",&query);
        ImGui::BeginChild("options",ImVec2(0,180));
        for(const auto& option:options){std::string haystack=option.label+" "+option.value,needle=query;
            std::transform(haystack.begin(),haystack.end(),haystack.begin(),[](unsigned char c){return char(std::tolower(c));});
            std::transform(needle.begin(),needle.end(),needle.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(!needle.empty()&&haystack.find(needle)==std::string::npos)continue;
            ImGui::PushID(option.value.c_str());
            if(HudText::Selectable(option.label.c_str(),option.value==value)){value=option.value;changed=true;ImGui::CloseCurrentPopup();}
            if(ImGui::IsItemHovered())HudText::SetTooltip("%s",option.value.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();ImGui::EndCombo();
    }
    return changed;
}
inline bool MultiChoose(const Spec& spec,Value& value,const Catalog& catalog) {
    const std::string key=spec.key;auto options=Options(key,catalog);
    if(key=="codes")options.insert(options.begin(),{"All items","allitems"});
    if(key=="name_style"||key=="background_style"){
        std::string selected=value.choices.empty()?"":value.choices[0];
        if(Choose("##value",selected,options)){value.choices={selected};return true;}return false;
    }
    bool changed=false;
    ImGui::SetNextItemWidth((std::min)(180.f,ImGui::GetContentRegionAvail().x*.36f));
    int match=value.exclude?1:0;
    if(HudText::Combo("##match",&match,"Is any of\0Is none of\0")){value.exclude=match==1;changed=true;}
    ImGui::SameLine();ImGui::SetNextItemWidth(-1);
    std::string preview;
    for(const auto& selected:value.choices){if(!preview.empty())preview+=", ";preview+=Label(options,selected);if(preview.size()>85){preview+="...";break;}}
    if(preview.empty())preview="Select...";
    static std::map<std::string,std::string> searches;
    if(HudText::BeginCombo("##choices",preview.c_str())){
        auto& query=searches[key];InputTextWithHint("##search","Search by name...",&query);
        HudText::TextDisabled("Choose one or more; click outside to finish.");
        ImGui::BeginChild("options",ImVec2(0,210));
        if(HudText::Selectable("Clear selection",false,ImGuiSelectableFlags_DontClosePopups)){value.choices.clear();changed=true;}
        // Unknown/custom selected values stay visible and removable even when
        // their catalog is unavailable or the player changed mods.
        for(const auto& selected:value.choices)if(std::none_of(options.begin(),options.end(),[&](const Option& option){return option.value==selected;}))options.insert(options.begin(),{selected,selected});
        for(const auto& option:options){std::string haystack=option.label+" "+option.value,needle=query;
            std::transform(haystack.begin(),haystack.end(),haystack.begin(),[](unsigned char c){return char(std::tolower(c));});
            std::transform(needle.begin(),needle.end(),needle.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(!needle.empty()&&haystack.find(needle)==std::string::npos)continue;
            auto found=std::find(value.choices.begin(),value.choices.end(),option.value);bool selected=found!=value.choices.end();
            ImGui::PushID(option.value.c_str());
            if(HudText::Selectable(option.label.c_str(),selected,ImGuiSelectableFlags_DontClosePopups)){
                if(selected)value.choices.erase(found);else{if(key=="codes"){if(option.value=="allitems")value.choices.clear();else value.choices.erase(std::remove(value.choices.begin(),value.choices.end(),"allitems"),value.choices.end());}value.choices.push_back(option.value);}changed=true;
            }
            if(ImGui::IsItemHovered())HudText::SetTooltip("%s",option.value.c_str());ImGui::PopID();
        }
        ImGui::EndChild();ImGui::EndCombo();
    }
    return changed;
}
inline bool ValueControl(const Spec& spec,Value& value,const Catalog& catalog) {
    bool changed=false;
    if(spec.kind==Kind::Choices)return MultiChoose(spec,value,catalog);
    if(spec.kind==Kind::Boolean){int selected=value.flag?0:1;ImGui::SetNextItemWidth(-1);if(HudText::Combo("##value",&selected,"Yes\0No\0")){value.flag=selected==0;return true;}}
    if(spec.kind==Kind::Number||spec.kind==Kind::Stat){
        if(spec.kind==Kind::Stat){
            std::string stat=std::to_string(value.stat);if(Choose("##stat",stat,catalog.stats)){Integer(stat,value.stat);changed=true;}
        }
        ImGui::SetNextItemWidth((std::min)(180.f,ImGui::GetContentRegionAvail().x*.36f));
        changed|=HudText::Combo("##operation",&value.operation,Operators,spec.kind==Kind::Stat?7:6);
        ImGui::SameLine();ImGui::SetNextItemWidth(-1);changed|=HudText::InputInt("##amount",&value.number,0);
        if(spec.kind==Kind::Stat){
            if(value.operation==6){HudText::TextUnformatted("Through");ImGui::SameLine();ImGui::SetNextItemWidth(-1);changed|=HudText::InputInt("##upper",&value.upper,0);}
            if(HudText::TreeNode("Optional stat parameter")){ImGui::SetNextItemWidth(-1);changed|=HudText::InputInt("##parameter",&value.parameter,0);ImGui::TreePop();}
        }
    }
    if(spec.kind==Kind::Text){
        ImGui::SetNextItemWidth(-1);changed|=InputText("##text",&value.text);
        if(std::string(spec.key)!="style"){
            const float half=(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)*.5f;
            ImGui::SetNextItemWidth(half);
            if(HudText::BeginCombo("##insert_detail","Insert item detail...")){
                for(const auto& token:std::vector<Option>{{"Item name","{name}"},{"Socket count","{sockets}"},{"Maximum sockets","{maxsock}"},{"Item level","{ilvl}"},{"Item link (chat)","{link}"}})
                    if(HudText::Selectable(token.label.c_str())){value.text+=token.value;changed=true;}
                ImGui::EndCombo();
            }
            ImGui::SameLine();ImGui::SetNextItemWidth(-1);
            if(HudText::BeginCombo("##insert_color","Insert text color...")){
                for(const char* color:{"white","red","green","blue","gold","gray","orange","yellow","purple","turquoise","pink"})
                    if(HudText::Selectable(color)){value.text+="{"+std::string(color)+"}";changed=true;}
                ImGui::EndCombo();
            }
        }
        if(std::string(spec.key)=="audio")HudText::TextDisabled("Enable audio in Settings to hear this action.");
        if(std::string(spec.key)=="hide_mismatches")HudText::TextWrapped("Hides items with the same item/location match that fail another condition.");
    }
    if(spec.kind==Kind::Color){
        changed|=HudText::ColorEdit4("##color",value.color.data(),ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
        if(std::string(spec.key)=="border"){HudText::TextUnformatted("Thickness");ImGui::SetNextItemWidth(-1);changed|=HudText::SliderFloat("##thickness",&value.thickness,1,12,"%.0f px");}
    }
    return changed;
}
inline Value Default(const Spec& spec,const Catalog& catalog) {
    Value value;
    if(spec.kind==Kind::Choices){auto options=Options(spec.key,catalog);if(std::string(spec.key)=="codes")value.choices={"allitems"};else if(!options.empty())value.choices={options[0].value};}
    if(std::string(spec.key)=="quality")value.choices={"Unique"};
    if(std::string(spec.key)=="location")value.choices={"onground"};
    if(std::string(spec.key)=="ilvl"){value.number=80;value.operation=1;}
    if(std::string(spec.key)=="suffix")value.text=" Keep";
    if(std::string(spec.key)=="name_override")value.text="{name}";
    if(std::string(spec.key)=="notify")value.text="Dropped {link}";
    if(std::string(spec.key)=="audio")value.text="Valuable item";
    if(std::string(spec.key)=="stat"||std::string(spec.key)=="pstat"){value.operation=1;value.number=30;}
    if(std::string(spec.key)=="background")value.color={0.08f,0.08f,0.08f,0.9f};
    if(std::string(spec.key)=="border")value.color={0.94f,0.63f,0.16f,0.9f};
    return value;
}
inline bool Render(std::string& raw,std::string& status,const Catalog& catalog,int section=-1) {
    struct Row { const Spec* spec; Value value; };
    static std::string observed,title;static std::vector<Row> rows;
    static bool enabled=true,titleEditable=true,enabledEditable=true;static int unknown=0;
    if(observed!=raw){
        observed=raw;rows.clear();title.clear();enabled=true;titleEditable=enabledEditable=true;unknown=0;
        std::set<std::string> seen;
        for(const auto& field:RuleFields(raw)){
            std::string key=Canonical(field.first);if(Trim(field.second)=="nil"||!seen.insert(key).second)continue;
            if(key=="label"){Document doc;titleEditable=doc.IsString(field.second);if(titleEditable)title=DecodeString(field.second);continue;}
            if(key=="enabled"){enabledEditable=field.second=="true"||field.second=="false";enabled=field.second!="false";continue;}
            if(const auto* spec=FindSpec(key))rows.push_back({spec,Decode(*spec,field.second)});else ++unknown;
        }
    }
    bool changed=false;
    auto set=[&](const std::string& key,const std::string& expression){if(SetField(raw,key,expression,status))changed=true;};
    if(section<=0){
        HudText::TextUnformatted("Rule name");ImGui::SetNextItemWidth(-1);ImGui::BeginDisabled(!titleEditable);
        if(InputText("##rule_name",&title))set("label",Quote(title));ImGui::EndDisabled();
        ImGui::BeginDisabled(!enabledEditable);if(HudText::Checkbox("Rule enabled",&enabled))set("enabled",enabled?"true":"false");ImGui::EndDisabled();
    }
    if(!catalog.loaded&&!catalog.status.empty())HudText::TextWrapped("%s",catalog.status.c_str());
    for(bool actions:{false,true}){
        if(section>=0&&section!=int(actions))continue;
        Banner(actions?Symbol::Actions:Symbol::Conditions,actions?"Then apply these actions":"Match these conditions",actions?"Choose how matching items look or behave.":"All conditions must match. Multiple choices mean any.");
        bool any=false;
        for(auto& row:rows){if(row.spec->action!=actions)continue;any=true;
            ImGui::PushID(row.spec->key);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{14,12});
            ImGui::PushStyleColor(ImGuiCol_ChildBg,IM_COL32(23,22,20,230));
            ImGui::BeginChild("field",{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY,ImGuiWindowFlags_NoScrollbar);
            HudText::TextColored({.85f,.76f,.56f,1},"%s",row.spec->label);
            const float removeWidth=HudText::CalcTextSize("Remove").x+ImGui::GetStyle().FramePadding.x*2;
            if(ImGui::GetContentRegionAvail().x>removeWidth+12)ImGui::SameLine(ImGui::GetWindowContentRegionMax().x-removeWidth);
            if(HudText::SmallButton("Remove"))set(row.spec->key,"nil");
            if(!row.value.editable){
                HudText::TextWrapped("Custom expression preserved. Edit in Rule code or replace with selections.");
                if(ThemeButton("Use selections")){row.value=Default(*row.spec,catalog);set(row.spec->key,Encode(*row.spec,row.value));}
            } else if(ValueControl(*row.spec,row.value,catalog))set(row.spec->key,Encode(*row.spec,row.value));
            ImGui::EndChild();ImGui::PopStyleColor();ImGui::PopStyleVar();ImGui::Spacing();ImGui::PopID();
        }
        if(!any)HudText::TextDisabled(actions?"No actions yet. Add an action below.":"All items match until you add conditions.");
        ImGui::SetNextItemWidth(-1);
        if(HudText::BeginCombo(actions?"##add_action":"##add_condition",actions?"+ Add an action...":"+ Add a condition...")){
            for(const auto& spec:Specs){if(spec.action!=actions||std::any_of(rows.begin(),rows.end(),[&](const Row& row){return row.spec==&spec;}))continue;
                if(HudText::Selectable(spec.label)){Value value=Default(spec,catalog);set(spec.key,Encode(spec,value));}
            }
            ImGui::EndCombo();
        }
    }
    if(unknown)HudText::TextWrapped("%d additional custom field(s) are preserved in this rule.",unknown);
    if(rows.empty()&&RuleFields(raw).empty())HudText::TextWrapped("If this rule is generated by a function, edit it in advanced mode or add a new rule.");
    return changed;
}
}
