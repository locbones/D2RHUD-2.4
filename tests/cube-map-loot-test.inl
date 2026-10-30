std::vector<int> treasureItems,treasureQualities;
std::array<Unit,64> treasureCreated{};
std::vector<Unit*> treasureObserved;
std::map<Unit*,int> treasureGold;
int __fastcall TreasureStat(Unit* unit,uint32_t stat,uint16_t){return stat==14 ? treasureGold.at(unit) : 0;}
void __fastcall TreasureSet(Unit* unit,int stat,int value,uint16_t){assert(stat==14);treasureGold[unit]=value;}
int __fastcall TreasureName(void*,const char* name){if(std::string(name)=="Blank")return 4;if(std::string(name)=="SoulSurprise")return 2;assert(std::string(name)=="MapGroupA");return 1;}
void TreasureObserve(Unit* unit){assert(std::find(treasureObserved.begin(),treasureObserved.end(),unit)==treasureObserved.end());treasureObserved.push_back(unit);}
Unit* __fastcall TreasureCreate(Game*,Unit*,int item,int quality,int,int){
    assert(treasureItems.size()<treasureCreated.size());
    auto result=&treasureCreated[treasureItems.size()];result->dwUnitType=4;result->dwClassId=item;treasureGold[result]=100;
    treasureItems.push_back(item);treasureQualities.push_back(quality);return result;
}
int __fastcall TreasureQuality(void*,Unit*,Unit*,int level,int item,void*){assert(level==90 && item>=7 && item<=10);return 2;}
int __fastcall TreasureItemType(Unit*,int){return 0;}
void __fastcall TreasureOwner(Unit*,Unit*){}
void __fastcall TreasureCookie(uintptr_t){}
int __fastcall TreasureLevel(void*,int,void*){return 95;}
void __fastcall TreasurePlayers(Game*,void*,int* count){*count=1;}
Room* treasureRoom=nullptr;
Room* __fastcall TreasureRoom(Unit*){return treasureRoom;}
void NativeLoot(uint8_t* image){
    // Run the actual TC walker, selector, upgrade logic, RNG and nested picks.
    // Only final item creation, quality, ownership and cookie checks are stubs.
    std::map<uint32_t,Bytes<14>> savedCode;
    auto stub=[&](uint32_t rva,auto fn){Bytes<14> old{};memcpy(old.data(),image+rva,old.size());savedCode.emplace(rva,old);const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    std::map<uint32_t,uint64_t> savedData;
    auto global=[&](uint32_t rva,uint64_t value){savedData.emplace(rva,Field<uint64_t>(image,rva));Put(image,rva,value);};
    stub(0x2f8820,TreasureCreate);stub(0x2f8a60,TreasureQuality);stub(0x1fe680,TreasureItemType);stub(0x2f9060,TreasureOwner);stub(0x1434c50,TreasureCookie);stub(0x1e2e70,TreasureLevel);stub(0x2c64c0,TreasurePlayers);stub(0x6d0470,TreasureName);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    std::array<Bytes<0x140>,5> records{};std::array<Bytes<0x1c>,2> entries{};Bytes<0x1c> child{};Bytes<0x1dc> monStats{};
    std::array<Bytes<0x1b4>,11> itemRows{};global(0x1ca0390,reinterpret_cast<uintptr_t>(itemRows.data()));global(0x1ca0398,itemRows.size());
    global(0x1c9f6a8,uintptr_t(1));
    global(0x1c9f6b0,reinterpret_cast<uintptr_t>(records.data()));global(0x1c9f6b8,records.size());
    global(0x1c9f878,reinterpret_cast<uintptr_t>(monStats.data()));global(0x1c9f880,1);
    // A non-null stat-list plus this bound enters the actual TC upgrade branch.
    Bytes<0x1000> statTable{};global(0x1c9fb78,reinterpret_cast<uintptr_t>(statTable.data()));global(0x1c9fb80,13);
    Bytes<0x5900> gameBytes{};auto game=reinterpret_cast<Game*>(gameBytes.data());Put(game,0x108,int(1));
    Unit monster{};monster.dwUnitType=1;monster.dwClassId=0;Put(&monster,0x88,statTable.data());
    auto record=[&](int index,int picks,void* info,int entriesCount,int chance){auto r=records[index].data();Put(r,4,chance);Put(r,8,chance);Put(r,0xc,picks);Put(r,0x28,info);Put(r,0x30,uint64_t(entriesCount));};
    auto entry=[&](void* p,int probability,int item,uint8_t flags=0){Put(p,0,probability);Put(p,4,probability);Put(p,8,uint16_t(item));Put(p,0xc,flags);};
    entry(entries[0].data(),0,7);entry(entries[1].data(),1,8);entry(child.data(),0,9);
    record(1,2,entries.data(),2,2);record(2,6,entries.data(),2,2);record(3,2,child.data(),1,1);
    Put(records[1].data(),0,uint16_t(7));Put(records[1].data(),2,int16_t(1));Put(records[2].data(),0,uint16_t(7));Put(records[2].data(),2,int16_t(90));
    assert(lootSelector);auto selector=lootSelector.get();
    auto call=[&](int bonus,Unit** items=nullptr,int* count=nullptr,int maximum=0){treasureItems.clear();treasureQualities.clear();Put(&monster,0x28,uint32_t(1));Put(&monster,0x2c,uint32_t(0));assert(DropExtraPicks(game,&monster,nullptr,1,0,90,0,items,count,maximum,bonus));assert(additionalTreasurePicks==0);};
    const auto originalRecords=records;int count=-1;
    for(int bonus=1;bonus<=3;++bonus){call(bonus,nullptr,&count);assert(count==2+bonus && treasureItems.size()==size_t(count));assert(records==originalRecords);}
    // NoDrop remains part of each root pick, using native game/player-count
    // calculations. Extra picks must not turn the row into guaranteed items.
    Put(records[1].data(),0x10,int(2));std::vector<int> expected;uint32_t low=1,high=0;
    for(int pick=0;pick<5;++pick){const uint64_t roll=uint64_t(low)*0x6ac690c5u+high;low=uint32_t(roll);high=uint32_t(roll>>32);const int result=low&3;if(result>=2)expected.push_back(7+result-2);}
    call(3,nullptr,&count);assert(treasureItems==expected && count==int(expected.size()));Put(records[1].data(),0x10,int(0));
    // NM/Hell upgrades the root TC before the private Picks override. The six
    // vanilla picks plus three bonus picks can all create items (default cap9).
    Put(game,0x104,uint8_t(1));call(3,nullptr,&count);assert(count==9 && treasureItems.size()==9 && records==originalRecords);Put(game,0x104,uint8_t(0));
    // A negative root retains its deterministic 7,8 prefix, and only adds the
    // selected bonus weighted picks. Simply changing -2 to -5 cannot do this.
    Put(records[1].data(),0xc,int(-2));call(3,nullptr,&count);assert(count==5 && treasureItems==std::vector<int>({7,8,treasureItems[2],treasureItems[3],treasureItems[4]}));assert(Field<int>(records[1].data(),0xc)==-2);
    // Nested TC Picks remain two. Two bonus ROOT picks give six child items,
    // which distinguishes picks from adding two arbitrary item drops.
    entry(entries[0].data(),0,3,4);record(1,1,entries.data(),1,1);call(2,nullptr,&count);assert(count==6 && treasureItems==std::vector<int>(6,9) && Field<int>(records[3].data(),0xc)==2);
    struct Output {std::array<Unit*,2> items{};uintptr_t sentinel=0x1234567812345678ULL;} output;
    call(2,output.items.data(),&count,2);assert(count==2 && treasureItems.size()==4 && output.items[0]==&treasureCreated[0] && output.items[1]==&treasureCreated[1] && output.sentinel==0x1234567812345678ULL);
    // NoDrop is cleared at all depths, but only root Picks gain one.
    // Root1 -> child1 now drops two items, rather than multiplying to four.
    record(1,1,entries.data(),1,1);record(3,1,child.data(),1,1);
    Put(records[1].data(),0x10,int(1000));Put(records[3].data(),0x10,int(1000));
    const auto plentifulOriginal=records;
    auto plentifulCall=[&](int rootBonus=0,int overrideTC=-1,Unit** items=nullptr,int maximum=0){treasureItems.clear();treasureQualities.clear();Put(&monster,0x28,uint32_t(1));Put(&monster,0x2c,uint32_t(0));count=-1;assert(DropWithTreasureRules(game,&monster,nullptr,1,0,90,0,items,&count,maximum,overrideTC,rootBonus,true));assert(!plentifulTreasure && additionalTreasurePicks==0 && !activeTreasureGraph);};
    plentifulCall();assert(count==2 && treasureItems==std::vector<int>(2,9) && records==plentifulOriginal);
    plentifulCall(2);assert(count==4 && treasureItems==std::vector<int>(4,9) && records==plentifulOriginal);
    plentifulCall(0,-1,output.items.data(),2);assert(count==2 && treasureItems.size()==2 && output.sentinel==0x1234567812345678ULL);
    monster.dwUnitType=2;plentifulCall();assert(count==2 && treasureItems==std::vector<int>(2,9));
    plentifulCall(0,3);assert(count==2 && treasureItems==std::vector<int>(2,9));monster.dwUnitType=1;
    // Explicitly named event roots are exact, even when vanilla would upgrade
    // the same group to its next high-level TC. Unspecified roots still upgrade.
    Put(game,0x104,uint8_t(1));plentifulCall(0,1);assert(count==2 && treasureItems==std::vector<int>(2,9) && overriddenTreasureClass==-1);Put(game,0x104,uint8_t(0));
    // Three nested one-Pick levels must still produce two items, not 2^3.
    Bytes<0x1c> deeper{};entry(deeper.data(),0,2,4);
    record(3,1,deeper.data(),1,1);record(2,1,child.data(),1,1);
    Put(records[2].data(),0x10,int(1000));
    plentifulCall();assert(count==2 && treasureItems==std::vector<int>(2,9));
    plentifulCall(2);assert(count==4 && treasureItems==std::vector<int>(4,9));
    {PrivateTreasureGraph graph;record(1,0,entries.data(),1,1);record(3,0,child.data(),1,1);
     auto root=graph.Build(records[1].data(),0);assert(root && Field<int>(root,0xc)==1 && Field<int>(graph.records.at(3)->record.data(),0xc)==0);}
    record(1,1,entries.data(),1,1);record(3,1,child.data(),1,1);
    // Negative child rows retain exactly their deterministic 7,8 sequence.
    // They gain no nested bonus; only the two root rolls visit them twice.
    std::array<Bytes<0x1c>,2> negativeChildren{};entry(negativeChildren[0].data(),0,7);entry(negativeChildren[1].data(),1,8);
    record(3,-2,negativeChildren.data(),2,2);plentifulCall();
    assert(count==4 && treasureItems==std::vector<int>({7,8,7,8}) && Field<int>(records[3].data(),0xc)==-2);
    // A negative root can combine Plentiful with a Treasure Surge root
    // bonus without multiplying its deterministic vanilla outputs.
    record(1,-2,negativeChildren.data(),2,2);plentifulCall(2);assert(count==5 && treasureItems[0]==7 && treasureItems[1]==8);
    // Ending the timed rule restores ordinary NoDrop and root-only behavior.
    record(1,1,entries.data(),1,1);record(3,1,child.data(),1,1);call(1,nullptr,&count);assert(count==0 && treasureItems.empty());
    Put(records[1].data(),0x10,int(0));Put(records[3].data(),0x10,int(0));call(1,nullptr,&count);assert(count==2 && treasureItems==std::vector<int>(2,9));
    // Blank now follows NoDrop=0: remove its weight at every depth, but
    // retain native nested Picks and both distributions' real-entry weights.
    std::array<Bytes<0x1c>,3> blankRoot{},blankChild{};Bytes<0x1c> invalid{};entry(invalid.data(),0,65535);
    record(4,1,invalid.data(),1,1);
    entry(blankRoot[0].data(),0,4,4);entry(blankRoot[1].data(),4500,3,4);entry(blankRoot[2].data(),4501,8);
    entry(blankChild[0].data(),0,7);entry(blankChild[1].data(),1,4,4);entry(blankChild[2].data(),4501,9);
    Put(blankRoot[1].data(),4,9000);Put(blankRoot[2].data(),4,9002);
    Put(blankChild[1].data(),4,3);Put(blankChild[2].data(),4,9003);
    record(1,1,blankRoot.data(),3,4502);Put(records[1].data(),8,9005);
    record(3,1,blankChild.data(),3,4502);Put(records[3].data(),8,9005);
    const auto withBlank=records;const auto rootEntries=blankRoot,childEntries=blankChild;
    {PrivateTreasureGraph graph;auto root=graph.Build(records[1].data(),0);
     assert(root && Field<int>(root,4)==2 && Field<int>(root,8)==5 && Field<int>(root,0xc)==2);
     assert(Field<uint64_t>(root,0x30)==2 && Field<void*>(root,0x28)!=blankRoot.data());
     auto info=Field<uint8_t*>(root,0x28);assert(Field<int>(info,0)==0 && Field<int>(info+0x1c,0)==1 && Field<int>(info+0x1c,4)==2);
     auto childRecord=graph.records.at(3)->record.data();
     assert(Field<int>(childRecord,4)==2 && Field<int>(childRecord,8)==5 && Field<int>(childRecord,0xc)==1);
     assert(!graph.records.contains(4));}
    for(int expansion:{0,1}){
        Put(game,0x108,expansion);int seen7=0,seen8=0,seen9=0;
        for(uint32_t seed=1;seed<=128;++seed){treasureItems.clear();Put(&monster,0x28,seed);Put(&monster,0x2c,uint32_t(0));
            assert(DropWithTreasureRules(game,&monster,nullptr,1,0,90,0,nullptr,&count,0,-1,0,true));
            assert(count==2 && treasureItems.size()==2);
            for(int id:treasureItems){assert(id==7 || id==8 || id==9);seen7+=id==7;seen8+=id==8;seen9+=id==9;}}
        assert(seen7 && seen8 && seen9);
    }
    assert(records==withBlank && blankRoot==rootEntries && blankChild==childEntries);Put(game,0x108,1);
    // Deterministic roots retain their real fixed drops and one weighted
    // root bonus. All-Blank rows terminate without reroll loops.
    entry(blankRoot[0].data(),0,7);entry(blankRoot[1].data(),1,4,4);entry(blankRoot[2].data(),2,8);record(1,-3,blankRoot.data(),3,3);
    plentifulCall();assert(count==3 && treasureItems[0]==7 && treasureItems[1]==8);
    entry(blankRoot[0].data(),0,4,4);record(1,1,blankRoot.data(),1,1000000);plentifulCall();assert(count==0 && treasureItems.empty());
    record(1,1,blankRoot.data(),1,1);call(1,nullptr,&count);assert(count==0 && treasureItems.empty());
    record(1,1,entries.data(),1,1);record(3,1,child.data(),1,1);
    // A creation observer sees native overflow beyond a caller's two slots,
    // including the combined Plentiful/Surge root bonus path and negative phases.
    auto observedCall=[&](int bonus,bool plentiful,Unit** items,int maximum){treasureItems.clear();treasureObserved.clear();Put(&monster,0x28,uint32_t(1));Put(&monster,0x2c,uint32_t(0));assert(DropWithTreasureRules(game,&monster,nullptr,1,0,90,0,items,&count,maximum,-1,bonus,plentiful,TreasureObserve));assert(!treasureCreatedObserver);};
    observedCall(1,true,output.items.data(),2);assert(count==2 && treasureItems.size()==3 && treasureObserved.size()==3 && output.sentinel==0x1234567812345678ULL);
    record(1,-2,negativeChildren.data(),2,2);observedCall(2,false,output.items.data(),2);assert(count==2 && treasureItems.size()==4 && treasureObserved.size()==4 && treasureItems[0]==7 && treasureItems[1]==8);
    record(1,2,negativeChildren.data(),2,2);treasureItems.clear();Put(&monster,0x28,uint32_t(1));Put(&monster,0x2c,uint32_t(0));
    Native<DropFunction>(LootNativeData::Source)(game,&monster,nullptr,1,0,90,0,nullptr,&count,0);const auto observerBaseline=treasureItems;const auto observerSeed=Field<uint64_t>(&monster,0x28);
    observedCall(0,false,nullptr,0);assert(count==2 && treasureObserved.size()==2 && treasureItems==observerBaseline && Field<uint64_t>(&monster,0x28)==observerSeed);
    // Exercise the exported hook, not just its low-level helper: a timed
    // Golden Goose + Plentiful recipient gets exact event loot and every
    // resulting gold pile is multiplied, including caller-array overflow.
    Put(itemRows[7].data(),0x80,uint32_t(0x20646c67));
    global(0x1ca0390,reinterpret_cast<uintptr_t>(itemRows.data()));global(0x1ca0398,itemRows.size());
    void* linker=reinterpret_cast<void*>(1);global(0x1c9f6a8,reinterpret_cast<uintptr_t>(linker));
    stub(0x1e5620,TreasureStat);stub(0x1e5440,TreasureSet);stub(0x6d0470,TreasureName);
    Put(monStats.data(),0x3c,uint32_t((1u<<25)|(1u<<15)|1));D2MonsterDataStrc data{};data.pMonstatsTxt=reinterpret_cast<D2MonStatsTxt*>(monStats.data());monster.pMonsterData=&data;
    Unit hero{},other{};hero.dwUnitId=55;other.dwUnitId=56;game->dwGameFrame=100;
    const bool wasInstalled=installed.exchange(true);GrantTimedReward(game,55,Event::GoldenGoose);GrantTimedReward(game,55,Event::PlentifulPirating);
    record(1,1,entries.data(),1,1);record(3,1,child.data(),1,1);entry(child.data(),0,7);
    auto hook=[&](Unit* source){treasureItems.clear();Put(source,0x28,uint32_t(1));Put(source,0x2c,uint32_t(0));return Drop(game,source,&hero,1,0,90,0,output.items.data(),&count,2,Native<DropFunction>(LootNativeData::Source));};
    assert(hook(&monster) && count==2 && treasureItems.size()==2 && output.sentinel==0x1234567812345678ULL);
    for(size_t i=0;i<treasureItems.size();++i)assert(treasureGold.at(&treasureCreated[i])==600);
    monster.dwUnitType=2;assert(hook(&monster) && treasureItems.size()==2);for(size_t i=0;i<treasureItems.size();++i)assert(treasureGold.at(&treasureCreated[i])==600);monster.dwUnitType=1;
    assert(!Drop(game,&monster,&other,1,0,90,0,nullptr,nullptr,0,Native<DropFunction>(LootNativeData::Source)));
    Unit ordinaryItem{};ordinaryItem.dwUnitType=4;ordinaryItem.dwClassId=9;treasureGold[&ordinaryItem]=100;GoldSixfold(&ordinaryItem);assert(treasureGold.at(&ordinaryItem)==100);
    ordinaryItem.dwClassId=7;treasureGold[&ordinaryItem]=INT_MAX;GoldSixfold(&ordinaryItem);assert(treasureGold.at(&ordinaryItem)==INT_MAX);
    // The event table (item8) differs from the goblin's native table (gold7).
    // Exercise the complete Drop path with the actual native TC selector and
    // walker, both normally and with simultaneous Plentiful/Golden bonuses.
    Bytes<0x108> grottoRoom{};Bytes<0x1c0> grottoR2{};Bytes<0x288> grottoLevel{};
    treasureRoom=reinterpret_cast<Room*>(grottoRoom.data());Put(grottoRoom.data(),0x18,grottoR2.data());Put(grottoR2.data(),0x90,grottoLevel.data());Put(grottoLevel.data(),0x1f8,39);
    stub(0x209530,TreasureRoom);FlushInstructionCache(GetCurrentProcess(),image+0x209530,14);
    auto& grotto=State(game).run;grotto=std::make_unique<Run>();grotto->event=Event::GoblinGrotto;grotto->eventPhase=EventPhase::Active;grotto->phase=Phase::Running;grotto->level=39;grotto->eventTreasureClass=1;
    entry(entries[0].data(),0,8);record(1,1,entries.data(),1,1);entry(child.data(),0,7);record(3,1,child.data(),1,1);
    auto grottoDrop=[&](Unit* killer){
        treasureItems.clear();Put(&monster,0x28,uint32_t(1));Put(&monster,0x2c,uint32_t(0));count=-1;
        auto original=Native<DropFunction>(LootNativeData::Source);
        const bool handled=Drop(game,&monster,killer,3,0,90,0,nullptr,&count,0,original);
        if(!handled)original(game,&monster,killer,3,0,90,0,nullptr,&count,0);
        return handled;
    };
    assert(!grottoDrop(&other) && count==1 && treasureItems==std::vector<int>{7});
    assert(grottoDrop(&hero) && count==2 && treasureItems==std::vector<int>({7,7}));
    for(size_t i=0;i<treasureItems.size();++i)assert(treasureGold.at(&treasureCreated[i])==600);
    const auto goblinTables=records;
    for(auto phase:{EventPhase::Entering,EventPhase::Active,EventPhase::Returning}){
        grotto->eventPhase=phase;assert(EventTreasureClass(game,grotto.get(),&monster,true)==-1);
    }
    game->dwGameFrame=3099;assert(grottoDrop(&hero) && count==2);
    game->dwGameFrame=3100;assert(grottoDrop(&hero) && count==1 && treasureItems==std::vector<int>{7});
    assert(treasureGold.at(&treasureCreated[0])==600); // Golden Goose still lasts ten minutes.
    assert(records==goblinTables);grotto.reset();treasureRoom=nullptr;
    // Soul Surprise must override the generic MapGroupA root with its own
    // deterministic wrapper -> weighted chest -> S02/S03/S04/S05 chain.
    treasureRoom=reinterpret_cast<Room*>(grottoRoom.data());Put(grottoLevel.data(),0x1f8,239);
    grotto=std::make_unique<Run>();grotto->event=Event::SoulSurprise;grotto->eventPhase=EventPhase::Active;
    grotto->phase=Phase::Running;grotto->level=239;grotto->eventTreasureClass=LookupName(0x1c9f6a8,"SoulSurprise");
    assert(grotto->eventTreasureClass==2);
    std::array<Bytes<0x1c>,4> soulEntries{};Bytes<0x1c> soulRoot{};
    for(int i=0;i<4;++i){entry(soulEntries[i].data(),i,7+i);Put(itemRows[7+i].data(),0x80,uint32_t(0x00323053+(i<<16)));}
    entry(soulRoot.data(),0,3,4);records[2]={};records[3]={};
    record(2,-1,soulRoot.data(),1,1);record(3,1,soulEntries.data(),4,4);
    const auto soulTables=records;const auto soulChild=soulEntries;
    for(bool timed:{false,true}){
        // Regrant Plentiful alongside Golden Goose and Treasure Trove; its
        // additional root pick may add a fragment, but must keep this chain.
        game->dwGameFrame=3200;
        if(timed){GrantTimedReward(game,55,Event::PlentifulPirating);GrantTimedReward(game,55,Event::TreasureTrove);}
        std::set<int> found;
        for(uint32_t seed=1;seed<=128;++seed){
            grotto->soulDropObserved=false;treasureItems.clear();Put(&monster,0x28,seed);Put(&monster,0x2c,uint32_t(0));
            assert(Drop(game,&monster,timed?&hero:&other,1,0,90,0,nullptr,&count,0,Native<DropFunction>(LootNativeData::Source)));
            assert(count==(timed?2:1) && grotto->soulDropObserved);
            for(int id:treasureItems){assert(id>=7&&id<=10);found.insert(id);}
        }
        assert(found.size()==4 && records==soulTables && soulEntries==soulChild);
    }
    grotto.reset();treasureRoom=nullptr;
    game->dwGameFrame+=15000;assert(!hook(&monster));games.erase(game);installed.store(wasInstalled);
    assert(lootSelector.get()==selector);
    DWORD64 runtimeBase=0;assert(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(lootSelector->code+0x100),&runtimeBase,nullptr));
    assert(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(plentifulLootWalker->code+0x1800),&runtimeBase,nullptr));
    for(const auto& [rva,bytes]:savedCode)memcpy(image+rva,bytes.data(),bytes.size());
    for(const auto& [rva,value]:savedData)Put(image,rva,value);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    std::cout<<"Native Soul Surprise: exact root/chest chain reaches all four fragment items, including simultaneous timed loot bonuses; shared tables remain unchanged.\n";
    std::cout<<"Native Goblin Grotto: original root survives normal drops and simultaneous Plentiful/Golden bonuses.\n";
    std::cout<<"Native loot: TC upgrades, positive/negative extra picks, NoDrop RNG, nested TC counts, expanded 7-9 cap, immutable tables and bounded output arrays passed.\n";
    std::cout<<"Native Plentiful loot: NoDrop0/Blank removal at all depths, +1 root Pick only and preserved nested Picks; deterministic rows, objects, exact per-kill overrides, mixed bonuses, full creation observers and timed scope remain correct.\n";
}
