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
// Optional outer-load result tracking. Conversion is always performed by the
// leaf reader hook, independently of map/summon hooks or save descriptors.
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
