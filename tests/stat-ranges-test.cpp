// Exercise native layout capture with synthetic memory; never inject into D2R.
#include "../d2rhud/plugin/StatRanges.cpp"
#include <imgui_internal.h>
#include <cassert>
#include <iostream>
#include <limits>
#include <fstream>

uintptr_t testExecutable = 0;
DWORD64 Pattern::Address(uint32_t rva) { return testExecutable ? testExecutable + rva : 0; }
D2UnitStrc* GetClientPlayerUnit() { return nullptr; }

namespace {
void* __fastcall MockBuild(void* output,D2UnitStrc* viewer,D2UnitStrc* owner,D2UnitStrc* item,int32_t context,bool details) {
    assert(viewer && owner && item && context==37 && details);
    return output;
}
std::array<uint8_t,0x2638> nativeLayout{};
int destroyedLayouts=0;
std::array<int,2> lastLayoutLimits{};
void __fastcall MockDestroyLayout(void*) { ++destroyedLayouts; }
void __fastcall MockLayout(const char*, const void*, void* output, float, const int* limits) {
    std::copy_n(limits,2,lastLayoutLimits.begin());
    memcpy(output,nativeLayout.data(),nativeLayout.size());
}
void __fastcall MockDraw(const char* text, const int* rect, const void* style, float scale) {
    std::array<uint8_t,0x2638> output{};
    StatRanges::HookLayout(text,style,output.data(),scale,rect+2);
}
void CheckNativeExecutable(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate); assert(input);
    std::vector<uint8_t> exe(static_cast<size_t>(input.tellg())); input.seekg(0);
    input.read(reinterpret_cast<char*>(exe.data()),exe.size()); assert(input);
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(exe.data()); assert(dos->e_magic==IMAGE_DOS_SIGNATURE);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(exe.data()+dos->e_lfanew); assert(nt->Signature==IMAGE_NT_SIGNATURE);
    const auto sections=IMAGE_FIRST_SECTION(nt);
    auto at=[&](uint32_t rva)->uintptr_t {
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
            if(rva>=sections[i].VirtualAddress && rva-sections[i].VirtualAddress<sections[i].SizeOfRawData)
                return reinterpret_cast<uintptr_t>(exe.data()+sections[i].PointerToRawData+rva-sections[i].VirtualAddress);
        return 0;
    };
    assert(StatRanges::SupportedBuilder(at));
    assert(memcmp(reinterpret_cast<const void*>(at(0x5d9940)),"\x40\x53\x48\x83\xec\x20",6)==0);
    auto changed=std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(at(0x1bb940)),reinterpret_cast<const uint8_t*>(at(0x1bb940))+0x127a);
    changed[changed.size()/2]^=1;
    assert(!StatRanges::SupportedBuilder([&](uint32_t){return reinterpret_cast<uintptr_t>(changed.data());}));
    std::cout<<"Native item-tooltip builder signature matches the installed executable; altered code is rejected.\n";
}
}

