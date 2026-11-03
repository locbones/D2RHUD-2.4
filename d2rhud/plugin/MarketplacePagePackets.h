#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include <stdexcept>

// CLIENT_SendSaveFile (2.4 RVA 0x10dfc0): each tab is one save header,
// fragmented into 255-byte 0x6c payloads; only its first fragment has flag 1.
// Prepare every packet before queuing any. No character save is read or sent.
namespace Marketplace::PagePackets {
inline std::vector<std::vector<uint8_t>> Build(const std::vector<uint8_t>& bytes){
    if(bytes.size()>7*65535)throw std::runtime_error("Trade page exceeds native save limits.");
    auto word=[&](size_t at,unsigned n){if(at>bytes.size()||n>bytes.size()-at)throw std::runtime_error("Trade page is truncated.");uint32_t v=0;for(unsigned k=0;k<n;++k)v|=uint32_t(bytes[at+k])<<(k*8);return v;};
    // The in-game shared-stash parser (RVA 0x319b70) starts at save-header
    // index 1. Reserve index 0 with a bounded ignored placeholder rather
    // than re-sending the character save. AttachSaveFile accepts opaque bytes.
    std::vector<std::vector<uint8_t>> packets{{0x6c,1,1,0}};size_t at=0;
    for(unsigned tab=0;tab<7;++tab){
        if(word(at,4)!=0xaa55aa55||word(at+8,4)!=98||word(at+64,2)!=0x4d4a)throw std::runtime_error("Trade page has an unsupported native tab header.");
        const auto length=word(at+16,4);
        if(length<68||length>65535||length>bytes.size()-at||word(at+66,2)>4096)throw std::runtime_error("Trade page has an invalid native tab boundary.");
        for(size_t offset=0;offset<length;){
            const auto count=uint8_t((std::min)(size_t(255),size_t(length)-offset));
            std::vector<uint8_t> packet{0x6c,count,uint8_t(offset==0)};
            packet.insert(packet.end(),bytes.begin()+at+offset,bytes.begin()+at+offset+count);packets.push_back(std::move(packet));offset+=count;
        }
        at+=length;
    }
    if(at!=bytes.size())throw std::runtime_error("Trade page contains extra native tab data.");
    return packets;
}
}
