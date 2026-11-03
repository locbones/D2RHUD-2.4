#include "../../RuntimeTelemetry.h"
#include "MonsterStatsOverlay.h"
#include "MonsterStatsData.h"
#include "../../d2/D2Ptrs.h"
#include "../ItemFilter/ItemFilter.h"
#include "../HudText.h"
#include "../SunderSystem.h"
#include <array>
#include <format>
#include <imgui.h>

namespace HudMonsterStats {
namespace {
constexpr std::array<uint32_t,6> Stats{39,41,43,45,36,37};
constexpr std::array<ImU32,6> Colors{
    IM_COL32(170,50,50,255),IM_COL32(170,170,50,255),IM_COL32(50,90,170,255),
    IM_COL32(50,170,50,255),IM_COL32(255,255,255,255),IM_COL32(255,175,0,255)};
D2UnitStrc* Lookup(D2GameStrc* game,D2C_UnitTypes type,uint32_t id){
    if(type>UNIT_MONSTER)return nullptr;
    if(game)return UNITS_GetServerUnitByTypeAndId?UNITS_GetServerUnitByTypeAndId(game,type,id):nullptr;
    using Get= D2UnitStrc*(__fastcall*)(uint64_t,uint32_t,uint32_t,uint32_t);
    return reinterpret_cast<Get>(Pattern::Address(0x66510))(
        Pattern::Address(0x1d442e0+0x400*type),id&0x7f,id,type);
}
struct FontScope {
    bool pushed=false;
    explicit FontScope(ImFont* font):pushed(font!=nullptr){if(pushed)ImGui::PushFont(font);}
    ~FontScope(){if(pushed)ImGui::PopFont();}
};
}
int LocalTerrorStat(D2GameStrc* game){
    if(!STATLIST_GetUnitStatSigned)return 0;
    const auto local=GetClientPlayerUnit();
    if(!local)return 0;
    const auto player=game?Lookup(game,UNIT_PLAYER,local->dwUnitId):local;
    return player?STATLIST_GetUnitStatSigned(player,361,0):0;
}
void Draw(D2GameStrc* game,bool enabled){
    HudTelemetry::Scope measured(HudTelemetry::Feature::MonsterStats);
    if(!enabled || !gMouseHover || !gMouseHover->IsHovered ||
        gMouseHover->HoveredUnitType>UNIT_MONSTER || !STATLIST_GetUnitStatSigned)return;
    // One lookup per hovered unit, with the correct host/joiner ownership.
    const auto unit=Lookup(game,gMouseHover->HoveredUnitType,gMouseHover->HoveredUnitId);
    if(!unit)return;
    const int hp=STATLIST_GetUnitStatSigned(unit,STAT_HITPOINTS,0);
    if(hp<=0)return;
    const auto& io=ImGui::GetIO();
    if(io.DisplaySize.x<=0 || io.DisplaySize.y<=0)return;
    const int index=Data::FontIndex(io.DisplaySize.y,io.Fonts?io.Fonts->Fonts.Size:0);
    FontScope font(index>=0?io.Fonts->Fonts[index]:nullptr);
    const auto draw=ImGui::GetBackgroundDrawList();
    const float center=(draw->GetClipRectMin().x+draw->GetClipRectMax().x)*0.5f;
    const float spacing=HudText::CalcTextSize("  ").x;
    float total=0;
    std::array<std::string,6> values;
    std::array<float,6> widths{};
    for(size_t i=0;i<Stats.size();++i){
        values[i]=std::format("{}",SunderSystem::EffectiveResistance(game,unit,Stats[i],STATLIST_GetUnitStatSigned(unit,Stats[i],0)));
        widths[i]=HudText::CalcTextSize(values[i].c_str()).x;
        total+=widths[i]+(i?spacing:0);
    }
    float x=center-total*0.5f;
    for(size_t i=0;i<Stats.size();++i){
        if(i)x+=spacing;
        HudText::AddText(draw,{x,io.DisplaySize.y*0.0745f},Colors[i],values[i].c_str());x+=widths[i];
    }
    const int maximum=STATLIST_GetUnitStatSigned(unit,STAT_MAXHP,0);
    std::string health;
    if(game)health=std::format("{} / {}",hp>>8,maximum>>8);
    else if(const auto percent=Data::HealthPercent(hp,maximum))health=std::format("{}%",*percent);
    if(!health.empty()){
        const auto width=HudText::CalcTextSize(health.c_str()).x;
        HudText::AddText(draw,{center-width*0.5f+1,io.DisplaySize.y*0.043f},IM_COL32_WHITE,health.c_str());
    }
}
}
