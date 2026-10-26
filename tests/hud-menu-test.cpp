#define main ExistingJournalFixtureMain
#include "quest-journal-ui-test.cpp"
#undef main
#include "hud-options-fixture.inl"
#include "../d2rhud/plugin/CandyCornSaveUI.h"
static std::vector<CandyCornSave::Notification> conversionNotices;
bool CandyCornSave::TakeNotification(Notification& notice){if(conversionNotices.empty())return false;notice=std::move(conversionNotices.back());conversionNotices.pop_back();return true;}

int main(int argc,char** argv){try{
    Check(argc==4||argc==5,"Usage: hud-menu-test output native-art-data font [language-json]");
    Check(HudMenu::ResolveTheme("RMD-MP","",0)=="Sanctuary","new RMD default");
    Check(HudMenu::ResolveTheme("rmd-mp","Default",0)=="Sanctuary","legacy RMD default migration");
    Check(HudMenu::ResolveTheme("RMD-MP","Default",1)=="Default","explicit Default preserved");
    Check(HudMenu::ResolveTheme("RMD-MP","My custom theme",0)=="My custom theme","custom theme preserved");
    Check(HudMenu::ResolveTheme("Other","",0)=="Default","other mods keep Default");
    Check(HudMenu::ValidPage(-1)==HudMenu::Page::Home&&HudMenu::ValidPage(99)==HudMenu::Page::Home,"invalid page fallback");
    auto route=HudMenu::Page::Options;bool routedOpen=false;
    HudMenu::RouteJournal(route,routedOpen,true,false);Check(routedOpen&&route==HudMenu::Page::Journal,"journal request routing");
    routedOpen=false;HudMenu::RouteJournal(route,routedOpen,true,true);Check(!routedOpen,"closing workspace reopened the journal");
    routedOpen=true;HudMenu::RouteJournal(route,routedOpen,false,true);Check(!routedOpen,"journal hotkey did not close workspace");
    route=HudMenu::Page::Options;routedOpen=true;HudMenu::RouteJournal(route,routedOpen,false,true);Check(routedOpen,"leaving journal closed another page");
    HudOptions::Filter filter;
    Check(filter.Visible(HudOptions::Category::Display,"Floating Damage"),"display category missing");
    Check(!filter.Visible(HudOptions::Category::Gameplay,"Persistent Summons"),"category filtering failed");
    strcpy_s(filter.query,"SuMmOnS");Check(filter.Visible(HudOptions::Category::Gameplay,"Persistent Summons"),"case insensitive cross-category search failed");
    strcpy_s(filter.query,"DPS");Check(filter.Visible(HudOptions::Category::Display,"Floating Damage","DPS meter"),"keyword search failed");

    const std::filesystem::path data=argv[2];
    struct Asset {LegacyTransmog::Skin::Part part;const char* path;uint64_t texture;};
    for(const auto& asset:std::array<Asset,4>{{{LegacyTransmog::Skin::JournalBackground,"ReMoDDeD/Panels/Quest/QuestBG_R.sprite",3},{LegacyTransmog::Skin::JournalTab,"ReMoDDeD/Panels/Quest/QuestTab_R.sprite",4},{LegacyTransmog::Skin::JournalButton,"ReMoDDeD/Panels/Buttons/ButtonMed_R.sprite",5},{LegacyTransmog::Skin::Close,"ReMoDDeD/Panels/Buttons/Exit.sprite",6}}}){
        std::ifstream input(data/asset.path,std::ios::binary|std::ios::ate);Check(bool(input),"Native artwork missing");
        std::vector<uint8_t> bytes(size_t(input.tellg()));input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        LegacyTransmog::Skin::Sprite sprite;Check(LegacyTransmog::Skin::Decode(bytes,sprite),"Native artwork decode failed");
        const auto& a=sprite.atlas;journalImages[asset.part]={asset.texture,a.width,a.height,a.frameWidth,a.frames,sprite.pitch};
    }
    ImVector<ImWchar> languageGlyphs;
    if(argc==5){
        std::ifstream input(argv[4],std::ios::binary);auto pack=std::make_shared<HudLocale::Catalog>();pack->Merge({std::istreambuf_iterator<char>(input),{}});HudLocale::Publish(pack);
        ImFontGlyphRangesBuilder ranges;for(const auto& [key,value]:pack->strings){ranges.AddText(key.c_str());ranges.AddText(value.c_str());}ranges.BuildRanges(&languageGlyphs);
    }
    for(const auto display:{ImVec2{1280,720},ImVec2{1920,1080},ImVec2{3840,2160}}){
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=display;io.DeltaTime=1.f/60;
        io.BackendFlags|=ImGuiBackendFlags_HasMouseCursors;
        for(float size:{12.f,15.f,18.f,24.f})Check(io.Fonts->AddFontFromFileTTF(argv[3],size,nullptr,languageGlyphs.Data)!=nullptr,"font unavailable");
        const auto bodyPath=argc==5?std::filesystem::path(argv[3]):std::filesystem::path(__FILE__).parent_path()/"../d2rhud/fonts/10.otf";
        HudSettings::bodyFont=io.Fonts->AddFontFromFileTTF(bodyPath.string().c_str(),36,nullptr,languageGlyphs.Data);Check(HudSettings::bodyFont!=nullptr,"body font unavailable");HudSettings::bodyFont->Scale=.5f;
        HudSettings::noteFont=io.Fonts->AddFontFromFileTTF(bodyPath.string().c_str(),32,nullptr,languageGlyphs.Data);Check(HudSettings::noteFont!=nullptr,"note font unavailable");HudSettings::noteFont->Scale=.5f;
        HudSettings::headingFont=io.Fonts->Fonts[3];
        unsigned char* pixels=nullptr;int tw=0,th=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&tw,&th);io.Fonts->SetTexID(1);
        const float scale=display.x>=3840?2.f:1.f;io.FontGlobalScale=scale;
        HudMenu::nativeTheme=true;HudMenu::ApplySanctuaryPalette();
        auto page=HudMenu::Page::Home;bool open=true;
        auto frame=[&]{
            optionRects.clear();ImGui::NewFrame();ImGui::PushFont(io.Fonts->Fonts[1]);
            HudMenu::Workspace(page,open,scale,"RMD-MP  /  D2RHUD",[&](HudMenu::Page active){
                if(active==HudMenu::Page::Home)return;
                if(active==HudMenu::Page::Options)ShowD2RHUDMenu();
                else {
                    bool visible=true;HudMenu::Begin(HudMenu::Info(active).title,&visible,ImGuiWindowFlags_NoTitleBar);
                    if(active==HudMenu::Page::Loot)RenderLootFilterEditor();
                    else if(active==HudMenu::Page::Appearance){
                        HudSettings::InlineField("Theme preset",180,scale);
                        const auto labelMin=ImGui::GetItemRectMin(),labelMax=ImGui::GetItemRectMax();
                        int preset=0;ImGui::Combo("##Theme",&preset,"Sanctuary\0Default\0");
                        const auto fieldMin=ImGui::GetItemRectMin();
                        Check(fieldMin.x-labelMax.x>=12*scale-.1f,"theme label overlaps its control");
                        Check(fabsf(labelMin.y-fieldMin.y-ImGui::GetStyle().FramePadding.y)<.1f,"theme label/control baselines differ");
                    }
                    else ImGui::TextWrapped("%s",HudMenu::Info(active).description);
                    HudMenu::End();
                }
            });ImGui::PopFont();ImGui::Render();
            int roots=0;for(auto window:ImGui::GetCurrentContext()->Windows)if(window->Active&&!(window->Flags&(ImGuiWindowFlags_ChildWindow|ImGuiWindowFlags_Tooltip)))++roots;
            Check(roots==1,"page spawned another top-level menu");
            Check(!HudMenu::embedded,"embedded editor state leaked");
            bool panel=false;for(auto* list:ImGui::GetDrawData()->CmdLists)for(const auto& cmd:list->CmdBuffer)panel|=cmd.GetTexID()==3;
            Check(panel,"journal panel artwork absent");
        };
        frame();frame();Capture(argv[1],"hud-home-"+std::to_string(int(display.x)),pixels,tw,th);
        for(const auto& entry:HudMenu::Entries){page=entry.page;frame();frame();}
        Capture(argv[1],"hud-appearance-"+std::to_string(int(display.x)),pixels,tw,th);
        page=HudMenu::Page::Options;frame();frame();
        Capture(argv[1],"hud-options-"+std::to_string(int(display.x)),pixels,tw,th);
        Check(optionRects.count("Monster Stats Display")&&optionRects.count("Floating Damage")&&optionRects.count("No Widescreen Bars"),"Display options missing");
        Check(!optionRects.count("Persistent Summons"),"other category leaked into Display");
        auto click=[&](ImVec2 point){io.AddMousePosEvent(point.x,point.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();};
        ImGuiWindow* navigation=nullptr;
        for(auto window:ImGui::GetCurrentContext()->Windows)if(window->Active&&strrchr(window->Name,'/')&&std::string_view(strrchr(window->Name,'/')).starts_with("/WorkspaceNavigation_"))navigation=window;
        Check(navigation!=nullptr,"workspace navigation missing");
        const float row=(std::max)(30*scale,15*scale+10*scale);
        const float gap=ImGui::GetStyle().ItemSpacing.y;
        click({navigation->Pos.x+40*scale,navigation->DC.CursorStartPos.y+2*(row+gap)+2*(15*scale+2*gap)+row*.5f});
        Check(page==HudMenu::Page::Damage,"sidebar click did not change the active page");
        page=HudMenu::Page::Options;frame();frame();
        const auto damage=optionRects.at("Floating Damage").GetCenter();const int priorSaves=saves;
        const bool prior=d2rHUDConfig.FloatingDamage;click(damage);
        Check(d2rHUDConfig.FloatingDamage!=prior&&FloatingDamage::enabled==d2rHUDConfig.FloatingDamage&&cachedSettings.FloatingDamage==d2rHUDConfig.FloatingDamage&&settings.FloatingDamage==d2rHUDConfig.FloatingDamage,"damage toggle did not apply");
        Check(saves==priorSaves+1,"option change should save exactly once");
        lockMonster=true;frame();const bool monster=d2rHUDConfig.MonsterStatsDisplay;click(optionRects.at("Monster Stats Display").GetCenter());
        Check(d2rHUDConfig.MonsterStatsDisplay==monster,"mod lock was bypassed");lockMonster=false;
        ImGuiWindow* options=nullptr;for(auto window:ImGui::GetCurrentContext()->Windows)if(window->Active&&strrchr(window->Name,'/')&&std::string_view(strrchr(window->Name,'/')).starts_with("/D2RHUD Options_"))options=window;
        Check(options!=nullptr,"embedded options window absent");
        ImGui::ActivateItemByID(options->GetID("##OptionSearch"));frame();io.AddInputCharactersUTF8("widescreen");frame();frame();
        Check(optionRects.size()==1&&optionRects.count("No Widescreen Bars"),"search did not filter actual options");
        Capture(argv[1],"hud-search-"+std::to_string(int(display.x)),pixels,tw,th);
        io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);frame();
        io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();
        io.AddKeyEvent(ImGuiKey_Backspace,true);frame();io.AddKeyEvent(ImGuiKey_Backspace,false);frame();
        Check(optionRects.size()==3,"clearing search did not restore the category");
        conversionNotices.push_back({"Candy Corn updated","Candy Tester: converted 27 Candy Corn to stacks of 1. Your original save was backed up.",false});
        ImGui::NewFrame();CandyCornSave::DrawNotification();ImGui::Render();
        ImGui::NewFrame();CandyCornSave::DrawNotification();ImGui::Render();
        auto notice=ImGui::FindWindowByName("Candy Corn update###CandyCornConversion");
        Check(notice&&notice->Active&&notice->Pos.x>=0&&notice->Pos.y>=0&&notice->Pos.x+notice->Size.x<=display.x,"Candy Corn notice is clipped");
        Check((notice->Flags&ImGuiWindowFlags_NoInputs)==ImGuiWindowFlags_NoInputs,"Candy Corn notice captures gameplay input");
        Check((notice->Flags&ImGuiWindowFlags_NoSavedSettings)!=0,"Candy Corn notice changes saved layout");
        Capture(argv[1],"candy-corn-notice-"+std::to_string(int(display.x)),pixels,tw,th);
        ImGui::DestroyContext();HudSettings::bodyFont=HudSettings::noteFont=HudSettings::headingFont=nullptr;
    }
    std::cout<<"PASS: unified workspace at 720p/1080p/4K, native artwork, all page shells, actual option search/toggles/locks/autosave, and theme default migration\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
