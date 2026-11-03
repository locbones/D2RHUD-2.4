#pragma once
#include "CandyCornSaveTables.h"
#include "MarketplaceData.h"
#include <functional>
#include <map>

namespace Marketplace {
struct SaveTables : CandyCornSaveData::Tables {std::map<std::string,unsigned> bagStats;};
using ReadAsset = std::function<std::vector<uint8_t>(const std::string&)>;
inline SaveTables LoadSaveTables(const ReadAsset& read,const std::vector<std::string>& statNames={}) {
    using namespace CandyCornSaveData;
    auto table = [&](const char* name, size_t stride) {
        auto bytes = read(name);
        if (bytes.size() < 4 || Detail::Word(bytes,0) > 100000 || bytes.size() != 4+size_t(Detail::Word(bytes,0))*stride)
            throw std::runtime_error(std::string("Missing or unsupported 2.4 compiled table: ")+name);
        return bytes;
    };
    std::vector<uint8_t> items;
    for (auto name : {"weapons","armor","misc"}) { auto bytes=table(name,0x1b4);items.insert(items.end(),bytes.begin()+4,bytes.end()); }
    const auto stats=table("itemstatcost",0x144),types=table("itemtypes",0xe4);
    const auto count=Detail::Word(types,0),stride=(count+31)/32;
    if (count <= 50 || count > 4096) throw std::runtime_error("Unsupported compiled item type count.");
    std::vector<uint32_t> inheritance(size_t(count)*stride);
    for (uint32_t n=0;n<count;++n) {
        std::vector<uint32_t> pending{n};
        while (!pending.empty()) {
            const auto type=pending.back();pending.pop_back();auto& word=inheritance[size_t(n)*stride+type/32];
            const auto mask=uint32_t(1)<<(type%32);if(word&mask)continue;word|=mask;
            for (unsigned offset : {4,6}) {
                const auto parent=int16_t(Detail::Word(types,4+size_t(type)*0xe4+offset,2));
                if (parent < 0 || parent >= int(count)) throw std::runtime_error("Invalid compiled item type parent.");
                if (parent > 0) pending.push_back(uint32_t(parent));
            }
        }
    }
    SaveTables output;
    if (!DecodeTables(items,{stats.data()+4,stats.size()-4},inheritance,count,stride,output))
        throw std::runtime_error("Unsupported 2.4 compiled item/stat definitions.");
    std::sort(output.items.begin(),output.items.end(),[](const auto& a,const auto& b){return a.code<b.code;});
    for(size_t i=1;i<output.items.size();++i) if(output.items[i-1].code==output.items[i].code)
        throw std::runtime_error("Duplicate compiled item codes cannot be safely catalogued.");
    if(statNames.size()==Detail::Word(stats,0))for(const auto& resource:BagResources){
        const auto at=std::find(statNames.begin(),statNames.end(),resource.stat);
        if(at==statNames.end()||std::find(at+1,statNames.end(),resource.stat)!=statNames.end())continue;
        const auto id=unsigned(at-statNames.begin());const auto def=Detail::Stat(output,id);
        if(def&&def->saveBits&&def->saveBits<=32&&!def->paramBits)output.bagStats.emplace(resource.stat,id);
    }
    return output;
}
inline std::string LegacyItemHex(const std::string& hex,const Json& children){
    std::vector<uint8_t> bytes;bytes.reserve(hex.size()/2);auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(size_t n=0;n<hex.size();n+=2)bytes.push_back(uint8_t(digit(hex[n])*16+digit(hex[n+1])));
    CandyCornSaveData::Detail::Budget budget;CandyCornSaveData::Detail::Bits bits{bytes,0,&budget,true};const auto flags=bits.Read(32);
    if(!(flags&(1u<<21))&&!(flags&(1u<<16))){bits.pos=53;std::array<char,4> code{};if(!bits.Code(code))throw std::runtime_error("Invalid identity header.");bits.Skip(3);const auto at=bits.pos;bits.Skip(32);if(!bits.ok)throw std::runtime_error("Invalid identity field.");for(size_t n=at;n<at+32;++n)bytes[n/8]&=uint8_t(~(1u<<(n%8)));}
    size_t tail=bytes.size();for(auto it=children.rbegin();it!=children.rend();++it){const auto child=LegacyItemHex(it->at("item_hex").get<std::string>(),it->at("socketed_items"));if(child.size()/2>tail)throw std::runtime_error("Invalid socket identity boundary.");tail-=child.size()/2;for(size_t n=0;n<child.size();n+=2)bytes[tail+n/2]=uint8_t(digit(child[n])*16+digit(child[n+1]));}
    return Hex(bytes);
}
struct SavedItem {
    unsigned tab=1,ordinal=0,x=0,y=0,container=0;
    uint32_t flags=0,id=0,quantity=1;
    int quality=2,index=-1,level=0,picture=-1;
    std::string code,itemHex,uuid;
    Json properties=Json::array(),children=Json::array();
    std::vector<PaymentItem> balances;
    std::string escrowId,escrowState,ledgerId;
    Draft escrowDraft;
    PaymentItem Payment() const {return {code,quality,index,quantity,bool(flags&(1u<<4)),bool(flags&(1u<<22))};}
    Json ToJson() const {
        return {{"tab",tab},{"ordinal",ordinal},{"x",x},{"y",y},{"container",container},{"flags",flags},{"saved_id",id},
            {"quantity",quantity},{"quality",quality},{"index",index},{"level",level},{"picture",picture},{"code",code},{"item_uuid",uuid},
            {"identified",bool(flags&(1u<<4))},{"ethereal",bool(flags&(1u<<22))},
            {"item_hex",itemHex},{"saved_properties",properties},{"socketed_items",children}};
    }
};
inline void ReadBagBalances(SavedItem& item,const SaveTables& tables){
    item.balances.clear();std::vector<PaymentItem> balances;
    if(item.code!="Z01"||!(item.flags&(1u<<4)))return;
    for(const auto& [symbol,id]:tables.bagStats){
        int64_t balance=0;bool found=false;
        for(const auto& row:item.properties)if(row.at("id")==id){
            if(found||row.at("list")!="base"||row.at("param")!=0)return; // No derived/ambiguous balances.
            found=true;balance=row.at("value").get<int64_t>();if(balance<0||balance>UINT32_MAX)return;
        }
        if(found&&balance)balances.push_back({"Z01",2,-1,uint32_t(balance),true,false,"bag",symbol});
    }
    item.balances=std::move(balances);
}
namespace StashDetail {
using namespace CandyCornSaveData;
inline Json Properties(Detail::Bits& bits,const Tables& tables,const char* list) {
    Json result=Json::array();
    auto value=[&](unsigned id) {
        const auto stat=Detail::Stat(tables,id);if(!stat){bits.ok=false;return;}
        const auto param=bits.Read(stat->paramBits),raw=bits.Read(stat->saveBits);
        result.push_back({{"id",id},{"param",param},{"raw",raw},{"value",int64_t(raw)-stat->saveAdd},{"list",list}});
    };
    for(unsigned n=0;n<4096 && bits.ok;++n) {
        const auto id=bits.Read(9);if(id==511)return result;value(id);
        if(id==17||id==48||id==50||id==52)value(id+1);
        else if(id==54||id==57){value(id+1);value(id+2);}
    }
    bits.ok=false;return result;
}
inline SavedItem Item(Detail::State& state,const Tables& tables,unsigned depth) {
    auto& bits=state.bits;const auto start=bits.pos;
    Detail::Prefix prefix;
    if(!Detail::ItemPrefix(state,tables,depth,prefix))throw std::runtime_error("Saved item header could not be parsed.");
    SavedItem item;item.flags=prefix.flags;item.quality=prefix.quality?int(prefix.quality):2;item.uuid=prefix.uuid;
    if(prefix.definition)item.code=Code(std::string(prefix.definition->code.data(),4));
    else throw std::runtime_error("Ear items are not supported by the marketplace.");
    // Decode identity/location independently; use the audited parser for the
    // variable prefix and record boundary, including personalization/runewords.
    auto header=bits;header.pos=start+35;const auto mode=header.Read(3);
    if(mode==3||mode==5)throw std::runtime_error("Ground items cannot be listed from a stash.");
    header.Skip(4);item.x=header.Read(4);item.y=header.Read(4);item.container=header.Read(3);
    std::array<char,4> code{};if(!header.Code(code))throw std::runtime_error("Invalid saved item code.");
    if(!(prefix.flags&(1u<<21))) {
        header.Skip(3);item.id=header.Read(32);item.level=int(header.Read(7));header.Skip(4);
        if(header.Read(1))item.picture=int(header.Read(3));if(header.Read(1))header.Skip(11);
        if(prefix.quality==5||prefix.quality==7)item.index=int(header.Read(12));
        // Prefix parsing stops immediately before quantity/socket/list data.
        auto properties=bits;
        if(prefix.definition->stackable)item.quantity=properties.Read(9);
        if(prefix.flags&(1u<<11)){auto stat=Detail::Stat(tables,194);if(!stat)throw std::runtime_error("Missing socket stat.");properties.Skip(stat->saveBits);}
        unsigned extra=0;
        if(prefix.quality==5){auto mask=properties.Read(5);for(unsigned n=0;n<5;++n)extra+=(mask>>n)&1;}
        item.properties=Properties(properties,tables,"base");
        for(unsigned n=0;n<extra;++n){auto list=Properties(properties,tables,"set");for(auto& row:list)item.properties.push_back(std::move(row));}
        if(prefix.flags&(1u<<26)){auto list=Properties(properties,tables,"runeword");for(auto& row:list)item.properties.push_back(std::move(row));}
        if(!properties.ok || !Detail::ItemSuffix(state,tables,prefix,false))throw std::runtime_error("Saved item properties could not be parsed.");
        // ItemSuffix queues socket children for its original iterative parser.
        // This reader owns recursion so remove that task, then include children
        // in the parent's exact byte slice. They are never separate listings.
        if(prefix.sockets)state.tasks.pop_back();
        for(unsigned n=0;n<prefix.sockets;++n)item.children.push_back(Item(state,tables,depth+1).ToJson());
    }
    if(!bits.ok || bits.pos%8 || bits.pos<start)throw std::runtime_error("Invalid saved item boundary.");
    item.itemHex=Hex(bits.bytes.subspan(start/8,(bits.pos-start)/8));return item;
}
}
inline SavedItem ReadItem(std::span<const uint8_t> bytes,const SaveTables& tables,unsigned tab){
    if(bytes.empty()||bytes.size()>32768||tab<1||tab>7)throw std::runtime_error("Invalid live stash item.");
    CandyCornSaveData::Detail::Budget budget;CandyCornSaveData::Detail::State state;state.bits={bytes,0,&budget,true};
    auto item=StashDetail::Item(state,tables,0);
    if(!state.bits.ok||budget.limit||state.bits.pos!=bytes.size()*8)throw std::runtime_error("Live stash item has an invalid boundary.");
    item.tab=tab;ReadBagBalances(item,tables);return item;
}
inline std::vector<SavedItem> ReadStash(std::span<const uint8_t> bytes,const SaveTables& tables,uint64_t* gold=nullptr) {
    using namespace CandyCornSaveData;
    if(bytes.empty()||bytes.size()>8*1024*1024)throw std::runtime_error("Invalid shared stash file size.");
    std::vector<SavedItem> items;unsigned tab=0;uint64_t savedGold=0;
    for(size_t at=0;at<bytes.size();) {
        if(++tab>7 || bytes.size()-at<68)throw std::runtime_error("Invalid 2.4 shared stash tab count or header.");
        const auto size=Detail::Word(bytes,at+16);
        if(size<68||size>bytes.size()-at)throw std::runtime_error("Invalid shared stash tab length.");
        const auto record=bytes.subspan(at,size);
        if(Detail::Word(record,0)!=0xaa55aa55||Detail::Word(record,4)>1||Detail::Word(record,8)!=98||Detail::Word(record,64,2)!=0x4d4a)
            throw std::runtime_error("Only current-format 2.4 (version 98) shared stash tabs are supported.");
        savedGold+=Detail::Word(record,12);
        Detail::Budget budget;Detail::State state;state.bits={record,68*8,&budget,true};
        const auto count=Detail::Word(record,66,2);
        if(count>4096)throw std::runtime_error("Shared stash item count exceeds marketplace limits.");
        for(unsigned n=0;n<count;++n) {
            auto item=StashDetail::Item(state,tables,0);item.tab=tab;item.ordinal=n+1;ReadBagBalances(item,tables);items.push_back(std::move(item));
        }
        if(!state.bits.ok||state.bits.pos!=record.size()*8||budget.limit)throw std::runtime_error("Shared stash item list has an invalid boundary.");
        at+=size;
    }
    // A complete 2.4 trade page is seven shared tabs. Refuse incomplete files
    // rather than silently retiring listings from an in-progress save.
    if(tab!=7)throw std::runtime_error("A 2.4 trade page must contain all seven shared tabs.");
    if(gold)*gold=savedGold;
    return items;
}
}
