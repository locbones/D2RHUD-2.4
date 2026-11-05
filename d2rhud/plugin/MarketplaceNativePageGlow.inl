// Read-only capture on native bank paint. Share only normalized coordinates;
// the existing stash-border renderer supplies the same green inward fade.
namespace Marketplace::NativePanel {
inline bool PagePopupSupported(){
    static const bool valid=[](){
        const std::pair<uint32_t,uint64_t> signatures[]{{0x5cbad0,0xef1759540334636aULL},{0x5cbff4,0x58ed0cb028ed95f6ULL},{0x158120,0xd155ab2566322a2eULL}};
        for(const auto& [rva,expected]:signatures){std::array<uint8_t,32> bytes{};
            if(!Read(reinterpret_cast<void*>(Pattern::Address(rva)),bytes))return false;
            uint64_t hash=14695981039346656037ULL;for(auto b:bytes)hash=(hash^b)*1099511628211ULL;
            if(hash!=expected)return false;
        }return WidgetLayoutSupported();
    }();return valid;
}
inline Panel::PageHighlight MarketPageHighlight(void* bank,int width,int height){
    Panel::PageHighlight result;
    const auto page=Panel::marketPage.load();
    if(!page || page>64 || width<=0 || height<=0 || Panel::NativeOpen() || Panel::standaloneMarket.load())return result;
    auto pages=WidgetFindChild(bank,"BankPages");
    // 2.4's active DropdownListWidget global, checked by its popup resolver.
    if(!pages || Field<void*>(reinterpret_cast<void*>(Pattern::Address(0x1d7c988)))!=pages || !ChoiceSupported(pages) || !PagePopupSupported())return result;
    // Resolve through the game's own popup getter (including its native type,
    // active-dropdown and visibility checks), rather than guessing a UI root.
    auto popup=reinterpret_cast<void*(__fastcall*)(void*)>(Pattern::Address(0x5cbad0))(pages);
    if(!popup || !Field<uint8_t>(popup,0x51))return result;
    auto list=Field<void*>(popup,0x1a0);
    if(!list || list!=WidgetFindChild(popup,"Items") || !Field<uint8_t>(list,0x51))return result;
    // Popup population addresses rows by the same state index in this container.
    auto content=Field<void*>(list,0xd8);const auto count=Field<uint64_t>(content,0x60);
    auto rows=Field<void*>(content,0x58);
    const auto choices=Field<int>(pages,0x1788);
    if(!rows || count>64 || page>count || choices<1 || choices>64 || page>unsigned(choices))return result;
    auto row=Field<void*>(rows,size_t(page-1)*sizeof(void*));
    if(!row || Field<void*>(row,0x30)!=content || !Field<uint8_t>(row,0x51))return result;
    auto bounds=[&](void* widget,Panel::Rect& rect){
        uint64_t position=0,size=0;
        using Getter=uint64_t(__fastcall*)(void*,uint64_t*);
        reinterpret_cast<Getter>(Pattern::Address(0x576f00))(widget,&position);
        reinterpret_cast<Getter>(Pattern::Address(0x158120))(widget,&size);
        const auto x=int32_t(position),y=int32_t(position>>32),w=int32_t(size),h=int32_t(size>>32);
        if(w<=0 || h<=0 || w>32768 || h>32768)return false;
        rect={float(x)/width,float(y)/height,(float(x)+w)/width,(float(y)+h)/height};return true;
    };
    if(!bounds(row,result.row) || !bounds(list,result.clip))return {};
    result.clip.left=(std::max)(0.f,result.clip.left);result.clip.top=(std::max)(0.f,result.clip.top);
    result.clip.right=(std::min)(1.f,result.clip.right);result.clip.bottom=(std::min)(1.f,result.clip.bottom);
    if((std::max)(result.row.left,result.clip.left)>=(std::min)(result.row.right,result.clip.right) ||
       (std::max)(result.row.top,result.clip.top)>=(std::min)(result.row.bottom,result.clip.bottom))return {};
    result.page=page;return result;
}
}
