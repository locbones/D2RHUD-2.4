#define NOMINMAX
#include "../d2rhud/plugin/HudLocalization.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace HudLocale;
using std::chrono::duration;
using std::chrono::steady_clock;
unsigned checks=0;
void Check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
std::shared_ptr<Catalog> Make(const std::string& name){
    auto catalog=std::make_shared<Catalog>();catalog->strings={{"name",name},{"value",name+" value"}};return catalog;
}
void Ownership(){
    CatalogStore<Catalog> store;
    Check(!store.Read() && store.RetainedCatalogs()==0,"New store has no active catalog");
    Check(!store.Publish(nullptr),"Empty publication is idempotent");
    auto first=Make("first");std::weak_ptr<Catalog> source=first;
    Check(store.Publish(first),"Initial catalog published");
    const auto original=store.Read();const char* label=original->strings.at("name").c_str();
    Check(original!=first.get(),"Publication freezes an independent immutable snapshot");
    first->strings["name"]="edited";
    Check(original->strings.at("name")=="first","Source alias cannot mutate published text");
    first.reset();Check(source.expired(),"Store does not retain mutable input aliases");
    Check(!store.Publish(Make("first")),"Identical bytes reuse active snapshot");
    Check(store.Read()==original && store.RetainedCatalogs()==1,"Unchanged reload allocates no retained catalog");
    auto second=Make("second");store.Publish(second);
    Check(store.Read()->strings.at("name")=="second" && store.RetainedCatalogs()==2,"New text publishes a distinct snapshot");
    Check(std::string(label)=="first","Old text pointer remains valid after publication");
    Check(store.Publish(nullptr) && !store.Read(),"Null publication enables source fallback");
    Check(std::string(label)=="first" && store.RetainedCatalogs()==2,"Null publication keeps old text pointers alive");
    Check(store.Publish(Make("first")) && store.Read()==original,"Returning to earlier content reuses original snapshot");
    Check(store.RetainedCatalogs()==2,"Revisiting translations does not grow retention");
    auto metadata=Make("first");metadata->rejected=1;store.Publish(metadata);
    Check(store.Read()->rejected==1 && store.RetainedCatalogs()==3,"Rejected-entry metadata participates in catalog identity");
}
struct Collision {size_t operator()(const Catalog&)const{return 0;}};
void Collisions(){
    CatalogStore<Catalog,Collision> store;
    auto first=Make("one"),second=Make("two");store.Publish(first);const auto one=store.Read();
    store.Publish(second);const auto two=store.Read();
    Check(one!=two && two->strings.at("name")=="two","Hash collision cannot reuse wrong translations");
    store.Publish(Make("one"));Check(store.Read()==one,"Collision bucket finds exact prior content");
    Check(store.RetainedCatalogs()==2,"Collision bucket retains only distinct contents");
}
struct FallibleCatalog {
    std::map<std::string,std::string,std::less<>> strings;
    size_t rejected=0;
    inline static bool failCopy=false;
    FallibleCatalog()=default;
    FallibleCatalog(const FallibleCatalog& source):strings(source.strings),rejected(source.rejected){if(failCopy)throw std::runtime_error("Injected copy failure");}
};
struct FallibleHash {
    inline static bool fail=false;
    size_t operator()(const FallibleCatalog& catalog)const{if(fail)throw std::runtime_error("Injected hash failure");return CatalogHash<FallibleCatalog>{}(catalog);}
};
void Failures(){
    CatalogStore<FallibleCatalog,FallibleHash> store;
    auto first=std::make_shared<FallibleCatalog>();first->strings["name"]="safe";store.Publish(first);
    const auto original=store.Read();auto next=std::make_shared<FallibleCatalog>();next->strings["name"]="next";
    FallibleCatalog::failCopy=true;bool caught=false;try{store.Publish(next);}catch(const std::runtime_error&){caught=true;}FallibleCatalog::failCopy=false;
    Check(caught && store.Read()==original && store.RetainedCatalogs()==1,"Copy failure leaves current catalog and retention intact");
    FallibleHash::fail=true;caught=false;try{store.Publish(next);}catch(const std::runtime_error&){caught=true;}FallibleHash::fail=false;
    Check(caught && store.Read()==original && store.RetainedCatalogs()==1,"Fingerprint failure leaves current catalog intact");
    Check(store.Publish(next) && store.Read()->strings.at("name")=="next","Store recovers after publication failures");
}
void Wrappers(){
    Publish(nullptr);
    const char* missing="Missing";
    Check(Tr(nullptr)==nullptr && Tr(missing)==missing,"Null and source-pointer fallbacks preserved");
    auto first=Make("Français 日本語");first->strings["Cancel"]="Annuler";Publish(first);
    const char* saved=Tr("Cancel");Check(std::string(saved)=="Annuler","Translated C-string returned");
    first->strings["Cancel"]="alias edited";
    Check(std::string(Tr("Cancel"))=="Annuler","Runtime wrapper reads frozen catalog");
    Publish(first);Check(std::string(Tr("Cancel"))=="alias edited" && std::string(saved)=="Annuler","Explicit publication picks up edits and retains old pointer");
    Check(Translate("name")=="Français 日本語","UTF-8 translation preserved");
    const std::string sliced="[Cancel]";Check(Translate(std::string_view(sliced).substr(1,6))=="alias edited","Non-terminated string view lookup respects boundaries");
    const std::string nul("Cancel\0tail",11);Check(Translate(nul)=="alias edited","Embedded NUL keeps previous C-string semantics");
    const std::string unknown(512,'x');Check(Translate(unknown)==unknown,"Long untranslated text preserved");
    Publish(nullptr);Check(std::string(saved)=="Annuler" && Translate("Cancel")=="Cancel","Fallback reload cannot invalidate old translated arguments");
}
void Concurrency(){
    CatalogStore<Catalog> store;
    const std::array<std::shared_ptr<Catalog>,3> variants{Make("alpha"),Make("beta"),Make("gamma")};
    for(auto& catalog:variants){for(int i=0;i<256;++i)catalog->strings["other "+std::to_string(i)]="value "+std::to_string(i);store.Publish(catalog);}
    std::atomic<bool> go=false,done=false;std::atomic<unsigned> ready=0,errors=0;
    std::atomic<uint64_t> reads=0;
    std::vector<std::thread> readers;
    for(unsigned thread=0;thread<4;++thread)readers.emplace_back([&]{
        ++ready;while(!go.load(std::memory_order_acquire))std::this_thread::yield();
        uint64_t count=0;
        do{
            if(const auto catalog=store.Read()){
                const auto& name=catalog->strings.at("name");
                if(catalog->strings.at("value")!=name+" value")++errors;
                const auto pointer=name.c_str();const std::string expected=name;
                std::this_thread::yield();
                if(std::string(pointer)!=expected)++errors;
            }
            ++count;
        }while(!done.load(std::memory_order_acquire) || count<1000);
        reads+=count;
    });
    while(ready.load()!=4)std::this_thread::yield();go.store(true,std::memory_order_release);
    std::vector<std::thread> writers;
    for(unsigned thread=0;thread<2;++thread)writers.emplace_back([&,thread]{
        for(unsigned i=0;i<300;++i){store.Publish(variants[(i+thread)%3]);if(i%17==0)store.Publish(nullptr);}
    });
    for(auto& writer:writers)writer.join();done.store(true,std::memory_order_release);
    for(auto& reader:readers)reader.join();
    Check(!errors && reads>=4000,"Concurrent readers see complete immutable catalogs and valid old text");
    Check(store.RetainedCatalogs()==3,"Concurrent repeated publications retain only three unique catalogs");
    std::cout<<reads<<" concurrent reads during 600 publications and null resets.\n";
}
struct PreviousStore {
    std::atomic<std::shared_ptr<const Catalog>> active;
    std::vector<std::shared_ptr<const Catalog>> retained;
    void Publish(std::shared_ptr<const Catalog> next){retained.push_back(next);active.store(std::move(next));}
    const char* Tr(const char* text)const{const auto table=active.load();if(!table)return text;const auto found=table->strings.find(text);return found==table->strings.end()?text:found->second.c_str();}
};
void ReloadsAndBenchmark(const std::filesystem::path& repository){
    std::ifstream file(repository/"translations/frFR.json",std::ios::binary);Check(bool(file),"Shipped French pack opened read-only");
    const std::string bytes{std::istreambuf_iterator<char>(file),{}};auto pack=std::make_shared<Catalog>();pack->Merge(bytes);
    CatalogStore<Catalog> store;PreviousStore previous;
    for(int reload=0;reload<128;++reload){auto next=std::make_shared<Catalog>(*pack);store.Publish(next);previous.Publish(next);}
    Check(store.RetainedCatalogs()==1 && previous.retained.size()==128,"128 unchanged full-pack reloads retain one snapshot instead of 128");
    Publish(pack);
    std::vector<std::string> queries{"Quest Journal","Cancel","Floating Damage","Not translated"};
    for(const auto& [key,value]:pack->strings){if(queries.size()==64)break;queries.push_back(key);}
    bool same=true;for(const auto& query:queries)same &= std::string(HudLocale::Tr(query.c_str()))==previous.Tr(query.c_str());
    Check(same,"Lookup benchmark results match previous runtime");
    std::vector<double> oldSamples,newSamples;uint64_t oldResult=0,newResult=0;
    constexpr int lookups=300000;
    for(int trial=0;trial<7;++trial){
        auto start=steady_clock::now();uint64_t result=0;
        for(int i=0;i<lookups;++i)result+=static_cast<unsigned char>(previous.Tr(queries[i%queries.size()].c_str())[0]);
        oldResult=result;oldSamples.push_back(duration<double,std::nano>(steady_clock::now()-start).count()/lookups);
        start=steady_clock::now();result=0;
        for(int i=0;i<lookups;++i)result+=static_cast<unsigned char>(HudLocale::Tr(queries[i%queries.size()].c_str())[0]);
        newResult=result;newSamples.push_back(duration<double,std::nano>(steady_clock::now()-start).count()/lookups);
        Check(oldResult==newResult,"Timed lookup checksums agree");
    }
    std::sort(oldSamples.begin(),oldSamples.end());std::sort(newSamples.begin(),newSamples.end());
    std::cout<<"128 unchanged full-pack reloads: previous retained "<<previous.retained.size()<<" catalogs; new retained "<<store.RetainedCatalogs()<<".\n";
    std::cout<<"Translation lookup seven-sample median: previous "<<oldSamples[3]<<" ns; immutable store "<<newSamples[3]<<" ns.\n";
}
int main(int argc,char** argv){try{Check(argc==2,"Expected read-only repository root");Ownership();Collisions();Failures();Wrappers();Concurrency();ReloadsAndBenchmark(argv[1]);std::cout<<checks<<" translation-store checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
