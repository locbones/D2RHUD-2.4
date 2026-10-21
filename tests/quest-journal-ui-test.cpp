#define main ExistingEditorTestMain
#include "lootfilter-editor-test.cpp"
#undef main
#include "../d2rhud/plugin/QuestJournalData.h"
#include "../d2rhud/plugin/StackSplitSkin.h"
static bool showQuestJournalMenu=true;
static std::shared_ptr<const QuestJournal::Rules> journalRules;
static QuestJournal::View journalView;
static LegacyTransmog::Skin::Image panelImage;
static std::array<LegacyTransmog::Skin::Image,LegacyTransmog::Skin::Count> journalImages;
static float GetMenuScaleFactor(){return ImGui::GetIO().DisplaySize.x>=3840?2.f:1.f;}
static void EnableAllInput(){}
static void PushFontSafe(int){}
static void PopFontSafe(int){}
namespace LegacyTransmog::Skin {Image PanelArtwork(){return panelImage;}Image ArtworkImage(Part part){return journalImages[part];}}
namespace QuestJournal {
std::shared_ptr<const Rules> Bindings(){return journalRules;}
View CurrentView(){return journalView;}
std::string Status(){return "Per-character quests configured. Companion folder: LOCALAPPDATA/D2RHUD/Quests";}
void RequestLegacyImport(const View&){}
void RequestFreshStart(const View&){}
void RequestAcknowledge(const View&){}
void RequestReward(const View&){}
void RequestProofRecovery(const View&){}
}
#include "../d2rhud/plugin/QuestJournalUI.inl"
int main(int argc,char** argv){try{
    Check(argc>=6,"Usage: quest-journal-ui-test output cubemain quests panel font");
    if(argc>6){auto state=luaL_newstate();Check(state!=nullptr,"Lua initialization failed");const auto status=luaL_loadfile(state,argv[6]);const auto message=status==LUA_OK?std::string{}:std::string(lua_tostring(state,-1));lua_close(state);Check(status==LUA_OK,"Quest filter syntax failed: "+message);}
    auto read=[](const char* path){std::ifstream f(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});};
    journalRules=QuestJournal::Data::ParseRules(read(argv[2]),read(argv[3]));Check(journalRules->error.empty()&&journalRules->events,"event definitions invalid");
    std::ifstream fixture(argv[4],std::ios::binary);uint32_t dimensions[2]{};fixture.read(reinterpret_cast<char*>(dimensions),8);Check(fixture&&dimensions[0]&&dimensions[1],"panel fixture invalid");
    panelImage={2,int(dimensions[0]),int(dimensions[1]),int(dimensions[0]),1,int(dimensions[0])};
    if(argc>7){
        const std::filesystem::path data=argv[7];
        struct Asset {LegacyTransmog::Skin::Part part;const char* path;uint64_t texture;};
        for(const auto& asset:std::array<Asset,4>{{{LegacyTransmog::Skin::JournalBackground,"ReMoDDeD/Panels/Quest/QuestBG_R.sprite",3},{LegacyTransmog::Skin::JournalTab,"ReMoDDeD/Panels/Quest/QuestTab_R.sprite",4},{LegacyTransmog::Skin::JournalButton,"ReMoDDeD/Panels/Buttons/ButtonMed_R.sprite",5},{LegacyTransmog::Skin::Close,"ReMoDDeD/Panels/Buttons/Exit.sprite",6}}}){
            std::ifstream input(data/asset.path,std::ios::binary|std::ios::ate);Check(bool(input),"Native quest artwork missing");std::vector<uint8_t> bytes(size_t(input.tellg()));input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
            LegacyTransmog::Skin::Sprite image;Check(LegacyTransmog::Skin::Decode(bytes,image),"Native quest artwork decode failed");const auto& a=image.atlas;
            journalImages[asset.part]={asset.texture,a.width,a.height,a.frameWidth,a.frames,image.pitch};
        }
    }
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DeltaTime=1.f/60;Check(io.Fonts->AddFontFromFileTTF(argv[5],18)!=nullptr,"native font missing");
    unsigned char* pixels=nullptr;int w=0,h=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);io.Fonts->SetTexID(1);
    for(const auto size:{ImVec2(960,720),ImVec2(1920,1080),ImVec2(3840,2160)}){
        io.DisplaySize=size;io.FontGlobalScale=GetMenuScaleFactor();
        for(int quest=0;quest<45;++quest){
            journalView={};journalView.connected=true;journalView.character="The Wanderer";journalView.completed=quest;journalView.session=1;journalView.recipient=1;journalView.sequence=1;journalView.kills=625;journalView.message="Quest journal loaded for this character.";
            for(int n=0;n<2;++n){ImGui::NewFrame();ShowQuestJournalMenu();ImGui::Render();}
            Check(ImGui::GetDrawData()->TotalVtxCount>0,"quest journal did not draw");
            if(argc>7){bool nativePanel=false,nativeTabs=false;for(auto* list:ImGui::GetDrawData()->CmdLists)for(const auto& command:list->CmdBuffer){nativePanel|=command.GetTexID()==3;nativeTabs|=command.GetTexID()==4;}Check(nativePanel&&nativeTabs,"native quest panel/tab textures were not drawn");}
            const auto window=ImGui::FindWindowByName("Mod Quest Journal");Check(window&&window->Pos.x>=0&&window->Pos.y>=0&&window->Pos.x+window->Size.x<=size.x&&window->Pos.y+window->Size.y<=size.y,"journal outside viewport");
            if(quest==4||quest==14||quest==33)Capture(argv[1],"quest-"+std::to_string(quest+1)+"-"+std::to_string(int(size.x)),pixels,w,h);
        }
    }
    const auto previous=journalRules;journalRules.reset();showQuestJournalMenu=true;ImGui::NewFrame();ShowQuestJournalMenu();ImGui::Render();Check(!showQuestJournalMenu,"other mod kept journal open");journalRules=previous;
    ImGui::DestroyContext();std::cout<<"PASS: all 45 quest views render at 960, 1920 and 3840 widths using native font and panel artwork\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
