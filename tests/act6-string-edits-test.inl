void StringEditTests(const std::filesystem::path& data,const nlohmann::json& config,const std::filesystem::path& source) {
    using namespace ExtendedActs;
    using Json=StringEdits::Json;
    for(const char* name:{"levels.json","quests.json"}) {
        const auto relative="local/lng/strings/"+std::string(name);
        const auto target=data/relative,archive=data/("Pandemonium/ActVI/preserved-"+std::string(name));
        const auto entry=*std::find_if(config["switch_files"].begin(),config["switch_files"].end(),[&](const auto& f){return f["path"]==relative;});
        const auto onPath=data/entry["enabled"].get<std::string>(),offPath=data/entry["disabled"].get<std::string>();
        const auto on=LevelRead(onPath),off=LevelRead(offPath);
        auto values=Json::parse(off);
        if(std::string(name)=="levels.json") {
            // The actual event overlay caused the startup rejection: 725
            // original entries, event boss-rush descriptions, no Act VI keys.
            values=Json::parse(LevelRead(source/"Pandemonium/Events/Halloween Havoc"/relative));
        }else values[0]["enUS"]="Edited ordinary quest translation";
        values.push_back(Json{{"id",63000},{"Key","ActVIRegressionUserString"},{"enUS","Keep my additional entry"}});
        const auto event=values.dump(4)+'\n';LevelWrite(target,event);
        assert(ConfigureData(data,config,false,true,5)==Config::Disabled);
        assert(LevelRead(target)==event); // Disabled startup is a byte-exact no-op.
        assert(ConfigureData(data,config,true,true,5)==Config::Disabled && pendingRestart);
        const StringEdits::Table enabled(LevelRead(target)),original(event),six(on),five(off);
        assert(enabled.rows.size()==original.rows.size()+six.rows.size()-five.rows.size());
        for(const auto& [key,index]:original.keys)assert(enabled.rows[enabled.keys.at(key)]==original.rows[index]);
        const auto enabledBytes=LevelRead(target);
        assert(ConfigureData(data,config,true,true,6)==Config::Enabled);
        assert(LevelRead(target)==enabledBytes);
        // Act VI translations survive removing and re-adding owned entries,
        // including a fresh process and an event overlay applied in between.
        auto edited=enabled.rows;
        const auto owned=std::find_if(six.keys.begin(),six.keys.end(),[&](const auto& pair){return !five.keys.contains(pair.first);});
        assert(owned!=six.keys.end());edited[enabled.keys.at(owned->first)]["enUS"]="Custom Act VI label";
        LevelWrite(target,edited.dump(4)+'\n');
        assert(ConfigureData(data,config,false,true,6)==Config::Enabled && pendingRestart);
        assert(Json::parse(LevelRead(target))==values && std::filesystem::exists(archive));
        LevelWrite(target,event);configured=false;configuredManifest={};configuredData.clear();
        assert(ConfigureData(data,config,true,true,5)==Config::Disabled);
        const StringEdits::Table restored(LevelRead(target));
        assert(restored.rows[restored.keys.at(owned->first)]["enUS"]=="Custom Act VI label");
        for(const auto& [key,index]:original.keys)assert(restored.rows[restored.keys.at(key)]==original.rows[index]);
        auto reject=[&](const Json& invalid) {
            const auto raw=invalid.dump();LevelWrite(target,raw);bool rejected=false;
            try{SelectData(data,config,true,true,DataGroup::Launch);}catch(const std::exception&){rejected=true;}
            assert(rejected && LevelRead(target)==raw);
        };
        auto bad=values;bad.push_back(values[0]);reject(bad); // duplicate ID and key
        bad=values;bad.back()["id"]=six.rows[owned->second]["id"];reject(bad); // reserved ID
        bad=restored.rows;bad[restored.keys.at(owned->first)]["id"]=63001;reject(bad);
        // Template integrity is checked before any replacement is staged.
        LevelWrite(target,event);LevelWrite(onPath,on+" ");bool rejected=false;
        try{SelectData(data,config,true);}catch(const std::exception&){rejected=true;}
        assert(rejected && LevelRead(target)==event);LevelWrite(onPath,on);
        // A locked active JSON must roll back earlier tables and the new archive.
        std::filesystem::remove(archive);LevelWrite(target,enabledBytes);
        const auto levelsBefore=LevelRead(data/"global/excel/levels.txt");
        auto lock=CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        assert(lock!=INVALID_HANDLE_VALUE);assert(!SelectData(data,config,false));CloseHandle(lock);
        assert(LevelRead(target)==enabledBytes && LevelRead(data/"global/excel/levels.txt")==levelsBefore);
        assert(!std::filesystem::exists(archive) && !std::filesystem::exists(target.wstring()+L".act6-pending"));
        // Restore pristine fixture bytes for existing startup checks.
        LevelWrite(target,off);
        assert(ConfigureData(data,config,false,true,5)==Config::Disabled);
        if(std::filesystem::exists(archive))std::filesystem::remove(archive);
    }
    std::cout<<"Halloween locale overlay startup, ordinary/custom translations, owned-label recovery, ID conflicts, template validation and rollback passed.\n";
}
