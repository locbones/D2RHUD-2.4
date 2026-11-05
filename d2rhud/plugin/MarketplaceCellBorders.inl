// Minimal cell outlines; native stash widgets retain drawing, hit tests and tooltips.
// Disk reads happen on one background poll while the market page is visible.
namespace Marketplace::CellBorders {
inline std::string Key(const SavedItem& item){return SavedIdentity(LegacyItemHex(item.itemHex,item.children));}
struct Manifest {std::set<std::string> delivered;};
inline Manifest Read(const std::filesystem::path& path,const SaveTables& tables,const std::string& mod,bool hc){
    Manifest result;if(!std::filesystem::exists(path))return result;
    const auto raw=ReadFile(path,16*1024*1024);const auto value=Json::parse(raw.begin(),raw.end());
    if(value.at("schema")!=1||value.at("mod")!=mod||value.at("hardcore")!=hc||!value.at("items").is_array()||value.at("items").size()>4096)throw std::runtime_error("Invalid stash delivery markers.");
    for(const auto& hex:value.at("items"))result.delivered.insert(Key(ReadItem(Unhex(hex.get<std::string>()),tables,1)));return result;
}
inline void Border(ImDrawList* draw,ImVec2 a,ImVec2 b,bool green){
    if(b.x<=a.x || b.y<=a.y)return;
    // A broad inward fade keeps content visible and stays within each row
    // or item cell. Four quads have constant vertex cost.
    const float extent=(std::min)(b.x-a.x,b.y-a.y);const float band=(std::min)(extent*.45f,(std::min)(14.f,(std::max)(6.f,extent*.18f)));
    const auto glow=green?IM_COL32(85,190,95,125):IM_COL32(200,65,50,115);
    const auto clear=green?IM_COL32(85,190,95,0):IM_COL32(200,65,50,0);
    const auto edge=green?IM_COL32(100,180,110,115):IM_COL32(190,85,70,100);
    draw->AddRectFilledMultiColor(a,{b.x,a.y+band},glow,glow,clear,clear);
    draw->AddRectFilledMultiColor({a.x,b.y-band},b,clear,clear,glow,glow);
    draw->AddRectFilledMultiColor({a.x,a.y+band},{a.x+band,b.y-band},glow,clear,clear,glow);
    draw->AddRectFilledMultiColor({b.x-band,a.y+band},{b.x,b.y-band},clear,glow,glow,clear);
    draw->AddRect(a,b,edge,0,0,1.5f);
}
inline void MarketPage(const Settings& settings,const Panel::Frame& view,bool marketOpen){
    const auto& choice=view.marketChoice;
    if(!settings.enabled || marketOpen || view.suspended || !choice.page || choice.page!=unsigned(settings.page))return;
    const auto size=ImGui::GetIO().DisplaySize;auto draw=ImGui::GetBackgroundDrawList();
    const auto& clip=choice.clip;const auto& row=choice.row;
    if(clip.right<=clip.left || clip.bottom<=clip.top || row.right<=row.left || row.bottom<=row.top)return;
    draw->PushClipRect({clip.left*size.x,clip.top*size.y},{clip.right*size.x,clip.bottom*size.y},true);
    Border(draw,{row.left*size.x+1,row.top*size.y+1},{row.right*size.x-1,row.bottom*size.y-1},true);
    draw->PopClipRect();
}
inline void Draw(const std::filesystem::path& settingsPath,const Settings& settings,const Panel::Frame& view,const SaveTables& tables,bool marketOpen){
    Panel::marketPage=settings.enabled?unsigned(settings.page):0;
    MarketPage(settings,view,marketOpen);
    if(!settings.enabled||marketOpen||view.page!=unsigned(settings.page)||view.cursorOccupied||view.suspended||tables.items.empty())return;
    struct Proof {std::vector<uint8_t> bytes;std::string key;};
    static std::filesystem::path path;static std::future<Manifest> pending;static Manifest markers;
    static uint64_t poll=0;static std::map<uint64_t,Proof> proofs;static bool known=false;
    const auto target=settingsPath.parent_path()/(view.hardcore?L"MarketplaceReceipts_2.4_HC.json":L"MarketplaceReceipts_2.4_SC.json");
    if(path!=target){if(pending.valid()&&pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;if(pending.valid())try{pending.get();}catch(...){}path=target;markers={};proofs.clear();poll=0;known=false;}
    if(pending.valid()&&pending.wait_for(std::chrono::seconds(0))==std::future_status::ready){try{markers=pending.get();known=true;}catch(...){known=false;}}
    const auto now=GetTickCount64();if(now>=poll&&!pending.valid()){
        poll=now+2000;const auto mod=modName;const auto hc=view.hardcore;
        pending=std::async(std::launch::async,[target,tables,mod,hc](){return Read(target,tables,mod,hc);});
    }
    if(!known)return;
    if(proofs.size()>1024)proofs.clear();
    static std::vector<std::pair<unsigned,std::string>> draftKeys;static std::set<std::string> priced;
    const bool changed=draftKeys.size()!=settings.drafts.size()||!std::equal(draftKeys.begin(),draftKeys.end(),settings.drafts.begin(),[](const auto& cached,const auto& draft){return cached.first==draft.tab&&cached.second==draft.itemHex;});
    if(changed){draftKeys.clear();priced.clear();for(const auto& draft:settings.drafts){draftKeys.emplace_back(draft.tab,draft.itemHex);try{priced.insert(Key(ReadItem(Unhex(draft.itemHex),tables,draft.tab)));}catch(...){}}}
    std::map<std::string,unsigned> occurrences;
    for(const auto& cell:view.cells){if(cell.bytes.empty())continue;const auto id=(uint64_t(cell.unit)<<32)|cell.seed;auto& proof=proofs[id];
        if(proof.bytes!=cell.bytes){proof.bytes=cell.bytes;proof.key.clear();try{proof.key=Key(ReadItem(cell.bytes,tables,cell.tab));}catch(...){}}
        if(!proof.key.empty())++occurrences[proof.key];
    }
    const auto size=ImGui::GetIO().DisplaySize;auto draw=ImGui::GetBackgroundDrawList();
    draw->PushClipRect({view.grid.left*size.x,view.grid.top*size.y},{view.grid.right*size.x,view.grid.bottom*size.y},true);
    for(const auto& cell:view.cells){const auto proof=proofs.find((uint64_t(cell.unit)<<32)|cell.seed);if(proof==proofs.end()||proof->second.key.empty()||priced.contains(proof->second.key))continue;
        const bool green=occurrences[proof->second.key]==1&&markers.delivered.contains(proof->second.key);
        const auto& r=cell.rect;const ImVec2 a{r.left*size.x+1,r.top*size.y+1},b{r.right*size.x-1,r.bottom*size.y-1};
        Border(draw,a,b,green);
    }draw->PopClipRect();
}
}
