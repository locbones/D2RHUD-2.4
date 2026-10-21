// Included by the private-image cube harness. Executes the shipped modern
// property dispatcher/func1 and native saved-list addition/propagation.
namespace QuestPropertyNativeTest {
void Native(uint8_t* image) {
    const auto previousRules=::testQuestRules;const auto previousNotify=originalNotify;
    Reset();
    std::map<uint32_t,std::array<uint8_t,14>> oldCode;
    std::map<uint32_t,uint64_t> oldData;
    auto data=[&](uint32_t rva,uint64_t value){uint64_t old=0;memcpy(&old,image+rva,8);oldData.emplace(rva,old);memcpy(image+rva,&value,8);};
    auto stub=[&](uint32_t rva,auto fn){std::array<uint8_t,14> old{};memcpy(old.data(),image+rva,14);oldCode.emplace(rva,old);const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);const auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    std::vector<std::array<uint8_t,0x144>> stats(100);
    std::array<uint8_t,0x2e> propertyRow{};propertyRow[0x18]=1;
    std::array<SavedEntry,16> baseValues{},fullValues{},modValues{},playerValues{};
    std::array<uint8_t,0xb58> playerExtended{};
    auto vector=[&](uint8_t* list,size_t offset,auto& entries){auto address=reinterpret_cast<uintptr_t>(entries.data());uint64_t length=0,capacity=0x8000000000000010ULL;memcpy(list+offset,&address,8);memcpy(list+offset+8,&length,8);memcpy(list+offset+16,&capacity,8);};
    vector(extended.data(),0x30,baseValues);vector(extended.data(),0x80,fullValues);vector(savedList.data(),0x30,modValues);
    auto parent=reinterpret_cast<uintptr_t>(extended.data()),owner=reinterpret_cast<uintptr_t>(&item);
    memcpy(savedList.data()+0x58,&parent,8);memcpy(extended.data()+0x78,&owner,8);
    vector(playerExtended.data(),0x80,playerValues);const auto playerList=reinterpret_cast<uintptr_t>(playerExtended.data());
    memcpy(extended.data()+0x58,&playerList,8);uint32_t playerFlags=0x80000000;memcpy(playerExtended.data()+0x1c,&playerFlags,4);
    const auto playerOwner=reinterpret_cast<uintptr_t>(&player);memcpy(playerExtended.data()+0x78,&playerOwner,8);
    data(0x1c9fb78,reinterpret_cast<uintptr_t>(stats.data()));data(0x1c9fb80,stats.size());
    data(0x14bc568,reinterpret_cast<uintptr_t>(image+0x249b90));data(0x1c9ebc0,reinterpret_cast<uintptr_t>(&propertyRow));data(0x1c9ebc8,1);
    stub(0x14383a0,&memmove);stub(0x1438100,&memset);stub(0x1434c50,Zero);
    // Select an already attached state-zero MAGIC list; the dispatcher, exact
    // property function, value shift, native additive setter and propagation run.
    stub(0x246000,+[](Unit* player,Unit* target,int state,uint32_t flags)->void*{assert(!player&&target==&item&&!state&&flags==0x40);return SavedList(target);});
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    struct Property{int id,param,min,max;};
    using Apply=void(__fastcall*)(int,Unit*,Unit*,void*,int,int,Property*,int,uint32_t,Unit*);
    const auto apply=reinterpret_cast<Apply>(image+0x24bad0);
    for(const auto [stat,shift,amount]:std::array<std::array<int,3>,5>{{{39,0,25},{43,0,25},{45,0,25},{7,8,100},{9,8,100}}}) {
        propertyRow[0x20]=uint8_t(stat);propertyRow[0x21]=0;stats[stat][0x14]=uint8_t(shift);
        Property property{0,0,amount,amount};
        apply(7,nullptr,&item,nullptr,0,0,&property,0,0x40,nullptr);
        assert(SavedStat(savedList.data(),stat)==(amount<<shift));
        assert(std::any_of(fullValues.begin(),fullValues.end(),[&](const auto& entry){return entry.key==(uint32_t(stat)<<16)&&entry.value==(amount<<shift);}));
        apply(7,nullptr,&item,nullptr,0,0,&property,0,0x40,nullptr);
        assert(SavedStat(savedList.data(),stat)==(amount<<shift)*2);
        assert(std::any_of(playerValues.begin(),playerValues.end(),[&](const auto& entry){return entry.key==(uint32_t(stat)<<16)&&entry.value==(amount<<shift)*2;}));
    }
    // The production bonus adapter is exercised against actual native saved
    // modifier mutation and propagation, with only name parsing/notifications
    // replaced at their engine boundary.
    Recipe reward{};reward.outputs[0].type=254;
    reward.outputs[0].mods[0].property=0;reward.outputs[0].mods[0].min=reward.outputs[0].mods[0].max=25;
    auto rules=std::make_shared<QuestJournal::Rules>();rules->events=true;rules->rows=1;rules->quests.resize(5);rules->quests[4].bonus=true;
    rules->gates={{0,5,true,1,1,0,0,0}};::testQuestRules=rules;
    data(0x1ca0b18,reinterpret_cast<uintptr_t>(&reward));data(0x1ca0b20,1);data(0x1c9ebd8,1);
    stub(0x6d0470,+[](void*,const char* name)->int{return !strcmp(name,"rng-generator")?99:-1;});
    stub(0x2616b0,+[](Output* result,char* name,int,int)->int{assert(!strcmp(name,"Barrier1"));*result={};result->type=252;result->flags=8;result->quality=7;result->special=43;return 1;});
    int published=0;static int* publications=nullptr;publications=&published;
    originalNotify=+[](Game*,Unit* owner,Unit* target,uint32_t flags,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t){assert(owner==&player&&target==&item&&flags==1);++*publications;};
    Game game{};D2InventoryStrc inventory{};inventory.pOwner=&player;inventory.pFirstItem=&item;player.pInventory=&inventory;
    itemData.dwQualityNo=7;itemData.dwFileIndex=42;itemData.nPage=0;itemData.pExtraData.pParentInv=&inventory;
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    for(const auto [stat,shift,amount]:std::array<std::array<int,3>,3>{{{39,0,25},{7,8,100},{9,8,100}}}){
        propertyRow[0x20]=uint8_t(stat);stats[stat][0x14]=uint8_t(shift);stats[stat][8]=32;stats[stat][0x15]=uint8_t(stat==7?9:stat==9?8:9);uint32_t bias=stat==7||stat==9?32:200;memcpy(stats[stat].data()+0x18,&bias,4);
        SetSavedStat(savedList.data(),stat,0);reward.outputs[0].mods[0].min=reward.outputs[0].mods[0].max=int16_t(amount);
        const int previous=published;assert(ClaimQuestBonus(&game,&player,5));assert(published==previous+1&&SavedStat(savedList.data(),stat)==(amount<<shift));
    }
    const int before=published;propertyRow[0x18]=2;assert(!ClaimQuestBonus(&game,&player,5)&&published==before);propertyRow[0x18]=1;
    SetSavedStat(savedList.data(),9,INT_MAX-10);assert(!ClaimQuestBonus(&game,&player,5)&&SavedStat(savedList.data(),9)==INT_MAX-10&&published==before);
    SetSavedStat(savedList.data(),9,0);Unit duplicate=item;D2ItemDataStrc duplicateData=itemData;duplicate.pItemData=&duplicateData;duplicateData.pExtraData.pNextItem=nullptr;itemData.pExtraData.pNextItem=&duplicate;
    assert(!ClaimQuestBonus(&game,&player,5)&&!SavedStat(savedList.data(),9)&&published==before);itemData.pExtraData.pNextItem=nullptr;
    itemData.nPage=1;assert(!ClaimQuestBonus(&game,&player,5)&&published==before);itemData.nPage=0;
    ::testQuestRules=previousRules;originalNotify=previousNotify;
    for(const auto& [rva,bytes]:oldCode)memcpy(image+rva,bytes.data(),bytes.size());for(const auto& [rva,value]:oldData)memcpy(image+rva,&value,8);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    std::cout<<"Actual native property func1 uses ItemStatCost byte0x14 value shift, adds saved resist/HP/mana modifiers and propagates changed item and attached player full stats. Production reward adapter preserves exact HP/mana shifts and refuses unsupported properties, full capacity, duplicate charms and noncarried charms.\n";
}
}
