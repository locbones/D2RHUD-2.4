#pragma once
#include "RunTrackerData.h"
#include <filesystem>
#include <functional>
#include <set>

namespace RunTracker::Analysis {
struct Base {std::string category="Other",type;};
struct Edge {std::string target;double weight=0;};
struct Treasure {double noDrop=0;int picks=1;std::vector<Edge> edges;};
struct Sale {std::string identity,currency,label;double amount=0;int quantity=0;int64_t at=0;};
struct Sources {
    std::map<std::string,Base> bases;std::map<std::string,Treasure> treasures;
    std::map<std::string,std::string> namedCodes;
    std::vector<Sale> sales;std::map<std::string,std::string> currencies;
    std::string mod,catalogStatus,treasureStatus,marketStatus;bool hardcore=false;
    int64_t loadedUtc=0,marketExpires=0;size_t ignoredSales=0;
};
inline std::string Identity(const Item& i){return i.code+"/"+std::to_string(i.quality)+"/"+std::to_string(i.quality==5||i.quality==7?i.fileIndex:-1)+"/"+std::to_string(i.ethereal)+"/"+std::to_string(i.sockets);}
inline void ReadMarket(Sources& out,const Json& document,const std::string& mod,bool hardcore,int64_t now){
    if(!document.is_object()||document.value("schema",0)!=1||document.value("game_mode","")!="2.4"||document.value("mod","")!=mod||!document.contains("hardcore")||!document["hardcore"].is_boolean()||document["hardcore"].get<bool>()!=hardcore)throw std::runtime_error("Marketplace history belongs to another mod or game mode.");
    const auto& history=document.at("history");if(!history.is_array()||history.size()>1000)throw std::runtime_error("Unsupported marketplace history size.");
    out.marketExpires=document.value("expires_at",int64_t(0));std::set<std::string> ids;
    for(const auto& row:history){try{
        const auto id=row.at("id").get<std::string>();if(id.empty()||id.size()>128||!ids.insert(id).second)throw std::runtime_error("Duplicate sale");
        if(row.value("kind","")!="completed_sale"||row.contains("bundle")||row.value("simulated",false)||row.value("development",false))throw std::runtime_error("Not an individual completed sale");
        Sale s;Item item;item.code=row.at("code").get<std::string>();while(!item.code.empty()&&item.code.back()==' ')item.code.pop_back();if(item.code.empty()||item.code.size()>4)throw std::runtime_error("Invalid code");
        item.quality=row.at("quality").get<int>();item.fileIndex=row.at("item_index").get<int>();const auto eth=row.at("ethereal").get<int>();item.ethereal=eth!=0;item.sockets=row.at("sockets").get<int>();
        if(eth<0||eth>1||item.fileIndex< -1||item.fileIndex>2000000000||item.quality<1||item.quality>9||item.sockets<0||item.sockets>255)throw std::runtime_error("Invalid item criteria");s.identity=Identity(item);s.at=row.at("at").get<int64_t>();if(s.at<1||s.at>now)throw std::runtime_error("Invalid sale date");
        s.quantity=row.value("item_quantity",0);if(s.quantity<0||s.quantity>2000000000)throw std::runtime_error("Invalid sold quantity");
        const auto& price=row.at("price");if(!price.is_array()||price.empty()||price.size()>16)throw std::runtime_error("Invalid payment");
        for(const auto& line:price){const auto kind=line.value("kind",std::string("item"));if(kind!="item"&&kind!="gold"&&kind!="bag")throw std::runtime_error("Free or unsupported payment");
            const auto quantity=line.at("quantity").get<int64_t>();if(quantity<=0||quantity>2000000000)throw std::runtime_error("Invalid payment quantity");
            const auto code=line.value("code","");const auto stat=line.value("stat","");if(code.empty()||code.size()>4||stat.size()>128)throw std::runtime_error("Invalid currency");
            const auto key=kind+"/"+code+"/"+std::to_string(line.value("quality",2))+"/"+std::to_string(line.value("index",-1))+"/"+std::to_string(line.value("ethereal",-1))+"/"+stat;
            if(!s.currency.empty()&&s.currency!=key)throw std::runtime_error("Mixed barter cannot be converted");s.currency=key;s.label=kind=="gold"?"Gold":line.value("label",code);if(s.label.empty())s.label=code;if(s.label.size()>512)throw std::runtime_error("Invalid currency label");s.amount+=double(quantity);
        }
        out.currencies.emplace(s.currency,s.label);out.sales.push_back(std::move(s));
    }catch(...){++out.ignoredSales;}}
    out.marketStatus="Completed sales from the local marketplace sync cache (latest 1,000 maximum).";
    if(out.marketExpires<now)out.marketStatus+=" The cache is stale; sync the marketplace to refresh it.";
}
struct Weight {double value=0;bool complete=true;};
inline Weight TableWeight(const Sources& source,const std::string& root,const std::string& code){
    std::map<std::string,Weight> memo;std::set<std::string> path;size_t budget=0;
    std::function<Weight(const std::string&)> visit=[&](const std::string& node)->Weight{
        if(node==code)return {1,true};if(source.bases.contains(node))return {0,true};
        if(auto cached=memo.find(node);cached!=memo.end())return cached->second;
        if(++budget>20000||path.size()>=64||!path.insert(node).second)return {0,false};
        const auto at=source.treasures.find(node);if(at==source.treasures.end()){path.erase(node);return {0,false};}
        const auto& tc=at->second;Weight result;double total=tc.noDrop;
        if(tc.picks<=0||tc.noDrop<0){path.erase(node);return {0,false};}
        for(const auto& edge:tc.edges)if(edge.weight>0)total+=edge.weight;
        if(total<=0){path.erase(node);return {0,false};}
        for(const auto& edge:tc.edges)if(edge.weight>0){auto target=edge.target;const auto alias=source.namedCodes.find(target);if(alias!=source.namedCodes.end())target=alias->second;
            const auto child=visit(target);result.value+=edge.weight/total*child.value;result.complete&=child.complete;
        }
        // Deliberately one weighted selection per node, independent of Picks:
        // this is a relative base-selection model, not a per-kill drop chance.
        path.erase(node);memo[node]=result;return result;
    };return visit(root);
}
struct Entry {Item item;int area=0;std::string run,character;int64_t utc=0;bool modeKnown=false,hardcore=false;std::string mod;};
struct Input {std::vector<Entry> entries;std::map<int,uint64_t> activeMs;std::map<int,size_t> visits;size_t runs=0;bool truncated=false;};
inline Input Gather(const std::vector<const Run*>& runs){
    Input input;for(const auto* run:runs){if(input.runs>=500||input.entries.size()+run->items.size()>100000){input.truncated=true;break;}++input.runs;
        for(const auto& v:run->visits){input.activeMs[v.area]+=v.activeMs;++input.visits[v.area];}
        for(const auto& f:run->items)input.entries.push_back({f.item,f.area,run->id,run->character,run->startedUtc,run->modeKnown,run->hardcore,run->mod});
    }return input;
}
struct Options {bool units=false,broadQualities=false;int days=30,minSamples=3;std::string currency,treasure;};
struct Share {std::string name;uint64_t count=0;double percent=0;};
struct Area {int id=0;uint64_t entries=0,units=0,activeMs=0;size_t visits=0,priced=0;double estimate=0;};
struct Valued {Entry entry;double unit=0,total=0;size_t samples=0;int64_t newest=0;};
struct Rare {Entry entry;double weight=0;};
struct Report {
    size_t entries=0,unpriced=0,runs=0;uint64_t units=0;bool truncated=false,rarityComplete=true;
    std::vector<Share> categories,qualities;std::vector<Area> areas;std::vector<Valued> valuable;std::vector<Rare> rare;
};
inline Report Build(const Input& input,const Sources& source,const Options& options,int64_t now){
    Report result;result.entries=input.entries.size();result.runs=input.runs;result.truncated=input.truncated;
    std::map<std::string,uint64_t> categories,qualities;std::map<int,Area> areas;std::map<std::string,std::vector<const Sale*>> prices;
    for(const auto& sale:source.sales)if(sale.currency==options.currency&&sale.at>=now-int64_t(options.days)*86400&&sale.at<=now)prices[sale.identity].push_back(&sale);
    std::map<std::string,Weight> rarity;
    struct Estimate {double median=0;size_t samples=0;int64_t newest=0;};
    std::map<std::pair<std::string,bool>,Estimate> estimates;
    for(const auto& [id,time]:input.activeMs){auto& area=areas[id];area.id=id;area.activeMs=time;if(input.visits.contains(id))area.visits=input.visits.at(id);}
    for(const auto& entry:input.entries){const auto& item=entry.item;const auto quantity=uint64_t((std::max)(1,item.quantity));result.units+=quantity;const auto amount=options.units?quantity:1;
        const auto base=source.bases.find(item.code);categories[entry.mod!=source.mod?"Other mod":base==source.bases.end()?"Unknown item type":base->second.category]+=amount;qualities[Quality(item.quality)]+=amount;
        auto& area=areas[entry.area];area.id=entry.area;++area.entries;area.units+=quantity;
        const bool eligible=entry.mod==source.mod&&entry.modeKnown&&entry.hardcore==source.hardcore&&item.sockets>=0&&item.stackableKnown&&(options.broadQualities||(item.quality!=4&&item.quality!=6&&item.quality!=8&&item.quality!=9));
        Estimate estimate;
        if(eligible){const auto key=std::make_pair(Identity(item),item.stackable);auto [at,created]=estimates.try_emplace(key);
            if(created){std::vector<double> samples;
                if(const auto price=prices.find(key.first);price!=prices.end())for(auto* sale:price->second){if(item.stackable&&sale->quantity<1)continue;samples.push_back(sale->amount/(sale->quantity>0?sale->quantity:1));at->second.newest=(std::max)(at->second.newest,sale->at);}
                at->second.samples=samples.size();if(!samples.empty()){std::sort(samples.begin(),samples.end());at->second.median=(samples[(samples.size()-1)/2]+samples[samples.size()/2])*.5;}
            }estimate=at->second;
        }
        if(estimate.samples>=size_t((std::max)(1,options.minSamples))){
            result.valuable.push_back({entry,estimate.median,estimate.median*quantity,estimate.samples,estimate.newest});++area.priced;area.estimate+=estimate.median*quantity;
        }else ++result.unpriced;
        if(!options.treasure.empty()&&entry.mod==source.mod){
            auto at=rarity.find(item.code);if(at==rarity.end())at=rarity.emplace(item.code,TableWeight(source,options.treasure,item.code)).first;
            result.rarityComplete&=at->second.complete;if(at->second.complete&&at->second.value>0)result.rare.push_back({entry,at->second.value});
        }
    }
    for(auto& [id,area]:areas){if(input.activeMs.contains(id))area.activeMs=input.activeMs.at(id);if(input.visits.contains(id))area.visits=input.visits.at(id);result.areas.push_back(area);}
    auto shares=[&](const auto& counts,auto& out){uint64_t total=0;for(const auto& [key,value]:counts)total+=value;for(const auto& [key,value]:counts)out.push_back({key,value,total?double(value)*100/total:0});std::sort(out.begin(),out.end(),[](const Share& a,const Share& b){return a.count>b.count||(a.count==b.count&&a.name<b.name);});};shares(categories,result.categories);shares(qualities,result.qualities);
    std::sort(result.valuable.begin(),result.valuable.end(),[](const Valued& a,const Valued& b){return a.total>b.total;});if(result.valuable.size()>10)result.valuable.resize(10);
    std::sort(result.rare.begin(),result.rare.end(),[](const Rare& a,const Rare& b){return a.weight<b.weight;});if(result.rare.size()>10)result.rare.resize(10);return result;
}
Sources Load(const std::filesystem::path& install,const std::string& mod,const std::filesystem::path& marketFolder,bool hardcore,int64_t now);
}
