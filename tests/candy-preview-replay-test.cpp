// Independent replay of the client-side path from the TryingAgain crash.
// Reuse the conversion/native-fixture support without running its entry point.
#define wmain CandyRegressionMain
#include "candy-corn-save-test.cpp"
#undef wmain

namespace Replay {
using namespace CandyCornSaveData;
Tables tables;
std::vector<std::vector<uint8_t>> expectedItems;
size_t calls=0;
std::array<uint8_t,4096> quest{};
void* __fastcall Quest(){return quest.data();}
uint8_t* __fastcall Item(uint8_t* bytes,int64_t remaining,void*,int version){
    assert(version==98 && calls<expectedItems.size());
    const auto& expected=expectedItems[calls++];
    assert(remaining>=int64_t(expected.size()) && !memcmp(bytes,expected.data(),expected.size()));
    return bytes+expected.size();
}
void Bind(const std::filesystem::path& excel){
    using namespace Test;
    itemRows.clear();
    for(const auto name:{L"weapons.bin",L"armor.bin",L"misc.bin"}){
        const auto bytes=Read(excel/name);
        assert(bytes.size()==4+size_t(Detail::Word(bytes,0))*0x1b4);
        itemRows.insert(itemRows.end(),bytes.begin()+4,bytes.end());
    }
    const auto stats=Read(excel/L"itemstatcost.bin"),types=Read(excel/L"itemtypes.bin");
    statRows.assign(stats.begin()+4,stats.end());
    const auto count=Detail::Word(types,0),stride=(count+31)/32;
    typeRows.assign(size_t(count)*stride,0);
    for(uint32_t n=0;n<count;++n){
        std::vector<uint32_t> pending{n};
        while(!pending.empty()){
            const auto type=pending.back();pending.pop_back();
            auto& bits=typeRows[size_t(n)*stride+type/32];const auto mask=uint32_t(1)<<(type%32);
            if(bits&mask)continue;bits|=mask;
            for(unsigned offset:{4,6}){
                const auto parent=int16_t(Detail::Word(types,4+size_t(type)*0xe4+offset,2));
                if(parent>0 && parent<int(count))pending.push_back(uint32_t(parent));
            }
        }
    }
    assert(DecodeTables(itemRows,statRows,typeRows,count,stride,tables));
    std::sort(tables.items.begin(),tables.items.end(),[](const auto& a,const auto& b){return a.code<b.code;});
    Put(image,0x1ca0390,itemRows.data());Put(image,0x1ca0398,uint32_t(itemRows.size()/0x1b4));
    Put(image,0x1c9fb78,statRows.data());Put(image,0x1c9fb80,uint32_t(statRows.size()/0x144));
    Put(image,0x1c9fc10,typeRows.data());Put(image,0x1c9fbf8,count);Put(image,0x1c9fc08,stride);
}
void Expect(std::span<const uint8_t> bytes){
    Detail::Budget budget;Detail::State state;state.bits={bytes,0,&budget,true};
    assert(Detail::Header(state.bits,tables) && state.bits.Mark(0x4d4a));
    const auto count=state.bits.Read(16);
    for(unsigned n=0;n<count;++n){
        const auto begin=state.bits.pos/8;
        state.tasks={{Detail::Kind::Items,1,0}};
        while(!state.tasks.empty()){
            const auto task=state.tasks.back();state.tasks.pop_back();
            if(!task.count)continue;
            state.tasks.push_back({Detail::Kind::Items,task.count-1,task.depth});
            Detail::Prefix prefix;assert(Detail::ItemPrefix(state,tables,task.depth,prefix));
            if(prefix.definition && !(prefix.flags&(1u<<21)))assert(Detail::ItemSuffix(state,tables,prefix,false));
        }
        const auto end=state.bits.pos/8;
        expectedItems.emplace_back(bytes.begin()+begin,bytes.begin()+end);
    }
    assert(!expectedItems.empty());
}
}

int wmain(int argc,wchar_t** argv){
    using namespace Test;using namespace CandyCornSave;
    assert(argc==5);
    const auto output=std::filesystem::path(argv[1]);std::filesystem::create_directories(output);
    const auto file=Read(argv[2]);const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data()+dos->e_lfanew);
    image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    assert(image);testImage=reinterpret_cast<uintptr_t>(image);
    const auto sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned n=0;n<nt->FileHeader.NumberOfSections;++n)memcpy(image+sections[n].VirtualAddress,file.data()+sections[n].PointerToRawData,sections[n].SizeOfRawData);
    Replay::Bind(argv[3]);
    const auto original=Read(argv[4]);const auto converted=Convert(original,Replay::tables);
    assert(converted.status==CandyCornSaveData::Status::Converted && converted.converted>0);
    Replay::Expect(converted.bytes);
    SetEnvironmentVariableW(L"LOCALAPPDATA",output.c_str());Initialize("RMD-MP");assert(installed.load());
    Put(image,0x149cb50,&AcquireSRWLockShared);Put(image,0x149cb58,&ReleaseSRWLockShared);
    image[0x1434c50]=0xc3;
    // Execute the real header/stat parsing, item-list traversal and preview
    // control flow. Stub only appearance strings/quest storage and the exact
    // item decoder that aborted in the recorded crash. Check every input byte
    // reaching that decoder against the independently converted character.
    image[0x622d0]=0xc3;image[0x243ae0]=0xc3;
    Jump(0x243a20,reinterpret_cast<void*>(Replay::Quest));
    Jump(0x41ee40,reinterpret_cast<void*>(Replay::Item));
    auto source=original;PreviewBuffer buffer;buffer.bytes=source.data();buffer.size=source.size();
    std::array<uint8_t,64> summary{};std::array<uint8_t,4096> appearance{};std::array<uint8_t,3> progression{};uint8_t flag=0;
    const auto reader=Native<Preview>(0x41f570);
    assert(reader(&buffer,summary.data(),appearance.data(),progression.data(),reinterpret_cast<uintptr_t>(&flag)));
    assert(Replay::calls==Replay::expectedItems.size() && source==original && Read(argv[4])==original);
    assert(buffer.bytes==source.data() && buffer.size==original.size());
    assert(TryAcquireSRWLockExclusive(&buffer.lock));ReleaseSRWLockExclusive(&buffer.lock);
    const auto backup=directory/(Storage::Hex(Storage::Hash(original))+L"-"+std::to_wstring(original.size())+L".before-candy-corn.d2s");
    assert(Read(backup)==original);
    Uninstall();testImage=0;VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::cout<<"Actual native client preview replay passed: "<<converted.converted<<" legacy Candy Corn converted before "<<Replay::calls<<" root item decodes; original save/cache/lock and backup verified.\n";
}
