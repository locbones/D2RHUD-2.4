#define NOMINMAX
#include "../d2rhud/plugin/GameAssets.h"
#include "../d2rhud/plugin/QuestJournalData.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
std::string ReadText(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void WriteText(const fs::path& p,const std::string& s){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f<<s;if(!f)throw std::runtime_error("Fixture write failed");}
void Check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    Check(argc==2,"Provide the installed mod data folder");const fs::path source=argv[1];
    const auto temp=fs::canonical(fs::temp_directory_path());
    const auto root=temp/("d2rhud-quest-source-"+std::to_string(GetCurrentProcessId()));
    Check(!fs::exists(root),"Fixture already exists");
    const auto data=root/"Mods/RMD-MP/RMD-MP.mpq/data",excel=data/"global/excel";
    const auto originalMetadata=ReadText(source/"D2RHUD/table-metadata.json");
    auto metadata=nlohmann::json::parse(originalMetadata);
    const auto reference=metadata.at("tables").at("cubemain").at("source").get<std::string>();
    const auto config=ReadText(source/"D2RHUD/quests.json");
    const auto originalBin=ReadText(source/"global/excel/cubemain.bin");
    WriteText(data/"D2RHUD/table-metadata.json",originalMetadata);WriteText(excel/"cubemain.bin",originalBin);
    auto read=[&](bool compiling=false){GameAssets assets(root,"RMD-MP");return assets.ReadTableSource("cubemain",!compiling);};
    Check(read()==reference,"Installed BIN recipe source was discarded because its checksum changed");
    auto rules=QuestJournal::Data::ParseRules(read(),config);
    Check(rules->error.empty()&&rules->events&&rules->quests.size()==45,"Installed 45-quest definitions failed to load");
    const auto digest=rules->digest;
    auto bin=originalBin;Check(bin.size()>4+0x148,"Invalid BIN fixture");bin[4+0x4e]^=1;
    WriteText(excel/"cubemain.bin",bin);Check(read()==reference,"Unrelated recipe edit disabled the journal");
    uint32_t count=0;memcpy(&count,bin.data(),4);++count;memcpy(bin.data(),&count,4);bin.append(0x148,'\0');
    WriteText(excel/"cubemain.bin",bin);Check(read()==reference,"Appending an ordinary recipe disabled the journal");
    auto& entry=metadata["tables"]["cubemain"];entry["bin_hash"]="intentionally unrelated";entry["bin_size"]=1;
    WriteText(data/"D2RHUD/table-metadata.json",metadata.dump());
    Check(read()==reference,"Quest loading still depends on a BIN checksum or total size");
    rules=QuestJournal::Data::ParseRules(read(),config);
    Check(rules->error.empty()&&rules->digest==digest,"Unrelated BIN changes altered saved quest identity");
    WriteText(excel/"cubemain.txt","description\tenabled\nleftover unrelated source\t1\n");
    Check(read()==reference,"Leftover TXT overrode packaged recipe annotations");
    Check(read(true)==ReadText(excel/"cubemain.txt"),"TXT authoring launch ignored its selected source");
    fs::remove(excel/"cubemain.txt");fs::remove(data/"D2RHUD/table-metadata.json");
    WriteText(root/"data/D2RHUD/table-metadata.json",originalMetadata);
    Check(read().empty(),"Recipe annotations crossed data layers");
    WriteText(data/"D2RHUD/table-metadata.json","not json");Check(read().empty(),"Malformed annotations were accepted");
    metadata["version"]=999;WriteText(data/"D2RHUD/table-metadata.json",metadata.dump());
    Check(read().empty(),"Unsupported annotation format was accepted");
    Check(root.parent_path()==temp&&root.filename().string().starts_with("d2rhud-quest-source-"),"Unsafe cleanup target");
    fs::remove_all(root);
    std::cout<<"PASS: installed BIN-only 45 quests; unrelated edits/appends/checksums/sizes preserve definitions and progress identity; source selection and malformed/cross-layer annotations checked.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
