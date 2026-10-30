void TerrainRosters(){
    Reset();std::array<Bytes<0x1dc>,11> monsters{};std::array<Bytes<0x184>,3> levelRows{};
    uint8_t* table=monsters[0].data();uint64_t count=monsters.size();uint8_t* levelTable=levelRows[0].data();uint32_t levelCount=levelRows.size();
    for(size_t n=0;n<monsters.size();++n){
        Put(monsters[n].data(),0x3c,uint32_t((1u<<25)|(1u<<15)|1u));Put(monsters[n].data(),0x4e,uint16_t(3));
        for(size_t offset:{size_t(0x32),size_t(0x50),size_t(0x56),size_t(0x58)})Put(monsters[n].data(),offset,int16_t(-1));
    }
    for(auto& level:levelRows)for(size_t offset:{size_t(0x3a),size_t(0x6c),size_t(0x9e)})for(size_t n=0;n<25;++n)Put(level.data(),offset+n*2,int16_t(-1));
    for(int n=0;n<int(monsters.size());++n)for(size_t offset:{size_t(0x3a),size_t(0x6c),size_t(0x9e)})Put(levelRows[1].data(),offset+size_t(n)*2,int16_t(n));
    bindings[0x1c9f878]=uintptr_t(&table);bindings[0x1c9f880]=uintptr_t(&count);bindings[0x1c9fd08]=uintptr_t(&levelTable);bindings[0x1c9fd10]=uintptr_t(&levelCount);
    Put(monsters[1].data(),0x4e,uint16_t(52)); // FrogDemon
    Put(monsters[2].data(),0x4e,uint16_t(57)); // TentacleHead, independent of class/name
    Put(monsters[3].data(),0x4e,uint16_t(56)); // Tentacle
    Put(monsters[4].data(),0x32,int16_t(2)); // Ground AI, inherited aquatic family
    Put(monsters[5].data(),0x58,int16_t(1)); // Unsafe secondary minion
    Put(monsters[6].data(),0x50,int16_t(2)); // Unsafe spawned monster
    Put(monsters[7].data(),0x32,int16_t(8));Put(monsters[8].data(),0x32,int16_t(7)); // Valid recursive family
    Put(monsters[9].data(),0x4e,uint16_t(139)); // Ground-spawned Baal tentacles are a different behavior
    Put(monsters[10].data(),0x4e,uint16_t(29)); // Flying bats are not blanket-excluded
    assert(!TerrainIndependent(-1)&&!TerrainIndependent(11));
    for(int n=1;n<=6;++n)assert(!RandomCandidate(n)&&!MapPackCandidate(n));
    for(int n:{0,7,8,9,10})assert(RandomCandidate(n)&&MapPackCandidate(n));
    // Explicit authored encounters retain their separate selection rules.
    assert(EncounterCandidate(2,true));
    Run run;run.random.seed(17);assert(BuildRoster(run));const std::set<int> expected{0,7,8,9,10};
    assert(std::set<int>(run.roster.begin(),run.roster.end())==expected);const auto packs=MapPackRoster(run);assert(std::set<int>(packs.begin(),packs.end())==expected);
    Put(monsters[0].data(),0x56,int16_t(4));assert(!RandomCandidate(0)); // Transitive base/minion dependency
    Put(monsters[0].data(),0x56,int16_t(11));assert(!RandomCandidate(0)); // Invalid link cannot be dereferenced
    std::cout<<"Random map/event/pack pools exclude aquatic AIs, inherited families and linked spawns/minions; ground/flying monsters and valid recursive families remain eligible.\n";
    Reset();populationCode=nullptr;installed=false;
}

void TerrainBin(const char* directory){
    auto read=[](const std::filesystem::path& path){std::ifstream input(path,std::ios::binary|std::ios::ate);assert(input);std::vector<uint8_t> bytes(size_t(input.tellg()));input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());assert(input);return bytes;};
    Reset();const auto root=std::filesystem::path(directory);auto monsters=read(root/"monstats.bin"),levelRows=read(root/"levels.bin");
    const uint64_t count=Field<uint32_t>(monsters.data(),0);const auto levelCount=Field<uint32_t>(levelRows.data(),0);
    assert(count&&count<=100000&&monsters.size()==4+count*0x1dc&&levelCount&&levelRows.size()==4+size_t(levelCount)*0x184);
    auto table=monsters.data()+4;auto levelTable=levelRows.data()+4;
    bindings[0x1c9f878]=uintptr_t(&table);bindings[0x1c9f880]=uintptr_t(&count);bindings[0x1c9fd08]=uintptr_t(&levelTable);bindings[0x1c9fd10]=uintptr_t(&levelCount);
    size_t aquatic=0,removed=0;int watcher=-1;
    for(size_t n=0;n<count;++n){
        const auto record=table+n*0x1dc;const auto ai=Field<uint16_t>(record,0x4e);const auto flags=Field<uint32_t>(record,0x3c);
        if(ai==52||ai==56||ai==57){++aquatic;assert(!RandomCandidate(int(n)));if(ai==57)watcher=int(n);}
        const bool previous=(flags&((1u<<25)|(1u<<15)|1u))==((1u<<25)|(1u<<15)|1u)&&!(flags&(0x700|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<18)))&&record[0x80]==0;
        if(previous&&!RandomCandidate(int(n)))++removed;
    }
    assert(aquatic&&watcher>=0);
    // A mod can make a Watcher unique-compatible; the terrain guard must still
    // reject it. Mutate only this private copy, never the installed BIN.
    auto record=table+size_t(watcher)*0x1dc;const auto flags=Field<uint32_t>(record,0x3c);const auto alignment=record[0x80];
    Put(record,0x3c,uint32_t((1u<<25)|(1u<<15)|1u));record[0x80]=0;assert(!RandomCandidate(watcher));Put(record,0x3c,flags);record[0x80]=alignment;
    for(unsigned seed=0;seed<32;++seed){Run run;run.random.seed(seed);assert(BuildRoster(run)&&!run.roster.empty());for(int id:run.roster)assert(TerrainIndependent(id));}
    assert(read(root/"monstats.bin")==monsters&&read(root/"levels.bin")==levelRows);
    std::cout<<"Actual compiled tables: "<<aquatic<<" aquatic-AI rows protected, "<<removed<<" previously eligible monster rows excluded; 32 map rosters generated; original BIN files unchanged.\n";
    Reset();populationCode=nullptr;installed=false;
}
