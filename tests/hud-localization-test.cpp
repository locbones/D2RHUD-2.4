#define NOMINMAX
#include "../d2rhud/plugin/HudText.h"
#include "../d2rhud/plugin/D2RHUD/OptionsCatalog.h"
#include "../d2rhud/plugin/MapEventFonts.h"
#include <imgui_internal.h>
#include <windows.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <set>

std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);assert(file);return {std::istreambuf_iterator<char>(file),{}};}
void Write(const std::filesystem::path& path,const std::string& bytes){std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);assert(file.write(bytes.data(),bytes.size()));}
int main(int argc,char** argv){
    assert(argc==3);const std::filesystem::path repository=argv[1],output=argv[2];
    std::filesystem::create_directories(output);
    using namespace HudLocale;
    assert(Supported("jaJP") && Supported("zhCN") && !Supported("../../frFR"));
    assert(Fold("ÉCRAN ŁUPY ПЛАВАЮЩИЙ УРОН")==Fold("écran łupy плавающий урон"));
    assert(CommandLanguage(L"D2R.exe -locale frFR -mod RMD-MP")=="frFR");
    assert(CommandLanguage(L"D2R.exe -locale=\"JAJP\"")=="jaJP");
    assert(CommandLanguage(L"D2R.exe -locale \"zhTW\"")=="zhTW");
    assert(CommandLanguage(L"D2R.exe -locale ../../bad").empty());
    auto french=std::make_shared<Catalog>();french->Merge(R"({"strings":{"Quest Journal":"Journal des quêtes","Floating Damage":"Dégâts flottants","Cancel":"Annuler","Count: %d":"Nombre : %d","Unsafe":"%n","Empty":"","Type: %d":"Type: %s","Bad ID":"Wrong###ID","Display":"Affichage"}})");
    assert(french->rejected==4);Publish(french);
    assert(Translate("Quest Journal")=="Journal des quêtes" && Translate("Untranslated source")=="Untranslated source");
    assert(Translate("Unsafe")=="Unsafe" && Translate("Type: %d")=="Type: %d");
    const auto retainedLabel=Tr("Cancel");
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1000,800};io.DeltaTime=1.f/60;io.Fonts->AddFontDefault();io.Fonts->Build();
    ImGui::NewFrame();ImGui::Begin("Test");
    for(const char* source:{"Quest Journal","Cancel##button","Cancel###StableCancel","##hidden","Count: %d"}){
        const auto localized=Label(source);Publish(std::make_shared<Catalog>());const auto english=Label(source);Publish(french);
        assert(ImGui::GetID(english.c_str())==ImGui::GetID(localized.c_str()));
        if(strstr(source,"###")||!strncmp(source,"##",2))assert(ImGui::GetID(source)==ImGui::GetID(localized.c_str()));
    }
    bool value=false;HudText::Checkbox("Floating Damage",&value);assert(ImGui::GetItemID()==ImGui::GetID(Label("Floating Damage").c_str()) && !value);
    char input[32]="raw user value";HudText::InputText("Quest Journal",input,sizeof(input));assert(!strcmp(input,"raw user value"));
    int selected=1;HudText::Combo("Choice",&selected,"Quest Journal\0Cancel\0");assert(selected==1);
    HudText::Text("%s", "Quest Journal");HudText::Text("Count: %d",42);
    HudText::OpenPopup("Quest Journal");assert(HudText::IsPopupOpen("Quest Journal"));
    if(HudText::BeginPopupModal("Quest Journal")){assert(ImGui::GetCurrentWindow()->ID==ImHashStr(Label("Quest Journal").c_str()));ImGui::CloseCurrentPopup();ImGui::EndPopup();}
    HudOptions::Filter filter;strcpy_s(filter.query,"flottants");assert(filter.Visible(HudOptions::Category::Display,"Floating Damage"));
    ImGui::End();ImGui::Render();ImGui::DestroyContext();
    auto newer=std::make_shared<Catalog>();newer->Merge(R"({"strings":{"Cancel":"Nouveau"}})");Publish(newer);
    assert(!strcmp(retainedLabel,"Annuler") && Translate("Cancel")=="Nouveau");
    // Every shipped pack is parseable and safe to use as printf text.
    for(const auto& locale:Languages){
        Catalog pack;pack.Merge(Read(repository/"translations"/(std::string(locale.code)+".json")));assert(!pack.rejected && pack.strings.size()>=3200);
        for(const char* key:{"Quest Journal","Floating Damage","Language and translations","Store a skull","Rogue Scouting Report:"})assert(pack.strings.contains(key));
    }
    // Exercise actual embedded-resource loading, file overrides, fallback and persistence
    // with a private LOCALAPPDATA. No real player's language preference is touched.
    const auto local=std::filesystem::absolute(output/L"AppData");assert(_wputenv_s(L"LOCALAPPDATA",local.c_str())==0);
    Write(local/L"D2RHUD/language.json",R"({"language":"frFR"})");
    Initialize();assert(LanguageCode()=="frFR" && Preference()=="frFR");
    assert(Translate("Quest Journal")!="Quest Journal" && UnicodeFont());
    size_t bytes=0;assert(FontBytes(bytes) && bytes>10000000);
    const auto edited=Directory()/L"frFR.json";
    Write(edited,R"({"_meta":{"locale":"frFR"},"strings":{"Quest Journal":"Journal corrigé","Floating Damage":"%n"}})");
    assert(!Reload() && Translate("Quest Journal")=="Journal corrigé" && Translate("Floating Damage")!="%n");
    assert(PrepareEditor() && Read(edited).find("Journal corrigé")!=std::string::npos);
    assert(std::filesystem::exists(Directory()/L"editor.html") && std::filesystem::exists(Directory()/L"jaJP.json"));
    Write(edited,"{invalid JSON");assert(!Reload() && Translate("Quest Journal")!="Quest Journal");
    assert(SetPreference("jaJP") && Preference()=="jaJP" && LanguageCode()=="frFR"); // Font/language switch waits for restart.
    assert(!SetPreference("../untrusted") && Read(local/L"D2RHUD/language.json").find("jaJP")!=std::string::npos);
    // The bundled fallback rasterizes accented Latin, Cyrillic, Kana, Han and Hangul.
    ImGui::CreateContext();auto& atlas=*ImGui::GetIO().Fonts;ImFontGlyphRangesBuilder ranges;ImVector<ImWchar> glyphs;
    ranges.AddText("Français Polski Русский 日本語 한국어 简体中文 繁體中文");ranges.BuildRanges(&glyphs);
    const auto font=atlas.AddFontFromFileTTF((repository/"d2rhud/fonts/NotoSansCJKsc-Regular.otf").string().c_str(),24,nullptr,glyphs.Data);assert(font && atlas.Build());
    for(unsigned point:{0xe7u,0x420u,0x65e5u,0x8a9eu,0xd55cu,0x7b80u,0x9ad4u})assert(font->FindGlyphNoFallback(ImWchar(point)));
    ImGui::DestroyContext();
    for(const char* code:{"jaJP","koKR","zhCN","zhTW"}){
        Catalog pack;pack.Merge(Read(repository/"translations"/(std::string(code)+".json")));
        ImGui::CreateContext();auto& full=*ImGui::GetIO().Fonts;full.TexDesiredWidth=4096;
        ImFontGlyphRangesBuilder all;ImVector<ImWchar> allRanges;all.AddRanges(full.GetGlyphRangesDefault());
        for(const auto& [key,value]:pack.strings){all.AddText(key.c_str());all.AddText(value.c_str());}all.BuildRanges(&allRanges);
        size_t size=0;const auto data=FontBytes(size);ImFontConfig config;config.FontDataOwnedByAtlas=false;config.OversampleH=config.OversampleV=1;
        for(float raster:{12.f,15.f,18.f,24.f,36.f,48.f,36.f,32.f})assert(full.AddFontFromMemoryTTF(const_cast<unsigned char*>(data),int(size),raster,&config,allRanges.Data));
        CubeMap::Text::FontBank events;events.Load(full,"",data,size,allRanges.Data,true);
        for(int i=0;i<12;++i)assert(full.AddFontFromFileTTF((repository/"d2rhud/fonts"/(std::to_string(i)+".otf")).string().c_str(),i==0?24.f:32.f));
        assert(full.Build() && full.TexWidth<=16384 && full.TexHeight<=16384);
        std::cout<<code<<" complete production font atlas: "<<full.TexWidth<<'x'<<full.TexHeight<<'\n';
        ImGui::DestroyContext();
    }
    std::cout<<"PASS: all language packs, safe format fallback, stable widget IDs/values, localized search, embedded defaults, personal overrides, reload, restart preference and multilingual font rasterization.\n";
}
