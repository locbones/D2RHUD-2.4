#pragma once
#include "../json.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>
#include <tuple>

// Shared by the in-game editor and the offline companion. Prices are an AND
// bundle of exact item identities, never localized names or inventory slots.
namespace Marketplace {
using Json = nlohmann::json;
inline constexpr int Schema = 1;
inline constexpr int MaxQuantity = 1000000;
inline constexpr int MaxResourceQuantity = 2000000000;
inline constexpr size_t MaxPriceLines = 16;
struct BagResource {const char* stat;const char* label;};
inline constexpr std::array BagResources{
    BagResource{"GB_Total","Gems (bag)"},
    BagResource{"RB_Low","Low rune points (bag)"}, BagResource{"RB_Mid","Mid rune points (bag)"},
    BagResource{"RB_High","High rune points (bag)"}, BagResource{"RB_Ultra","Ultra rune points (bag)"},
    BagResource{"JB_Magic","Magic jewels (bag)"}, BagResource{"JB_Rare","Rare jewels (bag)"},
    BagResource{"PB_Rejuv","Full rejuvenation potions (bag)"},
    BagResource{"KB_Terror","Keys of Terror (bag)"}, BagResource{"KB_Hate","Keys of Hate (bag)"},
    BagResource{"KB_Dest","Keys of Destruction (bag)"}, BagResource{"OB_Diablo","Horns (bag)"},
    BagResource{"OB_Baal","Eyes (bag)"}, BagResource{"OB_Meph","Brains (bag)"},
    BagResource{"score_tracker","Set cores (bag)"}, BagResource{"ucore_tracker","Unique cores (bag)"}};
inline int Integer(const Json& value,int minimum,int maximum) {
    if(!value.is_number_integer() || (value.is_number_unsigned() && value.get<uint64_t>()>uint64_t(maximum)))
        throw std::runtime_error("Invalid marketplace number.");
    const auto number=value.get<int64_t>();
    if(number<minimum||number>maximum)throw std::runtime_error("Invalid marketplace number.");
    return int(number);
}
inline std::string Code(std::string value) {
    while (!value.empty() && value.back() == ' ') value.pop_back();
    return value;
}
struct Requirement {
    std::string code, label;
    int quality = 2; // Normal base/rune, Set=5, Unique=7.
    int index = -1; // Compiled uniqueitems/setitems row, not a string ID.
    int quantity = 1;
    int ethereal = -1; // -1 either, 0 non-ethereal, 1 ethereal.
    std::string kind="item",stat; // Saved symbols, never table-row IDs for bag balances.
    int classId=-1; // Optional Hellfire Torch class; absent/-1 accepts any class.
};
inline constexpr std::array<const char*,7> ClassNames{"Amazon","Sorceress","Necromancer","Paladin","Barbarian","Druid","Assassin"};
inline bool ClassChoice(const Requirement& r){return r.kind=="item"&&r.code=="cm2"&&r.quality==7;}
inline std::string RequirementName(const Requirement& r){return r.label+(r.classId>=0&&r.classId<7?" ("+std::string(ClassNames[r.classId])+")":"");}
inline int QuantityLimit(const Requirement& r){return r.kind=="item"?MaxQuantity:MaxResourceQuantity;}
inline bool IsFree(const std::vector<Requirement>& price){return price.size()==1&&price[0].kind=="free";}
inline bool IsOpen(const std::vector<Requirement>& price){return price.size()==1&&price[0].kind=="open";}
inline std::vector<Requirement> FreePrice(){return {{"free","Free",2,-1,1,-1,"free",{}}};}
inline std::string Validate(const std::vector<Requirement>& price) {
    if (price.empty()) return "Add at least one payment item.";
    if (price.size() > MaxPriceLines) return "A price can contain at most 16 item types.";
    if(IsOpen(price)){const auto& r=price[0];return r.code=="open"&&r.quality==2&&r.index==-1&&r.quantity==1&&r.ethereal==-1&&r.stat.empty()&&r.classId==-1?std::string():"Invalid open-ended listing.";}
    if(IsFree(price)){const auto& r=price[0];return r.code=="free"&&r.quality==2&&r.index==-1&&r.quantity==1&&r.ethereal==-1&&r.stat.empty()&&r.classId==-1?std::string():"Invalid free listing.";}
    for (size_t i = 0; i < price.size(); ++i) {
        const auto& r = price[i];
        if(r.classId < -1 || r.classId > 6 || (r.classId>=0&&!ClassChoice(r)))return "Choose a valid Hellfire Torch class.";
        if (r.label.size() > 512 || r.code.empty() || r.code.size() > 4 || r.code != Code(r.code) ||
            !std::all_of(r.code.begin(), r.code.end(), [](unsigned char c) { return c >= 33 && c <= 126; }))
            return "Select a valid item from the list.";
        if (r.quantity < 1 || r.quantity > QuantityLimit(r)) return "Enter a quantity within the payment limit.";
        if(r.kind!="item"){
            if(r.quality!=2||r.index!=-1||r.ethereal!=-1||
               (r.kind=="gold"?(r.code!="gld"||!r.stat.empty()):r.kind!="bag"||r.code!="Z01"||
                std::none_of(BagResources.begin(),BagResources.end(),[&](const auto& b){return r.stat==b.stat;})))
                return "Select a supported gold or bag balance.";
        }else if(!r.stat.empty()||r.code=="gld")return "Choose Gold as a balance, not as an item.";
        if ((r.quality != 2 && r.quality != 5 && r.quality != 7) ||
            (r.quality == 2 ? r.index != -1 : r.index < 0) || r.ethereal < -1 || r.ethereal > 1)
            return "The selected payment item has an invalid identity.";
        for (size_t j = 0; j < i; ++j) {
            const auto& other = price[j];
            if(r.code=="Z01"&&other.code=="Z01"&&((r.kind=="bag")!=(other.kind=="bag")))
                return "Choose the storage bag or its contents in one price, not both.";
            if (r.kind==other.kind&&r.stat==other.stat&&r.code == other.code && r.quality == other.quality && r.index == other.index &&
                (r.ethereal == -1 || other.ethereal == -1 || r.ethereal == other.ethereal)&&
                (r.classId==-1||other.classId==-1||r.classId==other.classId))
                return "Combine overlapping payment lines into one quantity.";
        }
    }
    return {};
}
inline Json PriceJson(const std::vector<Requirement>& price) {
    const auto error = Validate(price); if (!error.empty()) throw std::runtime_error(error);
    Json result = Json::array();
    for (const auto& r : price){Json row={{"code",r.code},{"label",r.label},{"quality",r.quality},
        {"index",r.index},{"quantity",r.quantity},{"ethereal",r.ethereal}};
        if(r.kind!="item"){row["kind"]=r.kind;if(!r.stat.empty())row["stat"]=r.stat;}if(r.classId>=0)row["class_id"]=r.classId;result.push_back(std::move(row));}
    return result;
}
inline std::vector<Requirement> ReadPrice(const Json& value) {
    if (!value.is_array() || value.size() > MaxPriceLines) throw std::runtime_error("Invalid payment bundle.");
    std::vector<Requirement> result;
    for (const auto& row : value) {
        result.push_back({row.at("code").get<std::string>(),row.at("label").get<std::string>(),
            Integer(row.at("quality"),2,7),Integer(row.at("index"),-1,MaxQuantity),
            Integer(row.at("quantity"),1,MaxResourceQuantity),Integer(row.at("ethereal"),-1,1),row.value("kind",std::string("item")),row.value("stat",std::string()),Integer(row.value("class_id",Json(-1)),-1,6)});
        if (result.back().label.size() > 512) throw std::runtime_error("Payment label is too long.");
    }
    const auto error = Validate(result); if (!error.empty()) throw std::runtime_error(error);
    return result;
}
struct PaymentItem {
    std::string code;
    int quality = 2, index = -1;
    uint32_t quantity = 1;
    bool identified = true, ethereal = false;
    std::string kind="item",stat;
    int classId=-1;
};
inline bool Matches(const Requirement& r, const PaymentItem& item) {
    return item.identified && item.kind==r.kind&&item.stat==r.stat&&Code(item.code) == r.code && item.quality == r.quality &&
        (r.index < 0 || item.index == r.index) && (r.ethereal < 0 || bool(r.ethereal) == item.ethereal)&&
        (r.classId<0||r.classId==item.classId);
}
// Pure availability check only. Custody, locking and consuming payment belong
// to the transaction service; this must never be treated as a purchase receipt.
inline bool CanPay(const std::vector<Requirement>& price, const std::vector<PaymentItem>& items) {
    if (!Validate(price).empty()) return false;
    if(IsOpen(price))return false;
    if(IsFree(price))return true;
    for (const auto& r : price) {
        uint64_t available = 0;
        for (const auto& item : items) if (Matches(r,item)) available += item.quantity;
        if (available < uint64_t(r.quantity)) return false;
    }
    return true;
}
inline std::string Hex(std::span<const uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef"; std::string result; result.reserve(bytes.size()*2);
    for (auto b : bytes) { result += digits[b >> 4]; result += digits[b & 15]; } return result;
}
struct Draft {
    unsigned tab = 1;
    std::string itemHex; // Complete parent and socket children; changes invalidate the draft.
    std::vector<Requirement> price;
    std::string comment;
};
inline bool ValidListingComment(const std::string& text){return text.size()<=960&&std::count_if(text.begin(),text.end(),[](unsigned char c){return (c&0xc0)!=0x80;})<=240&&std::none_of(text.begin(),text.end(),[](unsigned char c){return c<32||c==127;});}
inline bool ValidIdentity(const std::string& text) {
    return !text.empty()&&text.size()<=128&&std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=32&&c!=127;});
}
inline bool ValidItemHex(const std::string& hex) {
    return !hex.empty()&&hex.size()<=65536&&hex.size()%2==0&&hex.find_first_not_of("0123456789abcdef")==std::string::npos;
}
struct AgreementDraft {
    std::string seller,listing,revision,itemHex;
    std::vector<Requirement> price;
    bool simulated=false;
};
struct SimulationSettings {bool enabled=false;int page=3;std::vector<Draft> drafts;};
struct Settings {
    bool enabled = false;
    int page = 1;
    std::vector<Draft> drafts;
    int withdrawPage = 2;
    bool autoTrade = false;
    std::vector<AgreementDraft> agreements;
    SimulationSettings simulation;
};
inline Json SettingsJson(const Settings& settings, const std::string& mod, bool hardcore) {
    if (settings.page < 1 || settings.page > 64 || settings.drafts.size() > 4096) throw std::runtime_error("Invalid trade page or draft count.");
    if (settings.withdrawPage < 1 || settings.withdrawPage > 64)
        throw std::runtime_error("Market page must be between 1 and 64.");
    if(settings.simulation.page<1||settings.simulation.page>64||
        (settings.simulation.enabled&&(settings.simulation.page==settings.page||settings.simulation.page==settings.withdrawPage)))
        throw std::runtime_error("The simulated seller needs a third, separate page between 1 and 64.");
    auto encodeDrafts=[](const std::vector<Draft>& rows){
        if(rows.size()>4096)throw std::runtime_error("Too many asking-price drafts.");
        Json result=Json::array();std::set<std::pair<unsigned,std::string>> identities;
        for(const auto& draft:rows){
            if(draft.tab<1||draft.tab>7||!ValidItemHex(draft.itemHex))throw std::runtime_error("Invalid listing item.");
            if(!identities.emplace(draft.tab,draft.itemHex).second)throw std::runtime_error("An item has more than one asking price.");
            if(!ValidListingComment(draft.comment))throw std::runtime_error("Write a listing comment of up to 240 characters.");
            result.push_back({{"tab",draft.tab},{"item_hex",draft.itemHex},{"price",PriceJson(draft.price)},{"comment",draft.comment}});
        }return result;
    };
    const auto drafts=encodeDrafts(settings.drafts),sellerDrafts=encodeDrafts(settings.simulation.drafts);
    Json agreements=Json::array();std::set<std::tuple<bool,std::string,std::string>> agreed;
    if(settings.agreements.size()>256)throw std::runtime_error("Too many agreement drafts.");
    for(const auto& a:settings.agreements){
        if(!ValidIdentity(a.seller)||!ValidIdentity(a.listing)||!ValidIdentity(a.revision)||!ValidItemHex(a.itemHex)||
            !agreed.emplace(a.simulated,a.seller,a.listing).second)throw std::runtime_error("Invalid or duplicate agreement draft.");
        agreements.push_back({{"seller",a.seller},{"listing",a.listing},{"revision",a.revision},{"item_hex",a.itemHex},{"price",PriceJson(a.price)},{"simulated",a.simulated}});
    }
    return {{"schema",Schema},{"game_mode","2.4"},{"mod",mod},{"hardcore",hardcore},
        {"enabled",settings.enabled},{"page",settings.page},{"withdraw_page",settings.page},{"single_market_page",true},
        {"auto_trade",settings.autoTrade},{"drafts",drafts},{"agreements",agreements},
        {"simulation",{{"enabled",settings.simulation.enabled},{"page",settings.simulation.page},{"drafts",sellerDrafts}}}};
}
inline Settings ReadSettings(const Json& source, const std::string& mod, bool hardcore) {
    if (source.at("schema") != Schema || source.at("game_mode") != "2.4" || source.at("mod") != mod ||
        source.at("hardcore") != hardcore || !source.at("enabled").is_boolean() ||
        !source.at("page").is_number_integer() || !source.at("drafts").is_array() || source.at("drafts").size() > 4096)
        throw std::runtime_error("Marketplace settings belong to another mode or use an unsupported format.");
    Settings result; result.page = Integer(source.at("page"),1,64); result.enabled = source.at("enabled").get<bool>();
    // Older drafts only designated a deposit page. Preserve their prices and
    // default to manual approval; never opt an existing player into auto-trade.
    result.withdrawPage = result.page; // Legacy routes converge on the existing sale page.
    if(source.contains("auto_trade")) {
        if(!source.at("auto_trade").is_boolean())throw std::runtime_error("Invalid auto-trade policy.");
        result.autoTrade=source.at("auto_trade").get<bool>();
    }
    for (const auto& row : source.at("drafts")) {
        result.drafts.push_back({unsigned(Integer(row.at("tab"),1,7)),row.at("item_hex").get<std::string>(),ReadPrice(row.at("price")),row.value("comment",std::string())});
    }
    while(result.simulation.page==result.page||result.simulation.page==result.withdrawPage)++result.simulation.page;
    if(source.contains("simulation")){
        const auto& sim=source.at("simulation");
        if(!sim.at("enabled").is_boolean()||!sim.at("drafts").is_array()||sim.at("drafts").size()>4096)throw std::runtime_error("Invalid simulated seller settings.");
        result.simulation.enabled=sim.at("enabled").get<bool>();result.simulation.page=Integer(sim.at("page"),1,64);
        for(const auto& row:sim.at("drafts"))result.simulation.drafts.push_back({unsigned(Integer(row.at("tab"),1,7)),row.at("item_hex").get<std::string>(),ReadPrice(row.at("price")),row.value("comment",std::string())});
    }
    if(source.contains("agreements")){
        const auto& rows=source.at("agreements");if(!rows.is_array()||rows.size()>256)throw std::runtime_error("Invalid agreement drafts.");
        for(const auto& row:rows)result.agreements.push_back({row.at("seller").get<std::string>(),row.at("listing").get<std::string>(),
            row.at("revision").get<std::string>(),row.at("item_hex").get<std::string>(),ReadPrice(row.at("price")),false});
        for(size_t n=0;n<rows.size();++n)if(rows[n].contains("simulated")){if(!rows[n]["simulated"].is_boolean())throw std::runtime_error("Invalid simulated agreement flag.");result.agreements[n].simulated=rows[n]["simulated"].get<bool>();}
    }
    (void)SettingsJson(result,mod,hardcore); return result;
}
}
