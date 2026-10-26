#pragma once
#include "QuestJournal.h"

namespace QuestJournal::ClientUI {
// UI acknowledgements stay local; opening a journal never claims a reward.
struct Notice {
    uint32_t recipient=0,session=0;
    int completed=0;
    bool ready=false,pending=false,known=false;
    bool Changed(const View& view)const{return known&&(!view.connected||recipient!=view.recipient||session!=view.session);}
    void Observe(const View& view,bool open){
        if(!view.connected||view.blocked||view.legacyAvailable){*this={};return;}
        if(!known||Changed(view))pending=view.objectiveReady;
        else if(view.completed>completed||(view.objectiveReady&&(!ready||view.completed!=completed)))pending=true;
        recipient=view.recipient;session=view.session;completed=view.completed;ready=view.objectiveReady;known=true;
        if(open)pending=false;
    }
};
bool Install();
void Uninstall();
const char* Status();
// Called on the render thread before menu input/capture is calculated.
void Update(bool& open,bool playing);
void RequestOpen();
void RequestToggle();
// WndProc supplies repeat information; a configured journal chord is consumed
// so assigning the game's Quest Log key cannot open/toggle both interfaces.
void BeginInput(bool repeated);
void Hotkey();
bool ConsumeInput();
}
