#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>

namespace LegacyTransmog::StackData {
constexpr uint8_t Marker=0xd7, Begin=0xf1, Finish=0xf2;
constexpr uint32_t Magic=0x31545053; // SPT1; private local-server use packet
struct Request { uint32_t owner=0,item=0,amount=0,expected=0,token=0; uint8_t action=0; };
inline std::array<uint8_t,36> Encode(Request request) {
    std::array<uint8_t,36> packet{};packet[0]=0x26;packet[1]=Marker;packet[14]=request.action;
    memcpy(packet.data()+2,&request.owner,4);memcpy(packet.data()+10,&request.item,4);
    memcpy(packet.data()+18,&request.amount,4);memcpy(packet.data()+22,&request.expected,4);
    memcpy(packet.data()+26,&request.token,4);memcpy(packet.data()+30,&Magic,4);return packet;
}
inline bool Recognized(std::span<const uint8_t> packet) { return packet.size()==36 && packet[0]==0x26 && packet[1]==Marker; }
inline bool Decode(std::span<const uint8_t> packet,Request& out) {
    if(!Recognized(packet))return false;
    memcpy(&out.owner,packet.data()+2,4);memcpy(&out.item,packet.data()+10,4);
    memcpy(&out.amount,packet.data()+18,4);memcpy(&out.expected,packet.data()+22,4);memcpy(&out.token,packet.data()+26,4);out.action=packet[14];
    uint32_t magic=0;memcpy(&magic,packet.data()+30,4);
    return magic==Magic && !packet[6] && !packet[7] && !packet[8] && !packet[9] && !packet[15] && !packet[16] && !packet[17] && !packet[34] && !packet[35] &&
        out.token && (out.action==Begin || out.action==Finish) && out.expected>1 && out.expected<=0x7fffffff && out.amount>0 && out.amount<out.expected;
}
inline bool StoragePage(uint32_t page) { return page==0 || page==3 || page==4; }
}
