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
    data(0x14bc568,reinterpret_cast<uintptr_t>(image+0x249b90));data(0x14bc5a0,reinterpret_cast<uintptr_t>(image+0x24a960));data(0x1c9ebc0,reinterpret_cast<uintptr_t>(&propertyRow));data(0x1c9ebc8,1);
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
    // Quest 25's real 'move' property uses func8, not func1. Execute its native
    // dispatcher path with an existing modifier to verify additive semantics.
    propertyRow[0x18]=8;propertyRow[0x20]=96;SetSavedStat(savedList.data(),96,15);
    Property movement{0,0,20,20};apply(7,nullptr,&item,nullptr,0,0,&movement,0,0x40,nullptr);
    assert(SavedStat(savedList.data(),96)==35);
    assert(std::any_of(playerValues.begin(),playerValues.end(),[](const auto& entry){return entry.key==(96u<<16)&&entry.value==35;}));
    propertyRow[0x18]=1;
    // The production bonus adapter is exercised against actual native saved
    // modifier mutation and propagation, with only name parsing/notifications
    // replaced at their engine boundary.
    Recipe reward{};reward.outputs[0].type=254;
    auto ingredient=[&](uint16_t flags,uint16_t base,uint16_t unique){memcpy(reward.header.data()+0x14,&flags,2);memcpy(reward.header.data()+0x16,&base,2);memcpy(reward.header.data()+0x18,&unique,2);reward.header[0x1a]=7;reward.header[0x1b]=0;};
    ingredient(0x41,0,43);
    reward.outputs[0].mods[0].property=0;reward.outputs[0].mods[0].min=reward.outputs[0].mods[0].max=25;
    auto rules=std::make_shared<QuestJournal::Rules>();rules->events=true;rules->rows=1;rules->quests.resize(5);rules->quests[4].bonus=true;
    rules->gates={{0,5,true,1,1,0,0,0}};::testQuestRules=rules;
    data(0x1ca0b18,reinterpret_cast<uintptr_t>(&reward));data(0x1ca0b20,1);data(0x1c9ebd8,1);
    data(0x1ca0398,4);data(0x1c9fc78,100);
    stub(0x6d0470,+[](void*,const char* name)->int{return !strcmp(name,"rng-generator")?99:-1;});
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
    // All six fixed bonuses obey the compiled first ingredient. Tier-two
    // and tier-three upgrades may keep the original unique ID (,mod), while
    // ordinary generated versions may carry their tier's own unique ID.
    rules->quests.resize(40);for(const int q:{5,10,20,25,35,40})rules->quests[q-1].bonus=true;
    for(const auto [quest,base,stat,shift,amount]:std::array<std::array<int,5>,6>{{{5,0,41,0,25},{10,0,45,0,25},{20,1,43,0,25},{25,1,96,0,20},{35,2,7,8,100},{40,2,9,8,100}}}){
        rules->gates[0].quest=quest;ingredient(base?1:0x41,uint16_t(base),base?0:43);
        propertyRow[0x18]=uint8_t(quest==25?8:1);
        propertyRow[0x20]=uint8_t(stat);stats[stat][0x14]=uint8_t(shift);stats[stat][8]=32;stats[stat][0x15]=16;
        reward.outputs[0].mods[0].min=reward.outputs[0].mods[0].max=int16_t(amount);
        item.dwClassId=uint32_t(base);itemData.dwFileIndex=42;
        for(const int uniqueId:base?std::vector<int>{42,42+base}:std::vector<int>{42}){
            itemData.dwFileIndex=uniqueId;SetSavedStat(savedList.data(),stat,0);const int old=published;
            const int retained=SavedStat(savedList.data(),39);
            assert(ClaimQuestBonus(&game,&player,quest)&&published==old+1&&SavedStat(savedList.data(),stat)==(amount<<shift));
            assert(item.dwClassId==uint32_t(base)&&itemData.dwFileIndex==uniqueId&&SavedStat(savedList.data(),39)==retained);
        }
        const int old=published;SetSavedStat(savedList.data(),stat,0);
        item.dwClassId=uint32_t((base+1)%3);assert(!ClaimQuestBonus(&game,&player,quest)&&published==old&&!SavedStat(savedList.data(),stat));item.dwClassId=uint32_t(base);
        itemData.dwQualityNo=2;assert(!ClaimQuestBonus(&game,&player,quest)&&published==old);itemData.dwQualityNo=7;
        itemData.nPage=3;assert(!ClaimQuestBonus(&game,&player,quest)&&published==old);itemData.nPage=0;
        if(!base){itemData.dwFileIndex=43;assert(!ClaimQuestBonus(&game,&player,quest)&&published==old);itemData.dwFileIndex=42;}
        duplicate=item;duplicateData=itemData;duplicate.pItemData=&duplicateData;duplicateData.pExtraData.pNextItem=nullptr;itemData.pExtraData.pNextItem=&duplicate;
        assert(!ClaimQuestBonus(&game,&player,quest)&&published==old);itemData.pExtraData.pNextItem=nullptr;
        if(quest==25){
            // Actual RMD movement encoding: 8 saved bits, bias60, 9 send bits.
            stats[96][0x15]=8;stats[96][8]=9;uint32_t bias=60;memcpy(stats[96].data()+0x18,&bias,4);
            SetSavedStat(savedList.data(),96,15);assert(ClaimQuestBonus(&game,&player,25)&&SavedStat(savedList.data(),96)==35);
            SetSavedStat(savedList.data(),96,180);const int previous=published;
            assert(!ClaimQuestBonus(&game,&player,25)&&SavedStat(savedList.data(),96)==180&&published==previous);
        }
    }
    ::testQuestRules=previousRules;originalNotify=previousNotify;
    for(const auto& [rva,bytes]:oldCode)memcpy(image+rva,bytes.data(),bytes.size());for(const auto& [rva,value]:oldData)memcpy(image+rva,&value,8);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    std::cout<<"Actual native property func1 and func8 add saved resist/HP/mana/movement modifiers and propagate changed item and attached player full stats. Production reward adapter preserves exact HP/mana shifts and existing movement bonuses, supports all six compiled rewards and refuses unsupported properties, full capacity, duplicate charms and noncarried charms.\n";
}
}
