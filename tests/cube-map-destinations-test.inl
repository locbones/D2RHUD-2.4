// Included after Put/Bytes in cube-map-test.cpp; invoke RandomDestinations().
void RandomDestinations(){
    for(auto text:{"map=zzz"," MAP = ZzZ ","\"useitem,map=ZZZ\"","map=zzz,useitem"}){
        const auto parsed=Parse(text);assert(parsed.present && parsed.valid && parsed.level==RandomDestination);
        assert(IsMap(Encode(parsed.level)) && Encode(parsed.level).base==0);
    }
    for(auto text:{"map=0","map=zz","map=zzzz","map=zzz,map=12","map=zzz,vis=cap"})assert(!Parse(text).valid);
    const auto source=CubeVisual::Data::ParseSource("enabled\tnuminputs\toutput\toutput b\n1\t1\tuseitem,MAP=zzz\tEv12\n");
    assert(source.rows==1 && source.visuals.size()==1);
    const auto recovered=Parse(source.visuals.at(0).rawOutputs[0]);
    assert(recovered.present && recovered.valid && recovered.useItem && recovered.level==RandomDestination);
    assert(Parse("map=65535").valid && Parse("map=65535").level==65535);
    const auto previous=bindings;const uint32_t count=800;
    std::vector<Bytes<0x184>> records(count);std::vector<Bytes<0x9c>> definitions(count);
    auto table=records.data();auto defs=definitions.data();
    bindings[0x1c9fd10]=reinterpret_cast<uintptr_t>(&count);
    bindings[0x1c9fd08]=reinterpret_cast<uintptr_t>(&table);
    bindings[0x1c9fd28]=reinterpret_cast<uintptr_t>(&defs);
    auto eligible=[&](int id,const char* key,unsigned act=0){
        auto& record=records[id];auto& def=definitions[id];record={};def={};
        record[6]=uint8_t(act);record[0x36]=5;
        for(size_t offset:{size_t(0x3a),size_t(0x6c)})for(size_t n=0;n<25;++n)Put(record.data(),offset+n*2,int16_t(-1));
        Put(record.data(),0x3a,int16_t(0));Put(record.data(),0x6c,int16_t(10));
        for(unsigned difficulty=0;difficulty<3;++difficulty){Put(record.data(),0x20+difficulty*4,uint32_t(1000));Put(def.data(),0xc+difficulty*4,uint32_t(80));Put(def.data(),0x18+difficulty*4,uint32_t(80));}
        Put(def.data(),0x30,uint32_t(1));Put(def.data(),0x34,uint32_t(2));
        memcpy(record.data()+0xf9,key,(std::min)(strlen(key),size_t(39)));
    };
    for(int id:{1,2,38,78,136,238,512,700})eligible(id,id==38 ? "Tristram" : id==136 ? "Tristram" : id==238 ? "ForestRuins" : "CustomMap",id==136 ? 4 : 0);
    for(int i=0;i<50;++i){const auto key="Torment"+std::to_string(i+1);eligible(137+i,key.c_str(),4);const auto icy="IcyHell"+std::to_string(i+1);eligible(187+i,icy.c_str(),4);}
    for(const char* key:{"Act 5 - Torment Trials 99","ICY_HELL-4","Uber Tristram","Pandemonium Finale","Forest-Ruins"})assert(ExcludedRandomDestination(600,4,key));
    eligible(600,"Uber_Tristram");eligible(601,"Forest_Ruins");eligible(602,"Torment Trials 99");eligible(603,"Icy Hell 99");
    eligible(604,"Tristram",4);eligible(605,"NoGeometry");Put(definitions[605].data(),0x30,uint32_t(0));
    eligible(606,"NoMonsters");for(unsigned d=0;d<3;++d)Put(records[606].data(),0x20+d*4,uint32_t(0));
    eligible(607,"EmptyRoster");for(size_t n=0;n<25;++n){Put(records[607].data(),0x3a+n*2,int16_t(-1));Put(records[607].data(),0x6c+n*2,int16_t(-1));}
    eligible(608,"BadAct",5);eligible(609,"NoSize");for(unsigned d=0;d<3;++d)Put(definitions[609].data(),0xc+d*4,uint32_t(0));
    eligible(610,"NoTileset");Put(definitions[610].data(),0x34,uint32_t(0));eligible(611,"");
    Bytes<0x5900> memory{};auto game=reinterpret_cast<Game*>(memory.data());
    for(unsigned difficulty=0;difficulty<3;++difficulty){
        game->nDifficulty=uint8_t(difficulty);
        assert(RandomMapDestinations(game)==std::vector<int>({2,38,78,512,700}));
    }
    // A custom level's population availability must be checked for the current
    // difficulty, rather than borrowing Normal's density/roster in Hell.
    Put(records[700].data(),0x28,uint32_t(0));game->nDifficulty=2;
    assert(RandomMapDestinations(game)==std::vector<int>({2,38,78,512}));
    game->nDifficulty=0;std::mt19937 a(1234),b(1234);std::set<int> selected;
    for(int i=0;i<200;++i){const int level=SelectRandomMapDestination(game,a);assert(level==SelectRandomMapDestination(game,b));assert((std::set<int>{2,38,78,512,700}).contains(level));selected.insert(level);}
    assert(selected.size()==5);
    table=nullptr;assert(RandomMapDestinations(game).empty() && !SelectRandomMapDestination(game,a));
    table=records.data();defs=nullptr;assert(RandomMapDestinations(game).empty());
    assert(RandomMapDestinations(nullptr).empty());bindings=previous;
    std::cout<<"Random map marker/source recovery, all restricted realms, difficulty-safe custom pools and reproducible selection passed.\n";
}
