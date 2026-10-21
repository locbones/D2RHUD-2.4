#pragma once
// Reusable, CRT-free helper. Call on the normal client skill thread around
// the existing ORIGINAL Whirlwind-start trampoline, not its detoured entry.
namespace WhirlwindRestart {
using Byte=unsigned char;
using UInt=unsigned int;
using NativeStart=int (*)(Byte*,int);
template<class T> static T& Field(Byte* p,UInt n) { return *reinterpret_cast<T*>(p+n); }
static Byte* UsedSkill(Byte* unit)
{
    auto list=Field<Byte*>(unit,0x100);return list ? Field<Byte*>(list,0x18) : nullptr;
}
static bool IsWhirlwind(Byte* skill)
{
    auto row=skill ? Field<Byte*>(skill,0) : nullptr;
    return row && *reinterpret_cast<unsigned short*>(row)==151;
}
static bool Supported(Byte* exe)
{
    if (!exe || Field<unsigned short>(exe,0)!=0x5A4D) return false;
    UInt pe=Field<UInt>(exe,0x3C);
    if (pe>0x1000 || Field<UInt>(exe,pe)!=0x4550 || Field<UInt>(exe,pe+8)!=0x62607AAC ||
        Field<UInt>(exe,pe+0x50)!=0x261A000) return false;
    const Byte guard[]={0x83,0x78,0x1C,0x00,0x7D,0x10,0xF7,0x80,0xCC,0x0A,0x00,0x00,0x00,0x00,0x40,0x00};
    for (UInt i=0;i<sizeof(guard);++i) if (exe[0x17C019+i]!=guard[i]) return false;
    return true;
}
static Byte* Begin(Byte* exe,Byte* unit,int id,bool d3StyleEnabled)
{
    if (id!=151 || d3StyleEnabled || !unit || Field<UInt>(unit,0)!=0 ||
        Field<UInt>(unit,4)!=6 || Field<UInt>(unit,0xC)!=18 || !Supported(exe)) return nullptr;
    auto skill=UsedSkill(unit);
    if (!IsWhirlwind(skill) || (Field<UInt>(skill,0x14)&3)!=0) return nullptr;
    auto session=Field<Byte*>(exe,0x1D637F0);
    auto game=session ? Field<Byte*>(session,0x230) : nullptr;
    if (!game || Field<Byte>(game,0x101)!=3) return nullptr;
    using Local=Byte* (*)(int);
    if (reinterpret_cast<Local>(exe+0x6B050)(Field<int>(exe,0x1D442D8))!=unit) return nullptr;
    UInt uid=Field<UInt>(unit,8);
    auto server=Field<Byte*>(game,(0x447+(uid&0x7F))*8);
    bool active=false;
    for (UInt i=0;server && i<128;++i,server=Field<Byte*>(server,0x150)) {
        if (Field<UInt>(server,0)!=0 || Field<UInt>(server,8)!=uid) continue;
        auto serverSkill=UsedSkill(server);
        active=Field<UInt>(server,4)==6 && Field<UInt>(server,0xC)==18 &&
            IsWhirlwind(serverSkill) && (Field<UInt>(serverSkill,0x14)&3)==1;
        break;
    }
    if (!active) return nullptr;
    auto stats=Field<Byte*>(unit,0x88);
    if (!stats || !(Field<UInt>(stats,0x1C)&0x80000000U) ||
        !(Field<UInt>(stats,0xACC)&0x400000U)) return nullptr;
    Field<UInt>(stats,0xACC)&=~0x400000U;
    return stats;
}
// Original path validation and the native result are preserved. Optional
// diagnostic bits: 0x10000 mask applied, 0x20000 restored, 0x40000 pointer changed.
static int Invoke(Byte* exe,Byte* unit,int id,NativeStart original,
                  bool d3StyleEnabled,UInt* diagnosticFlags=nullptr)
{
    auto stats=Begin(exe,unit,id,d3StyleEnabled);
    if (stats && diagnosticFlags) *diagnosticFlags|=0x10000;
    int result=original(unit,id);
    if (stats) {
        if (Field<Byte*>(unit,0x88)==stats) {
            Field<UInt>(stats,0xACC)|=0x400000U;
            if (diagnosticFlags) *diagnosticFlags|=0x20000;
        } else if (diagnosticFlags) *diagnosticFlags|=0x40000;
    }
    return result;
}
}