int main(int argc, char** argv) {
    using namespace StatRanges;
    Database db;
    db.statIds = { {"strength", 0}, {"energy", 1}, {"dexterity", 2}, {"vitality", 3}, {"fireresist", 39}, {"lightresist", 41}, {"coldresist", 43}, {"poisonresist", 45},
        {"item_mindamage_percent", 18}, {"item_maxdamage_percent", 17}, {"item_singleskill", 107} };
    db.properties["str"] = {{1, 0}};
    db.properties["res-all"] = {{1, 39}, {3, 41}, {3, 43}, {3, 45}};
    db.properties["dmg%"] = {{7, -1}};
    db.properties["skill"] = {{22, 107}};
    db.strings["strength"] = {"%+d to Strength", "%+d zu St\xc3\xa4rke"};
    db.strings["all"] = {"All Resistances %+d%%"};
    db.strings["strModEnhancedDamage"] = {"%+d%% Enhanced Damage"};
    db.strings["skill"] = {"%+d to %s %s"};
    db.strings["shield"] = {"Energy Shield"};
    Stat str; str.id = 0; str.name = "strength"; str.function = 19; str.positive = str.negative = "strength"; db.stats[0] = str;
    for (int id : {39, 41, 43, 45}) { Stat s; s.id = id; s.group = 2; s.groupPositive = "all"; db.stats[id] = s; }
    Stat skill; skill.id = 107; skill.function = 27; skill.positive = "skill"; db.stats[107] = skill;
    db.skillNames[58] = "shield";
    db.uniques[0] = { {"str", "", 10, 20}, {"str", "", 5, 10} };
    db.affixes = { {{"str", "", 2, 5}}, {{"str", "", 8, 15}}, {{"res-all", "", 5, 10}} };
    db.sets[0].base = {{"str", "", 2, 5}};
    Item item; item.quality = 7; item.index = 0;
    assert((db.ForItem(item).at({0,0}) == Bounds{15,30}));
    item.quality = 6; item.prefixes[0] = 2; item.suffixes[0] = 1;
    assert((db.ForItem(item).at({0,0}) == Bounds{10,20}));
    item.automagic = 3;
    auto totals = db.ForItem(item);
    assert((totals.at({39,0}) == Bounds{5,10}));
    assert((totals.at({45,0}) == Bounds{5,10})); // not summed across four different stats
    item.prefixes = {}; item.suffixes = {}; item.automagic = 0; item.quality = 5;
    assert((db.ForItem(item).at({0,0}) == Bounds{2,5}));
    totals = {}; db.Apply(totals, {{"str", "", -10, -20}, {"str", "", 2, 3}});
    assert((totals.at({0,0}) == Bounds{-18,-7}));
    totals = {}; db.Apply(totals, {{"str", "", 20, 30}});
    assert(MatchLine(db, totals, "+25 to Strength", true).text == "[20-30]");
    assert(MatchLine(db, totals, "\xc3\xbf" "c3+25 to Strength\xc3\xbf" "c0", true).text == "[20-30]");
    assert(MatchLine(db, totals, "+25 zu St\xc3\xa4rke", true).text == "[20-30]");
    assert(!MatchLine(db, totals, "+25 to Dexterity", true).found);
    assert(!MatchLine(db, totals, "+50 to Strength", true).found);
    assert(!MatchLine(db, totals, "-25 to Strength", true).found);
    db.strings["pierce"]={"-%d%% to Enemy Cold Resistance"};
    Stat pierce; pierce.id=333; pierce.function=19; pierce.positive=pierce.negative="pierce"; db.stats[333]=pierce;
    assert(MatchLine(db,{{{333,0},{10,20}}},"-13% to Enemy Cold Resistance",true).text=="[10-20]");
    assert(!MatchLine(db,{{{333,0},{10,20}}},"-30% to Enemy Cold Resistance",true).found);
    assert(MatchLine(db,{{{333,0},{-20,-10}}},"-13% to Enemy Cold Resistance",true).text=="[-20--10]");
    totals = {}; db.Apply(totals, {{"str", "", 20, 20}});
    assert(!MatchLine(db, totals, "+20 to Strength", false).found);
    assert(MatchLine(db, totals, "+20 to Strength", true).text == "[20-20]");
    totals = {}; db.Apply(totals, {{"res-all", "", 5, 10}});
    assert(MatchLine(db, totals, "All Resistances +8%", true).text == "[5-10]");
    Add(totals, {39,0}, {0,5});
    assert(MatchLine(db, totals, "All Resistances +8%", true).text == "[5-10]");
    totals.erase({45,0});
    assert(!MatchLine(db, totals, "All Resistances +8%", true).found);
    totals = {}; db.Apply(totals, {{"dmg%", "", 100, 150}, {"dmg%", "", 50, 80}});
    assert(MatchLine(db, totals, "+190% Enhanced Damage", true).text == "[150-230]");
    totals = {}; db.Apply(totals, {{"skill", "58", 1, 3}, {"skill", "37", 1, 5}});
    assert(MatchLine(db, totals, "+2 to Energy Shield (Sorceress Only)", true).text == "[1-3]");
    assert(!MatchLine(db, totals, "+2 to Warmth (Sorceress Only)", true).found);

    const std::string customFormat="Min: %d, Max: %d";
    assert(ValidRangeFormat(customFormat) && ValidRangeFormat("%d%% to %d%%"));
    for(const auto& invalid:std::vector<std::string>{"", "%d", "%d %d %d", "%s %d", "%n %d", "%lld %d", "%02d %d", "%d %d %", "%d\n%d", "%d\t%d", std::string("%d\0%d",5), std::string(129,'x')+"%d%d"}) assert(!ValidRangeFormat(invalid));
    auto strengthRange=MatchLine(db,{{{0,0},{20,30}}},"+25 to Strength",true);
    assert(FormatAnnotation(strengthRange,customFormat)=="Min: 20, Max: 30");
    assert(FormatAnnotation(strengthRange,"%d%% to %d%%")=="20% to 30%");
    assert(FormatAnnotation(strengthRange,"invalid %n")=="[20-30]");
    assert(FormatAnnotation({"negative",true,{{-20,-10}}},customFormat)=="Min: -20, Max: -10");
    assert(FormatAnnotation({"fixed",true,{{20,20}}},customFormat)=="Min: 20, Max: 20");
    assert(FormatAnnotation({"wide",true,{{INT64_MIN,INT64_MAX}}},customFormat)=="Min: -9223372036854775808, Max: 9223372036854775807");
    db.statIds["firemindam"]=1000; db.statIds["firemaxdam"]=1001;
    db.strings["strModFireDamageRange"]={"Adds %d-%d Fire Damage"};
    auto damageRange=MatchLine(db,{{{1000,0},{1,3}},{{1001,0},{10,20}}},"Adds 2-15 Fire Damage",true);
    assert(damageRange.found && damageRange.text=="[1-3 / 10-20]");
    assert(FormatAnnotation(damageRange,customFormat)=="Min: 1, Max: 3 / Min: 10, Max: 20");
    db.stats[1002]=str; db.stats[1002].id=1002;
    assert(!MatchLine(db,{{{0,0},{1,234}},{{1002,0},{12,34}}},"+15 to Strength",true).found);

    // Multiple affixes can also belong to a socketed jewel; socket totals must
    // add to the item's rolls rather than replace them.
    Totals socketed = {{{0,0},{10,20}}};
    auto jewel = db.ForItem(Item{6,-1,1,{2,0,0},{1,0,0},0});
    for (const auto& entry : jewel) Add(socketed, entry.first, entry.second);
    assert((socketed.at({0,0}) == Bounds{20,40}));
    db.baseItems[10] = {"cap",1,"helm",""}; db.itemTypes["helm"] = {"armo"};
    db.runewords.push_back({{"r01","r02"},{"armo"},{"shld"},{{"str","",10,20}}});
    assert(db.ForRuneword(10,{"r01","r02"}));
    assert(!db.ForRuneword(10,{"r02","r01"}));
    assert(!db.ForRuneword(10,{"r01"}));

    // Restore full and partially truncated tails without reprocessing inserted
    // text, mixing item identities, or changing a short unrelated tooltip.
    std::string full;
    for (int i=0;i<180;++i) full += "Stat line " + std::to_string(i) + " +123\n";
    TooltipText::Remember(100,full);
    auto prefix=full.substr(0,TooltipText::LegacyCapacity-1);
    assert(TooltipText::Expand(100,"Name\n"+prefix)=="Name\n"+full);
    assert(TooltipText::Expand(100,"Long header\n"+full.substr(0,800))=="Long header\n"+full);
    assert(TooltipText::Expand(101,"Name\n"+prefix)=="Name\n"+prefix);
    assert(TooltipText::Expand(100,"Short text")=="Short text");
    TooltipText::Remember(100,prefix); // An exact, genuinely 1023-byte result is no longer truncated.
    assert(TooltipText::Expand(100,prefix)==prefix);
    std::array<uint8_t,0x200> panel{}; const char* panelText="Short text";
    auto panelPut=[&](size_t offset,auto value){memcpy(panel.data()+offset,&value,sizeof(value));};
    panelPut(0,reinterpret_cast<uintptr_t>(panelText)); panelPut(8,uint64_t(strlen(panelText)));
    std::array<int,4> originalRect{1700,200,200,100}; memcpy(panel.data()+0x60,originalRect.data(),16);
    TooltipText::SetViewport(1920,1080,1); TooltipText::BeginPanel(101,panel.data()); TooltipText::FitBounds(1700,200,2050,300);
    int movedX=0;memcpy(&movedX,panel.data()+0x60,4);assert(movedX==1570);
    auto content=TooltipText::ContentRect(reinterpret_cast<const int*>(panel.data()+0x60));
    assert(content[0]==1570 && content[2]==200); // Native text must not recenter into the added width.
    std::array<int,4> enlarged{};memcpy(enlarged.data(),panel.data()+0x60,16);assert(enlarged[2]==350);
    TooltipText::EndPanel(); assert(memcmp(panel.data()+0x60,originalRect.data(),16)==0);

    Activation a;
    assert(!a.Update(true, false, false, false, true));
    assert(a.Update(true, false, false, true, true));
    assert(a.Update(true, false, false, true, true)); // autorepeat
    assert(a.Update(true, false, false, false, true));
    assert(!a.Update(true, false, false, true, true));
    a.Reset();
    assert(a.Update(true, true, false, true, true));
    assert(!a.Update(true, true, false, false, true));
    assert(!a.Update(true, true, false, true, false)); // settings/capture/focus loss
    assert(!a.Update(true, true, false, true, true)); // held across focus transition
    assert(!a.Update(true, true, false, false, true));
    assert(a.Update(true, true, false, true, true));
    assert(a.Update(false, false, true, false, true));
    assert(!a.Update(false, false, false, true, true));

    // Opening the Control Center/settings must not suppress a saved always-on
    // display. Only hotkeys need protection against menu/text-field input.
    config = {}; config.enabled = true; activation.Reset();
    assert(UpdateActivation(true,true,false,false,false));
    assert(UpdateActivation(true,true,true,false,false));
    assert(UpdateActivation(true,true,true,true,false));
    assert(!UpdateActivation(false,true,false,false,false));
    assert(!UpdateActivation(true,false,false,false,false));
    config.enabled = false; assert(!UpdateActivation(true,true,true,false,false));
    config.hotkey = true; config.hold = true; activation.Reset();
    assert(!UpdateActivation(true,true,true,false,true));
    assert(!UpdateActivation(true,true,false,false,true)); // Held across menu close stays disarmed.
    assert(!UpdateActivation(true,true,false,false,false));
    assert(UpdateActivation(true,true,false,false,true));
    assert(!UpdateActivation(true,true,false,true,true));
    activation.Reset();
    assert(UpdateActivation(true,true,true,true,false,true)); // Live hold-mode settings preview.
    assert(!activation.latched);
    assert(!UpdateActivation(true,true,false,false,false)); // Preview closes without enabling hold.
    assert(!UpdateActivation(false,true,true,false,false,true));
    assert(!UpdateActivation(true,false,true,false,false,true));
    config.hold = false; activation.Reset();
    assert(!UpdateActivation(true,true,true,false,true));
    assert(!activation.latched);
    UpdateActivation(true,true,false,false,false);
    assert(UpdateActivation(true,true,false,false,true));
    assert(UpdateActivation(true,true,false,false,false));
    assert(UpdateActivation(true,true,true,true,false)); // A latched toggle stays visible while editing.
    activation.Reset();
    assert(UpdateActivation(true,true,true,true,false,true)); // Live toggle-mode settings preview.
    assert(!activation.latched);
    assert(!UpdateActivation(true,true,false,false,false));

    nlohmann::json settings = {{"StatRanges", {{"Enabled",true}, {"Mode","Hotkey"}, {"HotkeyBehavior","Toggle"}, {"TextSize",24}, {"Spacing",17}, {"Color",{0.2,0.4,0.6,0.8}}}}};
    LoadFromJson(settings); nlohmann::ordered_json saved; SaveToJson(saved);
    assert(saved["StatRanges"]["Mode"] == "Hotkey" && saved["StatRanges"]["HotkeyBehavior"] == "Toggle");
    assert(saved["StatRanges"]["TextSize"] == 24 && saved["StatRanges"]["Spacing"] == 17);
    assert(std::abs(saved["StatRanges"]["Color"][2].get<float>() - 0.6f) < 0.001f);
    settings["StatRanges"]["Format"]=customFormat; LoadFromJson(settings); SaveToJson(saved);
    assert(saved["StatRanges"]["Format"]==customFormat);
    LoadFromJson(saved); assert(std::string(config.format)==customFormat);
    for(const nlohmann::json invalid:{nlohmann::json(42),nlohmann::json("%n %d"),nlohmann::json(std::string(129,'x')+"%d%d")}) {
        LoadFromJson({{"StatRanges",{{"Format",invalid}}}}); SaveToJson(saved);
        assert(saved["StatRanges"]["Format"]==DefaultRangeFormat);
    }
    LoadFromJson({{"StatRanges",{{"Mode",42},{"HotkeyBehavior",false},{"TextSize",999},{"Spacing",-100},{"Color",{1,0,"bad",1}}}}});
    SaveToJson(saved); assert(saved["StatRanges"]["TextSize"] == 40 && saved["StatRanges"]["Spacing"] == 0);

    // Test the verified native glyph/row layout at its real offsets.
    auto copy = std::make_shared<Database>(db);
    tooltip = {}; tooltip.db = copy; tooltip.text = "+25 to Strength"; tooltip.totals = {{{0,0},{20,30}}};
    std::vector<Glyph> chars;
    for (const char* c = tooltip.text; *c; ++c) { Glyph glyph{}; glyph.character = *c; glyph.penX = static_cast<int>(chars.size()) * 8 * 64; glyph.advance = 8 * 64; chars.push_back(glyph); }
    Row row{chars.size(),0,0}; std::array<uint8_t,0x2638> layout{};
    auto put = [](auto& mem, size_t offset, auto value) { memcpy(mem.data()+offset, &value, sizeof(value)); };
    put(layout, 0, reinterpret_cast<uintptr_t>(chars.data())); put(layout, 8, uint64_t(chars.size()));
    put(layout, 0x1e18, reinterpret_cast<uintptr_t>(&row)); put(layout, 0x1e20, uint64_t(1)); put(layout, 0x2634, int32_t(20*64));
    std::array<uint8_t,0xa0> style{}; put(style, 0x34, 1.0f);
    int rect[] = {100,200,300,20}; textContext = {tooltip.text, rect, style.data()}; showFixed.store(true);
    CaptureLayout(layout.data());
    assert(drawLines.size() == 1 && drawLines[0].text == "[20-30]");
    assert(drawLines[0].right == 100 + chars.size()*8 && drawLines[0].middle == 210);
    row.end = chars.size()+1; drawLines.clear(); CaptureLayout(layout.data()); assert(drawLines.empty());
    EndTooltip();

    // Reproduce the inventory failure through the entire hook chain. The world
    // hover is null (or an unrelated monster) while the native tooltip builder
    // supplies the actual identified item. This must reach matching and drawing,
    // rather than testing CaptureLayout with manually populated tooltip context.
    D2ItemDataStrc itemData{}; itemData.dwQualityNo=7; itemData.dwFileIndex=0; itemData.dwItemLevel=20; itemData.dwItemFlags=0x10;
    D2UnitStrc itemUnit{}; itemUnit.dwUnitType=UNIT_ITEM; itemUnit.dwUnitId=456; itemUnit.pItemData=&itemData;
    D2UnitStrc monster{}; monster.dwUnitType=UNIT_MONSTER;
    D2UnitStrc viewer{},owner{};
    database.store(copy); active.store(false);
    textContext={};
    originalDraw=MockDraw; originalLayout=MockLayout; originalBuild=MockBuild; destroyLayout=MockDestroyLayout;
    auto metrics=std::make_shared<RangeMetrics>(); metrics->advance.assign(128,9); rangeMetrics.store(metrics);
    row.end=chars.size(); nativeLayout=layout;
    std::array<uint8_t,0x2d8> inventoryPanel{};
    const char* itemText="+25 to Strength";
    put(inventoryPanel,0,reinterpret_cast<uintptr_t>(itemText)); put(inventoryPanel,8,uint64_t(strlen(itemText)));
    memcpy(inventoryPanel.data()+0x60,rect,sizeof(rect));
    memcpy(inventoryPanel.data()+0x80,style.data(),style.size());
    assert(HookBuild(inventoryPanel.data(),&viewer,&owner,&itemUnit,37,true)==inventoryPanel.data());
    active.store(true); // Enabling ranges must work on an already-built tooltip.
    for(auto world:{static_cast<D2UnitStrc*>(nullptr),&monster}) {
        drawLines.clear(); BeginTooltip(world,inventoryPanel.data());
        assert(tooltip.db && tooltip.unitId==456 && (tooltip.totals.at({0,0})==Bounds{15,30}));
        HookDraw(itemText,reinterpret_cast<const int*>(inventoryPanel.data()+0x60),inventoryPanel.data()+0x80,1.0f);
        assert(drawLines.size()==1 && drawLines[0].text=="[15-30]");
        assert(textContext.text==nullptr); EndTooltip();
    }
    // The same native rectangle draws the background before HookDraw runs.
    // Measure in BeginTooltip so the first frame includes every annotation,
    // with configurable font size/spacing and no change to centered text width.
    for(const auto& format:{std::string(DefaultRangeFormat),customFormat}) for(float scale:{1.0f,2.0f}) for(float fontSize:{16.0f,40.0f}) for(float gap:{0.0f,40.0f}) {
        auto measured=std::make_shared<RangeMetrics>(); measured->size=fontSize*scale; measured->gap=gap*scale;
        measured->padding=4*scale; measured->advance.assign(128,fontSize*scale*0.6f); measured->format=format; rangeMetrics.store(measured);
        const std::array<int,4> edgeRect{int(1800*scale),int(1060*scale),int(100*scale),int(20*scale)};
        memcpy(inventoryPanel.data()+0x60,edgeRect.data(),16);
        const std::array<int,4> secondary{int(1800*scale),int(1020*scale),int(100*scale),int(30*scale)};
        memcpy(inventoryPanel.data()+0x70,secondary.data(),16);
        TooltipText::SetViewport(1920*scale,1080*scale,scale);
        const int destroyedBefore=destroyedLayouts;
        BeginTooltip(nullptr,inventoryPanel.data()); assert(destroyedLayouts==destroyedBefore+1);
        std::array<int,4> background{}; memcpy(background.data(),inventoryPanel.data()+0x60,16);
        assert(background[2]>edgeRect[2] && background[0]>=0 && background[1]>=0);
        assert(background[0]+background[2]<=1920*scale && background[1]+background[3]<=1080*scale);
        auto textRect=TooltipText::ContentRect(reinterpret_cast<const int*>(inventoryPanel.data()+0x60));
        assert(textRect[2]==edgeRect[2] && textRect[3]==edgeRect[3]);
        HookDraw(itemText,reinterpret_cast<const int*>(inventoryPanel.data()+0x60),inventoryPanel.data()+0x80,1.0f);
        assert(drawLines.size()==1);
        assert(drawLines[0].text==(format==customFormat ? "Min: 15, Max: 30" : "[15-30]"));
        assert(lastLayoutLimits[0]==edgeRect[2] && lastLayoutLimits[1]==edgeRect[3]);
        auto& line=drawLines.front();
        assert(line.right+measured->gap+measured->Width(line.text)+measured->padding<=background[0]+background[2]);
        assert(line.middle-measured->size*0.5f-measured->padding>=background[1]);
        assert(line.middle+measured->size*0.5f+measured->padding<=background[1]+background[3]);
        // Fitting twice in successive frames must not grow or move cumulatively.
        EndTooltip(); assert(memcmp(inventoryPanel.data()+0x60,edgeRect.data(),16)==0);
        assert(memcmp(inventoryPanel.data()+0x70,secondary.data(),16)==0);
        BeginTooltip(nullptr,inventoryPanel.data()); assert(memcmp(inventoryPanel.data()+0x60,background.data(),16)==0); EndTooltip();
    }
    memcpy(inventoryPanel.data()+0x60,rect,sizeof(rect));
    rangeMetrics.store(metrics); TooltipText::SetViewport(1920,1080,1);
    // Green conditional set-bonus lines retain their numeric bounds too.
    BeginTooltip(nullptr,inventoryPanel.data()); tooltip.totals.clear(); tooltip.conditional={{{{0,0},{20,30}}}};
    auto setMetrics=std::make_shared<RangeMetrics>(*metrics); setMetrics->format=customFormat; tooltip.metrics=setMetrics;
    chars[0].rgba[1]=255; HookDraw(itemText,reinterpret_cast<const int*>(inventoryPanel.data()+0x60),inventoryPanel.data()+0x80,1);
    // nativeLayout points to the same glyph buffer; matching cannot lose bounds in the set candidate cache.
    assert(drawLines.size()==1 && drawLines[0].text=="Min: 20, Max: 30"); EndTooltip(); chars[0].rgba[1]=0;
    itemData.dwFileIndex=9999; RememberBuiltItem(&itemUnit,inventoryPanel.data());
    BeginTooltip(nullptr,inventoryPanel.data()); assert(tooltip.db);
    assert(memcmp(inventoryPanel.data()+0x60,rect,sizeof(rect))==0); EndTooltip();
    itemData.dwFileIndex=0; RememberBuiltItem(&itemUnit,inventoryPanel.data());
    active.store(false); BeginTooltip(nullptr,inventoryPanel.data());
    assert(memcmp(inventoryPanel.data()+0x60,rect,sizeof(rect))==0); EndTooltip(); active.store(true);
    // Identical rendered values can come from different affix bounds. Retain
    // separate identities and reject ambiguity rather than mixing their rolls.
    D2ItemDataStrc otherData=itemData; otherData.dwFileIndex=1;
    D2UnitStrc otherUnit=itemUnit; otherUnit.dwUnitId=789; otherUnit.pItemData=&otherData;
    HookBuild(inventoryPanel.data(),&viewer,&owner,&otherUnit,37,true);
    BeginTooltip(nullptr,inventoryPanel.data()); assert(!tooltip.db); EndTooltip();
    BeginTooltip(&itemUnit,inventoryPanel.data()); assert(tooltip.db && tooltip.unitId==456); EndTooltip();
    otherUnit.dwUnitType=UNIT_MONSTER; // No longer a valid cached item.
    BeginTooltip(nullptr,inventoryPanel.data()); assert(tooltip.db && tooltip.unitId==456); EndTooltip();
    itemData.pSeed.dwSeed[0]=99; // Reused pointer/GUID cannot retain stale identity.
    BeginTooltip(nullptr,inventoryPanel.data()); assert(!tooltip.db); EndTooltip();
    HookBuild(inventoryPanel.data(),&viewer,&owner,&itemUnit,37,true);
    BeginTooltip(nullptr,inventoryPanel.data()); assert(tooltip.db); EndTooltip();
    itemData.dwItemFlags=0; BeginTooltip(nullptr,inventoryPanel.data()); assert(!tooltip.db); EndTooltip(); itemData.dwItemFlags=0x10;
    for(uint32_t quality:{8u,9u}) { itemData.dwQualityNo=quality; BeginTooltip(nullptr,inventoryPanel.data()); assert(!tooltip.db); EndTooltip(); }
    itemData.dwQualityNo=7;
    builtItems.clear(); builtOrder.clear();
    BeginTooltip(nullptr,inventoryPanel.data()); assert(!tooltip.db); EndTooltip();
    BeginTooltip(&monster,inventoryPanel.data()); assert(!tooltip.db); EndTooltip();
    BeginTooltip(&itemUnit,inventoryPanel.data()); assert(tooltip.db && tooltip.unitId==456); EndTooltip();

    // ImGui lifecycle and independently sized text at 1080p/4K.
    ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
    for (int i=0;i<4;++i) io.Fonts->AddFontDefault();
    unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    for (float scale : {1.0f,2.0f}) {
        io.DisplaySize = ImVec2(1920*scale,1080*scale); io.DeltaTime = 1.0f/60; io.FontGlobalScale = scale;
        ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(650*scale,570*scale)); ImGui::Begin("Stat range test");
        DrawSettingsPanel(scale); ImGui::End();
        PublishMetrics(scale); auto measured=rangeMetrics.load();
        for(const char* text:{"[20-30]","[-20000--10000]","[1-234 / 567-890]","Min: 20, Max: 30","M\xc3\xadn: 20, M\xc3\xa1x: 30"}) {
            const auto actual=io.Fonts->Fonts[2]->CalcTextSizeA(measured->size,FLT_MAX,0,text);
            assert(measured->Width(text)>=actual.x && measured->Width(text)<actual.x+1.01f);
        }
        strcpy_s(config.format,customFormat.c_str()); PublishMetrics(scale);
        assert(rangeMetrics.load()->format==customFormat);
        strcpy_s(config.format,"%n"); PublishMetrics(scale); assert(rangeMetrics.load()->format==DefaultRangeFormat);
        strcpy_s(config.format,DefaultRangeFormat);
        drawLines = {{500,300,"[20-30]"}}; capturedAt = GetTickCount64(); active.store(true); Render(scale);
        ImGui::Render(); assert(ImGui::GetDrawData()->TotalVtxCount>0);
    }
    ImGui::DestroyContext();
    // A report distinguishes activation/hooks from data and tooltip matching.
    wchar_t previousLocal[32768]{};const auto oldLocal=GetEnvironmentVariableW(L"LOCALAPPDATA",previousLocal,32768);
    const auto reportRoot=std::filesystem::temp_directory_path()/("d2rhud-stat-range-report-"+std::to_string(GetCurrentProcessId()));
    SetEnvironmentVariableW(L"LOCALAPPDATA",reportRoot.c_str());
    database.store(std::make_shared<const Database>(db));diagnosticSample="+25 to Strength => [20-30]\n";
    SaveDiagnosticReport();
    std::ifstream savedReport(reportRoot/"D2RHUD/stat-ranges-diagnostics.txt");
    const std::string report((std::istreambuf_iterator<char>(savedReport)),{});
    assert(report.find("Hooks installed:")!=std::string::npos && report.find("Activity: built=")!=std::string::npos);
    assert(report.find("+25 to Strength => [20-30]")!=std::string::npos);
    SetEnvironmentVariableW(L"LOCALAPPDATA",oldLocal?previousLocal:nullptr);
    if (argc > 1) {
        CheckNativeExecutable(std::filesystem::path(argv[1])/"D2R.exe");
        GameAssets assets(argv[1], argc > 2 ? argv[2] : ""); auto real = LoadDatabase(assets);
        assert(real.error.empty() && !real.affixes.empty() && !real.uniques.empty() && !real.sets.empty());
        std::cout << "Loaded " << real.uniques.size() << " unique, " << real.sets.size() << " set, " << real.affixes.size() << " affix records.\n";
        if (argc > 2 && std::string(argv[2]) == "RMD-MP") {
            // BIN tables retain localized numeric string IDs rather than keys.
            assert(real.strings.at(real.classSkills.at(1)) == real.strings.at("ModStr3d"));
            assert(real.strings.at(real.tabSkills.at(8)) == real.strings.at("StrSklTabItem15"));
            Item rakescar; rakescar.quality=7; rakescar.index=10;
            auto values=real.ForItem(rakescar);
            assert(MatchLine(real, values, "+100% Enhanced Damage", true).text == "[75-150]");
            assert(MatchLine(real, values, "+3% to Experience Gained", true).text == "[2-5]");
            assert(MatchLine(real, values, "+38 poison damage over 3 seconds", true).text == "[38-38]");
            // Lines observed in the running game's cached inventory tooltip.
            Item endlesshail; endlesshail.quality=7; endlesshail.index=157;
            auto bow=real.ForItem(endlesshail);
            assert(MatchLine(real,bow,"+202% Enhanced Damage",true).text=="[180-220]");
            assert(MatchLine(real,bow,"Cold Resist +35%",true).text=="[35-35]");
            assert(MatchLine(real,bow,"-13% to Enemy Cold Resistance",true).text=="[10-20]");
            auto gem=real.gems.find("gcv"); assert(gem!=real.gems.end());
            values={}; real.Apply(values,gem->second[1]); assert((values.at({0,0})==Bounds{3,3}));
        }
    }
    // Verify the build guard and every patched limit in a dedicated allocation.
    // No native function in this synthetic image is executed.
    auto fake = static_cast<uint8_t*>(VirtualAlloc(nullptr,0x600000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(fake); testExecutable=reinterpret_cast<uintptr_t>(fake);
    const uint32_t loads[]={0x1c7709,0x1c7a18,0x1c7a29,0x1c7a59,0x1c7baa,0x1c7bbb,0x1c7bd0,0x1c7be5};
    const uint32_t compares[]={0x1c771c,0x1c7a6c};
    const uint32_t oldCapacity=1024;
    for(auto rva:loads){fake[rva]=(rva==0x1c7709||rva==0x1c7a59)?0xbb:0xba;memcpy(fake+rva+1,&oldCapacity,4);}
    for(auto rva:compares){fake[rva]=0x48;fake[rva+1]=0x3d;memcpy(fake+rva+2,&oldCapacity,4);}
    const uint8_t layoutCode[]={0x40,0x53,0x55,0x56,0x57,0xb8,0x98,0x26,0,0};memcpy(fake+0x5d97d0,layoutCode,sizeof(layoutCode));
    assert(!TooltipText::InstallLimits()); // Missing destructor signature: no partial patch.
    uint32_t value=0;memcpy(&value,fake+loads[0]+1,4);assert(value==1024);
    const uint8_t destructorCode[]={0x40,0x53,0x48,0x83,0xec,0x20};memcpy(fake+0x5d9940,destructorCode,sizeof(destructorCode));
    assert(TooltipText::InstallLimits());
    for(auto rva:loads){memcpy(&value,fake+rva+1,4);assert(value==TooltipText::NativeCapacity);}
    for(auto rva:compares){memcpy(&value,fake+rva+2,4);assert(value==TooltipText::NativeCapacity);}
    testExecutable=0;VirtualFree(fake,0,MEM_RELEASE);
    std::cout << "Stat range calculation, layout, configuration, hotkey and scaling checks passed.\n";
}
