// Exercise production startup/switching against copies of the real data pack.
std::string LevelRead(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);assert(file);
    return {std::istreambuf_iterator<char>(file),{}};
}
void LevelWrite(const std::filesystem::path& path,const std::string& value) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc);assert(file);file<<value;file.close();assert(file);
}
std::string LevelCell(std::string raw,int id,const char* column,const std::string& value) {
    const ExtendedActs::LevelEdits::Table table(raw);
    std::string result;
    for(size_t n=0;n<table.lines.size();++n) {
        const auto& line=table.lines[n];
        if(line.id && *line.id==id) {
            auto cells=line.cells;cells[table.Column(column)]=value;
            for(size_t i=0;i<cells.size();++i){if(i)result+='\t';result+=cells[i];}
        }else result+=line.raw;
        result+=line.ending;
    }
    return result;
}
void LevelEditTests(const std::filesystem::path& data,const nlohmann::json& config) {
    using namespace ExtendedActs;
    const auto target=data/"global/excel/levels.txt";
    const auto archive=data/"Pandemonium/ActVI/preserved-levels.txt";
    const auto levelEntry=*std::find_if(config["switch_files"].begin(),config["switch_files"].end(),
        [](const auto& value){return value["path"]=="global/excel/levels.txt";});
    const auto onPath=data/levelEntry["enabled"].get<std::string>();
    const auto offPath=data/levelEntry["disabled"].get<std::string>();
    const auto on=LevelRead(onPath),off=LevelRead(offPath);
    const auto areas=config["levels"].get<std::array<int,9>>();
    const std::optional<std::string> originalArchive=std::filesystem::exists(archive)?
        std::optional<std::string>(LevelRead(archive)):std::nullopt;
    if(originalArchive)std::filesystem::remove(archive);
    auto check=[&](const std::string& bytes,int id,const char* name,const char* value) {
        assert(LevelEdits::Table(bytes).Cell(id,name)==value);
    };
    auto edited=LevelCell(off,2,"mon1","fallen1");
    edited=LevelCell(edited,2,"mon3","fallen1");
    edited=LevelCell(edited,2,"MonUMin","14");
    edited=LevelCell(edited,2,"MonUMax","15");
    edited=LevelCell(edited,2,"MonDen","1777");
    // Use a different density in case the pack already contains the original
    // four edits. This reproduces a full-file hash mismatch on every pack.
    assert(LegacyTransmog::Data::Hash({reinterpret_cast<const uint8_t*>(edited.data()),edited.size()})!=levelEntry["disabled_hash"].get<uint64_t>());
    LevelWrite(target,edited);
    assert(ConfigureData(data,config,false,true,5)==Config::Disabled);
    assert(LevelRead(target)==edited && !std::filesystem::exists(archive));
    // Native BIN mode doesn't inspect or replace authoring TXT at all.
    assert(ConfigureData(data,config,false,false,5)==Config::Disabled);
    assert(LevelRead(target)==edited);
    assert(ConfigureData(data,config,false,true,5)==Config::Disabled);
    SetRequested(true);auto six=LevelRead(target);
    assert(LevelEdits::Table(six).rows.size()==LevelEdits::Table(off).rows.size()+9);
    check(six,2,"MonUMax","15");check(six,2,"MonDen","1777");
    assert(ConfigureData(data,config,true,true,6)==Config::Enabled);
    assert(LevelRead(target)==six); // Repeated startup is a byte-exact no-op.
    six=LevelCell(six,areas[1],"mon1","fallen1");
    six=LevelCell(six,areas[1],"MonDen","2888");LevelWrite(target,six);
    // No archive existed: failure replacing the locked active table must undo
    // the new archive too, leaving both the live file and templates intact.
    auto lock=CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    assert(lock!=INVALID_HANDLE_VALUE);
    assert(!SelectData(data,config,false,true,DataGroup::Launch));CloseHandle(lock);
    assert(LevelRead(target)==six && !std::filesystem::exists(archive));
    assert(!std::filesystem::exists(target.wstring()+L".act6-pending"));
    SetRequested(false);
    assert(LevelRead(target)==edited && std::filesystem::exists(archive));
    check(LevelRead(archive),areas[1],"MonDen","2888");
    assert(ConfigureData(data,config,false,true,5)==Config::Disabled);
    // A new HUD/process can recover custom Act VI rows from the disk archive.
    configured=false;configuredManifest={};configuredData.clear();
    assert(ConfigureData(data,config,true,true,5)==Config::Disabled && pendingRestart);
    assert(LevelRead(target)==six);
    assert(ConfigureData(data,config,true,true,6)==Config::Enabled);
    for(int n=0;n<3;++n) {
        SetRequested(false);assert(LevelRead(target)==edited);
        SetRequested(true);assert(LevelRead(target)==six);
    }
    // Still reject damaged IDs, reserved references and malformed input. The
    // whole selection must remain untouched when any validation fails.
    const auto savedArchive=LevelRead(archive);
    auto rejected=[&](const std::string& bad) {
        LevelWrite(target,bad);bool refused=false;
        try{SelectData(data,config,false,true,DataGroup::Launch);}catch(const std::exception& e) {
            refused=std::string(e.what()).find("Act VI levels.txt:")!=std::string::npos;
        }
        assert(refused && LevelRead(target)==bad && LevelRead(archive)==savedArchive);
        assert(LevelRead(onPath)==on && LevelRead(offPath)==off);
    };
    rejected(LevelCell(six,areas[0],"Act","4"));
    rejected(LevelCell(six,areas[0],"Waypoint","38"));
    rejected(LevelCell(six,areas[0],"Name","renamed-town"));
    rejected(LevelCell(six,areas[0],"Id","2"));
    rejected(LevelCell(six,areas[0],"Id","999"));
    rejected(LevelCell(six,areas[0],"Id",""));
    rejected(LevelCell(six,2,"Waypoint","39"));
    rejected(LevelCell(six,2,"Act","5"));
    rejected(six+"broken\trow\r\n");
    const LevelEdits::Table parsed(six);
    auto missing=six;
    const auto& row=parsed.lines[parsed.rows.at(areas.back())];
    auto pos=missing.rfind(row.raw+row.ending);assert(pos!=missing.npos);
    missing.erase(pos,row.raw.size()+row.ending.size());rejected(missing);
    auto reordered=missing;
    const auto first=parsed.lines[parsed.rows.at(areas.front())].raw;
    pos=reordered.find(first);assert(pos!=reordered.npos);
    reordered.insert(pos,row.raw+row.ending);rejected(reordered);
    // BOM, LF, missing final newline and author-added columns are retained.
    auto lf=edited;for(size_t at=0;(at=lf.find("\r\n",at))!=lf.npos;)lf.erase(at,1);
    lf="\xef\xbb\xbf"+lf;lf.pop_back();
    assert(LevelEdits::Select(lf,on,off,areas,false).text==lf);
    const auto lfOn=LevelEdits::Select(lf,on,off,areas,true).text;
    check(lfOn,2,"MonDen","1777");
    std::string extra;const LevelEdits::Table extraTable(edited);
    for(size_t i=0;i<extraTable.lines.size();++i)extra+=extraTable.lines[i].raw+"\t"+(i?"custom value":"*My column")+extraTable.lines[i].ending;
    assert(LevelEdits::Select(extra,on,off,areas,false).text==extra);
    const auto extraOn=LevelEdits::Select(extra,on,off,areas,true).text;
    check(extraOn,2,"*My column","custom value");
    assert(LevelEdits::Select(extraOn,on,off,areas,false).text==extra);
    // Template corruption remains a hard error, even though ordinary target
    // edits no longer require refreshing the manifest's whole-file hash.
    LevelWrite(target,six);LevelWrite(onPath,on+"\r\n");bool templateRefused=false;
    try{SelectData(data,config,true);}catch(const std::exception& e){templateRefused=std::string(e.what()).find("template mismatch")!=std::string::npos;}
    assert(templateRefused && LevelRead(target)==six);LevelWrite(onPath,on);
    assert(LevelRead(offPath)==off);
    LevelWrite(target,six);assert(ConfigureData(data,config,false,true,5)==Config::Disabled);
    LevelWrite(target,off);
    if(originalArchive)LevelWrite(archive,*originalArchive);else std::filesystem::remove(archive);
    std::cout<<"Level edits: original/Act VI monsters and density survive startup, BIN mode and repeated five/six-act switches; archive rollback, formatting and structural rejection passed.\n";
}
