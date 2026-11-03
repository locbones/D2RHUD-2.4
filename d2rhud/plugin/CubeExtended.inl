// Source-only D-G columns accompany the unchanged three-output native BIN.
// Native token parsing handles qty, quality, sockets, affixes and other commands;
// loaded item records / table symbols also resolve targets in BIN-only games.
bool ExtraNumber(const std::string& text,int low,int high,int& value){
    value=0;if(text.empty())return true;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    return parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&value>=low&&value<=high;
}
bool CompileExtra(const ExtraOutputSource& source,Output& out,int row,int slot){
    if(source.command.empty()){out={};return true;}
    auto text=Trim(source.command);if(text.size()>1&&text.front()=='"'&&text.back()=='"')text=text.substr(1,text.size()-2);
    const auto command=Trim(text.substr(0,text.find(',')));
    const auto use=ParseUseItem(text);if(!use.valid)return false;
    const auto visual=Parse(text);const auto map=CubeMap::Data::Parse(text);
    if(visual.present||map.present){
        Recipe probe;probe.header[0]=1;LinkerHook(text.data(),&probe,0,0,row,slot);
        if(!probe.header[0]||!probe.outputs[0].type)return false;out=probe.outputs[0];
    }else if(use.present||Equal(command,"usetype")||Equal(command,"Cow Portal")||Equal(command,"Red Portal")||Equal(command,"Pandemonium Portal")||Equal(command,"Pandemonium Finale Portal")){
        auto native=use.indexed?use.native:text;out={};Native<Parser>(0x2616b0)(&out,native.data(),row,slot);
        if(use.indexed&&out.type==254)out.special=UseInputMarker|use.input;
        if(!out.type)return false;
    }else{
        const auto comma=text.find(',');const auto target=Trim(text.substr(0,comma));
        auto tokens=std::string("useitem")+(comma==text.npos?std::string{}:text.substr(comma));
        out={};Native<Parser>(0x2616b0)(&out,tokens.data(),row,slot);if(out.type!=254)return false;
        const auto resolved=Resolve(target,row,slot,false);uint32_t base=0;
        const int type=CompiledNames::Resolve(0x1c9fbe8,target.c_str());
        // Retail resolves a base code before an item type, then set/unique names.
        if(resolved.kind!=0&&type>=0&&type<=65535){out.type=253;out.base=uint16_t(type);}
        else if(resolved.kind>=0&&resolved.index>=0&&resolved.index<65535&&Base(resolved,base,false)&&base<=65535){
            out.type=252;out.base=uint16_t(base);
            if(resolved.kind){
                out.flags|=8;out.special=uint16_t(resolved.index+1);if(!out.quality)out.quality=resolved.kind==1?7:5;
                const auto address=resolved.kind==1?0x1c9fc70:0x1c9fc50;const size_t stride=resolved.kind==1?0x150:0x1bc;
                uintptr_t table=0;if(Read(Pattern::Address(address),table)&&table)Read(table+size_t(resolved.index)*stride+0x34,out.itemLevel);
            }
        }else return false;
    }
    int value=0;uint8_t* levels[]{&out.level,&out.playerLevel,&out.itemLevel};
    for(size_t i=0;i<3;++i){if(!ExtraNumber(source.levels[i],0,255,value))return false;*levels[i]=uint8_t(value);}
    for(size_t i=0;i<5;++i){const auto& raw=source.mods[i];auto& mod=out.mods[i];if(raw.property.empty())continue;
        mod.property=CompiledNames::Resolve(0x1c9ebd8,raw.property.c_str());if(mod.property<0)return false;
        if(!ExtraNumber(raw.param,-32768,65535,value))return false;mod.param=uint16_t(value);
        if(!ExtraNumber(raw.min,-32768,32767,value))return false;mod.min=int16_t(value);
        if(!ExtraNumber(raw.max,-32768,32767,value))return false;mod.max=int16_t(value);
        if(!ExtraNumber(raw.chance,0,65535,value))return false;mod.chance=uint16_t(value);
    }
    return true;
}
bool CompileExtraRecipe(const Recipe& recipe,size_t row,WideRecipe& wide){
    ExtraRecipeSource source;
    {std::lock_guard lock(sourceMutex);const auto found=sourceTable.extended.find(row);
        if(found==sourceTable.extended.end())return false;
        source=found->second;
    }
    if(source.enabled!=recipe.header[0]||source.inputs!=recipe.header[0x10])return false;
    wide=WideRecipe(recipe);
    for(size_t i=0;i<4;++i)if(!CompileExtra(source.outputs[i],wide.outputs[i+3],int(row),int(i+3)))return false;
    return true;
}
