// Included on the native UI path. Rendering changes never send panel lifecycle
// messages or change item/grid records. Re-resolve widgets before restoration.
namespace Marketplace::NativePanel {
inline void ShortcutVisuals(bool active){
    struct Saved {void* identity=nullptr;uint8_t draw=0;};
    static std::array<Saved,2> saved;
    const std::array<const char*,2> names{"PlayerInventoryExpansionLayout","PlayerInventoryOriginalLayout"};
    for(size_t n=0;n<names.size();++n){
        auto widget=gpPanelManager?WidgetFindChild(*gpPanelManager,names[n]):nullptr;
        auto& previous=saved[n];
        if(previous.identity!=widget)previous={}; // A replaced layout owns its own visibility.
        if(active&&widget){
            if(!previous.identity)previous={widget,Field<uint8_t>(widget,0x51)};
            Visibility(widget,false,false);
        }else if(previous.identity){Visibility(widget,previous.draw!=0,false);previous={};}
    }
}
}
static bool DrawStandaloneMarketplace(void* bank){
    using namespace Marketplace;
    if(!Panel::standaloneMarket.load())return false;
    // The verified Widget::Draw child loop calls vtable +0x18. Draw the actual
    // JSON subtree, including its own background, without bank/grid siblings.
    auto root=WidgetFindChild(bank,"MarketplaceWorkspace");
    if(root&&NativePanel::Field<uint8_t>(root,0x51)){
        const auto table=NativePanel::Field<void*>(root);
        const auto draw=NativePanel::Field<uintptr_t>(table,0x18);
        if(draw)reinterpret_cast<void(__fastcall*)(void*)>(draw)(root);
    }
    return true;
}
