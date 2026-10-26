#pragma once
#include <memory>
#include <string>

class D2ClientStrc;
class D2UnitStrc;
namespace CandyCornSave {
void Initialize(const char* mod);
void Uninstall();
std::string Status();
struct Notification {std::string title,message;bool error=false;};
bool TakeNotification(Notification& notice);
// Owns a converted copy for the synchronous leaf parser. The native outer
// loader retains ownership of its original save buffer, including its cleanup.
class LoadScope {
    struct State;
    std::unique_ptr<State> state;
public:
    explicit LoadScope(D2ClientStrc* client,D2UnitStrc** output);
    ~LoadScope();
    LoadScope(const LoadScope&)=delete;
    LoadScope& operator=(const LoadScope&)=delete;
    void Complete(bool success,int nativeResult=-1);
};
}
