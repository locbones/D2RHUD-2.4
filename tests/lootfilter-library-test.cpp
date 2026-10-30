#include "../d2rhud/plugin/ItemFilter/LootFilterLibrary.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace LootFilterLibrary;
int main(int argc,char** argv){
    assert(argc>=2);const auto root=fs::path(argv[1])/("library-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto write=[](const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream out(path);out<<text;assert(out);};
    const auto modern=root/"Mods/RMD-MP/RMD-MP.mpq/data/Pandemonium/Filters";
    const auto legacy=root/"Mods/RMD-MP/RMD-MP.mpq/data/D2RLAN/filters";
    const auto extracted=root/"Mods/RMD-MP/data/Pandemonium/Filters";
    write(modern/"Charsi's Special.lua","modern");write(modern/"Duplicate.lua","modern duplicate");write(legacy/"Duplicate.lua","legacy duplicate");
    write(legacy/"Legacy.lua","legacy");write(extracted/"Extracted.lua","extracted");write(root/"My Filters/Duplicate.lua","user duplicate");
    write(modern/"Package/lootfilter_config.lua","folder");write(modern/"UPPER.LUA","uppercase");write(modern/"No Filter.lua","no filter");
    write(modern/"lootfilter_config_blank.lua","blank");write(modern/"last_active_lootfilter.lua","archive");
    write(modern/".updates/patch.lua","update");write(modern/"sounds/drop.wav","sound");fs::create_directories(modern/"Other tools");
    const auto directories=Directories(root,"RMD-MP");const auto names=Names(directories);
    assert(names.size()==8);
    for(const auto& name:names)assert(!Resolve(directories,name).empty());
    assert(Resolve(directories,"Duplicate")==root/"My Filters/Duplicate.lua");
    assert(Resolve(directories,"Charsi's Special")==modern/"Charsi's Special.lua");
    assert(Resolve(directories,"Package")==modern/"Package/lootfilter_config.lua");
    assert(fs::equivalent(Resolve(directories,"UPPER"),modern/"UPPER.LUA"));
    assert(Resolve(directories,"Extracted")==extracted/"Extracted.lua");
    assert(Resolve(directories,"../Duplicate").empty());
    write(root/"My Filters/Imported_config.lua","imported");assert(Resolve(directories,"Imported")==root/"My Filters/Imported_config.lua");
    assert(Names(directories).size()==9);
    const auto active=root/"lootfilter_config.lua";fs::copy_file(Resolve(directories,"Duplicate"),active);
    std::ifstream selected(active);std::string text{std::istreambuf_iterator<char>(selected),{}};assert(text=="user duplicate");
    assert(Names(Directories(root/"missing","OtherMod")).empty());assert(Names(Directories({},"RMD-MP")).empty());
    if(argc>2){const auto actual=Directories(argv[2],"RMD-MP");const auto installed=Names(actual);assert(!installed.empty());
        bool defaultFound=false;for(const auto& name:installed){assert(!Resolve(actual,name).empty());defaultFound|=name=="lootfilter_default";}
        assert(defaultFound);std::cout<<"Installed RMD-MP package: "<<installed.size()<<" selectable filters resolved read-only.\n";}
    std::cout<<"Modern/legacy/extracted filter discovery, user precedence, packaged configs, Lua extensions, hidden/support exclusion, refreshed imports and selected-file contents passed.\n";
}
