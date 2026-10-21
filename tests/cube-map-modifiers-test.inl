std::map<std::string,int> modifierNames;
int __fastcall ModifierName(void*,const char* name){auto it=modifierNames.find(name);return it==modifierNames.end()?-1:it->second;}
void MapModifierRolls(){
    Reset();modifierNames.clear();std::vector<Bytes<0x144>> costs(512);auto table=costs.data();uint64_t total=costs.size();void* linker=reinterpret_cast<void*>(1);
    for(const auto& effect:Effects)for(auto name:{effect.stat,effect.pairedStat})if(name && !modifierNames.contains(name)){
        const int id=int(modifierNames.size());modifierNames[name]=id;costs[id][4]=2;costs[id][8]=16;
    }
    bindings[0x1c9fb90]=reinterpret_cast<uintptr_t>(&linker);bindings[0x6d0470]=reinterpret_cast<uintptr_t>(ModifierName);
    bindings[0x1c9fb78]=reinterpret_cast<uintptr_t>(&table);bindings[0x1c9fb80]=reinterpret_cast<uintptr_t>(&total);
    std::set<int> seen;auto game=fixture.GamePtr();
    for(uint32_t id=1;id<=500;++id){
        Run run;run.id=id;assert(RollModifiers(run,game) && run.rolls.size()==7);
        Packet packet;packet.effects=uint8_t(run.rolls.size());std::copy(run.rolls.begin(),run.rolls.end(),packet.rolls.begin());assert(Data::Valid(packet));
        for(size_t i=0;i<run.rolls.size();++i){
            const auto roll=run.rolls[i];seen.insert(roll.effect);assert(Data::Valid(roll));
            if(roll.effect==LevelEffect)assert(roll.value>=2 && roll.value<=6);
            if(roll.effect==TreasureEffect)assert(roll.value>=1 && roll.value<=3 && run.stats[TreasureEffect]==-1);
            const auto& effect=Effects[roll.effect];if(effect.pairedStat)assert(run.pairedStats[roll.effect]==modifierNames.at(effect.pairedStat));
            for(size_t j=0;j<i;++j)assert(!ConflictingEffects(roll.effect,run.rolls[j].effect));
        }
    }
    for(int effect=27;effect<int(Effects.size());++effect)assert(seen.contains(effect));
    assert(PairedValue(Effects[28],60)==-15 && PairedValue(Effects[29],25)==25 && PairedValue(Effects[30],7)==3);
    assert(PairedValue(Effects[31],51)==25 && PairedValue(Effects[33],-25)==25);
    Packet conflict;conflict.effects=2;conflict.rolls[0]={28,60};conflict.rolls[1]={17,-10};assert(!Data::Valid(conflict));
    conflict.rolls[1]={29,20};assert(Data::Valid(conflict));conflict.version=3;assert(!Data::Valid(conflict));
    const auto cast=modifierNames.at("item_fastercastrate");modifierNames.erase("item_fastercastrate");
    for(uint32_t id=1;id<=80;++id){Run run;run.id=id;assert(RollModifiers(run,game));for(auto roll:run.rolls)assert(roll.effect!=29);}
    modifierNames["item_fastercastrate"]=cast;
    populationCode=nullptr;installed=false;
    std::cout<<"Map themes roll across the full pool, derived paired stats agree, conflicting stats never cancel each other, missing companion stats are skipped, and level/pick bounds and old-protocol rejection hold.\n";
}
void NativeMapModifierBuffs(uint8_t* image){
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    struct Entry{uint32_t key;int value;};std::vector<Bytes<0x144>> costs(512);
    for(auto& record:costs){record[4]=2;record[8]=16;}
    Put(image,0x1c9fb78,costs.data());Put(image,0x1c9fb80,uint64_t(costs.size()));Put(image,0x1c9ebf8,uint64_t(260));
    stub(0x1e4700,NativeAllocateBuff);stub(0x1e45b0,NativeFreeBuff);stub(0x14383a0,&memmove);stub(0x1438100,&memset);stub(0x1434c50,Nothing);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    for(size_t effect=28;effect<Effects.size();++effect){
        std::array<Entry,32> base{},full{},mods{};Bytes<0xb58> root{};
        Put(root.data(),0x1c,uint32_t(0x80000000));buffList={};
        auto vector=[](void* p,size_t offset,auto& values){Put(p,offset,values.data());Put(p,offset+8,uint64_t(0));Put(p,offset+16,uint64_t(0x8000000000000020));};
        vector(root.data(),0x30,base);vector(root.data(),0x80,full);vector(root.data(),0x98,mods);vector(buffList.data(),0x30,mods);
        Unit unit{};unit.dwUnitId=55;unit.dwAnimMode=1;unit.pStatListEx=reinterpret_cast<D2StatListExStrc*>(root.data());Put(root.data(),0x78,&unit);
        Run run;run.state=259;run.stats.fill(-1);run.pairedStats.fill(-1);run.stats[effect]=25;run.pairedStats[effect]=60;
        const int value=(Effects[effect].minimum+Effects[effect].maximum)/2;run.rolls.push_back({uint8_t(effect),int16_t(value)});
        const auto buff=Attach(&unit,run,Effects[effect].side);assert(Stat(&unit,25)==value);
        if(Effects[effect].pairedStat)assert(Stat(&unit,60)==PairedValue(Effects[effect],value));
        Detach(&unit,buff,259);assert(!Stat(&unit,25) && !Stat(&unit,60) && !ContainsList(&unit,buff.list));
    }
    std::cout<<"All new combat themes attach their real primary/companion stats to native unsaved lists and fully detach without changing base stats.\n";
}
