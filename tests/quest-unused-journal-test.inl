void UnusedJournalTests(const std::filesystem::path& root){
    Configure(root,"RMD-MP",Source(),"");SetAvailable(true);inventory.pFirstItem=nullptr;
    auto rules=Bindings();std::string error;
    auto fixture=[&](const char* name,const Data::State& earlier){
        auto bytes=Save(name,900);Data::Identity identity;Data::Checkpoint point;
        Check(Data::Header(bytes.data(),bytes.size(),identity,point),"unused fixture header failed");
        Data::History history;history.identity=identity;history.digest=rules->digest^1;
        point.state=earlier;history.checkpoints.push_back(point);point.checksum=901;point.state={};history.checkpoints.push_back(point);
        const auto path=directory/(SummonPersistence::Data::Key(identity)+".json");
        Check(Data::AtomicWrite(path,history,error),"unused fixture write failed");return path;
    };
    auto bytes=Save("UnusedDefinition",902);auto path=fixture("UnusedDefinition",{});const auto original=ReadText(path);
    Load(hero,client,bytes);Check(Inspect(&game,&hero).available,("unused journal stayed locked: "+sessions.at({&game,hero.dwUnitId}).message).c_str());
    auto recoveredHistory=Data::Decode(ReadText(path),2);Check(recoveredHistory&&recoveredHistory->digest==rules->digest&&Data::Pristine(*recoveredHistory)&&Data::Find(*recoveredHistory,902,512).has_value(),"unused recovery lost checkpoint or invented progress");
    auto backup=path;backup+=".unused-"+Data::Hex(rules->digest^1)+"-"+Data::Hex(Data::Hash(original))+".bak";
    Check(ReadText(backup)==original,"unused recovery did not retain exact original backup");
    Removed(&game,&hero);Load(hero,client,bytes);Check(Inspect(&game,&hero).available&&ReadText(backup)==original,"recovered unused journal did not reload");
    std::array<Data::State,9> used{};used[0].completed=1;used[1].objectiveReady=true;used[2].legacyClosed=true;used[3].uncertain=true;
    used[4].kills=1;used[5].evidence=1;used[6].countess=true;used[7].thul=true;used[8].menus=1;
    for(size_t n=0;n<used.size();++n){
        const auto name="UsedDefinition"+std::to_string(n);auto save=Save(name.c_str(),901);path=fixture(name.c_str(),used[n]);const auto before=ReadText(path);
        Removed(&game,&hero);Load(hero,client,save);Check(!Inspect(&game,&hero).available,"definition drift reset an earlier nonempty checkpoint");
        Saved(&game,&hero,save.data(),save.size());Check(ReadText(path)==before,"blocked used journal was overwritten");
    }
    Destroyed(&game);
}

// Read-only replay of the user's actual definition-mismatch failure, using
// isolated companion copies. Neither supplied file is ever written.
void UnusedDefinitionProfileTest(const std::filesystem::path& root,const char* cube,const char* config,const char* journal,const char* save){
    const auto originalJournal=ReadText(journal),originalSave=ReadText(save);
    Configure(root,"RMD-MP",ReadText(cube),ReadText(config));SetAvailable(true);inventory.pFirstItem=nullptr;const auto rules=Bindings();
    auto history=Data::Decode(originalJournal,int(rules->quests.size()));
    Check(history&&Data::Pristine(*history)&&history->digest!=rules->digest&&!history->identity.created,"profile is not the unused definition failure fixture");
    const auto path=directory/(SummonPersistence::Data::Key(history->identity)+".json");std::filesystem::create_directories(directory);std::ofstream(path,std::ios::binary)<<originalJournal;
    std::vector<uint8_t> bytes(originalSave.begin(),originalSave.end());std::array<uint64_t,2> block{reinterpret_cast<uint64_t>(bytes.data()),bytes.size()};
    auto load=[&]{client.pSaveHeaders=reinterpret_cast<D2SaveHeadersStrc*>(block.data());BeforeLoad(&game,&client);AfterLoad(&game,&client,&hero,true);client.pSaveHeaders=nullptr;};
    load();Check(Inspect(&game,&hero).available,"real unused definition journal did not recover");
    CommandAction(2);Crafted(&game,&hero,rules->quests[0].recipes[0],{ItemCode("Z01"),ItemCode("skz")},{});
    Check(Inspect(&game,&hero).completed==1,"recovered real profile could not complete skull deposit");
    Crafted(&game,&hero,rules->quests[1].recipes[0],{ItemCode("Z01"),ItemCode("Z08")},{});
    Check(Inspect(&game,&hero).completed==2,"recovered real profile could not complete skull withdrawal");
    Saved(&game,&hero,bytes.data(),bytes.size());Removed(&game,&hero);load();Check(Inspect(&game,&hero).available&&Inspect(&game,&hero).completed==2,"real recovered progress did not persist");
    Check(ReadText(journal)==originalJournal&&ReadText(save)==originalSave,"real profile inputs changed");Destroyed(&game);
}
