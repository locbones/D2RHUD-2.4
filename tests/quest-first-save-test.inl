// Optional read-only real profile replay. All companion writes stay in the
// caller's isolated temporary test root; the original save is never changed.
void FirstSaveProfileTest(const std::filesystem::path& root,const char* cube,const char* config,const char* journal,const char* save){
    Configure(root,"RMD-MP",ReadText(cube),ReadText(config));SetAvailable(true);inventory.pFirstItem=nullptr;
    const auto rules=Bindings();auto history=Data::Decode(ReadText(journal),int(rules->quests.size()));
    Check(history&&history->identity.created&&Data::Pristine(*history),"real profile is not the unused timestamp failure fixture");
    std::string error;const auto legacyPath=directory/(SummonPersistence::Data::Key(history->identity)+".json");
    Check(Data::AtomicWrite(legacyPath,*history,error),"real profile replay setup failed");
    const auto original=ReadText(save);std::vector<uint8_t> bytes(original.begin(),original.end());Data::Identity identity;Data::Checkpoint point;
    Check(Data::Header(bytes.data(),bytes.size(),identity,point)&&!identity.created,"real first-save header failed stable identity");
    std::array<uint64_t,2> block{reinterpret_cast<uint64_t>(bytes.data()),bytes.size()};client.pSaveHeaders=reinterpret_cast<D2SaveHeadersStrc*>(block.data());
    BeforeLoad(&game,&client);AfterLoad(&game,&client,&hero,true);client.pSaveHeaders=nullptr;
    Check(Inspect(&game,&hero).available&&Inspect(&game,&hero).completed==0,"real first-save journal did not recover");
    const auto key=sessions.at({&game,hero.dwUnitId}).path;Check(key!=legacyPath&&std::filesystem::exists(legacyPath),"real profile migration lost original companion");
    Crafted(&game,&hero,rules->quests[0].recipes[0],{ItemCode("Z01"),ItemCode("skz")},{});CommandAction(2);
    Check(Inspect(&game,&hero).completed==1,"real first-save profile cannot complete skull deposit");
    Saved(&game,&hero,bytes.data(),bytes.size());Check(Inspect(&game,&hero).available,"real first-save replay locked after saving");
    Check(ReadText(save)==original,"real profile save was modified");Destroyed(&game);
}
