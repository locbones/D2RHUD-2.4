// Included in CubeMap.cpp's anonymous namespace after ValidLevel. Work only
// from the already loaded tables: transmuting never reads mod files from disk.
std::string DestinationKey(std::string_view key){
    std::string result;result.reserve(key.size());
    for(unsigned char c:key){if(c>='A' && c<='Z')c+=32;if((c>='a' && c<='z') || (c>='0' && c<='9'))result.push_back(char(c));}
    return result;
}
bool ExcludedRandomDestination(int level,unsigned act,std::string_view name){
    const auto key=DestinationKey(name);
    // Native Uber Tristram reuses the ordinary Tristram string key. Include
    // its ID and Act V alias, while keeping Act I Tristram eligible.
    return level==136 || key.find("torment")!=key.npos || key.find("icyhell")!=key.npos ||
        key.find("ubertristram")!=key.npos || key.find("pandemoniumfinale")!=key.npos ||
        key.find("forestruins")!=key.npos || (act==4 && key.find("tristram")!=key.npos);
}
bool RandomDestinationRecord(int level,unsigned difficulty,const uint8_t* record,const uint8_t* definition){
    if(!record || !definition || difficulty>2 || !ValidLevel(level))return false;
    // Audited native Levels loader: record stride184, Act+6, NumMon+36,
    // density+20/24/28, roster+3a/6c and name key+f9. LevelDef retains9c
    // stride, dimension triplets+c/+18 and DRLG/tileset+30/+34.
    const auto act=record[6];const auto type=Field<uint32_t>(definition,0x30);
    if(act>4 || type<1 || type>3 || !Field<uint32_t>(definition,0x34) ||
        !Field<uint32_t>(definition,0xc+difficulty*4) || !Field<uint32_t>(definition,0x18+difficulty*4) ||
        !Field<uint32_t>(record,0x20+difficulty*4) || !record[0x36])return false;
    std::array<char,40> name{};memcpy(name.data(),record+0xf9,39);
    if(!name[0] || ExcludedRandomDestination(level,act,name.data()))return false;
    const size_t roster=difficulty ? 0x6c : 0x3a;
    // Empty roster slots use -1; monster zero is a valid monster class.
    for(size_t i=0;i<25;++i)if(Field<int16_t>(record,roster+i*2)>=0)return true;
    return false;
}
std::vector<int> RandomMapDestinations(Game* game){
    std::vector<int> result;if(!game)return result;
    const auto difficulty=unsigned(game->nDifficulty);
    const auto count=Field<uint32_t>(Native<void*>(0x1c9fd10),0);
    const auto table=Field<uint8_t*>(Native<void*>(0x1c9fd08),0);
    const auto definitions=Field<uint8_t*>(Native<void*>(0x1c9fd28),0);
    if(difficulty>2 || !count || count>1024 || !table || !definitions)return result;
    std::array<uint8_t,0x184> record{};std::array<uint8_t,0x9c> definition{};
    for(unsigned level=1;level<count;++level){
        if(!Read(reinterpret_cast<uintptr_t>(table)+size_t(level)*record.size(),record) ||
            !Read(reinterpret_cast<uintptr_t>(definitions)+size_t(level)*definition.size(),definition))continue;
        if(RandomDestinationRecord(int(level),difficulty,record.data(),definition.data()))result.push_back(int(level));
    }
    return result;
}
int SelectRandomMapDestination(Game* game,std::mt19937& random){
    const auto pool=RandomMapDestinations(game);
    return pool.empty() ? 0 : pool[std::uniform_int_distribution<size_t>(0,pool.size()-1)(random)];
}
