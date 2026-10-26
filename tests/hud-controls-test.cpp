#define NOMINMAX
#include "../d2rhud/plugin/HUDControls.cpp"
#include <cassert>
#include <iostream>
#include <map>
#include <vector>

std::unordered_map<std::string, short> keyMap{{"VK_SPACE", short(VK_SPACE)}, {"VK_LBUTTON", short(VK_LBUTTON)}, {"VK_RBUTTON", short(VK_RBUTTON)}, {"VK_MBUTTON", short(VK_MBUTTON)}};
uintptr_t testImage = 0;
std::map<uint32_t, uintptr_t> functions;
DWORD64 Pattern::Address(uint32_t rva) {
    if (auto found = functions.find(rva); found != functions.end()) return found->second;
    assert(testImage);
    return testImage + rva;
}
using namespace HUDControls;

int rebuilds = 0, messages = 0, shown = 0, hidden = 0, cancelled = 0;
int assignedAction = -1, assignedPrimary = -1, namedAction = -1;
std::vector<std::vector<int>> iterated;
std::array<std::array<uint8_t,0x600>,3> nativeRows;
std::vector<int> nativeActions;
size_t createdRows=0;
bool rejectAssignment = false;
void* __fastcall CreateNativeRow(void*) {
    assert(createdRows<nativeRows.size());
    auto& row=nativeRows[createdRows++];row.fill(0);
    Put(row.data(),0x7c,int(50));
    return row.data();
}
void __fastcall ConfigureNativeRow(void* row,void*,int index,int action) {
    assert(index==int(nativeActions.size()));nativeActions.push_back(action);
    Put(row,0x568,action);
}
void __fastcall Rebuild() { ++rebuilds; }
void* __fastcall Row(void* value) { return value; }
void __fastcall Sound(int, int, int, int, bool) {}
void __fastcall Populate(void*) {}
void __fastcall Show(void*) { ++shown; *Native<Bindings*>(BackupBindings) = Current(); }
void __fastcall Hide(void*) { ++hidden; }
void __fastcall Cancel(void*) { ++cancelled; Current() = *Native<Bindings*>(BackupBindings); }
void __fastcall FakeMessage(void*, const void*) { ++messages; }
Binding* __fastcall DefaultBindings(int) { return Current().data(); }
void __fastcall LoadBindings() {}
void* __fastcall FakeIterate(void* out, const int* begin, const int* end, void*) {
    iterated.emplace_back(begin, end);
    return out;
}
const char* __fastcall FakeName(int action) { namedAction = action; return "native action"; }
const char* __fastcall FakeKeyName(int action, int primary, bool shorthand) {
    namedAction = action;
    if (action == Action) {
        assert(Current()[0].action == Action && Current()[0].key == store.editing.primary && Current()[0].primary == 1);
        assert(Current()[1].action == Action && Current()[1].key == store.editing.secondary && Current()[1].primary == 0);
        assert(primary == 0 && shorthand);
    }
    return "localized native key";
}
void* __fastcall FakeColor(void* widget, bool) { return widget; }
bool __fastcall FakeAssign(int action, int primary, uint16_t key, const char** error) {
    assignedAction = action;
    assignedPrimary = primary;
    if (rejectAssignment) {
        Current()[18].key = 0x1234;
        memset(Native<void*>(0x184d050), 0x55, 9);
        if (error) *error = "native validation error";
        return false;
    }
    Binding* target = nullptr;
    for (auto& binding : Current()) {
        if (key != Unbound && binding.key == key) binding.key = Unbound;
        if (binding.action == uint32_t(action) && binding.primary == uint32_t(primary)) target = &binding;
    }
    assert(target);
    target->key = key;
    return true;
}

