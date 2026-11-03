// Included inside Marketplace::NativePanel, on the native paint thread.
inline bool KeyboardCodeSupported(){
    std::array<uint8_t,17> key{},character{};
    // InputTextBoxWidget::OnMessage selects KeyDown and reads a UTF-16
    // character at payload+0x10 for the native character-input verb.
    return Read(reinterpret_cast<void*>(Pattern::Address(0x58e482)),key)&&key==std::array<uint8_t,17>{
        0x48,0x8b,0x46,0x08,0x48,0xb9,0xd8,0xf9,0x6a,0x28,0x21,0x9a,0x6c,0x0c,0x48,0x3b,0xc1}&&
        Read(reinterpret_cast<void*>(Pattern::Address(0x58e4d6)),character)&&character[0]==0x41&&character[1]==0x0f&&character[2]==0xb7&&character[3]==0x46&&character[4]==0x10;
}
inline void ReplayKeyboard(void* root){
    auto events=Panel::TakeKeyboardEvents();
    if(events.empty()||!Panel::NativeOpen()||!VisibleIn(root,root)||!WidgetLayoutSupported()||!KeyboardCodeSupported())return;
    std::array<BYTE,256> previous{};if(!GetKeyboardState(previous.data()))return;
    struct Restore {std::array<BYTE,256>& state;~Restore(){SetKeyboardState(state.data());}} restore{previous};
    for(auto& event:events){
        // Native Ctrl/Shift editing uses thread-local GetKeyState. Restore the
        // captured state while dispatching only to this native subtree.
        if(!SetKeyboardState(event.state.data()))continue;
        const bool down=event.message==WM_KEYDOWN||event.message==WM_SYSKEYDOWN;
        const bool up=event.message==WM_KEYUP||event.message==WM_SYSKEYUP;
        const auto verb=down?Panel::MessageHash("KeyDown"):up?Panel::MessageHash("KeyUp"):0xb6b6f0a8c4aa4214ULL;
        // Native keyboard payloads carry VK / UTF-16 at +0x10. Mapped
        // gameplay actions never enter this private replay path.
        struct Payload {std::array<uint8_t,16> reserved{};uint32_t key=0;std::array<uint8_t,44> tail{};} payload;
        payload.key=event.key;
        struct Message {uint64_t domain,verb;void* payload;uint64_t unused[2]{};} message{Panel::MessageHash("InputMessage"),verb,&payload};
        reinterpret_cast<void(__fastcall*)(void*,void*)>(Pattern::Address(0x576960))(root,&message);
    }
}
