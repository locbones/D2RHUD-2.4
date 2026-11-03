#include "../d2rhud/plugin/ItemFilter/ItemText.h"
#include <Windows.h>
#include <array>
#include <cassert>
#include <iostream>
#include <string>

int main(){
    size_t checks=0;
    for(const std::string word:{std::string("Gold"),std::string("Золотая монета"),std::string("宝物"),std::string("😀")}){
        std::string text;for(int i=0;i<2000;++i)text+=word;
        for(size_t capacity=1;capacity<=1024;++capacity){
            std::array<char,1032> guarded;guarded.fill('!');
            ItemText::Copy(guarded.data()+4,capacity,text);
            for(size_t i=0;i<4;++i)assert(guarded[i]=='!');
            for(size_t i=4+capacity;i<guarded.size();++i)assert(guarded[i]=='!');
            const auto length=strnlen_s(guarded.data()+4,capacity);
            assert(length<capacity && text.starts_with(std::string_view(guarded.data()+4,length)));
            assert(!length || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,guarded.data()+4,int(length),nullptr,0)>0);
            ++checks;
        }
    }
    char same[12]="Example";ItemText::Copy(same,5,same);assert(std::string(same)=="Exam");
    ItemText::Copy(nullptr,0,"ignored");char zero='!';ItemText::Copy(&zero,0,"ignored");assert(zero=='!');
    char embedded[8];ItemText::Copy(embedded,sizeof(embedded),std::string_view("a\0b",3));assert(std::string(embedded)=="a");
    std::cout<<checks<<" bounded item-text copies preserve guard bytes, termination and UTF-8 across English, Russian, Chinese and emoji.\n";
}
