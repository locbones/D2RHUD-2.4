#define wmain ExistingMarketplaceTests
#include "marketplace-test.cpp"
#undef wmain
#include "../d2rhud/plugin/D2RHUD/OptionsCatalog.h"
#include "../d2rhud/plugin/ItemFilter/LootFilterRuleBuilder.h"
#include "../d2rhud/plugin/GameAssets.h"
#include <imgui.h>
static std::wstring savedFolder;
static std::string modName="MarketplaceFixture";
static LootFilterConfig::Builder::Catalog fixtureCatalog;
static std::wstring GetSavePath(){return savedFolder;}
static std::wstring GetExecutableDir(){return savedFolder;}
static const auto& GetLootFilterBuilderCatalog(){return fixtureCatalog;}
GameAssets::GameAssets(const std::filesystem::path&,const std::string&){}
GameAssets::~GameAssets(){}
std::vector<uint8_t> GameAssets::Read(const std::string&){return {};}
std::vector<std::string> GameAssets::ReadTableNames(const std::string&,bool){return {};}
GameAssets::TableFiles GameAssets::ReadTable(const std::string&,bool){return {};}
#include "../d2rhud/plugin/MarketplaceUI.inl"

#undef assert
#define assert(value) do{if(!(value))throw std::runtime_error("Marketplace UI check at line "+std::to_string(__LINE__)+": " #value);}while(false)
int wmain(int argc,wchar_t** argv)try{
    assert(argc==2||argc==3);savedFolder=std::filesystem::path(argv[1]).wstring();
    for(const auto& ref:Marketplace::CodexReferences){assert(Marketplace::CodexName("RMD-MP",ref.code,ref.quality,"Original")==ref.name);assert(Marketplace::CodexName("OtherMod",ref.code,ref.quality,"Original")=="Original");}
    assert(Marketplace::ValidListingComment("A short public note"));assert(!Marketplace::ValidListingComment(std::string(241,'x')));assert(!Marketplace::ValidListingComment("bad\ncomment"));
    fixtureCatalog.loaded=true;fixtureCatalog.items={{"Zod Rune","r33"},{"Ring","rin"}};
    fixtureCatalog.bases["r33"].name="Zod Rune";fixtureCatalog.bases["rin"].name="Ring";
    fixtureCatalog.namedItems={{"Stone of Jordan","rin","Unique",42},{"Bul-Kathos' Wedding Band","rin","Unique",43}};
    const auto folder=std::filesystem::path(savedFolder)/L"Diablo II Resurrected"/L"Mods"/L"MarketplaceFixture";std::filesystem::create_directories(folder);
    auto& state=Marketplace::UI::EditorState();state.path=folder/L"Marketplace_2.4_SC.json";state.valid=true;state.settings.enabled=true;state.settings.page=17;state.tables=Tables();state.tablesStarted=true;
    {
        Marketplace::UI::State first;first.path=folder/L"first-run.json";
        Marketplace::UI::Load(first,modName);assert(first.original.empty());
        const auto initialized=Marketplace::SettingsJson(Marketplace::Settings{},modName,false).dump();
        Marketplace::SaveSettingsFile(first.path,"",initialized);
        first.settings.enabled=true;first.settings.page=11;Marketplace::UI::Save(first,modName);
        assert(Marketplace::Json::parse(Marketplace::ReadSettingsFile(first.path)).at("page")==11);
        first.original.clear();first.settings.page=20;bool refused=false;
        try{Marketplace::UI::Save(first,modName);}catch(const std::runtime_error&){refused=true;}
        assert(refused&&Marketplace::Json::parse(Marketplace::ReadSettingsFile(first.path)).at("page")==11);
    }
    // Optional render fixture: real artwork mappings with the fixture's two
    // synthetic named IDs. This never writes to the installed game data.
    if(const auto assets=std::getenv("D2_MARKET_UI_ASSETS")){
        const std::filesystem::path data=assets;
        state.visuals=Marketplace::ItemVisuals::Catalog::Load([&](const std::string& key){
            auto raw=ReadFile(data/"global"/"excel"/(key+".bin"),16*1024*1024);std::vector<uint8_t> bytes(raw.begin(),raw.end());
            if(key=="uniqueitems")for(int n=0;n<2;++n){auto* row=bytes.data()+4+(42+n)*0x150;std::memset(row,0,0x150);const std::string title=n?"Bul-Kathos' Wedding Band":"The Stone of Jordan";std::memcpy(row+2,title.data(),title.size());std::memcpy(row+0x28,"rin",3);}return bytes;
        },[&](const std::string& key){auto raw=ReadFile(data/key,16*1024*1024);return std::vector<uint8_t>(raw.begin(),raw.end());});
    }
    {const auto bytes=Page();std::ofstream file(folder/L"Stash_SC_Page17.d2i",std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
    {auto feed=FeedFixture();feed["listings"].push_back({{"seller","Trader B"},{"id","rune-2"},{"revision","revision-1"},{"item_hex",Hex(Rune())},{"price",PriceJson({{"rin","Stone of Jordan",7,42,1,-1}})}});
        std::ofstream file(folder/L"MarketplaceFeed_2.4_SC.json");file<<feed.dump();}
    Marketplace::UI::Save(state,modName);
    Marketplace::Panel::Frame native;native.owner=7;native.page=17;native.tab=3;native.grid={.05f,.2f,.7f,.9f};
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1280,1000};io.DeltaTime=1.f/60;io.Fonts->Build();io.Fonts->SetTexID(1);
    // The market page remains identifiable while viewing another stash page.
    // Reuse the actual delivered-item border; no window or input capture added.
    {
        Marketplace::Settings settings;settings.enabled=true;settings.page=11;
        Marketplace::Panel::Frame picker;picker.page=2;picker.marketChoice={{.1f,.2f,.5f,.24f},{.1f,.1f,.5f,.5f},11};
        auto paint=[&](bool open=false){ImGui::NewFrame();Marketplace::CellBorders::MarketPage(settings,picker,open);ImGui::Render();return ImGui::GetDrawData()->TotalVtxCount;};
        assert(paint()>0);bool green=false;
        for(int n=0;n<ImGui::GetDrawData()->CmdListsCount;++n)for(const auto& v:ImGui::GetDrawData()->CmdLists[n]->VtxBuffer){
            if((v.col&0x00ffffff)==(IM_COL32(85,190,95,0)&0x00ffffff))green=true;
            assert(v.pos.x>=picker.marketChoice.row.left*io.DisplaySize.x&&v.pos.x<=picker.marketChoice.row.right*io.DisplaySize.x);
            assert(v.pos.y>=picker.marketChoice.row.top*io.DisplaySize.y&&v.pos.y<=picker.marketChoice.row.bottom*io.DisplaySize.y);
        }
        assert(green&&!io.WantCaptureMouse&&!io.WantCaptureKeyboard);
        settings.page=12;assert(!paint());settings.page=11;settings.enabled=false;assert(!paint());settings.enabled=true;
        picker.suspended=true;assert(!paint());picker.suspended=false;assert(!paint(true));picker.marketChoice.page=0;assert(!paint());
    }

    std::set<std::string> widgetNames;
    auto layout=Json::parse(ReadFile(std::filesystem::path(__FILE__).parent_path().parent_path()/"tools/marketplace/layouts/marketplace-workspace.json",2*1024*1024));
    auto collect=[&](auto&& self,const Json& node)->void{
        const auto name=node.at("name").get<std::string>();assert(widgetNames.insert(name).second);
        if(name.ends_with("Field")){
            // Native input backgrounds alone were invisible in-game. Every field
            // must keep its explicit opaque border/recess behind the input.
            const auto& children=node.at("children");assert(children.size()==4);
            assert(children[0].at("type")=="RectangleWidget"&&children[1].at("type")=="RectangleWidget");
            assert(children[0].at("fields").at("color")[3]==1);
            assert(children.back().at("type")=="InputTextBoxWidget");
            assert(children.back().at("name")==name.substr(0,name.size()-5));
        }
        if(node.at("type")=="ButtonWidget"){
            const auto message=node.at("fields").at("onClickMessage").get<std::string>();
            const std::string prefix="BankPanelMessage:";assert(message.starts_with(prefix));
            const auto command=message.substr(prefix.size());
            const auto& commands=Marketplace::Panel::NativeCommands();assert(command=="Close"||std::find(commands.begin(),commands.end(),command)!=commands.end());
            assert((command=="Close"&&name=="MarketCloseButton")||name==command||name==command+"Button");
        }
        if(node.at("type")=="TabBarWidget"){
            assert(name=="MarketSections"&&node.at("fields").at("tabCount")==4);
            assert(node.at("fields").at("tabMessages")==Json({"BankPanelMessage:MarketBrowse","BankPanelMessage:MarketItems","BankPanelMessage:MarketAgreements","BankPanelMessage:MarketSetup"}));
            const auto& r=node.at("fields").at("rect");const auto scale=r.value("scale",1.f);const float width=r.at("width").get<float>()*scale;
            assert(width>1200&&width<1500&&std::abs(r.at("x").get<float>()+width/2-1125)<1);
        }
        if(node.contains("children"))for(const auto& child:node["children"])self(self,child);
    };collect(collect,layout);
    for(const auto* removed:{"SellerPage","MarketSimulation","MarketSellerCurrent","MarketInventory","MarketSellerTitle","MarketSimulationHelp","MarketHeaderSummary","MarketDepositValue","MarketWithdrawValue","MarketApprovalValue","MarketSummary"})assert(!widgetNames.contains(removed));
    const auto shop=Json::parse(ReadFile(std::filesystem::path(__FILE__).parent_path().parent_path()/"tools/marketplace/layouts/stash-shop-button.json",1024*1024));
    assert(shop.at("type")=="ButtonWidget"&&shop.at("name")=="MarketplaceShop"&&shop.at("fields").at("onClickMessage")=="BankPanelMessage:MarketOpen");
    auto geometry=[&]{
        const auto presentation=Marketplace::Panel::Presentation();if(!presentation.open)return;
        assert(presentation.section==int(state.section));
        std::map<std::string,bool> visibility;for(const auto& w:presentation.widgets)visibility[w.name]=w.visible;
        struct Box {std::string name,path;float x,y,w,h;};std::vector<Box> controls;
        auto visit=[&](auto&& self,const Json& node,float x,float y,bool visible,std::string path,float parentScale)->void{
            const auto name=node.at("name").get<std::string>();const auto& f=node.at("fields");
            path+="/"+name;
            visible=visible&&(visibility.contains(name)?visibility[name]:f.value("visible",true));
            if(name=="MarketplaceWorkspace")visible=true;if(!visible)return;
            const auto r=f.value("rect",Json::object());
            if(name!="MarketAnchor"){x+=r.value("x",0.f)*parentScale;y+=r.value("y",0.f)*parentScale;}
            const auto scale=parentScale*r.value("scale",1.f);
            const auto type=node.at("type").get<std::string>();
            if(type=="ButtonWidget"||type=="InputTextBoxWidget"||type=="TabBarWidget"||type=="TextBoxWidget"||type=="TextBoxWithLinksWidget"||type=="DropdownListWidget"){
                const auto w=r.value("width",0.f)*scale,h=r.value("height",0.f)*scale;
                // Match InfoBGW_R's visible artwork, not its transparent padding.
                if(!(w>0&&h>0&&x>=10&&y>=60&&x+w<=2238&&y+h<=1558)){
                    std::cerr<<"Control outside panel artwork: "<<name<<" "<<x<<","<<y<<" "<<w<<"x"<<h<<"\n";assert(false);
                }controls.push_back({name,path,x,y,w,h});
            }
            if(node.contains("children"))for(const auto& child:node.at("children"))self(self,child,x,y,visible,path,scale);
        };visit(visit,layout,0,0,true,"",1);
        for(size_t i=0;i<controls.size();++i)for(size_t j=i+1;j<controls.size();++j){const auto& a=controls[i];const auto& b=controls[j];
            if(a.path.starts_with(b.path+"/")||b.path.starts_with(a.path+"/"))continue; // Native caption inside its button.
            if(a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h){std::cerr<<"Overlapping native controls: "<<a.name<<" and "<<b.name<<"\n";assert(false);}
        }
        assert(std::any_of(controls.begin(),controls.end(),[](const auto& b){return b.name=="MarketCloseButton";}));
    };
    auto frame=[&]{
        if(state.walletOpen)state.socialPoll=UINT64_MAX; // Wallet fixture owns its response snapshots.
        Marketplace::Panel::OpenStash();native.tick=GetTickCount64();Marketplace::Panel::Publish(native);
        Marketplace::Panel::CloseStash();assert(!Marketplace::Panel::Current().owner&&Marketplace::Panel::Current().cells.empty());
        Marketplace::Panel::Publish(native);assert(!Marketplace::Panel::Current().owner); // late paint cannot resurrect closed borders
        Marketplace::Panel::OpenStash();Marketplace::Panel::Publish(native);ImGui::NewFrame();Marketplace::UI::DrawStash();ImGui::Render();
        assert(ImGui::GetDrawData()->TotalVtxCount==0); // No marketplace ImGui windows, buttons or glyphs.
        for(const auto& update:Marketplace::Panel::Presentation().widgets)assert(widgetNames.contains(update.name));
        for(const auto& update:Marketplace::Panel::Presentation().previews)assert(widgetNames.contains(update.name));
        for(const auto& update:Marketplace::Panel::Presentation().images)assert(widgetNames.contains(update.name));
        for(const auto& update:Marketplace::Panel::Presentation().choices)assert(widgetNames.contains(update.name));
        for(const auto& r:Marketplace::Panel::Presentation().rectangles)assert(widgetNames.contains(r.name)&&r.x>=0&&r.y>=0&&r.width>0&&r.height>0&&r.x+r.width<=2250&&r.y+r.height<=1635);
        geometry();
    };
    auto command=[&](const std::string& name){assert(Marketplace::Panel::NativeMessage(Marketplace::Panel::MessageHash("BankPanelMessage"),Marketplace::Panel::MessageHash(name)));frame();frame();};
    auto input=[&](const std::string& name,const std::string& value){const auto presentation=Marketplace::Panel::Presentation();const auto at=std::find_if(presentation.widgets.begin(),presentation.widgets.end(),[&](const auto& w){return w.name==name&&w.input;});assert(at!=presentation.widgets.end());
        auto values=Marketplace::Panel::ReadNativeInputs();values[name]={value,at->revision};Marketplace::Panel::NativeInputs(std::move(values));frame();};
    auto presented=[&](const std::string& name){const auto view=Marketplace::Panel::Presentation();Marketplace::Panel::WidgetUpdate result;bool found=false;for(const auto& w:view.widgets)if(w.name==name){found=true;result.name=w.name;result.visible=w.visible;if(!w.text.empty()){result.text=w.text;result.input=w.input;result.revision=w.revision;}}if(found)return result;std::cerr<<"Missing presented widget: "<<name<<"\n";assert(false);return result;};
    {const std::string longToken(512,'x');auto wrapped=Marketplace::UI::WrapActivity(longToken);assert(std::count(wrapped.begin(),wrapped.end(),'\n')==12);std::erase(wrapped,'\n');assert(wrapped==longToken);}
    {using Marketplace::UI::CompactActivity;const auto text=CompactActivity(std::string(300,'W'),30,2);assert(std::count(text.begin(),text.end(),'\n')==1&&text.ends_with("..."));
        const auto utf8=CompactActivity(std::string(120,'x')+"\xc3\xa9\xe7\x95\x8c",30,2);assert(utf8.ends_with("...")&&Json(utf8).dump().size()>0);}
    auto previewFrame=[&](const std::string& name){const auto directory=std::getenv("D2_MARKET_PREVIEW_DIR");if(!directory)return;const auto view=Marketplace::Panel::Presentation();Json doc={{"section",view.section},{"widgets",Json::array()},{"images",Json::array()},{"rectangles",Json::array()},{"previews",Json::array()}};
        for(const auto& w:view.widgets)doc["widgets"].push_back({{"name",w.name},{"text",w.text},{"visible",w.visible},{"input",w.input}});
        for(const auto& w:view.images)doc["images"].push_back({{"name",w.name},{"filename",w.filename},{"width",w.width},{"height",w.height},{"visible",w.visible}});
        for(const auto& w:view.rectangles)doc["rectangles"].push_back({{"name",w.name},{"x",w.x},{"y",w.y},{"width",w.width},{"height",w.height}});
        for(const auto& w:view.previews)doc["previews"].push_back({{"name",w.name},{"label",w.label}});
        doc["choices"]=Json::array();for(const auto& w:view.choices)doc["choices"].push_back({{"name",w.name},{"selected",w.selection}});
        std::filesystem::create_directories(directory);std::ofstream file(std::filesystem::path(directory)/(name+".json"));file<<doc.dump(2);};
    auto wait=[&]{for(int n=0;n<400;++n){frame();if(!state.refresh&&!state.scanning.valid())break;Sleep(1);}assert(state.snapshot.depositValid&&state.snapshot.deposit.size()==2&&state.snapshot.feed.listings.size()==2);};
    frame();assert(!state.open);command("MarketOpen");assert(state.open);wait();
    { // A completed disk scan must supply persisted IDs even if live capture finished first.
        const auto canonical=state.snapshot;
        for(auto& item:state.snapshot.deposit)item=ReadItem(Unhex(LegacyItemHex(item.itemHex,item.children)),state.tables,item.tab);
        assert(state.snapshot.deposit.front().id==0);state.liveDeposit=true;
        std::promise<Marketplace::UI::SnapshotResult> disk;state.scanning=disk.get_future();
        disk.set_value({canonical,state.generation});frame();
        assert(state.snapshot.deposit.front().id==canonical.deposit.front().id&&state.snapshot.deposit.front().itemHex==canonical.deposit.front().itemHex);
        state.liveDeposit=false;
    }
    assert(presented("MarketCollection").visible&&!presented("MarketFocus").visible);previewFrame("browse-collection");
    { // Native dropdown choices sort the complete collection before its six-card page.
        const auto original=state.snapshot.feed.listings;
        state.sort=0;auto az=Marketplace::UI::MenuRows(state,fixtureCatalog);
        state.sort=4;auto za=Marketplace::UI::MenuRows(state,fixtureCatalog);std::reverse(az.begin(),az.end());assert(az==za);
        for(int choice:{4,5,6}){Marketplace::Panel::NativeChoices({{"MarketSort",{choice,state.filterRevision}}});frame();assert(state.sort==choice);}
        Marketplace::Panel::NativeChoices({});command("MarketSort");assert(state.sort==0);
        assert(state.snapshot.feed.listings.size()==original.size());
    }
    command("MarketFilterOpen");assert(state.filtersOpen&&presented("MarketFilterPanel").visible&&!presented("MarketCollection").visible);previewFrame("browse-filters");
    state.hideOwn=true;state.quality=2;command("MarketFilterReset");assert(!state.hideOwn&&state.quality==0);
    command("MarketFilterBack");assert(!state.filtersOpen&&presented("MarketCollection").visible);
    {const auto view=Marketplace::Panel::Presentation();assert(view.choices.size()==11);
        const auto preview=std::find_if(view.previews.begin(),view.previews.end(),[](const auto& p){return p.name=="MarketCardName0";});
        assert(preview!=view.previews.end()&&!preview->itemHex.empty());
        Marketplace::Panel::NativeChoices({{"MarketQuality",{5,state.filterRevision}}});frame();assert(state.quality==5);
        Marketplace::Panel::NativeChoices({{"MarketQuality",{1,state.filterRevision+1}}});frame();assert(state.quality==5); // Reject stale revision.
        Marketplace::Panel::NativeChoices({{"MarketQuality",{0,state.filterRevision}},{"MarketSort",{2,state.filterRevision}}});frame();assert(state.quality==0&&state.sort==2);
        Marketplace::Panel::NativeChoices({{"MarketSort",{0,state.filterRevision}},{"MarketSearchScope",{2,state.filterRevision}}});frame();assert(state.searchScope==2);
        Marketplace::Panel::NativeChoices({{"MarketSearchScope",{0,state.filterRevision}}});frame();Marketplace::Panel::NativeChoices({});}
    {using namespace Marketplace;using namespace Marketplace::UI;
        SavedItem ring;ring.code="rin";ring.quality=7;ring.index=42;ring.flags=1u<<4;
        SavedItem rune;rune.code="r33";rune.quality=2;rune.flags=1u<<4;
        const std::vector<Requirement> zod={{"r33","r33",2,-1,40,-1}};
        const std::vector<Requirement> soj={{"rin","",7,42,1,-1}};
        assert(TradeMatches(ring,"Trader",zod,"zod rune",0,0,fixtureCatalog));
        assert(!TradeMatches(ring,"Trader",zod,"zod rune",0,1,fixtureCatalog));
        assert(TradeMatches(ring,"Trader",zod,"zod rune",5,2,fixtureCatalog));
        assert(!TradeMatches(ring,"Trader",zod,"zod rune",1,0,fixtureCatalog)); // Same side must match both text and quality.
        assert(TradeMatches(rune,"Trader",soj,"stone of jordan",1,2,fixtureCatalog));
        assert(!TradeMatches(rune,"Trader",soj,"stone of jordan",1,1,fixtureCatalog));
        assert(TradeMatches(rune,"Trader",soj,"",1,2,fixtureCatalog));
        assert(TradeMatches(rune,"Trader",soj,"Trader",1,2,fixtureCatalog));
        assert(TradeMatches(rune,"Trader",{},"zod",0,1,fixtureCatalog));
        assert(!TradeMatches(rune,"Trader",{},"",0,2,fixtureCatalog));
        assert(TradeMatches(ring,"Trader",{{"gld","Gold",2,-1,100,-1,"gold"}},"gold",0,2,fixtureCatalog));
        assert(TradeMatches(ring,"Trader",{{"Z01","Gems (bag)",2,-1,100,-1,"bag","GB_Total"}},"gems",0,2,fixtureCatalog));
    }
    const auto request=Marketplace::Panel::PendingScan();assert(request.sequence);
    Marketplace::Panel::Item ring,rune,added;const auto savedRing=ReadItem(Unique(true),state.tables,3);ring.bytes=Unhex(LegacyItemHex(savedRing.itemHex,savedRing.children));ring.tab=3;rune.bytes=Rune(4);rune.tab=3;added.bytes=Rune(5);added.tab=7;
    Marketplace::Panel::PublishScan({request,{ring,rune,added},{}});frame();assert(state.liveDeposit&&state.snapshot.deposit.size()==3);
    assert(state.snapshot.deposit.front().itemHex==savedRing.itemHex);
    command("MarketAffordable");assert(state.affordableOnly);command("MarketCard0");command("MarketConfirm");assert(state.settings.agreements.size()==1);
    command("MarketAgreements");Marketplace::Panel::NativeChoices({{"MarketActivityType",{1,state.filterRevision}}});frame();Marketplace::Panel::NativeChoices({});command("MarketCard0");command("MarketConfirm");assert(state.settings.agreements.empty());
    command("MarketItems");command("MarketFilterOpen");previewFrame("own-filters");command("MarketFilterApply");assert(!state.filtersOpen);assert(state.ownSelection==-1&&Marketplace::Panel::Presentation().section==1);command("MarketCard0");
    { // Background captures keep item details/history, even when rows reorder.
        const auto selected=Marketplace::UI::OwnItems(state)[state.ownSelection].itemHex;
        state.historyOpen=true;state.historyOffset=0;
        state.liveRequest=Marketplace::Panel::RequestScan(native.owner,native.page,native.hardcore);
        const auto late=Marketplace::Panel::PendingScan();Marketplace::Panel::PublishScan({late,{rune,ring,added},{}});frame();
        assert(state.ownSelection>=0&&Marketplace::UI::OwnItems(state)[state.ownSelection].itemHex==selected);
        assert(state.open&&state.historyOpen&&!presented("MarketCollection").visible);
        state.historyOpen=false;state.historyOffset=0;
        auto replacement=state.snapshot;std::reverse(replacement.deposit.begin(),replacement.deposit.end());
        state.liveDeposit=false;std::promise<Marketplace::UI::SnapshotResult> disk;state.scanning=disk.get_future();
        disk.set_value({replacement,state.generation});frame();
        assert(state.ownSelection>=0&&Marketplace::UI::OwnItems(state)[state.ownSelection].itemHex==selected);
    }
    command("MarketPrice");assert(state.editing);
    { // Alt-tab, native page switching and long serialization suspend capture;
      // they must not be mistaken for the player closing the stash.
        const auto selected=state.selected.itemHex;Marketplace::Panel::SuspendCapture();
        ImGui::NewFrame();Marketplace::UI::DrawStash();ImGui::Render();
        assert(state.open&&state.editing&&state.selected.itemHex==selected);
        frame();assert(state.open&&state.editing&&state.selected.itemHex==selected);
    }
    { // A delayed live scan can reorder rows while the player is entering a price.
        const auto edited=state.selected.itemHex;
        state.liveRequest=Marketplace::Panel::RequestScan(native.owner,native.page,native.hardcore);
        const auto late=Marketplace::Panel::PendingScan();Marketplace::Panel::PublishScan({late,{rune,ring,added},{}});frame();
        assert(state.editing&&state.selected.itemHex==edited&&state.ownSelection>=0);
        assert(Marketplace::UI::OwnItems(state)[state.ownSelection].itemHex==edited);
    }
    assert(!presented("MarketCollection").visible&&presented("MarketFocus").visible);previewFrame("price-start");
    assert(presented("MarketPriceStart").visible&&!presented("MarketSavePriceButton").visible);
    {const auto view=Marketplace::Panel::Presentation();auto shown=[&](const std::string& name){bool result=true;for(const auto& w:view.widgets)if(w.name==name)result=w.visible;return result;};
        assert(shown("MarketItemSummary")&&shown("MarketItemFacts")&&!shown("MarketItemDetail")&&shown("MarketPriceEditor"));}
    {const auto view=Marketplace::Panel::Presentation();const auto preview=std::find_if(view.previews.begin(),view.previews.end(),[](const auto& p){return p.name=="MarketItemSummary";});
        assert(preview!=view.previews.end()&&preview->itemHex==state.snapshot.deposit[state.ownSelection].itemHex);}
    command("MarketAddPayment");assert(presented("MarketDetailTitle").text.find("STEP 1")!=std::string::npos);
    previewFrame("payment-picker");
    assert(presented("MarketPaymentCount").text.find("outlined box")!=std::string::npos&&!presented("MarketPriceStart").visible);
    input("PaymentSearch","no-such-item");assert(presented("MarketPaymentCount").text.find("No matches")!=std::string::npos);
    input("PaymentSearch","Zod");assert(presented("MarketPaymentCount").text.find("Choose one")!=std::string::npos);
    command("MarketPayment0");assert(presented("MarketDetailTitle").text.find("STEP 2")!=std::string::npos&&presented("MarketSavePriceButton").visible);
    {
        const auto original=state.price;state.price={{"cm2","Hellfire Torch",7,556,1,-1}};++state.priceRevision;frame();
        assert(presented("MarketTorchClass0").visible&&!presented("MarketEth0").visible);
        Marketplace::Panel::NativeChoices({{"MarketTorchClass0",{2,state.priceRevision}}});frame();Marketplace::Panel::NativeChoices({});
        assert(state.price[0].classId==1&&Marketplace::UI::MenuBundle(state.price).find("Sorceress")!=std::string::npos);
        previewFrame("torch-class-price");
        Marketplace::Panel::NativeChoices({{"MarketTorchClass0",{3,state.priceRevision-1}}});frame();Marketplace::Panel::NativeChoices({});assert(state.price[0].classId==1);
        state.price=original;++state.priceRevision;frame();
    }
    previewFrame("price-review");
    input("Quantity0","40");
    command("MarketAddPayment");input("PaymentSearch","Stone");command("MarketPayment0");
    command("MarketAddPayment");input("PaymentSearch","Bul");command("MarketPayment0");
    assert(state.price.size()==3&&state.price[0].quantity==40);command("MarketSavePrice");assert(!state.editing&&state.settings.drafts.size()==1);
    {const auto local=std::find_if(state.snapshot.feed.listings.begin(),state.snapshot.feed.listings.end(),[](const auto& l){return l.own;});
        assert(local!=state.snapshot.feed.listings.end()&&local->item.itemHex==state.selected.itemHex&&local->price[0].quantity==40);
        const auto ownPrice=state.settings.drafts.front().price;
        command("MarketBrowse");command("MarketAffordable");input("MarketSearch","You (local draft)");assert(Marketplace::UI::MenuRows(state,fixtureCatalog).size()==1);
        command("MarketCard0");const auto view=Marketplace::Panel::Presentation();bool confirm=true;for(const auto& w:view.widgets)if(w.name=="MarketConfirmButton")confirm=w.visible;assert(!confirm);
        command("MarketConfirm");assert(state.settings.agreements.empty());
        command("MarketItems");command("MarketCard0");assert(state.settings.drafts.front().price.size()==ownPrice.size());
        command("MarketAffordable"); // Restore original filter state for the remaining fixture.
    }
    auto saved=ReadSettings(Json::parse(ReadSettingsFile(state.path)),modName,false);assert(saved.drafts[0].price.size()==3&&saved.drafts[0].price[0].quantity==40);
    { // The companion can repair saved identities or change prices while the panel is closed.
        auto settle=[&]{for(int n=0;n<400;++n){frame();if(!state.refresh&&!state.scanning.valid())break;Sleep(1);}assert(state.snapshot.depositValid);};
        auto external=saved;external.drafts.front().price.front().quantity=41;
        SaveSettingsFile(state.path,state.original,SettingsJson(external,modName,false).dump(2)+"\n");
        command("MarketClose");command("MarketOpen");settle();
        assert(state.settings.drafts.front().price.front().quantity==41&&state.original==ReadSettingsFile(state.path));
        external.drafts.front().price.front().quantity=42;
        SaveSettingsFile(state.path,state.original,SettingsJson(external,modName,false).dump(2)+"\n");
        command("MarketRefresh");settle();
        assert(state.settings.drafts.front().price.front().quantity==42&&state.original==ReadSettingsFile(state.path));
        state.settings=saved;Marketplace::UI::Save(state,modName);Marketplace::UI::RefreshListings(state);
        command("MarketItems");command("MarketCard0");
    }
    {const auto price=state.settings.drafts.front().price;auto& terms=state.settings.drafts.front().price;terms.clear();
        for(int n=0;n<16;++n)terms.push_back({"r"+std::to_string(10+n),"Payment rune "+std::to_string(n+1),2,-1,40,-1});
        frame();assert(presented("MarketTermsNextButton").visible);previewFrame("sixteen-payments");
        command("MarketTermsNext");assert(state.termsOffset==4&&presented("MarketTerm0").text.find("Payment rune 5")!=std::string::npos);
        command("MarketTermsNext");command("MarketTermsNext");assert(state.termsOffset==12&&!presented("MarketTermsNextButton").visible);
        command("MarketFullPrice");assert(state.receiptOpen&&state.receiptDescription.find("Payment rune 16")!=std::string::npos);command("MarketBackReceipt");
        state.termsOffset=0;terms[0].label=std::string(512,'W');state.status=std::string(512,'W');state.statusUntil=GetTickCount64()+5000;frame();previewFrame("long-price-summary");
        assert(presented("MarketTerm0").text.ends_with("...")&&presented("MarketStatus").text.ends_with("..."));
        command("MarketFullPrice");assert(state.receiptDescription.find(std::string(512,'W'))!=std::string::npos&&Marketplace::UI::ActivityPages(state.receiptDescription).size()>1);previewFrame("long-full-price");command("MarketBackReceipt");state.statusUntil=0;
        terms=price;frame();assert(state.termsOffset==0);} // Exact terms remain available across bounded pages.
    {const auto deposit=state.snapshot.deposit;
        for(int n=0;n<12;++n)state.snapshot.deposit.push_back(ReadItem(Rune(20+n),state.tables,3));
        command("MarketBackList");frame();previewFrame("full-item-collection");command("MarketNext");assert(state.listOffset==6);previewFrame("second-item-page");command("MarketPrev");assert(state.listOffset==0);
        state.snapshot.deposit=deposit;frame();command("MarketCard0");} // A full six-row page and both pagination states.
    command("MarketPrice");command("MarketRemovePrice");assert(state.settings.drafts.empty()&&state.open);for(const auto& l:state.snapshot.feed.listings)assert(!l.own);
    command("MarketPrice");command("MarketAddPayment");input("PaymentSearch","Zod");command("MarketPayment0");input("Quantity0","0");command("MarketSavePrice");assert(state.editing&&state.settings.drafts.empty());
    input("Quantity0","40");const auto original=state.original;SaveSettingsFile(state.path,original,original+" ");command("MarketSavePrice");
    assert(!state.editing&&state.settings.drafts.size()==1&&state.settings.drafts.front().price.front().quantity==40);
    { // Disabled deposits still allow price drafts without admitting any goods.
        state.settings.enabled=false;Marketplace::UI::Save(state,modName);Marketplace::UI::RefreshListings(state);frame();
        assert(presented("MarketPriceButton").visible&&presented("MarketItemExtra").text.find("Deposit disabled")!=std::string::npos);
        command("MarketPrice");assert(state.editing);input("Quantity0","41");command("MarketSavePrice");
        assert(!state.editing&&!state.settings.enabled&&state.settings.drafts.front().price.front().quantity==41);
        const auto disabled=ReadSettings(Json::parse(ReadSettingsFile(state.path)),modName,false);
        assert(!disabled.enabled&&disabled.drafts.front().price.front().quantity==41);
        assert(state.status.find("Enable your deposit")!=std::string::npos);
        for(const auto& listing:state.snapshot.feed.listings)assert(!listing.own);
        state.settings.enabled=true;
    }
    state.settings.drafts.clear();Marketplace::UI::Save(state,modName);Marketplace::UI::RefreshListings(state);
    command("MarketCancelPrice");command("MarketSetup");command("MarketReload");wait();
    input("DepositPage","17");command("MarketAuto");command("MarketSaveSetup");
    saved=ReadSettings(Json::parse(ReadSettingsFile(state.path)),modName,false);assert(saved.autoTrade&&saved.withdrawPage==17&&saved.page==17);
    assert(!presented("MarketRefreshButton").visible&&!presented("MarketReloadButton").visible);previewFrame("compact-setup-saved");
    command("MarketAuto");assert(presented("MarketReloadButton").visible);previewFrame("compact-setup-editing");command("MarketAuto");
    const auto before=state.original;input("DepositPage","0");command("MarketSaveSetup");assert(state.original==before);
    assert(presented("MarketRouteNotice").text.find("1 to 64")!=std::string::npos);previewFrame("compact-setup-error");
    input("DepositPage","65");command("MarketSaveSetup");assert(state.original==before&&presented("MarketRouteNotice").text.find("1 to 64")!=std::string::npos);
    input("DepositPage","17");command("MarketBrowse");input("MarketSearch","no-match");assert(Marketplace::UI::MenuRows(state,fixtureCatalog).empty());
    input("MarketSearch","Zod");assert(!Marketplace::UI::MenuRows(state,fixtureCatalog).empty());
    command("MarketSort");command("MarketQuality");command("MarketRefresh");wait();
    // Old simulated conversations remain readable after retiring the page option.
    CounterThread legacy;legacy.id="legacy-test-offer";legacy.listing="legacy-test-listing";legacy.itemHex=Hex(Unique(false,43));legacy.seed=43;
    legacy.buyer="local-player";legacy.seller=legacy.proposer="Legacy seller";legacy.buyerName="You";legacy.sellerName="Legacy seller";legacy.status="pending";legacy.local=legacy.simulated=true;
    legacy.price={{"r33","Zod Rune",2,-1,1,-1}};legacy.events={{UnixNow()-60,"counter","Legacy seller","Previous test conversation",legacy.price}};
    state.local.threads.push_back(legacy);Marketplace::UI::SaveLocal(state);
    state.snapshot.deposit={ReadItem(Rune(4),state.tables,3)};state.snapshot.depositValid=true;state.liveDeposit=true;
    state.sort=state.quality=state.tabFilter=0;state.search.clear();state.affordableOnly=false;++state.filterRevision;++state.searchRevision;
    state.tables.bagStats={{"GB_Total",100}};state.snapshot.depositGold=2500000;state.snapshot.goldValid=true;
    command("MarketItems");
    command("MarketCard0");command("MarketPrice");command("MarketAddPayment");
    Marketplace::Panel::NativeChoices({{"MarketPaymentKind",{2,state.filterRevision}}});frame();Marketplace::Panel::NativeChoices({});
    assert(!presented("PaymentSearchField").visible&&presented("MarketGoldHelp").visible);
    command("MarketPayment0");input("Quantity0","1500000");assert(!presented("MarketEth0").visible&&presented("MarketUnits0").visible);
    command("MarketAddPayment");Marketplace::Panel::NativeChoices({{"MarketPaymentKind",{1,state.filterRevision}}});frame();Marketplace::Panel::NativeChoices({});
    assert(presented("PaymentSearchField").visible&&Marketplace::UI::MenuPayments(state,fixtureCatalog).size()==1);
    command("MarketPayment0");input("Quantity1","40");command("MarketSavePrice");
    const auto* resourceDraft=Marketplace::UI::OwnPrice(state,state.snapshot.deposit[0]);
    assert(resourceDraft&&resourceDraft->price.size()==2&&resourceDraft->price[0].kind=="gold"&&resourceDraft->price[0].quantity==1500000&&resourceDraft->price[1].stat=="GB_Total");
    state.snapshot.deposit.push_back(ReadItem(Rune(6),state.tables,3));state.search.clear();++state.searchRevision;state.sort=state.quality=state.tabFilter=0;state.affordableOnly=false;++state.filterRevision;
    command("MarketBrowse");input("MarketSearch","Trader A");command("MarketCard0");
    const auto* historyItem=Marketplace::UI::ActivityItem(state);assert(historyItem&&historyItem->id);
    Sale sale;sale.id="completed-1";sale.itemId="ledger-instance";sale.seed=historyItem->id;sale.code=historyItem->code;sale.quality=historyItem->quality;sale.index=historyItem->index;sale.ethereal=1;sale.sockets=0;sale.at=UnixNow()-86400;sale.seller="Seller A";sale.buyer="Buyer B";sale.receipt="receipt-1";sale.price={{"gld","Gold",2,-1,1000,-1,"gold"}};
    state.snapshot.activity.sales={sale};sale.id="completed-2";sale.receipt="receipt-2";sale.at=UnixNow()-3600;sale.price[0].quantity=2000;state.snapshot.activity.sales.push_back(sale);++state.activityGeneration;
    command("MarketHistory");assert(presented("MarketHistoryPanel").visible&&presented("MarketItemSummary").visible);previewFrame("item-history");
    command("MarketSale0");assert(presented("MarketReceiptPanel").visible&&state.receiptPrice[0].quantity==2000);
    Marketplace::Panel::NativeChoices({{"MarketReceiptMode",{1,state.filterRevision}}});frame();Marketplace::Panel::NativeChoices({});assert(presented("MarketReceiptTerm0").visible);command("MarketBackReceipt");
    Marketplace::Panel::NativeChoices({{"MarketHistoryKind",{1,state.filterRevision}}});frame();Marketplace::Panel::NativeChoices({});assert(Marketplace::Panel::Presentation().rectangles.size()==2);previewFrame("similar-sales");
    command("MarketSimilarSale0");command("MarketBackReceipt");command("MarketBackHistory");assert(!state.historyOpen);
    command("MarketCounter");command("MarketSavePrice");assert(!state.editing&&state.local.threads.back().status=="draft");
    const auto localThreadId=state.local.threads.back().id;command("MarketAgreements");assert(state.activityType==0);
    const auto& allThreads=Marketplace::UI::Threads(state);auto threadAt=std::find_if(allThreads.begin(),allThreads.end(),[&](const auto& t){return t.id==localThreadId;});assert(threadAt!=allThreads.end());
    command("MarketCard"+std::to_string(threadAt-allThreads.begin()));
    const auto message=std::string("Would you consider this price? ")+std::string(120,'x');input("MarketOfferMessage",message);command("MarketSendMessage");assert(state.local.threads.back().events.back().message==message);previewFrame("offer-conversation");
    command("MarketLog0");assert(state.receiptDescription.find(message)!=std::string::npos);previewFrame("offer-message");command("MarketBackReceipt");
    {const auto original=state.receiptDescription;const auto draft=std::string("Keep this unsent draft");input("MarketOfferMessage",draft);
        command("MarketLog0");state.receiptDescription=std::string(2048,'W')+"\n\nUTF-8: \xc3\xa9\xe7\x95\x8c";frame();
        const auto pages=Marketplace::UI::ActivityPages(state.receiptDescription);assert(pages.size()>1&&presented("MarketReceiptNextButton").visible);
        std::string recovered;for(const auto& p:pages){assert(std::count(p.begin(),p.end(),'\n')<14);for(size_t start=0,end=0;start<p.size();start=end+1){end=p.find('\n',start);if(end==std::string::npos)end=p.size();assert(end-start<=64*4);}}
        for(const auto& p:pages)recovered+=p;std::erase(recovered,'\n');auto expected=state.receiptDescription;std::erase(expected,'\n');assert(recovered==expected);
        command("MarketReceiptNext");assert(state.receiptPage==1&&presented("MarketReceiptPrevButton").visible);previewFrame("long-message-page");
        command("MarketReceiptPrev");assert(state.receiptPage==0);command("MarketBackReceipt");assert(state.message==draft&&presented("MarketThreadPanel").visible);
        input("MarketOfferMessage","");state.receiptDescription=original;}
    Marketplace::Panel::NativeChoices({{"MarketOfferAction",{2,state.filterRevision}}});frame();Marketplace::Panel::NativeChoices({});command("MarketApplyOfferAction");assert(state.local.threads.back().status=="withdrawn"&&state.snapshot.activity.sales.size()==2);
    command("MarketCard0");Marketplace::Panel::NativeChoices({{"MarketOfferAction",{3,state.filterRevision}}});frame();Marketplace::Panel::NativeChoices({});command("MarketApplyOfferAction");assert(state.editing&&!state.replyTo.empty());
    input("Quantity0","1");command("MarketSavePrice");assert(!state.editing);
    Marketplace::Panel::NativeChoices({{"MarketOfferAction",{1,state.filterRevision}}});frame();Marketplace::Panel::NativeChoices({});command("MarketApplyOfferAction");assert(state.local.threads.front().status=="rejected");
    const auto localLog=ReadLocalActivity(Json::parse(state.localOriginal),modName,false);assert(localLog.threads.back().events.back().kind=="withdraw");
    CounterThread remote=state.local.threads.front();remote.id="remote-offer";remote.local=remote.simulated=false;remote.status="pending";remote.buyer="buyer-service";remote.seller="seller-service";remote.proposer=remote.seller;remote.revision=3;remote.events={{UnixNow(),"counter","Seller","Please review this counter-offer.",remote.price}};
    state.snapshot.activity.self=remote.buyer;state.snapshot.activity.name="Buyer";state.snapshot.activity.expires=UnixNow()+3600;state.snapshot.activity.threads={remote};++state.activityGeneration;
    command("MarketAgreements");command("MarketCard0");assert(presented("MarketAcceptOfferButton").visible&&presented("MarketCounterOfferButton").visible&&!presented("MarketWithdrawOfferButton").visible);previewFrame("incoming-offer");command("MarketAcceptOffer");assert(!state.local.commands.empty()&&state.local.commands.back()["type"]=="accept"&&state.local.commands.back()["expected_revision"]==3);
    assert(Marketplace::UI::Threads(state).front().status=="pending"&&!presented("MarketApplyOfferActionButton").visible&&state.snapshot.activity.sales.size()==2); // Queued approval is not server acceptance or a sale.
    { // An unrelated late deposit capture must not dismiss a conversation/reply.
        const auto id=Marketplace::UI::Threads(state)[state.marketSelection].id;
        state.message="unfinished reply";++state.messageRevision;
        state.liveRequest=Marketplace::Panel::RequestScan(native.owner,native.page,native.hardcore);
        const auto late=Marketplace::Panel::PendingScan();Marketplace::Panel::PublishScan({late,{rune,ring,added},{}});frame();
        assert(state.marketSelection>=0&&Marketplace::UI::Threads(state)[state.marketSelection].id==id);
        assert(state.message=="unfinished reply"&&!presented("MarketCollection").visible);
        state.message.clear();++state.messageRevision;
    }
    const auto commandId=state.local.commands.back()["id"].get<std::string>();state.snapshot.activity.results[commandId]={{"offer_id",remote.id},{"status","accepted"}};state.snapshot.activity.threads[0].status="accepted";state.snapshot.activity.threads[0].revision=4;++state.activityGeneration;frame();
    assert(Marketplace::UI::Threads(state).front().status=="accepted"&&state.snapshot.activity.sales.size()==2);
    assert(!presented("MarketAcceptOfferButton").visible&&!presented("MarketRejectOfferButton").visible&&!presented("MarketCounterOfferButton").visible);previewFrame("agreed-offer");
    {const auto count=state.local.commands.size();command("MarketBackList");assert(state.marketSelection==-1&&presented("MarketCollection").visible&&!presented("MarketFocus").visible);command("MarketCard0");assert(state.local.commands.size()==count&&presented("MarketFocus").visible);}
    {auto trader=std::find_if(state.snapshot.feed.listings.begin(),state.snapshot.feed.listings.end(),[](const auto& l){return l.seller=="Trader A";});assert(trader!=state.snapshot.feed.listings.end());trader->sellerId="seller-server";trader->itemId="hosted-ledger";trader->revision="1";trader->development=true;
        command("MarketBrowse");input("MarketSearch","Trader A");command("MarketCard0");assert(presented("MarketConfirmLabel").text=="Propose asking price");const auto count=state.settings.agreements.size();command("MarketConfirm");assert(state.local.commands.back()["type"]=="propose"&&state.local.commands.back()["listing_revision"]==1&&state.settings.agreements.size()==count);assert(state.local.threads.back().development);}
    {const auto savedFeed=state.snapshot.feed;const auto savedDeposit=state.snapshot.deposit;
        auto doc=FeedFixture();doc["escrow"]={{"enrolled",true},{"items",Json::array({{{"item",std::string(32,'a')},{"state","listed"},{"item_hex",Hex(Unique())},{"metadata",{{"tab",3},{"price",PriceJson({{"gld","Gold",2,-1,1000,-1,"gold"}})}}}}})},
            {"balances",Json::array({{{"kind","gold"},{"stat",""},{"quantity",15000000}}})}};
        state.snapshot.feed=ReadFeed(doc,"MarketplaceFixture",false,Tables(),UnixNow());state.snapshot.deposit.clear();state.search.clear();++state.searchRevision;
        assert(CanPay({{"gld","Gold",2,-1,10000000,-1,"gold"}},Spendable({},state.settings,0,&state.snapshot.feed)));
        state.quality=state.tabFilter=state.sort=0;state.pricedOnly=false;
        command("MarketItems");state.ownSelection=int(state.snapshot.deposit.size());frame();assert(presented("MarketReturnButton").visible&&presented("MarketItemExtra").text.find("Vault")!=std::string::npos);previewFrame("escrow-item");
        assert(Marketplace::UI::ActivityLedgerId(state)==std::string(32,'a'));
        const auto count=state.local.commands.size();command("MarketReturn");assert(state.local.commands.size()==count+1&&state.local.commands.back()["type"]=="escrow_return");
        assert(state.status.find("page "+std::to_string(state.settings.withdrawPage))!=std::string::npos&&state.status.find("refreshes automatically")!=std::string::npos);
        command("MarketItems");assert(presented("MarketVaultNavigation").visible);
        Marketplace::Panel::NativeChoices({{"MarketVaultView",{1,state.walletRevision}}});frame();Marketplace::Panel::NativeChoices({});
        assert(state.walletOpen&&state.walletMode==0&&presented("MarketVaultNavigation").visible&&presented("MarketWalletTitle").text=="VAULT | GOLD & RESOURCES");
        Marketplace::Panel::NativeChoices({{"MarketVaultView",{3,state.walletRevision}}});frame();Marketplace::Panel::NativeChoices({});
        assert(state.walletOpen&&state.walletMode==2);
        Marketplace::Panel::NativeChoices({{"MarketVaultView",{0,state.walletRevision}}});frame();Marketplace::Panel::NativeChoices({});
        assert(!state.walletOpen&&presented("MarketVaultNavigation").visible);
        command("MarketWallet");assert(presented("MarketWalletGroup").visible&&!presented("MarketListGroup").visible&&presented("MarketWalletRow0Detail").text.find("15000000")!=std::string::npos);previewFrame("escrow-wallet");
        const auto walletCommands=state.local.commands.size();state.snapshot.activity.self=std::string(32,'e');
        command("MarketWalletAction0");assert(state.walletForm&&!state.walletReview);input("MarketWalletAmount","0");command("MarketWalletSubmit");assert(!state.walletReview&&state.local.commands.size()==walletCommands);
        input("MarketWalletAmount","10000000");command("MarketWalletSubmit");assert(state.walletReview&&state.local.commands.size()==walletCommands);previewFrame("wallet-withdraw-review");
        command("MarketWalletSubmit");assert(!state.walletForm&&state.local.commands.size()==walletCommands+1&&state.local.commands.back()["type"]=="wallet_withdraw"&&state.local.commands.back()["balances"][0]["quantity"]==10000000);
        const auto request=state.walletPending;state.snapshot.activity.results[request]={{"status","queued"}};frame();assert(state.walletPending.empty());
        state.snapshot.feed.wallet={{"withdrawals",Json::array({{{"id",std::string(32,'f')},{"status","queued"},{"revision",3},{"requested",PriceJson({{"gld","Gold",2,-1,10000000,-1,"gold"}})},{"remaining",PriceJson({{"gld","Gold",2,-1,4000000,-1,"gold"}})},{"message","Make room on delivery page"}}})},{"entries",Json::array()}};
        frame();assert(presented("MarketWalletAction0Button").visible);previewFrame("wallet-queued-withdrawal");
        const auto queued=state.snapshot.feed.wallet["withdrawals"][0];
        state.snapshot.feed.wallet["withdrawals"][0]["status"]="completed";state.snapshot.feed.wallet["withdrawals"][0]["remaining"]=Json::array();state.snapshot.feed.wallet["withdrawals"][0]["message"]="";
        frame();assert(presented("MarketWalletRow0Title").text=="Delivered"&&!presented("MarketWalletAction0Button").visible&&!presented("MarketWalletRow1").visible);
        state.snapshot.feed.wallet["withdrawals"][0]=queued;frame();command("MarketWalletAction0");assert(state.walletForm&&state.walletReview);command("MarketWalletSubmit");assert(state.local.commands.back()["type"]=="wallet_cancel"&&state.local.commands.back()["expected_revision"]==3);
        command("MarketWalletBack");assert(!state.walletOpen&&presented("MarketListGroup").visible);
        doc["escrow"]["lineage"]=Json::array({{{"item",std::string(32,'b')},{"item_hex",Hex(Unique())}}});const auto lineageFeed=ReadFeed(doc,"MarketplaceFixture",false,Tables(),UnixNow());
        auto physical=std::vector<SavedItem>{ReadItem(Unique(),Tables(),3)};ApplyLineage(physical,lineageFeed);assert(physical[0].ledgerId==std::string(32,'b')&&physical[0].escrowId.empty());
        auto ambiguous=lineageFeed;ambiguous.lineage.begin()->second.insert(std::string(32,'c'));ApplyLineage(physical,ambiguous);assert(physical[0].ledgerId.empty());
        state.snapshot.feed=savedFeed;state.snapshot.deposit=savedDeposit;
    }
    command("Close");assert(!state.open&&!Marketplace::Panel::Presentation().open);
    assert(!Marketplace::Panel::NativeMessage(Marketplace::Panel::MessageHash("BankPanelMessage"),Marketplace::Panel::MessageHash("Close"))); // Original stash close remains available.
    command("MarketOpen");assert(state.open);
    native.owner=0;frame();assert(!state.open&&!Marketplace::Panel::Presentation().open&&!Marketplace::Panel::KeyboardActive());
    if(state.scanning.valid())state.scanning.get();
    {Marketplace::UI::State migration;migration.path=folder/L"legacy-settings.json";migration.settings=state.settings;migration.settings.simulation.enabled=true;migration.settings.simulation.page=19;
        migration.settings.simulation.drafts={{2,Hex(Rune()),{{"r33","Zod Rune",2,-1,2,-1}}}};migration.settings.drafts={{3,Hex(Unique()),{{"gld","Gold",2,-1,1000,-1,"gold"}}}};
        Marketplace::UI::Save(migration,modName);Marketplace::UI::Load(migration,modName);assert(migration.valid&&!migration.settings.simulation.enabled&&!migration.ownSeller);
        assert(migration.settings.simulation.drafts.size()==1&&migration.settings.drafts.size()==1&&migration.local.threads.size()==state.local.threads.size());
        const auto stored=ReadSettings(Json::parse(migration.original),modName,false);assert(!stored.simulation.enabled&&stored.simulation.page==19&&stored.simulation.drafts.size()==1);
        const auto stable=migration.original;Marketplace::UI::Load(migration,modName);assert(migration.original==stable);}
    {Marketplace::UI::State walletPoll;walletPoll.walletOpen=true;walletPoll.refresh=false;walletPoll.socialPending={"acknowledged-withdrawal"};walletPoll.snapshot.activity.results["acknowledged-withdrawal"]={{"status","queued"}};
        Marketplace::UI::PollSocial(walletPoll);assert(walletPoll.socialPending.empty()&&walletPoll.refresh&&walletPoll.socialPoll>GetTickCount64());
        walletPoll.refresh=false;Marketplace::UI::PollSocial(walletPoll);assert(!walletPoll.refresh); // Bounded local polling.
        walletPoll.socialPoll=0;Marketplace::UI::PollSocial(walletPoll);assert(walletPoll.refresh); // Continues after the command acknowledgement.
        walletPoll.walletOpen=false;walletPoll.refresh=false;walletPoll.socialPoll=0;Marketplace::UI::PollSocial(walletPoll);assert(!walletPoll.refresh);}
    { // Real queued native actions: saving and closing in one tick must persist and export the price.
        Marketplace::UI::State writer;writer.path=folder/L"queued-price"/L"Marketplace_2.4_SC.json";std::filesystem::create_directories(writer.path.parent_path());
        writer.valid=writer.open=writer.editing=true;writer.section=Marketplace::UI::Section::Items;writer.ownSelection=0;writer.settings.enabled=true;writer.settings.page=17;
        writer.tables=Tables();writer.selected=ReadItem(Unique(),writer.tables,3);writer.snapshot.deposit={writer.selected};writer.snapshot.depositValid=true;
        writer.listingComment="Looking for Codex of Gluttony - Testament 1:3";writer.price={{"gld","Gold",2,-1,1234,-1,"gold"}};Marketplace::UI::Save(writer,modName);
        const auto domain=Marketplace::Panel::MessageHash("BankPanelMessage");
        auto saveClose=[&]{Marketplace::Panel::NativeInputs({});Marketplace::Panel::NativeChoices({});Marketplace::Panel::Present({true});
            assert(Marketplace::Panel::NativeMessage(domain,Marketplace::Panel::MessageHash("MarketSavePrice")));
            assert(Marketplace::Panel::NativeMessage(domain,Marketplace::Panel::MessageHash("Close")));
            Marketplace::UI::NativeMenu(writer,native,fixtureCatalog);};
        saveClose();assert(!writer.open&&!writer.editing);
        Marketplace::UI::Load(writer,modName);assert(writer.settings.drafts.size()==1&&writer.settings.drafts[0].price[0].quantity==1234);
        const auto published=Json::parse(ReadSettingsFile(writer.path.parent_path()/L"MarketplacePublish_2.4_SC.json"));
        assert(published["items"].size()==1&&published["items"][0]["price"][0]["quantity"]==1234);assert(published["items"][0]["comment"]==writer.listingComment&&writer.settings.drafts[0].comment==writer.listingComment);
        // A companion's unrelated preference/price change is retained, without rejecting this edit.
        auto external=writer.settings;external.autoTrade=true;external.drafts.push_back({3,Hex(Rune()),{{"gld","Gold",2,-1,222,-1,"gold"}}});
        SaveSettingsFile(writer.path,writer.original,SettingsJson(external,modName,false).dump());
        writer.open=writer.editing=true;writer.price[0].quantity=2345;saveClose();
        assert(!writer.open&&writer.settings.autoTrade&&writer.settings.drafts.size()==2);
        // A competing price for this same item is a real conflict. Keep the editor, entered terms and visible error.
        external=writer.settings;for(auto& draft:external.drafts)if(draft.itemHex==writer.selected.itemHex)draft.price[0].quantity=3456;
        const auto concurrent=SettingsJson(external,modName,false).dump();SaveSettingsFile(writer.path,writer.original,concurrent);
        writer.open=writer.editing=true;writer.price[0].quantity=4567;saveClose();
        if(!(writer.open&&writer.editing&&writer.price[0].quantity==4567&&!writer.priceSaveError.empty()&&ReadSettingsFile(writer.path)==concurrent))
            std::cerr<<"Price conflict state: open="<<writer.open<<" editing="<<writer.editing<<" quantity="<<writer.price[0].quantity<<" error="<<writer.priceSaveError<<" file-preserved="<<(ReadSettingsFile(writer.path)==concurrent)<<'\n';
        assert(writer.open&&writer.editing&&writer.price[0].quantity==4567&&!writer.priceSaveError.empty()&&ReadSettingsFile(writer.path)==concurrent);
        const auto ui=Marketplace::Panel::Presentation();const auto error=std::find_if(ui.widgets.begin(),ui.widgets.end(),[](const auto& widget){return widget.name=="MarketEditNotice";});
        assert(error!=ui.widgets.end()&&error->visible&&!error->text.empty());
        writer.statusUntil=0;Marketplace::UI::NativeMenu(writer,native,fixtureCatalog);
        assert(!writer.priceSaveError.empty());
        // A later deliberate close still works; failed saves cannot trap the user in the panel.
        Marketplace::Panel::NativeMessage(domain,Marketplace::Panel::MessageHash("Close"));Marketplace::UI::NativeMenu(writer,native,fixtureCatalog);assert(!writer.open);
    }
    {Marketplace::UI::State publisher;publisher.path=folder/L"publication-check"/L"Marketplace_2.4_SC.json";std::filesystem::create_directories(publisher.path.parent_path());publisher.valid=true;publisher.settings.enabled=true;publisher.settings.page=11;
        const auto item=ReadItem(Unique(),Tables(),3);publisher.snapshot.deposit={item};publisher.snapshot.depositValid=true;publisher.liveDeposit=true;publisher.settings.drafts={{3,item.itemHex,{{"gld","Gold",2,-1,1000,-1,"gold"}}}};
        publisher.settings.simulation.enabled=true;publisher.settings.simulation.page=4;publisher.snapshot.seller={ReadItem(Rune(),Tables(),2)};publisher.snapshot.sellerValid=true;publisher.settings.simulation.drafts={{2,publisher.snapshot.seller[0].itemHex,{{"gld","Gold",2,-1,2000,-1,"gold"}}}};
        Marketplace::UI::Save(publisher,modName);Marketplace::UI::ExportPublication(publisher);Marketplace::UI::ExportPublication(publisher,true);
        const auto out=publisher.path.parent_path()/L"MarketplacePublish_2.4_SC.json";const auto original=ReadSettingsFile(out);const auto doc=Json::parse(original);assert(doc["items"].size()==1&&doc["source"]=="live"&&doc["custody"]=="local_save"&&doc["purchasable"]==false&&doc["items"][0]["saved_id"]==item.id);
        const auto testDoc=Json::parse(ReadSettingsFile(publisher.path.parent_path()/L"MarketplaceTestPublish_2.4_SC.json"));assert(testDoc["page"]==4&&testDoc["items"][0]["price"][0]["quantity"]==2000);
        Marketplace::UI::ExportPublication(publisher);assert(ReadSettingsFile(out)==original);
        Feed hosted;Listing own{"You","hosted-id","1",item,publisher.settings.drafts[0].price,false,true};own.sellerId="self-service";own.itemId="ledger";hosted.listings={own};AppendOwnListings(hosted,publisher.snapshot.deposit,publisher.settings,true);assert(hosted.listings.size()==1&&hosted.listings[0].id=="hosted-id");
        AppendOwnListings(hosted,{},publisher.settings,false,true);assert(hosted.listings.size()==1&&hosted.listings[0].id=="hosted-id");AppendOwnListings(hosted,publisher.snapshot.deposit,publisher.settings,true);assert(hosted.listings.size()==1&&hosted.listings[0].id=="hosted-id");
        publisher.settings.drafts[0].price[0].quantity=2000;AppendOwnListings(hosted,publisher.snapshot.deposit,publisher.settings,true);assert(hosted.listings.size()==1&&hosted.listings[0].sellerId.empty());
        publisher.snapshot.depositValid=false;Marketplace::UI::ExportPublication(publisher);assert(ReadSettingsFile(out)==original); // A failed scan must not withdraw live offers.
        auto canonical=doc;canonical["source"]="saved";canonical["id"]="companion-refresh";SaveSettingsFile(out,original,canonical.dump());
        Marketplace::UI::Save(publisher,modName);publisher.snapshot.depositValid=true;Marketplace::UI::ExportPublication(publisher);assert(Json::parse(ReadSettingsFile(out))["id"]!=doc["id"]);
        const auto lastPublication=ReadSettingsFile(out);const auto ownSettings=publisher.original;
        SaveSettingsFile(publisher.path,ownSettings,ownSettings+"\n");
        bool settingsConflict=false;try{Marketplace::UI::ExportPublication(publisher);}catch(const std::exception&){settingsConflict=true;}
        assert(settingsConflict&&ReadSettingsFile(out)==lastPublication);SaveSettingsFile(publisher.path,ownSettings+"\n",ownSettings);
        publisher.settings.enabled=false;Marketplace::UI::Save(publisher,modName);publisher.snapshot.depositValid=false;Marketplace::UI::ExportPublication(publisher);const auto disabled=Json::parse(ReadSettingsFile(out));assert(disabled["enabled"]==false&&disabled["items"].empty());
        auto expired=FeedFixture();expired["listings"][0]["expires_at"]=UnixNow()-1;assert(ReadFeed(expired,"MarketplaceFixture",false,Tables(),UnixNow()).listings.empty());}
    {
        Marketplace::UI::State mover;mover.path=folder/L"withdrawal"/L"Marketplace_2.4_SC.json";std::filesystem::create_directories(mover.path.parent_path());
        mover.valid=mover.localValid=true;mover.tables=Tables();mover.context=native;mover.settings.enabled=true;mover.settings.page=int(native.page);mover.settings.withdrawPage=2;
        auto item=ReadItem(Unique(),Tables(),3);mover.settings.drafts={{3,item.itemHex,{{"gld","Gold",2,-1,1000,-1,"gold"}}}};mover.snapshot.deposit={item};mover.snapshot.depositValid=true;
        Listing owned{"You","owned-move","1",item,mover.settings.drafts[0].price,false,true};owned.itemId=std::string(32,'a');mover.snapshot.feed.listings={owned};
        mover.local.commands.push_back({{"id",ActivityId()},{"type","propose"},{"listing_id","owned-move"}});mover.local.commands.push_back({{"id",ActivityId()},{"type","message"},{"offer_id","unrelated"}});
        Marketplace::UI::Save(mover,modName);Marketplace::UI::SaveLocal(mover);const auto original=ReadSettingsFile(mover.path);native.cursorOccupied=false;Marketplace::Panel::Publish(native);
        Marketplace::Panel::withdrawalRequest={native,item.itemHex,1};Marketplace::UI::UpdateWithdrawalPrompt(mover,native,fixtureCatalog);assert(mover.withdrawalConfirm&&mover.open);
        Marketplace::UI::Close(mover);assert(ReadSettingsFile(mover.path)==original&&mover.settings.drafts.size()==1&&mover.local.commands.size()==2);
        Marketplace::Panel::withdrawalRequest={native,item.itemHex,2};Marketplace::UI::UpdateWithdrawalPrompt(mover,native,fixtureCatalog);Marketplace::UI::WithdrawBeforeMove(mover);
        assert(!mover.withdrawalConfirm&&mover.settings.drafts.empty()&&ReadSettingsFile(mover.path)!=original);
        assert(mover.local.commands.size()==2&&mover.local.commands[0]["offer_id"]=="unrelated"&&mover.local.commands[1]["type"]=="withdraw_physical");
        assert(mover.local.commands[1]["item_id"]==std::string(32,'a'));assert(!std::filesystem::exists(mover.path.parent_path()/L"Stash_SC_Page17.d2i"));
    }
    {using namespace Marketplace;using namespace Marketplace::UI;
        native.owner=7;
        State filters;Listing listing;listing.id="trade-1";listing.sellerId="seller-1";listing.seller="Bonesy";listing.own=true;listing.otherOfferCount=2;
        assert(SocialMatches(filters,listing));filters.hideOwn=true;assert(!SocialMatches(filters,listing));filters.hideOwn=false;
        filters.hideOtherOffers=true;assert(!SocialMatches(filters,listing));filters.hideOtherOffers=false;
        filters.sellerSearch="BONE";assert(SocialMatches(filters,listing));filters.sellerSearch="other";assert(!SocialMatches(filters,listing));filters.sellerSearch.clear();
        filters.favoriteFilter=1;assert(!SocialMatches(filters,listing));filters.snapshot.feed.favorites.emplace("listing",listing.id);assert(SocialMatches(filters,listing));
        filters.favoriteFilter=2;assert(!SocialMatches(filters,listing));filters.snapshot.feed.favorites.emplace("seller",listing.sellerId);assert(SocialMatches(filters,listing));
        filters.sellerFilter="another-seller";assert(!SocialMatches(filters,listing));
        State profile;profile.path=folder/L"social"/L"profile-commands.json";std::filesystem::create_directories(profile.path.parent_path());profile.localValid=true;profile.snapshot.activity.self=std::string(32,'b');profile.snapshot.activity.name="Bonesy";profile.profileName="New Bonesy";
        assert(SocialCommand(profile,Panel::MessageHash("MarketSaveName")));assert(!profile.local.commands.empty()&&profile.local.commands.back()["expected_name"]=="Bonesy");
        const auto id=profile.local.commands.back()["id"].get<std::string>();profile.snapshot.activity.results[id]={{"name","New Bonesy"}};PollSocial(profile);assert(profile.socialPending.empty()&&profile.profileName=="New Bonesy");
        assert(profile.path.extension()==L".json");
        state.section=Section::Browse;state.open=true;state.editing=state.historyOpen=state.receiptOpen=state.counterOffer=false;state.snapshot.feed.listings.clear();
        listing.item=ReadItem(Unique(true),Tables(),1);listing.price={{"r33","Zod Rune",2,-1,40,-1}};state.snapshot.feed.listings.push_back(listing);state.marketSelection=0;state.ownSelection=-1;
        state.offerBoard=true;state.offerRequest.clear();state.offerPage={{"total",8},{"next_before",4},{"rows",Json::array()}};
        for(int n=0;n<4;++n)state.offerPage["rows"].push_back({{"alias","Trader ABCD1234"},{"status","pending"},{"from","bidder"},{"price",PriceJson(listing.price)}});
        frame();assert(presented("MarketOfferBoard").visible&&!presented("MarketItemDetail").visible);previewFrame("anonymous-offer-board");
        command("MarketBid0");assert(state.receiptOpen&&state.receiptMode==1);command("MarketBackReceipt");assert(state.offerBoard);
        CounterThread privateBid;privateBid.id=std::string(32,'d');privateBid.listing=listing.id;privateBid.itemId=std::string(32,'a');privateBid.itemHex=listing.item.itemHex;privateBid.buyer=std::string(32,'b');privateBid.seller=std::string(32,'c');privateBid.proposer=privateBid.buyer;privateBid.buyerName="Buyer";privateBid.sellerName="Seller";privateBid.status="pending";privateBid.revision=privateBid.listingRevision=1;privateBid.price=listing.price;
        state.snapshot.activity.self=privateBid.seller;state.snapshot.activity.expires=UnixNow()+3600;state.offerPage["rows"][0]["conversation"]=ThreadJson(privateBid);
        command("MarketBid0");assert(state.section==Section::Agreements&&!state.offerBoard&&Threads(state)[state.marketSelection].id==privateBid.id&&presented("MarketAcceptOfferButton").visible);
        state.section=Section::Browse;state.marketSelection=0;state.offerBoard=true;command("MarketBackOffers");assert(!state.offerBoard);
        state.marketSelection=-1;frame();previewFrame("social-browse-controls");command("MarketSetup");frame();previewFrame("profile-name-setup");
    }
    {using namespace Marketplace;using namespace Marketplace::UI;
        State gift;gift.path=folder/L"gifts"/L"gift.json";std::filesystem::create_directories(gift.path.parent_path());gift.localValid=true;gift.snapshot.activity.self=std::string(32,'b');
        Json funds={{"kind","gold"},{"quantity",100}};OpenTransfer(gift,nullptr,funds);
        const auto rev=gift.transferRevision;std::map<std::string,Panel::NativeInput> fields={{"MarketGiftRecipient",{"Recipient",rev}},{"MarketGiftAmount",{"101",rev}},{"MarketGiftNote",{"A gift",rev}}};
        assert(TransferCommand(gift,Panel::MessageHash("MarketGiftSubmit"),fields)&&gift.local.commands.empty());
        fields["MarketGiftAmount"].text="40";assert(TransferCommand(gift,Panel::MessageHash("MarketGiftSubmit"),fields));
        assert(gift.local.commands.size()==1&&gift.local.commands.back()["type"]=="gift_recipient"&&!gift.transferReview);
        gift.snapshot.activity.results[gift.transferResolve]={{"recipient_id",std::string(32,'c')},{"recipient_name","Recipient"}};PollTransfers(gift);assert(gift.transferReview);
        Panel::NativeView view;PresentTransfer(gift,view,fixtureCatalog);assert(std::any_of(view.widgets.begin(),view.widgets.end(),[](const auto& w){return w.name=="MarketGiftReview"&&w.visible&&w.text.find("40 x Gold")!=std::string::npos;}));
        assert(TransferCommand(gift,Panel::MessageHash("MarketGiftSubmit"),{}));const auto& packet=gift.local.commands.back();assert(packet["type"]=="gift_balances"&&packet["to"]==std::string(32,'c')&&packet["balances"][0]["quantity"]==40&&packet["message"]=="A gift");
        TransferCommand(gift,Panel::MessageHash("MarketGiftSubmit"),{});assert(gift.local.commands.size()==2); // Pending submission cannot be duplicated.
        gift.snapshot.activity.results[gift.transferPending]={{"error","Recipient is no longer available"}};PollTransfers(gift);assert(gift.status=="Recipient is no longer available");
        Close(gift);assert(!gift.transferOpen);assert(ActivityTerms(FreePrice())=="Free | no payment required");assert(IsFree(ReadPrice(PriceJson(FreePrice())))&&CanPay(FreePrice(),{}));
    }
    {using namespace Marketplace;using namespace Marketplace::UI;
        const auto before=state.snapshot.activity;const auto savedNamedItems=fixtureCatalog.namedItems;fixtureCatalog.namedItems[0].label=std::string(120,'W');const auto wasValid=state.valid;state.valid=false;state.snapshot.activity.gifts.clear();state.snapshot.activity.self=std::string(32,'b');state.localValid=true;
        GiftCampaign gift;gift.id=std::string(32,'a');gift.title=std::string(80,'W');gift.from="Community Owner";gift.announcement=std::string(512,'W');gift.claimable=true;gift.expires=UnixNow()+86400;
        for(int i=0;i<6;++i)gift.items.push_back(ReadItem(i%2?Rune():Unique(),Tables(),1));
        state.snapshot.activity.gifts={gift,gift};state.snapshot.activity.gifts[1].id=std::string(32,'c');state.snapshot.activity.gifts[1].title="A second community gift";
        state.refresh=false;state.socialPoll=UINT64_MAX;state.campaignId=gift.id;
        command("MarketCampaignOpen");assert(state.campaignOpen&&presented("MarketCampaignBadge").visible&&presented("MarketCampaignBadgeGlow").visible);previewFrame("community-gift-bundle");
        assert(Panel::Presentation().previews.size()==4);command("MarketCampaignItemsNext");assert(state.campaignItemOffset==4&&Panel::Presentation().previews.size()==2);previewFrame("community-gift-more-items");command("MarketCampaignItemsPrev");
        std::reverse(state.snapshot.activity.gifts.begin(),state.snapshot.activity.gifts.end());frame();assert(state.campaignId==gift.id);command("MarketCampaignNext");assert(state.campaignId!=gift.id);command("MarketCampaignPrev");assert(state.campaignId==gift.id);
        const auto count=state.local.commands.size();command("MarketCampaignClaim");assert(state.local.commands.size()==count+1&&!state.campaignPending.empty());const auto pending=state.campaignPending;command("MarketCampaignClaim");assert(state.local.commands.size()==count+1);previewFrame("community-gift-claiming");
        state.snapshot.activity.results[pending]={{"claimed",true},{"message",std::string(1800,'W')}};PollCampaigns(state);state.refresh=false;frame();assert(state.campaignPending.empty()&&state.receiptOpen&&presented("MarketReceiptPanel").visible);previewFrame("community-gift-receipt");
        command("MarketReceiptNext");assert(state.receiptPage==1);command("MarketBackReceipt");assert(state.campaignOpen&&!state.receiptOpen);
        for(auto& g:state.snapshot.activity.gifts){g.claimed=true;g.claimable=false;g.message="Thank you for playing!";}frame();assert(!presented("MarketCampaignBadgeGlow").visible);command("MarketCampaignClaim");assert(state.receiptOpen&&state.local.commands.size()==count+1);command("MarketBackReceipt");command("MarketCampaignBack");assert(!state.campaignOpen);
        command("MarketCampaignOpen");command("MarketBrowse");assert(!state.campaignOpen);state.snapshot.activity=before;fixtureCatalog.namedItems=savedNamedItems;state.valid=wasValid;state.campaignId.clear();
    }
    {using namespace Marketplace;using namespace Marketplace::UI;
        const auto before=state.snapshot.activity;const auto wasValid=state.valid;state.valid=false;state.snapshot.activity.self=std::string(32,'b');state.localValid=true;
        GiftCampaign gift;gift.id=std::string(32,'d');gift.title="A gift for Sanctuary";gift.from="Bonesy";gift.announcement="Thank you for playing RMD-MP! Claim your community gift.";gift.claimable=true;
        gift.items={ReadItem(Unique(),Tables(),1),ReadItem(Rune(),Tables(),1)};
        Requirement gold;gold.kind="gold";gold.code="gld";gold.label="Gold";gold.quantity=1000000;
        Requirement bag;bag.kind="bag";bag.code="Z01";bag.stat="RB_High";bag.label="High rune points";bag.quantity=3;gift.balances={gold,bag};
        state.snapshot.activity.gifts={gift};state.status.clear();state.statusUntil=0;state.refresh=false;state.socialPoll=UINT64_MAX;state.campaignId=gift.id;frame();previewFrame("community-gift-announcement");
        command("MarketCampaignOpen");assert(Panel::Presentation().previews.size()==4&&Panel::Presentation().previews[2].itemHex.empty()&&Panel::Presentation().previews[3].itemHex.empty());
        assert(presented("MarketCampaignItemFacts2").text.find("1000000")!=std::string::npos&&presented("MarketCampaignItemFacts3").text.find("3")!=std::string::npos);previewFrame("community-gift-resources");
        state.snapshot.activity.gifts[0].items.clear();frame();assert(Panel::Presentation().previews.size()==2&&Panel::Presentation().previews[0].itemHex.empty());previewFrame("community-gift-currency-only");
        command("MarketCampaignBack");state.snapshot.activity=before;state.valid=wasValid;state.campaignId.clear();
    }
    {
        const auto notice=Json::parse(ReadFile(L"tools/marketplace/layouts/stash-sync-mask.json"));
        assert(notice.at("type")=="Widget"&&!notice.at("fields").contains("color")&&notice.at("children").size()==2);
        const auto& backdrop=notice.at("children")[0].at("fields");const auto& label=notice.at("children")[1].at("fields");
        assert(backdrop.at("anchor").at("x")==.5&&backdrop.at("anchor").at("y")==.5&&backdrop.at("color")[3].get<double>()>.9);
        assert(label.at("anchor").at("y")==.5&&label.at("style").at("pointSize")==64&&label.at("style").at("fontColor")=="$FontColorGoldYellow");
    }
    // Cell glows are the only additional drawing on a closed marketplace.
    // Receipt loading is asynchronous; neither path creates an ImGui window.
    {
        auto borderSettings=state.settings;borderSettings.enabled=true;borderSettings.page=borderSettings.withdrawPage=17;borderSettings.drafts.clear();
        auto cellFrame=native;cellFrame.page=17;cellFrame.cursorOccupied=false;cellFrame.grid={.1f,.1f,.9f,.9f};
        Marketplace::Panel::Item cell;cell.unit=123;cell.seed=456;cell.tab=3;cell.rect={.2f,.2f,.3f,.4f};cell.bytes=Unique();cellFrame.cells={cell};
        const auto green=IM_COL32(100,180,110,115),red=IM_COL32(190,85,70,100);
        for(bool received:{true,false}){
            const auto dir=folder/(received?L"green-cells":L"red-cells");std::filesystem::create_directories(dir);
            SaveSettingsFile(dir/L"MarketplaceReceipts_2.4_SC.json","",Json{{"schema",1},{"mod",modName},{"hardcore",false},{"items",received?Json::array({Hex(Unique())}):Json::array()}}.dump());
            bool found=false;for(int retry=0;retry<100&&!found;++retry){
                ImGui::NewFrame();Marketplace::CellBorders::Draw(dir/L"Marketplace_2.4_SC.json",borderSettings,cellFrame,Tables(),false);ImGui::Render();
                bool edge=false,glow=false,clear=false;for(int list=0;list<ImGui::GetDrawData()->CmdListsCount;++list)for(const auto& vertex:ImGui::GetDrawData()->CmdLists[list]->VtxBuffer){
                    if(vertex.col==(received?green:red))edge=true;
                    if(vertex.col==(received?IM_COL32(85,190,95,125):IM_COL32(200,65,50,115)))glow=true;
                    if((vertex.col>>IM_COL32_A_SHIFT)==0)clear=true;
                }found=edge&&glow&&clear;
                if(!found)Sleep(5);
            }assert(found);
            borderSettings.drafts={{3,Hex(Unique()),{{"r33","Zod Rune",2,-1,1,-1}}}};
            ImGui::NewFrame();Marketplace::CellBorders::Draw(dir/L"Marketplace_2.4_SC.json",borderSettings,cellFrame,Tables(),false);ImGui::Render();assert(ImGui::GetDrawData()->TotalVtxCount==0);borderSettings.drafts.clear();
        }
    }
    ImGui::DestroyContext();
    {using namespace Marketplace;using namespace Marketplace::UI;State reset;reset.path=folder/L"reset-case"/L"Marketplace_2.4_SC.json";std::filesystem::create_directories(reset.path.parent_path());
        reset.valid=reset.open=true;reset.settings=state.settings;reset.settings.enabled=true;reset.settings.drafts={{3,Hex(Unique()),{{"r33","Zod Rune",2,-1,1,-1}}}};Save(reset,modName);
        auto cleared=reset.settings;cleared.enabled=false;cleared.drafts.clear();SaveSettingsFile(reset.path,reset.original,SettingsJson(cleared,modName,false).dump());
        const auto marker=reset.path.parent_path()/L"MarketplaceReset_2.4_SC.json";SaveSettingsFile(marker,"",Json{{"schema",1},{"game_mode","2.4"},{"mod",modName},{"hardcore",false},{"id",std::string(32,'f')}}.dump());
        PollMarketplaceReset(reset);assert(reset.open&&reset.valid&&!reset.settings.enabled&&reset.settings.drafts.empty()&&reset.resetId==std::string(32,'f'));
        const auto generation=reset.generation;reset.resetPoll=0;PollMarketplaceReset(reset);assert(reset.generation==generation);}
    if(argc==3){const auto actual=Json::parse(ReadFile(std::filesystem::path(argv[2])/"global/ui/layouts/bankexpansionlayouthd.json",4*1024*1024));bool hasShop=false,workspace=false;for(const auto& child:actual.at("children")){if(child.at("name")=="MarketplaceShop"){assert(child==shop);hasShop=true;}if(child.at("name")=="MarketplaceWorkspace"){assert(child==layout);workspace=true;}}assert(hasShop&&workspace);}
    state.open=state.editing=true;Panel::SuspendCapture();Marketplace::UI::DrawStash(false);assert(!state.open&&!state.editing&&!Panel::NativeOpen()); // Leaving gameplay still closes a suspended workspace.
    std::cout<<"JSON-native marketplace: four native tabs, control bounds/non-overlap, persistent details/history/conversations across reordered background scans, temporary capture suspension and explicit close, delivery-page guidance, stock close routing, zero ImGui vertices, same-tick Save/Close, price drafts/conflicts, counter-offers, agreements and compact setup passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
