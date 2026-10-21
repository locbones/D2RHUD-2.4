// Real source/compiled table parity, using private copied assets only.
void BinaryBuffTables(const std::filesystem::path& install) {
    using namespace BuffIcons;
    namespace fs = std::filesystem;
    for (const char* name : {"skills", "skilldesc", "states"}) for (const char* extension : {".txt", ".bin"}) {
        if (!fs::is_regular_file(install / "data/global/excel" / (std::string(name)+extension))) {
            std::cout<<"Real TXT/BIN parity fixtures unavailable; retaining the ordinary installed-assets integration check.\n";
            return;
        }
    }
    const auto root = fs::temp_directory_path() / ("d2rhud-buff-bins-" + std::to_string(GetCurrentProcessId()));
    assert(!fs::exists(root));
    const auto excel = root / "data/global/excel";
    fs::create_directories(excel);
    auto write = [](const fs::path& path, const std::vector<uint8_t>& bytes) {
        fs::create_directories(path.parent_path());std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());assert(file.good());
    };
    auto read = [](const fs::path& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);assert(file);
        std::vector<uint8_t> bytes(size_t(file.tellg()));file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());assert(file);return bytes;
    };
    const std::array<std::string,3> tables{"skills", "skilldesc", "states"};
    std::array<std::vector<uint8_t>,3> text, binary;
    for (size_t i=0;i<tables.size();++i) {
        text[i]=read(install / "data/global/excel" / (tables[i]+".txt"));
        binary[i]=read(install / "data/global/excel" / (tables[i]+".bin"));
        write(excel/(tables[i]+".txt"),text[i]);write(excel/(tables[i]+".bin"),binary[i]);
        for (auto damaged : {std::vector<uint8_t>{},std::vector<uint8_t>(binary[i].begin(),binary[i].end()-1)}) {
            bool rejected=false;try{Tables::Decode(damaged,tables[i]);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
        }
        auto damaged=binary[i];memset(damaged.data(),0xff,4);
        bool rejected=false;try{Tables::Decode(damaged,tables[i]);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    }
    GameAssets assets(install, "");
    for (const char* path : {"local/lng/strings/skills.json", "global/ui/layouts/_profilehd.json"}) {
        const auto bytes=assets.Read(path);if(!bytes.empty())write(root/"data"/path,bytes);
    }
    auto source=LoadDatabase(install, "");assert(source.error.empty() && source.skills.count(138));
    for (const auto& [path,atlas] : source.atlases) write(root/"data"/path,assets.Read(path));
    for (unsigned mask=0;mask<8;++mask) {
        for (size_t i=0;i<tables.size();++i) {
            const auto path=excel/(tables[i]+".txt");
            if(mask&(1u<<i))fs::remove(path);else write(path,text[i]);
        }
        const auto loaded=LoadDatabase(root, "");
        if(!loaded.error.empty())std::cerr<<"TXT/BIN mask "<<mask<<": "<<loaded.error<<'\n';
        assert(loaded.error.empty() && loaded.skills.size()==source.skills.size() && loaded.atlases.size()==source.atlases.size());
        for (const auto& [id,skill] : source.skills) {
            const auto& other=loaded.skills.at(id);
            if(other.name!=skill.name || other.frame!=skill.frame || other.states!=skill.states || other.sprite!=skill.sprite)
                std::cerr<<"TXT/BIN mismatch mask="<<mask<<" skill="<<id<<" name="<<skill.name<<'\n';
            assert(other.name==skill.name && other.frame==skill.frame && other.states==skill.states && other.sprite==skill.sprite);
        }
    }
    // Bad BIN data is reported, rather than silently borrowing retail tables.
    auto damaged=binary[2];damaged.pop_back();write(excel/"states.bin",damaged);
    assert(LoadDatabase(root, "").error.find("states.bin")!=std::string::npos);
    write(excel/"states.bin",binary[2]);
    // A mixed installation needs compiled skill links alongside source skills.
    write(excel/"skills.txt",text[0]);fs::remove(excel/"skills.bin");
    assert(LoadDatabase(root, "").error.find("matching skills.bin")!=std::string::npos);
    write(excel/"skills.bin",binary[0]);
    for(size_t i=0;i<tables.size();++i)write(excel/(tables[i]+".txt"),text[i]);
    const auto mod= root / "Mods/Compiled/Compiled.mpq/data/global/excel";
    auto modified=binary[0];uint16_t state=30;
    memcpy(modified.data()+4+138*0x284+0xa0,&state,2);memcpy(modified.data()+4+138*0x284+0xa2,&state,2);
    write(mod/"skills.bin",modified);write(mod/"skilldesc.bin",binary[1]);write(mod/"states.bin",binary[2]);
    const auto custom=LoadDatabase(root,"Compiled");
    assert(custom.error.empty() && custom.skills.at(138).states==std::vector<int>{30});
    assert(source.skills.at(138).states!=custom.skills.at(138).states);
    // Named-mod text still takes precedence over that mod's compiled data.
    write(mod/"skills.txt",text[0]);write(mod/"skilldesc.txt",text[1]);write(mod/"states.txt",text[2]);
    assert(LoadDatabase(root,"Compiled").skills.at(138).states==source.skills.at(138).states);
    std::cout<<"All eight TXT/BIN combinations match "<<source.skills.size()<<" real buff skills, names, icons and state links; mod BIN overrides loose TXT, mod TXT stays preferred, and damaged/incompatible tables are rejected.\n";
}
