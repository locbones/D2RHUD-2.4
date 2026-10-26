// Exercise the integrated client/server potion gates with native-call seams.
// The dispatcher mock retains the native state/death/ownership restrictions;
// the shipped eligibility helper and generic dispatcher execute separately
// below. The inventory/effect transaction remains a boundary callback.
namespace PotionTest {
constexpr uint32_t MoveBit=0x400000,ExistingBit=0x20,EffectBit=0x800;
Unit item{};
D2ItemDataStrc itemData{};
D2InventoryStrc inventory{},foreignInventory{};
Bytes<0x1b4> itemRow{};
Bytes<0xad8> replacementStats{};
const uint8_t* table=nullptr;
uint64_t rows=1;
int family=76,disabledCalls=0,dispatchCalls=0,result=66;
bool found=true,itemRestricted=false,replaceStats=false,addEffect=false;
uint32_t observed=0;
Bytes<36> packet{};

int __fastcall ItemType(Unit* candidate,int type){return candidate==&item&&type==family;}
Unit* __fastcall Lookup(Game* game,int type,uint32_t id){
    assert(game==&fixture.game&&type==4);
    return found&&id==item.dwUnitId?&item:nullptr;
}
int __fastcall Disabled(Unit* candidate){
    assert(candidate==&item);++disabledCalls;
    auto stats=NativeData::Read<void*>(&fixture.owner,0x88);
    observed=NativeData::Read<uint32_t>(stats,0xacc);
    if(addEffect)Put(stats,0xacc,observed|EffectBit);
    if(replaceStats)Put(&fixture.owner,0x88,replacementStats.data());
    return itemRestricted||(observed&MoveBit)?1:0;
}
int __fastcall Dispatch(Game* game,Unit* player,const void* bytes,int size){
    assert(game==&fixture.game&&player==&fixture.owner&&bytes==packet.data());
    ++dispatchCalls;
    auto stats=NativeData::Read<void*>(player,0x88);
    observed=NativeData::Read<uint32_t>(stats,0xacc);
    if(observed&MoveBit)return 0;
    // These checks deliberately occur after state54, as in the native chain.
    // Bypassing its WW gate must not bypass its other refusal paths.
    if(!NativeData::Alive(player)||player->dwFlags&0x10000||size!=36||
        NativeData::Read<uint32_t>(bytes,2)!=player->dwUnitId||
        NativeData::Read<uint8_t>(bytes,15)!=255)return 1;
    if(addEffect)Put(stats,0xacc,observed|EffectBit);
    if(replaceStats)Put(player,0x88,replacementStats.data());
    return result;
}
void Setup(){
    auto& f=fixture;BeginHeldClient(2);
    item={};itemData={};inventory={};foreignInventory={};itemRow={};replacementStats={};packet={};
    inventory.dwSignature=0x01020304;inventory.pOwner=&f.owner;f.owner.pInventory=&inventory;
    item.dwUnitType=4;item.dwUnitId=90;item.dwClassId=0;item.dwItemMode=2;item.pItemData=&itemData;
    itemData.pExtraData.pParentInv=&inventory;
    table=itemRow.data();rows=1;Put(itemRow.data(),0x124,uint8_t(1));
    family=76;found=true;itemRestricted=replaceStats=addEffect=false;
    disabledCalls=dispatchCalls=0;result=66;observed=0;
    Put(f.stats.data(),0x1c,uint32_t(0x80000000));Put(f.stats.data(),0xacc,MoveBit|ExistingBit);
    packet[0]=0x26;Put(packet.data(),2,f.owner.dwUnitId);packet[7]=255;
    Put(packet.data(),10,item.dwUnitId);packet[14]=2;packet[15]=255;packet[16]=3;
    for(size_t offset:{18u,24u,30u})Put(packet.data(),offset,UINT32_MAX);
    Bind(0x1ca0390,&table);Bind(0x1ca0398,&rows);
    Bind(Potion::ItemTypeRva,ItemType);Bind(Potion::LookupRva,Lookup);
    originalItemDisabled=Disabled;originalDispatcher=Dispatch;
}
void Scope(){
    Setup();auto& f=fixture;
    {
        Potion::MoveStateScope outer(&f.owner);
        assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==ExistingBit);
        {Potion::MoveStateScope nested(&f.owner);Put(f.stats.data(),0xacc,ExistingBit|EffectBit);}
        assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==(ExistingBit|EffectBit));
    }
    assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==(MoveBit|ExistingBit|EffectBit));
    Put(f.stats.data(),0xacc,ExistingBit);
    {Potion::MoveStateScope noMoveState(&f.owner);Put(f.stats.data(),0xacc,ExistingBit|EffectBit);}
    assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==(ExistingBit|EffectBit));
    Setup();
    {Potion::MoveStateScope moved(&f.owner);Put(&f.owner,0x88,replacementStats.data());}
    assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==ExistingBit);
    assert(NativeData::Read<uint32_t>(replacementStats.data(),0xacc)==0);
    Put(&f.owner,0x88,f.stats.data());Put(f.stats.data(),0xacc,MoveBit|ExistingBit);Put(f.stats.data(),0x1c,int32_t(0));
    {Potion::MoveStateScope invalidStats(&f.owner);assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==(MoveBit|ExistingBit));}
}
void Eligibility(){
    auto& f=fixture;
    for(int type=76;type<=81;++type){Setup();family=type;assert(Potion::BeltItem(&f.owner,&item));}
    for(int exclusion=0;exclusion<13;++exclusion){
        Setup();
        switch(exclusion){
        case 0:item.dwItemMode=0;break;
        case 1:item.dwItemMode=3;break;
        case 2:item.dwUnitType=1;break;
        case 3:itemData.pExtraData.pParentInv=&foreignInventory;break;
        case 4:inventory.pOwner=nullptr;break;
        case 5:inventory.dwSignature=0;break;
        case 6:f.owner.pInventory=nullptr;break;
        case 7:family=9;break; // Generic potion includes RMD gold bars.
        case 8:itemRow[0x124]=0;break;
        case 9:item.dwClassId=1;break;
        case 10:table=nullptr;break;
        case 11:item.pItemData=nullptr;break;
        case 12:f.owner.dwAnimMode=17;break;
        }
        assert(!Potion::BeltItem(&f.owner,&item));
    }
    Setup();assert(!Potion::BeltItem(nullptr,&item)&&!Potion::BeltItem(&f.owner,nullptr));
    assert(Potion::RequestItem(&f.game,&f.owner,packet.data(),36)==&item);
    assert(!Potion::RequestItem(nullptr,&f.owner,packet.data(),36));
    assert(!Potion::RequestItem(&f.game,nullptr,packet.data(),36));
    assert(!Potion::RequestItem(&f.game,&f.owner,nullptr,36));
    assert(!Potion::RequestItem(&f.game,&f.owner,packet.data(),35));
    packet[0]=0x25;assert(!Potion::RequestItem(&f.game,&f.owner,packet.data(),36));packet[0]=0x26;
    packet[14]=0;assert(!Potion::RequestItem(&f.game,&f.owner,packet.data(),36));packet[14]=2;
    found=false;assert(!Potion::RequestItem(&f.game,&f.owner,packet.data(),36));
}
void Client(){
    auto& f=fixture;
    for(int type=76;type<=81;++type){
        Setup();family=type;const auto controls=f.controls.size();const auto path=f.path;const auto skill=f.skill;
        // Eligibility runs during drawing, too. It must not call Held(), send
        // control traffic, change the path or finish the active skill.
        InputMock::right=false;inputBlocked.store(true);
        assert(ItemDisabledHook(&item)==0&&disabledCalls==1&&observed==ExistingBit);
        assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==(MoveBit|ExistingBit));
        assert(f.controls.size()==controls&&f.path==path&&f.skill==skill&&client.active);
    }
    Setup();itemRestricted=true;addEffect=true;
    assert(ItemDisabledHook(&item)==1&&observed==ExistingBit);
    assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==(MoveBit|ExistingBit|EffectBit));
    for(int exclusion=0;exclusion<11;++exclusion){
        Setup();
        switch(exclusion){
        case 0:enabled.store(false);break;
        case 1:ready.store(false);break;
        case 2:client.active=false;break;
        case 3:f.owner.dwAnimMode=17;break;
        case 4:client.player=nullptr;break;
        case 5:client.skill=nullptr;break;
        case 6:Put(f.skill.data(),0x14,uint32_t(0));break;
        case 7:Put(f.row.data(),0x4e,int16_t(75));break;
        case 8:++client.seed;break;
        case 9:itemData.pExtraData.pParentInv=&foreignInventory;break;
        case 10:family=9;break;
        }
        assert(ItemDisabledHook(&item)==1&&disabledCalls==1&&observed==(MoveBit|ExistingBit));
        assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==(MoveBit|ExistingBit));
    }
    Setup();replaceStats=true;
    assert(ItemDisabledHook(&item)==0&&observed==ExistingBit);
    assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==ExistingBit&&NativeData::Read<uint32_t>(replacementStats.data(),0xacc)==0);
}
void Server(){
    auto& f=fixture;
    for(int nativeResult:{0,1,66}){
        Setup();result=nativeResult;addEffect=true;
        const auto path=f.path;const auto skill=f.skill;const auto controls=f.controls.size();const auto charge=f.Current().nextCharge;
        assert(DispatcherHook(&f.game,&f.owner,packet.data(),36)==nativeResult&&dispatchCalls==1&&observed==ExistingBit);
        assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==(MoveBit|ExistingBit|EffectBit));
        assert(f.path==path&&f.skill==skill&&f.controls.size()==controls&&client.active&&!f.Current().lease.stopped&&f.Current().nextCharge==charge);
    }
    for(int exclusion=0;exclusion<14;++exclusion){
        Setup();int size=36;
        switch(exclusion){
        case 0:enabled.store(false);break;
        case 1:ready.store(false);break;
        case 2:f.Current().lease.received=GetTickCount64()-Data::LeaseMilliseconds-1;break;
        case 3:f.Current().lease.accepted=false;break;
        case 4:f.Current().lease.stopped=true;break;
        case 5:itemData.pExtraData.pParentInv=&foreignInventory;break;
        case 6:family=9;break;
        case 7:item.dwItemMode=0;break;
        case 8:--size;break;
        case 9:packet[14]=0;break;
        case 10:packet[0]=0x25;break;
        case 11:f.owner.dwAnimMode=17;break;
        case 12:Put(f.skill.data(),0x14,uint32_t(0));break;
        case 13:f.owner.dwFlags|=0x10000;break;
        }
        assert(DispatcherHook(&f.game,&f.owner,packet.data(),size)==0&&dispatchCalls==1&&observed==(MoveBit|ExistingBit));
        assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==(MoveBit|ExistingBit));
    }
    for(int refusal=0;refusal<2;++refusal){
        Setup();
        if(refusal==0)Put(packet.data(),2,uint32_t(f.owner.dwUnitId+1));
        if(refusal==1)packet[15]=0;
        assert(DispatcherHook(&f.game,&f.owner,packet.data(),36)==1&&dispatchCalls==1);
        assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==(MoveBit|ExistingBit));
    }
    Setup();replaceStats=true;
    assert(DispatcherHook(&f.game,&f.owner,packet.data(),36)==66&&observed==ExistingBit);
    assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==ExistingBit&&NativeData::Read<uint32_t>(replacementStats.data(),0xacc)==0);
}
}
void WhirlwindPotions(){
    PotionTest::Scope();PotionTest::Eligibility();PotionTest::Client();PotionTest::Server();
    fixture.Reset();
    std::cout<<"D3 Whirlwind permits owned belt drinks through native client/server gates, preserves refusals, effect states and spin/path, and restores only the original state owner.\n";
}
// Execute the shipped eligibility helper and packet dispatcher. Only their
// external local-player/time/transaction boundaries are callbacks; their real
// state, death and player-data branches execute unchanged in a private PE.
void NativeWhirlwindPotions(const std::filesystem::path& path){
    using namespace PotionTest;
    std::ifstream input(path,std::ios::binary|std::ios::ate);assert(input);
    std::vector<uint8_t> file(size_t(input.tellg()));input.seekg(0);assert(input.read(reinterpret_cast<char*>(file.data()),file.size()));
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data()+dos->e_lfanew);
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
    for(auto section=IMAGE_FIRST_SECTION(nt);section<IMAGE_FIRST_SECTION(nt)+nt->FileHeader.NumberOfSections;++section)
        memcpy(image+section->VirtualAddress,file.data()+section->PointerToRawData,section->SizeOfRawData);
    auto stub=[&](uint32_t rva,auto fn){
        const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);
        const auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);
    };
    stub(0x6b050,NativeLocalPlayer);
    Put(image,0x1d442d8,uint32_t(0));Put(image,0x1c9ebf8,uint32_t(1000));
    Put(image,0x149c850,reinterpret_cast<uintptr_t>(+[](LARGE_INTEGER* value)->BOOL{value->QuadPart=50000000;return TRUE;}));
    Put(image,0x1baa460,reinterpret_cast<uintptr_t>(+[]()->uint64_t{return 10000000;}));
    const auto callback=+[](Game* game,Unit* player,const void* bytes,int size)->int{
        assert(game==&fixture.game&&player==&fixture.owner&&bytes==packet.data()&&size==36);
        ++dispatchCalls;auto stats=NativeData::Read<void*>(player,0x88);
        observed=NativeData::Read<uint32_t>(stats,0xacc);
        assert(!(observed&MoveBit));
        if(addEffect)Put(stats,0xacc,observed|EffectBit);
        return result;
    };
    Put(image,0x14bfc50+0x26*8,reinterpret_cast<uintptr_t>(callback));
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    auto nativeDisabled=reinterpret_cast<ItemDisabled>(image+Potion::ItemDisabledRva);
    auto nativeDispatch=reinterpret_cast<Pickup::Dispatcher>(image+Pickup::DispatcherRva);
    auto& f=fixture;
    for(int familyId=76;familyId<=81;++familyId){
        Setup();family=familyId;originalItemDisabled=nativeDisabled;
        assert(nativeDisabled(&item)==1);
        const auto controls=f.controls.size();const auto skill=f.skill;const auto pathBefore=f.path;
        assert(ItemDisabledHook(&item)==0);
        assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==(MoveBit|ExistingBit));
        assert(f.controls.size()==controls&&f.skill==skill&&f.path==pathBefore&&client.active);
        enabled.store(false);assert(ItemDisabledHook(&item)==1);enabled.store(true);
    }
    Setup();originalItemDisabled=nativeDisabled;
    Bytes<0xad8> itemStats{};Bytes<0x70> restriction{};
    Put(&item,0x88,itemStats.data());Put(itemStats.data(),0x1c,uint32_t(0x80000000));
    Put(itemStats.data(),0x68,restriction.data());Put(restriction.data(),0x1c,uint32_t(2));
    assert(ItemDisabledHook(&item)==1);
    assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==(MoveBit|ExistingBit));
    for(int nativeResult:{0,1,66}){
        Setup();originalDispatcher=nativeDispatch;result=nativeResult;addEffect=true;
        assert(nativeDispatch(&f.game,&f.owner,packet.data(),36)==0&&dispatchCalls==0);
        const auto skill=f.skill;const auto pathBefore=f.path;
        assert(DispatcherHook(&f.game,&f.owner,packet.data(),36)==nativeResult&&dispatchCalls==1&&observed==ExistingBit);
        assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==(MoveBit|ExistingBit|EffectBit));
        assert(NativeData::Read<uint64_t>(&f.game,0x57f8)==5000000000ULL);
        assert(f.skill==skill&&f.path==pathBefore&&!f.Current().lease.stopped);
    }
    for(int exclusion=0;exclusion<5;++exclusion){
        Setup();originalDispatcher=nativeDispatch;
        switch(exclusion){
        case 0:enabled.store(false);break;
        case 1:f.owner.dwAnimMode=17;break;
        case 2:f.owner.dwAnimMode=0;break;
        case 3:f.owner.dwFlags|=0x10000;break;
        case 4:f.owner.pPlayerData=nullptr;break;
        }
        assert(DispatcherHook(&f.game,&f.owner,packet.data(),36)==0&&dispatchCalls==0);
        assert(NativeData::Read<uint32_t>(f.stats.data(),0xacc)==(MoveBit|ExistingBit));
    }
    fixture.Reset();originalItemDisabled=Disabled;
    VirtualFree(image,0,MEM_RELEASE);
    std::cout<<"Shipped item eligibility and dispatcher execute WW-only potion bypass; item-specific disabled states, death/blocked/missing-player-data gates, native returns and activity timestamp remain intact.\n";
}
