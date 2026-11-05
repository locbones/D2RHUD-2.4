void LayoutEditTests(const std::filesystem::path& data,const nlohmann::json& config){
    using namespace ExtendedActs::LayoutEdits;
    auto read=[](const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});};
    auto color=[](std::string text,char chosen){
        for(char c:std::string("RBPYGD")){const auto from=std::string("_")+c+'"',to=std::string("_")+chosen+'"';
            for(size_t p=0;(p=text.find(from,p))!=std::string::npos;p+=to.size())text.replace(p,from.size(),to);}
        return text;
    };
    unsigned checked=0;
    for(const auto& file:config["switch_files"]){
        const auto relative=file["path"].get<std::string>();
        if(std::find(config["native_layouts"].begin(),config["native_layouts"].end(),relative)==config["native_layouts"].end())continue;
        const auto six=read(data/file["enabled"].get<std::string>()),five=read(data/file["disabled"].get<std::string>());
        for(char theme:std::string("RBPYGD"))for(bool from:{false,true})for(bool want:{false,true}){
            // Same suffix replacement as QoLOptionsService.ApplyUiColorTheme.
            auto document=Parse(color(from?six:five,theme));
            nlohmann::json sorted=document; // Editors may reorder object keys.
            auto current=std::string("\xef\xbb\xbf// editor comment\n")+sorted.dump(2)+"\n";
            const auto result=Select(current,six,five,want);
            assert(Canonical(Parse(result))==Canonical(Parse(want?six:five)));
            assert(nlohmann::json(Parse(result))==nlohmann::json(Parse(color(want?six:five,theme))));
            if(from==want)assert(result==current);
            ++checked;
        }
        auto invalid=Parse(six);invalid["type"]="IncompatiblePanel";
        bool rejected=false;try{Select(invalid.dump(),six,five,true);}catch(...){rejected=true;}assert(rejected);
        invalid=Parse(six);for(auto& child:invalid["children"])if(child.value("name",std::string{})=="Tabs")child["fields"]["tabCount"]=7;
        rejected=false;try{Select(invalid.dump(),six,five,true);}catch(...){rejected=true;}assert(rejected);
    }
    const auto relaxed=Parse(R"({"filename":"sprite_G", "text":"quoted // not a comment,}", /* block */ "list":[1,2,],})");
    assert(relaxed["list"].size()==2&&relaxed["text"]=="quoted // not a comment,}");
    bool rejected=false;try{Parse("{/* unclosed");}catch(...){rejected=true;}assert(rejected);
    std::cout<<"Native layout cosmetics: "<<checked<<" theme/act transitions, comments/BOM/key order, zero-write repeat selection and unsafe structure refusal passed.\n";
}
