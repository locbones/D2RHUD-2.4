#pragma once
#include <Windows.h>
#include <detours/detours.h>

namespace HudHooks {
struct Operations {
    decltype(&DetourTransactionBegin) begin=&DetourTransactionBegin;
    decltype(&DetourUpdateThread) update=&DetourUpdateThread;
    decltype(&DetourAttach) attach=&DetourAttach;
    decltype(&DetourTransactionCommit) commit=&DetourTransactionCommit;
    decltype(&DetourTransactionAbort) abort=&DetourTransactionAbort;
};
class Transaction {
public:
    explicit Transaction(Operations operations={}):operations_(operations){
        error_=operations_.begin();
        if(error_!=NO_ERROR)return;
        active_=true;error_=operations_.update(GetCurrentThread());
    }
    Transaction(const Transaction&)=delete;
    Transaction& operator=(const Transaction&)=delete;
    ~Transaction(){if(active_)operations_.abort();}
    template<class Function,class Detour>bool Attach(Function& function,Detour detour){
        return AttachRaw(reinterpret_cast<PVOID*>(&function),reinterpret_cast<PVOID>(detour));
    }
    bool AttachRaw(PVOID* function,PVOID detour){
        if(!active_ || error_!=NO_ERROR)return false;
        error_=operations_.attach(function,detour);
        return error_==NO_ERROR;
    }
    bool Commit(){
        if(!active_ || error_!=NO_ERROR)return false;
        error_=operations_.commit();active_=false;
        return error_==NO_ERROR;
    }
    LONG Error()const{return error_;}
private:
    Operations operations_;
    LONG error_=NO_ERROR;
    bool active_=false;
};
}
