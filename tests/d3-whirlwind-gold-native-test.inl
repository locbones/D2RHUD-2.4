// Execute the real AutoGoldManager update in a private mapped image. All I/O,
// configuration lookups and allocation boundaries are test callbacks; no D2R
// process is opened. This specifically checks the upstream native eligibility
// gates rather than replacing them with a second HUD gold scanner.
void NativeAutoGold(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary|std::ios::ate);assert(input);
    std::vector<uint8_t> file(size_t(input.tellg()));input.seekg(0);input.read(reinterpret_cast<char*>(file.data()),file.size());
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data()+dos->e_lfanew);
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
    for(auto section=IMAGE_FIRST_SECTION(nt);section<IMAGE_FIRST_SECTION(nt)+nt->FileHeader.NumberOfSections;++section)
        memcpy(image+section->VirtualAddress,file.data()+section->PointerToRawData,section->SizeOfRawData);
    auto stub=[&](uint32_t rva,auto fn){
        const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);
        const auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);
    };
    static Bytes<0x170> logging{};
    static bool autoGold=true,fullGold=false;static int requests=0,frees=0;
    static std::array<void*,5> vtable{};static void* allocator=nullptr;
    vtable[4]=reinterpret_cast<void*>(+[](void*,void*){++frees;});allocator=vtable.data();
    stub(0xb09710,+[]()->void*{return logging.data();});
    stub(0x135c20,+[]()->void*{return logging.data();});
    stub(0x776950,+[](void*){return autoGold?1:0;});
    stub(0x1e2e70,+[](void*,uint32_t stat,void*){return stat==0xc0000 ? 1 : (fullGold?10000:0);});
    stub(0x102810,+[](void*){}); // Queue already has one correctly ordered node.
    stub(Pickup::LookupRva,GoldLookup);stub(0x6e580,+[]()->uint16_t{return 0;});
    stub(0xacdd80,+[]()->void*{return &allocator;});
    stub(Pickup::ClientInteractionRva,+[](Unit* unit,int action,int x,int y,uint16_t modifiers){
        ++requests;return ClientInteractionHook(unit,action,x,y,modifiers);
    });
    uint64_t costCount=15;Bytes<0x1300> costs{};
    Put(image,0x1c9fb80,costCount);Put(image,0x1c9fb78,costs.data());
    // The native eligibility check follows this accessor into player data.
    stub(0x208330,+[](Unit* unit)->void*{return unit->pPlayerData;});
    auto manager=image+Pickup::AutoGoldTargetRva-0x18;Bytes<0x20> node{};
    for(int scenario=0;scenario<6;++scenario){
        BeginHeldClient(2);auto& f=fixture;
        SetRightClickPickupDisabled(true);
        Put(f.stats.data(),0xacc,uint32_t(0x400000));Put(f.path.data(),0x48,uint32_t(0x20));
        Put(&f.data,0,uint32_t(0x1020304));
        Bind(Pickup::AutoGoldTargetRva,image+Pickup::AutoGoldTargetRva);
        memset(manager,0,0x28);node={};Put(manager,0,node.data());Put(manager,8,node.data());Put(manager,0x10,uint64_t(1));
        Put(manager,0x20,uint32_t(100));Put(manager,0x24,uint32_t(100));
        Put(node.data(),0,manager);Put(node.data(),8,manager);Put(node.data(),0x10,f.gold.dwUnitId);
        autoGold=scenario!=1;fullGold=scenario==2;requests=frees=0;
        if(scenario==3)Put(node.data(),0x18,uint32_t(950)); // Native collection delay has not elapsed.
        if(scenario==4)Put(&f.data,0x40,&f.gold); // Native occupied-cursor check.
        if(scenario==5)enabled.store(false);
        FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
        reinterpret_cast<void(__fastcall*)(void*,Unit*,uint32_t)>(image+0x1023c0)(manager,&f.owner,1000);
        const bool eligible=scenario==0 || scenario==5;
        assert(requests==(eligible?1:0));
        assert(f.goldPackets.size()==size_t(scenario==0?1:0));
        assert(f.interactions==(scenario==5?1:0));
        assert(frees==(eligible?1:0));
        assert(!NativeData::Read<Unit*>(manager,0x18));
        assert(client.active && NativeData::Active(f.skill.data()));
    }
    // Replay the actual native hover accessor, including its per-local-player
    // 16-byte records and unit-table lookup, rather than a second HUD hover map.
    fixture.Reset();auto& f=fixture;
    SetEnabled(false);SetRightClickPickupDisabled(true);
    Bind(Pickup::LocalPlayerIdsRva,image+Pickup::LocalPlayerIdsRva);
    Bind(Pickup::SelectedUnitRva,image+Pickup::SelectedUnitRva);
    std::array<uint32_t,8> ids{};ids[6]=f.owner.dwUnitId;
    memcpy(image+Pickup::LocalPlayerIdsRva,ids.data(),sizeof(ids));
    auto hover=image+0x1c9bef0+6*16;memset(hover,0,16);
    Put(hover,0,uint8_t(1));Put(hover,4,uint32_t(4));Put(hover,8,f.gold.dwUnitId);
    stub(0x6baf0,+[](Unit* unit,int zero,int alsoZero,int any){
        assert(unit==&fixture.gold && !zero && !alsoZero && any==-1);return 1;
    });
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    for(int action:{3,4})assert(ClientInteractionHook(&f.owner,action,100,200,0)==1);
    assert(!f.interactions);
    assert(ClientInteractionHook(&f.owner,0,100,200,0)==99 && f.interactions==1);
    f.gold.dwAnimMode=0;
    assert(ClientInteractionHook(&f.owner,3,100,200,0)==99 && f.interactions==2);
    f.gold.dwAnimMode=3;Put(hover,0,uint8_t(0));
    assert(ClientInteractionHook(&f.owner,4,100,200,0)==99 && f.interactions==3);
    fixture.Reset();assert(VirtualFree(image,0,MEM_RELEASE));
    std::cout<<"Actual native per-player hover lookup confirms blocked ground right-clicks and unchanged left, inventory and empty-ground handling with WW off.\n";
    std::cout<<"Real native auto-gold update retains its option, gold-cap, delay and occupied-cursor gates; eligible gold uses the WW pickup path and disabled WW delegates normally.\n";
}
