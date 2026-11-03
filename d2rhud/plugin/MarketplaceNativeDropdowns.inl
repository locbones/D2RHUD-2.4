// Include after MarketplaceNative.inl's Read/Field helpers. These bindings run
// on the bank-panel paint thread, alongside the game's own native widgets.
#include <map>
#include <optional>

namespace Marketplace::NativePanel {
struct ChoiceResult {int selection=0;uint64_t revision=0;};

inline bool ChoiceCodeSupported(){
    // 2.4 DropdownListWidget::SetSelection reads a 0x28-byte state record,
    // updates its native caption, and stores the selected index. Calling the
    // setter keeps caption/popup selection synchronized; writing +0x178c alone
    // leaves the old caption visible. Guard the native state layout and store.
    std::array<uint8_t,20> states{};std::array<uint8_t,6> selected{},count{};
    return Read(reinterpret_cast<void*>(Pattern::Address(0x5cba36)),states)&&
        states==std::array<uint8_t,20>{0x48,0x8d,0x0c,0xb6,0x48,0x8b,0x94,0xcf,0x88,0x0d,0,0,0x4c,0x8b,0x84,0xcf,0x90,0x0d,0,0}&&
        Read(reinterpret_cast<void*>(Pattern::Address(0x5cbab3)),selected)&&selected==std::array<uint8_t,6>{0x89,0xb7,0x8c,0x17,0,0}&&
        Read(reinterpret_cast<void*>(Pattern::Address(0x5cb9f3)),count)&&count==std::array<uint8_t,6>{0x3b,0xb1,0x88,0x17,0,0};
}

inline bool ChoiceSupported(void* widget){
    const auto table=Field<void*>(widget);
    return table&&reinterpret_cast<uintptr_t>(table)==Pattern::Address(0x15b4038)&&
        Field<uintptr_t>(table,0x20)==Pattern::Address(0x5cb410)&&ChoiceCodeSupported();
}

inline std::optional<ChoiceResult> BindChoice(void* widget,int selection,uint64_t revision){
    if(!ChoiceSupported(widget))return {};
    const auto count=Field<int>(widget,0x1788);
    if(count<1||count>64||selection<0||selection>=count)return {};
    struct Binding {uint64_t revision=0;int count=0;bool initialized=false;};
    static std::map<void*,Binding> bindings;
    // Panel replacements retain no game pointers beyond these identity keys.
    // Bound the cache even if a session repeatedly constructs new panels.
    if(!bindings.contains(widget)&&bindings.size()>=128)bindings.clear();
    auto& binding=bindings[widget];
    if(!binding.initialized||binding.count!=count||binding.revision!=revision){
        if(Field<int>(widget,0x178c)!=selection){
            const auto setter=Pattern::Address(0x5cb9d0);if(!setter)return {};
            reinterpret_cast<void(__fastcall*)(void*,int)>(setter)(widget,selection);
            if(Field<int>(widget,0x178c)!=selection)return {};
        }
        binding={revision,count,true};
    }
    const auto current=Field<int>(widget,0x178c);
    if(current<0||current>=count)return {};
    // With the same revision, preserve native user selections. The model reads
    // this acknowledgement and issues a new revision only for external resets.
    return ChoiceResult{current,binding.revision};
}
}