void ResetBindings() {
    for (size_t n = 0; n < Current().size(); ++n) Current()[n] = {uint32_t(n / 2), Unbound, n % 2 == 0 ? 1u : 0u};
    Current()[0].key = 'A'; Current()[1].key = 'C'; Current()[8].key = 'Q';
    *Native<Bindings*>(BackupBindings) = Current();
}
void Fixture(const std::filesystem::path& root) {
    testImage = uintptr_t(VirtualAlloc(nullptr, 0x1e00000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(testImage);
    functions[0x11b260] = uintptr_t(Rebuild);
    functions[0xbedd50] = uintptr_t(Row);
    functions[0xe49e0] = uintptr_t(Sound);
    originalPopulate = Populate; originalShow = Show; originalHide = Hide; originalCancel = Cancel;
    originalIterate = FakeIterate; originalName = FakeName; originalKeyName = FakeKeyName; originalColor = FakeColor;
    originalAssign = FakeAssign; originalMessage = FakeMessage; originalDefaults = DefaultBindings; originalLoad = LoadBindings;
    std::filesystem::create_directories(root);
    storage = root / L"bridge-controls.json";
    std::error_code error; std::filesystem::remove(storage, error);
    memcpy(Native<void*>(0x1bf084f), "RMD-MP", 7);
    Put(Native<void*>(0x1d442d8), 0, uint32_t(2));
    memcpy(Native<void*>(0x1c9b200 + 2 * 62), "Test Hero", 10);
    assert(Scope() == "RMD-MP|Test Hero");
    ResetBindings(); fallback = Keys{}; scope.clear(); store = Store{};
    Sync();
    assert(store.saved == Keys{} && rebuilds == 1 && !std::filesystem::exists(storage));
}
void EnumerationAndNames() {
    iterated.clear();
    auto party = Native<const int*>(0x1509cf0);
    int out = 0;
    assert(IterateHook(&out, party, party + 2, nullptr) == &out);
    assert(iterated.size() == 2 && iterated[1] == std::vector<int>({NoAction, Action}));
    iterated.clear();
    const int unrelated[]{1,2};
    IterateHook(&out, unrelated, unrelated + 2, nullptr);
    assert(iterated.size() == 1);
    iterated.clear(); IterateHook(&out, party, party + 1, nullptr); assert(iterated.size() == 1);
    assert(std::string(NameHook(Action)) == "Open HUD Menu");
    assert(std::string(NameHook(14)) == "native action" && namedAction == 14);
    const auto before = Current();
    store.editing = Keys{0x2041, 0x4002};
    assert(std::string(KeyNameHook(Action, 0, true)) == "localized native key");
    assert(!memcmp(before.data(), Current().data(), sizeof(before)));
    assert(std::string(KeyNameHook(14, 1, false)) == "localized native key" && namedAction == 14);
    std::cout << "Native list insertion and localized names preserve native descriptors and unrelated rows.\n";
}
void Assignment() {
    ResetBindings();
    store.editing = Keys{VK_F12, Unbound};
    const auto originalCharacter = std::array<Binding,2>{Current()[0], Current()[1]};
    assert(AssignHook(Action, 1, 'Q', nullptr));
    assert(assignedAction == 0 && assignedPrimary == 1);
    assert((store.editing == Keys{'Q', Unbound}));
    assert(!memcmp(originalCharacter.data(), Current().data(), sizeof(originalCharacter)));
    assert(Current()[8].key == Unbound);
    assert(AssignHook(Action, 0, 'Z', nullptr));
    assert((store.editing == Keys{'Q', 'Z'}));
    assert(!memcmp(originalCharacter.data(), Current().data(), sizeof(originalCharacter)));
    assert(AssignHook(Action, 1, 'A', nullptr));
    assert(Current()[0].key == Unbound && Current()[1].key == 'C');
    const auto before = Current(); const auto keys = store.editing;
    std::array<uint8_t,9> modifierBytes{1,2,3,4,5,6,7,8,9};
    Put(Native<void*>(0x184d050), 0, modifierBytes);
    rejectAssignment = true; const char* error = nullptr;
    assert(!AssignHook(Action, 0, 0x1234, &error));
    assert(std::string(error) == "native validation error" && store.editing == keys);
    assert(!memcmp(before.data(), Current().data(), sizeof(before)));
    assert((Field<std::array<uint8_t,9>>(Native<void*>(0x184d050)) == modifierBytes));
    rejectAssignment = false;
    store.editing = Keys{'Z','Z'};
    assert(AssignHook(4, 1, 'Z', nullptr));
    assert((store.editing == Keys{Unbound,Unbound}) && Current()[8].key == 'Z');
    store.editing = Keys{'X','Y'};
    assert(AssignHook(4, 1, Unbound, nullptr) && (store.editing == Keys{'X','Y'}));
    std::cout << "Custom assignment uses native validation/conflict resolution, restores borrowed slots and rolls failed assignments back.\n";
}
void Lifecycle() {
    std::array<uint8_t,0x600> panel{};
    store.saved = store.editing = Keys{};
    ShowHook(panel.data()); assert(visible && shown == 1 && controls == panel.data());
    assert(AssignHook(Action,1,'Q',nullptr));
    CancelHook(panel.data()); assert(cancelled == 1 && store.editing == store.saved);
    const std::array<uint64_t,3> accept{Hash("OptionsPanelMessage"), Hash("AcceptChanges"), 0};
    store.editing = Keys{0x2041,Unbound};
    MessageHook(panel.data(),accept.data());
    assert(messages == 1 && store.saved == store.editing && !pendingSave);
    Store disk; assert(disk.Load(storage,Scope()) && disk.saved == store.saved);
    Current()[22].key=VK_F12;
    const auto beforeDefaults=Current();
    store.editing = Keys{'B','C'}; const auto defaults=DefaultsHook(0);
    assert(defaults && defaults!=Current().data() && defaults[22].key==Unbound);
    assert(!memcmp(beforeDefaults.data(),Current().data(),sizeof(beforeDefaults)));
    for(size_t n=0;n<Current().size();++n){
        const auto expected=beforeDefaults[n].key==VK_F12?Unbound:beforeDefaults[n].key;
        assert(defaults[n].action==beforeDefaults[n].action && defaults[n].primary==beforeDefaults[n].primary && defaults[n].key==expected);
    }
    assert(store.editing == Keys{} && store.saved.primary == 0x2041);
    CancelHook(panel.data()); assert(store.editing == store.saved);
    HideHook(panel.data()); assert(!visible && hidden == 1);
    store.editing = Keys{'B','C'}; DefaultsHook(1); assert((store.editing == Keys{'B','C'}));
    std::cout << "Controls show/cancel/accept/defaults follow native lifecycle and persist only accepted custom bindings.\n";
}
void CustomDefaults() {
    std::array<uint8_t,0x600> panel{};
    ShowHook(panel.data());
    const auto currentKeys=store.saved;
    const Keys custom{VK_F10,uint16_t(0x2000|'K')};
    store.editing=custom;
    const std::array<uint64_t,3> save{Hash("KeyBindingPanelMessage"),Hash("SaveCustomDefaultsConfirm"),0};
    MessageHook(panel.data(),save.data());
    assert(store.saved==currentKeys && store.editing==custom && !pendingSave);
    Store disk;assert(disk.Load(storage,"RMD-MP|Test Hero") && disk.saved==currentKeys);
    assert(disk.Load(storage,"RMD-MP|@custom-defaults") && disk.saved==custom);
    Current()[30].key=custom.primary;Current()[32].key=custom.secondary;
    const auto nativeDefaults=Current();
    store.editing=Keys{'B','C'};
    const auto defaults=DefaultsHook(2);
    assert(defaults!=Current().data() && store.editing==custom && store.saved==currentKeys);
    assert(defaults[30].key==Unbound && defaults[32].key==Unbound);
    assert(!memcmp(nativeDefaults.data(),Current().data(),sizeof(nativeDefaults)));
    HideHook(panel.data());

    // Native Custom.key and the HUD part of that preset share mod scope.
    memset(Native<void*>(0x1c9b200+2*62),0,62);
    memcpy(Native<void*>(0x1c9b200+2*62),"Other Hero",11);
    Sync(true);ShowHook(panel.data());store.editing=Keys{'X','Y'};
    DefaultsHook(2);assert(store.editing==custom && store.saved==Keys{});
    const std::array<uint64_t,3> erase{Hash("KeyBindingPanelMessage"),Hash("DeleteCustomDefaultsConfirm"),0};
    MessageHook(panel.data(),erase.data());
    assert(store.editing==custom && store.saved==Keys{});
    store.editing=Keys{'X','Y'};DefaultsHook(2);assert(store.editing==Keys{});
    HideHook(panel.data());
    memset(Native<void*>(0x1c9b200+2*62),0,62);
    memcpy(Native<void*>(0x1c9b200+2*62),"Test Hero",10);Sync(true);
    assert(store.saved==currentKeys);
    Store reloaded;assert(reloaded.Load(storage,"RMD-MP|Test Hero") && reloaded.saved==currentKeys);
    assert(reloaded.Load(storage,"RMD-MP|@custom-defaults") && reloaded.saved==Keys{});
    std::cout << "Custom defaults preserve accepted character keys, share within a mod, and filter native conflicts without changing built-in defaults.\n";
}
void ColorsAndDelete() {
    std::array<uint8_t,0x600> panel{}, widget{};
    std::array<uint8_t,0x160> list{}; std::array<uint8_t,0x80> data{}; void* items[]{widget.data()};
    Put(panel.data(),0x188,list.data()); Put(list.data(),0x148,int(0)); Put(list.data(),0xd8,data.data());
    Put(data.data(),0x58,static_cast<void*>(items)); Put(data.data(),0x60,size_t(1));
    Put(widget.data(),0x568,Action); Put(widget.data(),0x570,panel.data());
    Put(panel.data(),0x194,NoAction); Put(panel.data(),0x190,true);
    store.editing = Keys{'A',Unbound};
    assert(ColorHook(widget.data(),true) == widget.data()+0x520);
    assert(ColorHook(widget.data(),false) == widget.data()+0x540);
    Put(list.data(),0x148,int(-1));
    assert(ColorHook(widget.data(),true) == widget.data()+0x530);
    store.editing = Keys{Unbound,Unbound}; assert(ColorHook(widget.data(),true) == widget.data()+0x550);
    Put(list.data(),0x148,int(0)); store.editing.primary='A';
    std::array<uint8_t,24> payload{}; Put(payload.data(),16,int(VK_DELETE));
    struct InputMessage {uint64_t type,verb;void* payload;} input{Hash("InputMessage"),Hash("KeyDown"),payload.data()};
    const auto calls=messages; MessageHook(panel.data(),&input);
    assert(store.editing.primary==Unbound && Field<bool>(panel.data(),0x1aa) && messages==calls);
    Put(widget.data(),0x568,int(14)); assert(ColorHook(widget.data(),true)==widget.data());
    std::cout << "Native selection, bound/unbound colors and Delete clearing support the custom row.\n";
}
void Input() {
    installed=true; visible=false; held=Unbound; toggle=false; store.saved=Keys{};
    assert(!HandleInput(WM_KEYDOWN,VK_F11,0,false));
    assert(HandleInput(WM_KEYDOWN,VK_F12,0,false)); assert(TakeToggle()&&!TakeToggle());
    assert(HandleInput(WM_KEYDOWN,VK_F12,LPARAM(1)<<30,false) && !TakeToggle());
    assert(HandleInput(WM_KEYUP,VK_F12,0,false) && !HandleInput(WM_KEYUP,VK_F12,0,false));
    visible=true; assert(!HandleInput(WM_KEYDOWN,VK_F12,0,false)); visible=false;
    assert(!HandleInput(WM_KEYDOWN,VK_F12,0,true));
    Put(Native<void*>(0x1d53fb8+5),0,uint8_t(1)); assert(!HandleInput(WM_KEYDOWN,VK_F12,0,false));
    Put(Native<void*>(0x1d53fb8+5),0,uint8_t(0));
    assert(!TakeToggle());
    BYTE keyboard[256]{};assert(GetKeyboardState(keyboard));
    BYTE modified[256];memcpy(modified,keyboard,sizeof(modified));
    modified[VK_CONTROL]=0x80;assert(SetKeyboardState(modified));
    store.saved=Keys{uint16_t(0x2000|VK_F12),Unbound};
    assert(HandleInput(WM_KEYDOWN,VK_F12,0,false)&&TakeToggle());
    modified[VK_CONTROL]=0;assert(SetKeyboardState(modified));
    assert(HandleInput(WM_KEYDOWN,VK_F12,LPARAM(1)<<30,false)&&!TakeToggle());
    assert(HandleInput(WM_KEYUP,VK_F12,0,false));assert(SetKeyboardState(keyboard));
    store.saved=Keys{Unbound,0x102};
    assert(HandleInput(WM_MBUTTONDOWN,0,0,false)&&TakeToggle()); assert(HandleInput(WM_MBUTTONUP,0,0,false));
    store.saved=Keys{0x103,Unbound};
    assert(HandleInput(WM_MOUSEWHEEL,WPARAM(WHEEL_DELTA)<<16,0,false)&&TakeToggle());
    installed=false; assert(!HandleInput(WM_KEYDOWN,VK_F12,0,false));
    assert(Legacy("CTRL + SHIFT + F12")==0x307b && Legacy("ALT+VK_SPACE")==0x4020);
    assert(Legacy("VK_MBUTTON")==0x102 && Legacy("")==Unbound && !Legacy("F25") && !Legacy("F12+A"));
    std::cout << "HUD input consumes down/repeat/up once, supports mouse bindings and respects Controls/text input blocking.\n";
}
void Executable(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate); assert(file);
    std::vector<uint8_t> bytes(size_t(file.tellg())); file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));assert(file.good());
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());assert(dos->e_magic==IMAGE_DOS_SIGNATURE);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data()+dos->e_lfanew);assert(nt->Signature==IMAGE_NT_SIGNATURE);
    const auto previous=testImage;
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
    for(auto section=IMAGE_FIRST_SECTION(nt);section<IMAGE_FIRST_SECTION(nt)+nt->FileHeader.NumberOfSections;++section)
        memcpy(image+section->VirtualAddress,bytes.data()+section->PointerToRawData,section->SizeOfRawData);
    testImage=uintptr_t(image); assert(Supported());
    for(const auto signature:signatures){image[signature.rva]^=1;assert(!Supported());image[signature.rva]^=1;}
    assert(Supported());
    // Execute the game's unmodified validator in this private image. The F9
    // path uses only its own key table and binding globals, with no stubs.
    ResetBindings(); Current()[16].key=VK_F9;
    const std::array<Binding,2> character{Current()[0],Current()[1]};
    store.editing=Keys{};
    const auto fakeAssign=originalAssign;
    originalAssign=Native<HUDControls::Assign>(0x11c7a0);
    const char* error=nullptr;
    assert(AssignHook(Action,1,VK_F9,&error));
    assert(!error && (store.editing==Keys{VK_F9,Unbound}));
    assert(!memcmp(character.data(),Current().data(),sizeof(character)) && Current()[16].key==Unbound);
    assert(AssignHook(Action,0,uint16_t(0x2000|VK_F10),&error));
    assert(!error && (store.editing==Keys{VK_F9,uint16_t(0x2000|VK_F10)}));
    assert(!memcmp(character.data(),Current().data(),sizeof(character)));
    const auto before=Current();const auto beforeKeys=store.editing;
    const auto beforeModifiers=Field<std::array<uint8_t,9>>(Native<void*>(0x184d050));
    assert(!AssignHook(Action,1,VK_ESCAPE,&error));
    assert(error && std::string(error)=="CantAssignKey" && store.editing==beforeKeys);
    assert(!memcmp(before.data(),Current().data(),sizeof(before)));
    assert((Field<std::array<uint8_t,9>>(Native<void*>(0x184d050))==beforeModifiers));
    originalAssign=fakeAssign;

    // Run the actual iterator too; only widget creation/casting/configuration
    // are replaced, so its gap handling and action61 flow remain native.
    auto stub=[&](uint32_t rva,auto function){
        const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,sizeof(jump));
        const auto address=uintptr_t(function);memcpy(image+rva+sizeof(jump),&address,sizeof(address));
    };
    stub(0x58aeb0,CreateNativeRow);stub(0xbedd50,Row);stub(0xbf1070,ConfigureNativeRow);
    const auto fakeIterate=originalIterate;originalIterate=Native<HUDControls::Iterate>(0xbede00);
    std::array<uint8_t,0x1b0> panel{};
    int gaps=0,rowCount=0;std::array<void*,3> capture{&gaps,panel.data(),&rowCount},out{};
    const auto party=Native<const int*>(0x1509cf0);assert(party[0]==57 && party[1]==58);
    nativeActions.clear();createdRows=0;
    assert(IterateHook(out.data(),party,party+2,capture.data())==out.data());
    assert(nativeActions==std::vector<int>({57,58,Action}) && gaps==1 && rowCount==3 && createdRows==3);
    assert(Field<int>(nativeRows[0].data(),0x74)==0 && Field<int>(nativeRows[1].data(),0x74)==50 && Field<int>(nativeRows[2].data(),0x74)==150);
    assert(Field<int>(nativeRows[2].data(),0x568)==Action && out==capture);
    originalIterate=fakeIterate;
    testImage=previous;VirtualFree(image,0,MEM_RELEASE);
    std::cout << "Private game signatures, actual row iterator and native validator pass: gap/action61, conflict clearing, secondary modifier binding, reserved-key rejection and Character key preservation.\n";
}
int wmain(int argc,wchar_t** argv) {
    assert(argc>=2); Fixture(std::filesystem::path(argv[1]));
    EnumerationAndNames();Assignment();Lifecycle();CustomDefaults();ColorsAndDelete();Input();
    if(argc>2)Executable(argv[2]);
    VirtualFree(reinterpret_cast<void*>(testImage),0,MEM_RELEASE);testImage=0;
    std::cout << "PASS: native HUD Controls bridge\n";
}
