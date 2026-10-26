// Included by cube-visual-test.cpp, after its other native-image fixtures.
// The harness supplies QuestJournal mock Bindings/Inspect/Refused. This fixture
// tests the isolated scanner directly and cannot touch the installed game.
namespace QuestCubeNativeTest {
using Bytes=std::array<uint8_t,0x1b4>;
std::array<Bytes,3> definitions{};
std::vector<Bytes> eventDefinitions;
std::array<std::array<uint8_t,0x144>,512> costs{};
std::array<Unit,2> units{};std::array<D2ItemDataStrc,2> metadata{};
D2InventoryStrc inventory{};Unit owner{};Game game{};
int hidden=7,playerStat=0,itemSpecial=5,statReads=0,baseReads=0;
template<class T>void Put(void* p,size_t offset,T value){memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
uint32_t __fastcall Code(Unit* item){return QuestNative::Field<uint32_t>((eventDefinitions.empty()?definitions[item->dwClassId]:eventDefinitions.at(item->dwClassId)).data(),0x80);}
int __fastcall Stat(void*,uint32_t key,void*){
    ++statReads;if(key==(398u<<16))return hidden;if(key==(474u<<16))return itemSpecial;
    assert(key==(12u<<16) || key==(210u<<16));return playerStat;
}
int __fastcall BaseStat(Unit*,uint32_t stat,uint16_t layer){assert(stat==12 && !layer);++baseReads;return playerStat;}
int __fastcall PlayerStat(Unit*,uint32_t stat,uint16_t layer){assert(stat==12 && !layer);++baseReads;return playerStat;}
Recipe Make(){Recipe r{};r.header[0]=1;r.header[3]=255;r.header[4]=18;r.header[0x10]=2;
    Put(r.header.data(),8,int32_t(398));Put(r.header.data(),12,int32_t(7));
    for(size_t n=0;n<2;++n){Put(r.header.data(),0x14+n*8,uint16_t(1));Put(r.header.data(),0x16+n*8,uint16_t(n));}return r;
}
void Native(uint8_t* image){
    const auto savedTemplate=QuestNative::original;const bool wasReady=QuestNative::ready.load();
    assert(InitQuestMatcher());
    image[0x305d00]^=1;assert(!InitQuestMatcher());image[0x305d00]^=1;assert(InitQuestMatcher());
    image[0x308840]^=1;assert(!InitQuestMatcher());image[0x308840]^=1;assert(InitQuestMatcher());
    std::map<uint32_t,std::array<uint8_t,14>> oldCode;std::map<uint32_t,uint64_t> oldData;
    auto data=[&](uint32_t rva,uint64_t value){oldData.emplace(rva,QuestNative::Field<uint64_t>(image,rva));Put(image,rva,value);};
    auto stub=[&](uint32_t rva,auto fn){std::array<uint8_t,14> old{};memcpy(old.data(),image+rva,14);oldCode.emplace(rva,old);const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);const auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    definitions={};metadata={};units={};inventory={};owner={};game={};costs={};
    inventory.dwSignature=0x1020304;inventory.pFirstItem=&units[0];inventory.pOwner=&owner;
    owner.dwUnitType=0;owner.dwClassId=1;owner.pInventory=&inventory;owner.pStatListEx=reinterpret_cast<D2StatListExStrc*>(uintptr_t(1));game.bExpansion=1;
    for(size_t n=0;n<2;++n){auto& u=units[n];u.dwUnitType=4;u.dwClassId=uint32_t(n);u.pItemData=&metadata[n];u.pStatListEx=reinterpret_cast<D2StatListExStrc*>(uintptr_t(1));metadata[n].nPage=3;metadata[n].dwItemLevel=50;metadata[n].pExtraData.pNextItem=n==0?&units[1]:nullptr;Put(definitions[n].data(),0x80,uint32_t(0x20303061+n));}
    data(0x1ca0390,reinterpret_cast<uintptr_t>(definitions.data()));data(0x1ca0398,definitions.size());
    data(0x1c9fb78,reinterpret_cast<uintptr_t>(costs.data()));data(0x1c9fb80,costs.size());data(0x1bfa310,0);
    stub(0x1f9e70,Code);stub(0x1e2e70,Stat);stub(0x1e2e00,Stat);stub(0x1e56a0,PlayerStat);
    stub(0x208330,+[](Unit*)->void*{return &inventory;});stub(0x1434c50,+[](){});stub(0x1438100,+[](void* dest,int value,size_t count)->void*{return memset(dest,value,count);});
    stub(0xae3a10,+[]()->int{return 0;});stub(0xae4920,+[](void* time,void*,int)->void*{static int fields[8]{0,0,0,2026,10,19,0,0};memcpy(time,fields,sizeof(fields));return time;});
    data(0x1baa468,reinterpret_cast<uintptr_t>(+[](void* filetime){memset(filetime,0,8);}));
    std::vector<Recipe> recipes(3,Make());data(0x1ca0b18,reinterpret_cast<uintptr_t>(recipes.data()));data(0x1ca0b20,recipes.size());
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    Input output[3]{};RuneMatch match{};
    auto run=[&](std::vector<Recipe>& table){QuestNative::Invocation scanner;assert(scanner.Build(table.data(),table.size()));
        DWORD64 unwindBase=0;const auto unwind=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(scanner.memory+0x100),&unwindBase,nullptr);assert(unwind && unwindBase==reinterpret_cast<DWORD64>(scanner.memory));
        memset(output,0,sizeof(output));match={};reinterpret_cast<RuneMatcher>(scanner.memory)(&match,&game,&owner,output);return match.matched;};
    // Original native op still rejects a transferred bag's unrelated stat.
    hidden=8;assert(!run(recipes));hidden=7;assert(run(recipes)&&match.row==0);
    recipes[0].header[4]=0;hidden=0;const int reads=statReads;
    assert(run(recipes)&&match.row==0&&statReads==reads);
    // Blocked quest rows cannot shadow a later ordinary matching recipe.
    recipes[0].header[0]=0;recipes[0].header[0x10]=0;recipes[1].header[0x10]=0;recipes[2].header[4]=0;
    assert(run(recipes)&&match.row==2);
    // Native debug override also cannot reopen a skipped journal stage.
    data(0x1bfa310,1);assert(run(recipes)&&match.row==2);Put(image,0x1bfa310,uint64_t(0));
    // Exercise the relocated switch dispatcher with ordinary player-stat ops.
    recipes[2].header[4]=4;Put(recipes[2].header.data(),8,int32_t(12));Put(recipes[2].header.data(),12,int32_t(10));
    playerStat=11;assert(!run(recipes));playerStat=10;assert(run(recipes));playerStat=9;assert(run(recipes)&&match.row==2);
    recipes[2].header[4]=8;playerStat=11;assert(!run(recipes));playerStat=10;assert(run(recipes));
    recipes[2].header[4]=12;playerStat=11;assert(!run(recipes));playerStat=10;assert(run(recipes));
    recipes[2].header[4]=14;playerStat=10;assert(run(recipes));playerStat=9;assert(!run(recipes));
    recipes[2].header[4]=0;
    recipes[2].header[2]=1;game.nDifficulty=0;assert(!run(recipes));game.nDifficulty=1;assert(run(recipes));recipes[2].header[2]=0;
    recipes[2].header[3]=2;assert(!run(recipes));recipes[2].header[3]=1;assert(run(recipes));recipes[2].header[3]=255;
    Put(recipes[2].header.data(),0x12,uint16_t(100));game.bExpansion=0;assert(!run(recipes));game.bExpansion=1;assert(run(recipes));
    recipes[2].header[1]=1;assert(!run(recipes));Put(&game,0x10c,int32_t(1));assert(run(recipes));recipes[2].header[1]=0;
    metadata[0].dwQualityNo=2;recipes[2].header[0x1a]=7;assert(!run(recipes));recipes[2].header[0x1a]=0;
    units[1].dwClassId=2;assert(!run(recipes));units[1].dwClassId=1;
    metadata[1].nPage=0;assert(!run(recipes));metadata[1].nPage=3;
    recipes[2].header[0x23]=2;assert(!run(recipes));recipes[2].header[0x23]=0;
    Put(recipes[2].header.data(),0x1e,uint16_t(0));assert(!run(recipes));Put(recipes[2].header.data(),0x1e,uint16_t(1));
    recipes[2].outputs[0].type=254;assert(run(recipes));assert(output[0].item==&units[0] && output[0].base==-1 && output[0].level==50);
    assert(metadata[0].dwItemLevel==50&&metadata[1].dwItemLevel==50);
    // Exercise the public adapter, including ordinary fallthrough when an
    // earlier quest stage is unavailable and gates on unrelated stats.
    const auto previousRules=::testQuestRules;const auto previousProgress=::testQuestProgress;
    // Reproduce the event's native wildcard portal recipe, followed by an
    // ordinary single-item recipe. The unsafe row must not shadow the latter,
    // including when no journal is configured at all.
    ::testQuestRules.reset();metadata[1].nPage=0;
    recipes.assign(3,Recipe{});recipes[0].header[0]=1;recipes[0].header[3]=255;recipes[0].header[0x10]=1;
    recipes[0].outputs[0]=CubeMap::Data::Encode(0);
    recipes[1].header[0]=1;recipes[1].header[3]=255;recipes[1].header[0x10]=1;Put(recipes[1].header.data(),0x14,uint16_t(1));
    Put(image,0x1ca0b18,recipes.data());assert(run(recipes)&&match.row==0);
    const auto malformedSource=recipes;
    assert(TryQuestMatch(&match,&game,&owner,output)&&match.matched&&match.row==1);
    assert(!memcmp(malformedSource.data(),recipes.data(),recipes.size()*sizeof(Recipe)));
    if(!eventTableFixture.empty()){
        auto load=[&]<class T>(const char* name,std::vector<T>& rows){
            std::ifstream file(eventTableFixture/name,std::ios::binary|std::ios::ate);assert(file);
            const auto bytes=file.tellg();file.seekg(0);uint32_t count=0;file.read(reinterpret_cast<char*>(&count),4);
            assert(count&&bytes==std::streamoff(4+size_t(count)*sizeof(T)));const auto start=rows.size();rows.resize(start+count);
            file.read(reinterpret_cast<char*>(rows.data()+start),size_t(count)*sizeof(T));assert(file);
        };
        load("weapons.bin",eventDefinitions);load("armor.bin",eventDefinitions);load("misc.bin",eventDefinitions);
        std::vector<Recipe> eventRecipes;load("cubemain.bin",eventRecipes);assert(eventRecipes.size()>298);
        const auto found=std::find_if(eventDefinitions.begin(),eventDefinitions.end(),[](const auto& row){return !memcmp(row.data()+0x80,"m01 ",4);});
        assert(found!=eventDefinitions.end());const auto mapItem=uint32_t(found-eventDefinitions.begin());
        assert(!UnrestrictedMapRecipe(eventRecipes[298]));
        Put(image,0x1ca0390,eventDefinitions.data());Put(image,0x1ca0398,uint64_t(eventDefinitions.size()));
        std::vector<Recipe> portal{eventRecipes[298]};
        for(uint32_t ordinary:{0u,1u,1544u,1545u}){units[0].dwClassId=ordinary;assert(!run(portal));}
        units[0].dwClassId=mapItem;assert(run(portal)&&match.row==0);
        units[0].dwClassId=0;eventDefinitions.clear();
        Put(image,0x1ca0390,definitions.data());Put(image,0x1ca0398,uint64_t(definitions.size()));
        std::cout<<"Actual event BIN map recipe rejects ordinary items and existing m03/m04; accepts the appended m01 item.\n";
    }
    metadata[1].nPage=3;
    recipes.assign(3,Make());recipes[2].header[4]=0;Put(image,0x1ca0b18,recipes.data());
    auto rules=std::make_shared<QuestJournal::Rules>();rules->rows=3;
    rules->gates={{0,1,false,1,2,18,398,7},{1,1,true,1,2,18,398,7}};
    ::testQuestRules=rules;::testQuestProgress={true,0,false};hidden=0;
    auto managed=[&](){match={};assert(TryQuestMatch(&match,&game,&owner,output));return match.matched;};
    const auto untouched=recipes;
    assert(managed()&&match.row==0);::testQuestProgress.objectiveReady=true;assert(managed()&&match.row==1);
    ::testQuestProgress.completed=1;assert(managed()&&match.row==2);
    ::testQuestProgress={false,0,false};assert(managed()&&match.row==2);
    assert(!memcmp(untouched.data(),recipes.data(),recipes.size()*sizeof(Recipe)));
    // An unrelated appended recipe changes the BIN size/count, but the quest
    // rows are still valid. No whole-table identity may disable their matcher.
    recipes.push_back(Recipe{});Put(image,0x1ca0b18,recipes.data());Put(image,0x1ca0b20,uint64_t(recipes.size()));
    ::testQuestProgress={true,0,false};assert(managed()&&match.row==0);
    recipes.pop_back();Put(image,0x1ca0b18,recipes.data());Put(image,0x1ca0b20,uint64_t(recipes.size()));
    // Actual quest gate changes remain a targeted validation failure.
    recipes[0].header[0x10]=3;assert(!managed());recipes[0].header[0x10]=2;
    ::testQuestProgress={true,0,false};
    recipes[0].header[4]=6;rules->gates[0].op=6;assert(managed()&&match.row==0); // Higher player-stat progress gate is journal-owned too.
    recipes[0].header[4]=3;rules->gates[0].op=3;Put(recipes[0].header.data(),8,int32_t(210));rules->gates[0].param=210;
    playerStat=7;assert(managed()&&match.row==0);playerStat=6;assert(managed()&&match.row==2);
    recipes[0].header[4]=18;rules->gates[0].op=18;Put(recipes[0].header.data(),8,int32_t(474));rules->gates[0].param=474;
    itemSpecial=7;assert(managed()&&match.row==0);itemSpecial=6;assert(managed()&&match.row==2);
    rules->gates[0].param=398;assert(!managed()); // Source/native drift fails before output.
    // Event mode retires every proof-producing row. A milestone privately
    // removes only the proof slot, retaining three distinct real ingredients.
    recipes.assign(3,Make());recipes[1].header[0x10]=4;
    Put(recipes[1].header.data(),0x1e,uint16_t(6));
    for(int slot=2;slot<4;++slot){Put(recipes[1].header.data(),0x14+slot*8,uint16_t(1));Put(recipes[1].header.data(),0x16+slot*8,uint16_t(1));}
    Unit extra=units[1];D2ItemDataStrc extraData=metadata[1];extra.pItemData=&extraData;extraData.pExtraData.pNextItem=nullptr;metadata[1].pExtraData.pNextItem=&extra;extra.dwClassId=2;units[0].dwUnitId=1;units[1].dwUnitId=2;extra.dwUnitId=3;Put(recipes[1].header.data(),0x2e,uint16_t(2));
    recipes[2].header[4]=0;recipes[2].header[0x10]=3;Put(recipes[2].header.data(),0x24,uint16_t(1));Put(recipes[2].header.data(),0x26,uint16_t(2));
    Put(image,0x1ca0b18,recipes.data());rules->gates={{0,1,false,1,2,18,398,7},{1,1,true,1,4,18,398,7}};
    rules->events=true;rules->quests.resize(1);rules->quests[0].milestone=true;::testQuestProgress={true,0,false};
    assert(managed()&&match.row==2);::testQuestProgress.objectiveReady=true;
    const auto milestoneSource=recipes;assert(managed()&&match.row==1);
    assert(!memcmp(milestoneSource.data(),recipes.data(),recipes.size()*sizeof(Recipe)));
    rules->quests[0].milestone=false;assert(managed()&&match.row==2);
    // A declared recipe lesson enables only its objective row while current.
    recipes[0].header[0x10]=3;recipes[0].header[4]=0;rules->gates[0].inputs=3;rules->gates[0].op=0;
    QuestCubeNativeTest::Put(recipes[0].header.data(),0x24,uint16_t(1));QuestCubeNativeTest::Put(recipes[0].header.data(),0x26,uint16_t(2));
    rules->quests[0].trigger="questRecipe";::testQuestProgress={true,0,false};assert(managed()&&match.row==0);
    ::testQuestProgress.objectiveReady=true;assert(managed()&&match.row==2);
    ::testQuestProgress={true,1,false};assert(managed()&&match.row==2);
    metadata[1].pExtraData.pNextItem=nullptr;
    ::testQuestRules=previousRules;::testQuestProgress=previousProgress;
    for(const auto& [rva,bytes]:oldCode)memcpy(image+rva,bytes.data(),bytes.size());for(const auto& [rva,value]:oldData)Put(image,rva,value);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);QuestNative::original=savedTemplate;QuestNative::ready=wasReady;
    std::cout<<"Actual native quest scanner retains ordinary recipe ordering, debug exclusion, player-stat switch ops, expansion/ladder/difficulty/class gates, item quality/base/page/quantity/unique-consumption predicates and useitem selection without modifying shared recipes or hidden item stats.\n";
}
}
namespace QuestCubeOutputTest {
std::array<Unit,8> units{};std::array<D2ItemDataStrc,8> metadata{};std::array<bool,8> exists{};
std::array<std::array<uint8_t,0x1b4>,3> definitions{};
Unit owner{};Game game{};D2InventoryStrc inventory{};
int nextCreated=2,failGeneration=-1,failPlacement=-1,placements=0,destroyed=0,notifications=0;bool preflight=true,spaceAvailable=true;
Unit* Lookup(uint32_t id){for(size_t n=0;n<units.size();++n)if(exists[n]&&units[n].dwUnitId==id)return &units[n];return nullptr;}
void Native(uint8_t* image){
    const auto previousRules=::testQuestRules;const auto previousProgress=::testQuestProgress;const bool previousBegin=::testQuestBegin;
    const auto oldCreate=originalCreate;const auto oldAdd=originalAdd;const auto oldNotify=originalNotify;const bool oldInstalled=installed;
    std::map<uint32_t,std::array<uint8_t,14>> oldCode;std::map<uint32_t,uint64_t> oldData;
    auto data=[&](uint32_t rva,uint64_t value){oldData.emplace(rva,QuestNative::Field<uint64_t>(image,rva));QuestCubeNativeTest::Put(image,rva,value);};
    auto stub=[&](uint32_t rva,auto fn){std::array<uint8_t,14> old{};memcpy(old.data(),image+rva,14);oldCode.emplace(rva,old);const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);const auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    data(0x1ca0390,reinterpret_cast<uintptr_t>(definitions.data()));data(0x1ca0398,definitions.size());
    data(0x1c9ebd8,1);int maxLevel=99;data(0x1c9fd68,reinterpret_cast<uintptr_t>(&maxLevel));
    stub(0x208330,+[](Unit*)->void*{return &inventory;});stub(0x213a00,+[](void* location)->void*{memset(location,0,16);return location;});
    stub(0x213a20,+[](void* location,Unit*)->void*{memset(location,0,16);return location;});
    stub(0x2dba00,+[]()->bool{return preflight;});stub(0x1434c50,+[](){});
    stub(0x6d0470,+[](void*,const char* name)->int{return !strcmp(name,"rng-generator")?99:-1;});
    stub(0x1e5620,+[](Unit*,uint32_t stat,uint16_t)->int{return stat==12?99:0;});
    stub(0x2c49c0,+[](Game*,int,uint32_t id)->Unit*{return Lookup(id);});
    stub(0x2c4950,+[](int,Game*,uint32_t id)->Unit**{static Unit* result;result=Lookup(id);return &result;});
    stub(0x2f70a0,+[](Game*,Unit* item){for(size_t n=0;n<units.size();++n)if(&units[n]==item){exists[n]=false;++destroyed;return;}assert(false);});
    stub(0x22e750,+[](Unit*){});stub(0x209e40,+[]()->int{return 3;});
    stub(0x210200,+[](void*,Unit*,int,int* x,int* y,uint8_t)->int{*x=0;*y=0;return spaceAvailable?1:0;});
    stub(0x1fe680,+[]()->int{return 0;});stub(0x2d5e10,AddHook);stub(0x316b80,NotifyHook);
    stub(0x2f58c0,+[](Game*,LegacyTransmog::Data::Drop* drop,int)->Unit*{
        const int n=nextCreated++;if(n==failGeneration)return nullptr;assert(n>=2&&n<int(units.size()));
        auto& item=units[n];exists[n]=true;item.dwClassId=drop->id;metadata[n].dwItemLevel=drop->level;return &item;
    });
    originalAdd=+[](Unit* player,const Location*,const uint32_t* ids,size_t count,bool)->bool{
        assert(player==&owner);if(count==1){++placements;if(int(*ids)==failPlacement)return false;}
        return true;
    };
    originalNotify=+[](Game*,Unit*,Unit*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t){++notifications;};
    originalCreate=reinterpret_cast<Create>(image+0x306c50);installed=true;
    std::vector<Recipe> recipes(1);data(0x1ca0b18,reinterpret_cast<uintptr_t>(recipes.data()));data(0x1ca0b20,1);
    auto rules=std::make_shared<QuestJournal::Rules>();rules->rows=1;rules->gates={{0,1,false,1,2,18,398,7}};::testQuestRules=rules;::testQuestProgress={true,0,false};::testQuestBegin=true;
    auto reset=[&](int outputs){
        units={};metadata={};exists={};owner={};inventory={};game={};nextCreated=2;failGeneration=failPlacement=-1;placements=destroyed=notifications=0;preflight=spaceAvailable=true;questOutput=nullptr;current=nullptr;runeTransaction=nullptr;
        inventory.dwSignature=0x1020304;inventory.pOwner=&owner;inventory.pFirstItem=&units[0];owner.pInventory=&inventory;game.bExpansion=1;
        for(size_t n=0;n<units.size();++n){units[n].dwUnitType=4;units[n].dwUnitId=uint32_t(100+n);units[n].dwClassId=n?1:0;units[n].pItemData=&metadata[n];metadata[n].nPage=3;metadata[n].dwItemLevel=50;metadata[n].dwQualityNo=2;metadata[n].pExtraData.pParentInv=&inventory;}
        exists[0]=exists[1]=true;metadata[0].pExtraData.pNextItem=&units[1];
        auto& recipe=recipes[0];recipe=QuestCubeNativeTest::Make();recipe.outputs[0].type=254;
        for(int n=1;n<outputs;++n){recipe.outputs[n].type=252;recipe.outputs[n].base=1;recipe.outputs[n].quality=2;recipe.outputs[n].level=1;}
        ::testQuestOutcomes.clear();
    };
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    Input inputs[3]{};
    auto execute=[&](){inputs[0]={&units[0],-1,50};inputs[1]={};inputs[2]={};assert(CreateQuest(&game,&owner,recipes.data(),inputs));assert(::testQuestOutcomes.size()==1);return ::testQuestOutcomes.back();};
    reset(2);assert(execute()==QuestJournal::Outcome::Committed);assert(placements==2&&destroyed==1&&exists[0]&&!exists[1]);
    reset(3);assert(execute()==QuestJournal::Outcome::Committed);assert(placements==3&&destroyed==1);
    reset(3);failPlacement=102;assert(execute()==QuestJournal::Outcome::Ambiguous);
    reset(2);preflight=false;assert(execute()==QuestJournal::Outcome::Rejected);assert(!destroyed&&!placements&&exists[1]);
    reset(2);rules->events=true;rules->quests.resize(1);rules->quests[0].trigger="questRecipe";
    assert(execute()==QuestJournal::Outcome::Committed&&destroyed==1&&exists[2]&&placements==2);
    reset(2);preflight=false;assert(execute()==QuestJournal::Outcome::Rejected&&!destroyed&&!placements);
    reset(1);recipes[0].outputs[0]={};recipes[0].outputs[0].type=252;recipes[0].outputs[0].base=1;recipes[0].outputs[0].quality=2;recipes[0].outputs[0].level=1;
    assert(execute()==QuestJournal::Outcome::Committed&&destroyed==2&&!exists[0]&&!exists[1]&&exists[2]&&placements==1);
    rules->quests[0].trigger.clear();rules->events=false;
    reset(3);failGeneration=2;assert(execute()==QuestJournal::Outcome::Ambiguous);
    reset(2);::testQuestBegin=false;assert(CreateQuest(&game,&owner,recipes.data(),inputs));assert(::testQuestOutcomes.empty()&&!destroyed&&!placements);
    // Direct scope checks also cover failure before any output callback, using
    // native GUID/ownership/quantity snapshots rather than a void return value.
    reset(2);{QuestOutputScope scope(&game,&owner,rules->gates[0],recipes[0]);assert(scope.Outcome()==QuestJournal::Outcome::Rejected);exists[1]=false;assert(scope.Outcome()==QuestJournal::Outcome::Ambiguous);}
    // Native's final Add restores unconsumed quantities, even after every
    // ordinary output succeeded. A stacked skull/remover is not a failure.
    reset(1);{QuestOutputScope transaction(&game,&owner,rules->gates[0],recipes[0]);questOutput=&transaction;
        const uint32_t output=units[0].dwUnitId,remainder=units[1].dwUnitId;
        QuestAdded(true,&owner,&output,1,Pattern::Address(0x30812a));
        QuestAdded(true,&owner,&remainder,1,Pattern::Address(0x3085d2));
        assert(transaction.finalSeen&&transaction.Outcome()==QuestJournal::Outcome::Committed);
        QuestAdded(false,&owner,&remainder,1,Pattern::Address(0x3085d2));
        assert(transaction.Outcome()==QuestJournal::Outcome::Ambiguous);questOutput=nullptr;}
    reset(3);{QuestOutputScope transaction(&game,&owner,rules->gates[0],recipes[0]);questOutput=&transaction;
        const uint32_t output=units[0].dwUnitId,remainder=units[1].dwUnitId;
        QuestAdded(true,&owner,&output,1,Pattern::Address(0x30812a));
        QuestAdded(true,&owner,&remainder,1,Pattern::Address(0x3085d2));
        assert(transaction.Outcome()==QuestJournal::Outcome::Ambiguous);questOutput=nullptr;}
    reset(1);::testQuestBegin=true;rules->events=true;rules->quests.resize(1);rules->quests[0].milestone=true;
    rules->gates={{0,1,true,1,4,18,398,7}};recipes[0].header[0x10]=4;
    metadata[1].pExtraData.pNextItem=&units[2];exists[2]=true;nextCreated=3;
    auto upgradeOriginal=recipes[0];assert(execute()==QuestJournal::Outcome::Committed&&destroyed==2&&exists[0]&&!exists[1]&&!exists[2]);
    assert(!memcmp(&upgradeOriginal,&recipes[0],sizeof(Recipe)));rules->events=false;
    // A real ordinary useitem deposit must reach the journal after the
    // native input consumption and output placement, with pre-consumption codes.
    reset(1);rules->events=true;::testQuestCrafts.clear();
    QuestCubeNativeTest::Put(definitions[0].data(),0x80,uint32_t(0x2031305a));
    QuestCubeNativeTest::Put(definitions[1].data(),0x80,uint32_t(0x207a6b73));
    inputs[0]={&units[0],-1,50};ObserveCraft(&game,&owner,recipes.data(),inputs,0);
    assert(::testQuestCrafts.size()==1&&::testQuestCrafts[0].row==0&&::testQuestCrafts[0].outputs==1&&::testQuestCrafts[0].inputs==std::vector<uint32_t>({0x2031305a,0x207a6b73}));
    reset(1);::testQuestCrafts.clear();preflight=false;inputs[0]={&units[0],-1,50};ObserveCraft(&game,&owner,recipes.data(),inputs,0);assert(::testQuestCrafts.empty());
    reset(1);::testQuestCrafts.clear();failPlacement=100;inputs[0]={&units[0],-1,50};ObserveCraft(&game,&owner,recipes.data(),inputs,0);assert(::testQuestCrafts.empty());rules->events=false;
    // Proof replacement executes the actual native placement selector's normal
    // inventory branch; no rewards or quest progress are issued by recovery.
    stub(0x2616b0,+[](Output* output,char* name,int,int)->int{if(strcmp(name,"Bt01"))return 0;*output={};output->type=252;output->base=1;return 1;});
    stub(0x207970,+[]()->int{return 0;});stub(0x1fb440,+[]()->int{return 0;});
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    reset(2);assert(RecoverQuestProof(&game,&owner,1));assert(placements==1&&notifications==1&&!destroyed&&::testQuestOutcomes.empty());
    reset(2);spaceAvailable=false;assert(!RecoverQuestProof(&game,&owner,1));assert(!placements&&!notifications&&destroyed==1&&::testQuestOutcomes.empty());
    reset(2);failPlacement=102;assert(!RecoverQuestProof(&game,&owner,1));assert(placements==1&&!notifications&&destroyed==1);
    reset(2);failGeneration=2;assert(!RecoverQuestProof(&game,&owner,1));assert(!placements&&!notifications&&!destroyed);
    for(const auto& [rva,bytes]:oldCode)memcpy(image+rva,bytes.data(),bytes.size());for(const auto& [rva,value]:oldData)QuestCubeNativeTest::Put(image,rva,value);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);originalCreate=oldCreate;originalAdd=oldAdd;originalNotify=oldNotify;installed=oldInstalled;
    ::testQuestRules=previousRules;::testQuestProgress=previousProgress;::testQuestBegin=previousBegin;questOutput=nullptr;
    std::cout<<"Actual native multi-output quest creation commits only complete two/three-output transactions, rejects unchanged preflight failures and blocks partial placement/generation/consumption as ambiguous. Proof recovery uses the native inventory destination and cleans refused/full placement without rewards.\n";
}
}
