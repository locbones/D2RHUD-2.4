#define wmain ExistingMarketplaceTests
#include "marketplace-test.cpp"
#undef wmain
#undef assert
#define assert(value) do{if(!(value))throw std::runtime_error("Gift codec check: " #value);}while(false)
int wmain(int argc,wchar_t** argv)try{
 auto tables=Tables();tables.items[0].stackable=true;
 Json definition={{"schema",2},{"game_mode","2.4"},{"reader","d2r-98-v1"},{"codes",CandyCornSaveData::Detail::Codes},{"items",Json::object()},{"stats",Json::object()},{"bags",Json::object()}};
 for(const auto& d:tables.items)definition["items"][std::string(d.code.data(),4)]={d.armor,d.weapon,d.stackable,d.gold,d.tome,d.bodyPart,d.charm,d.questDifficulty,1,1};
 for(size_t i=0;i<tables.stats.size();++i){const auto& d=tables.stats[i];if(d.valid)definition["stats"]["s"+std::to_string(i)]={d.saveBits,d.paramBits,d.saveAdd,d.charBits};}
 Writer w;w.Add(1u<<4,32);w.Add(0,3);w.Add(0,3);w.Add(0,4);w.Add(0,4);w.Add(0,4);w.Add(5,3);w.Code("r33 ");w.Add(0,3);w.Add(0x1234,32);w.Add(85,7);w.Add(2,4);w.Add(0,1);w.Add(0,1);w.Add(0,1);const auto quantityBit=w.pos;w.Add(40,9);w.Add(511,9);w.Align();
 if(argc==2){auto plain=Unique(false);CandyCornSaveData::Detail::Budget budget;CandyCornSaveData::Detail::Bits bits{plain,53,&budget,true};std::array<char,4> code{};bits.Code(code);bits.Skip(3);for(size_t n=0;n<32;++n){const auto at=bits.pos+n;plain[at/8]=uint8_t((plain[at/8]&~(1u<<(at%8)))|(((0x87654321u>>n)&1u)<<(at%8)));}Json items=Json::array();for(const auto& bytes:{Unique(true),plain,Rune(),w.bytes}){auto row=ReadItem(bytes,tables,1).ToJson();row["page"]=11;row["quantity_bit"]=bytes==w.bytes?int(quantityBit):-1;row["sockets"]=row["socketed_items"].size();items.push_back(row);}Json out={{"definition",definition},{"items",items}};std::ofstream file(argv[1]);file<<out.dump();return 0;}
 assert(argc==3||argc==4);if(argc==4){const auto data=std::filesystem::path(argv[3]);tables=LoadSaveTables([&](const std::string& name){return ReadFile(data/L"global"/L"excel"/std::filesystem::u8path(name+".bin"));});}const auto rows=Json::parse(ReadFile(argv[2]));std::set<std::string> ids;
 for(const auto& row:rows){const auto original=ReadItem(Unhex(row.at("before").get<std::string>()),tables,1),copy=ReadItem(Unhex(row.at("after").get<std::string>()),tables,1);
  assert(copy.code==original.code&&copy.quality==original.quality&&copy.index==original.index&&copy.flags==original.flags&&copy.quantity==original.quantity&&copy.level==original.level&&copy.properties==original.properties&&copy.children.size()==original.children.size());
  auto verify=[&](auto&& self,const Json& before,const Json& after)->void{
   for(const auto* key:{"code","quality","index","flags","quantity","level","saved_properties"})assert(before.at(key)==after.at(key));
   const auto uuid=after.at("item_uuid").get<std::string>();assert(!uuid.empty()&&uuid!=before.at("item_uuid").get<std::string>()&&ids.insert(uuid).second);
   if(before.at("saved_id").get<uint32_t>())assert(after.at("saved_id")!=before.at("saved_id")&&after.at("saved_id").get<uint32_t>());
   const auto& oldChildren=before.at("socketed_items");const auto& children=after.at("socketed_items");assert(children.size()==oldChildren.size());for(size_t n=0;n<children.size();++n)self(self,oldChildren[n],children[n]);
  };verify(verify,original.ToJson(),copy.ToJson());
 }
 if(!rows.empty()){
  const auto now=UnixNow();Json gift={{"id",std::string(32,'a')},{"title","A community gift"},{"announcement","Welcome!"},{"owner",{{"name","Owner"}}},{"claimed",false},{"claimable",true},{"expires",now+3600},{"claim",nullptr},{"items",Json::array({{{"item_hex",rows[0].at("after")}}})}};
  Json doc={{"schema",1},{"game_mode","2.4"},{"mod","GiftFixture"},{"hardcore",false},{"self",{{"id","player"},{"name","Player"}}},{"expires_at",now+3600},{"history",Json::array()},{"offers",Json::array()},{"gifts",Json::array({gift})}};
  auto parsed=ReadActivity(doc,"GiftFixture",false,now,&tables);assert(parsed.gifts.size()==1&&parsed.gifts[0].items.size()==1&&parsed.gifts[0].claimable);
  doc["gifts"][0]["claimed"]=true;doc["gifts"][0]["claimable"]=false;doc["gifts"][0]["claim"]={{"message","A private message"}};assert(ReadActivity(doc,"GiftFixture",false,now,&tables).gifts[0].message=="A private message");
  Refuses([&]{ReadActivity(doc,"GiftFixture",true,now,&tables);});doc["gifts"][0]["items"][0]["item_hex"]="00";Refuses([&]{ReadActivity(doc,"GiftFixture",false,now,&tables);});
 }
 std::cout<<"Gift codec: native parser accepts every copy; root/socket UUIDs are unique; flags, exact properties, stack quantities and children are preserved.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
