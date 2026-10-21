#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
namespace LegacyTransmog {
bool Install();
void Uninstall();
const char* Status();
const char* StackSplitStatus();
const char* SocketStackStatus();
void SetQuantityRightClicks(bool enabled);
void SetMinimalistQuantityPopup(bool enabled);
// A snapshot crosses render/input/server threads; UI never owns game pointers.
struct SplitView {
    enum Phase {Closed,Waiting,Choosing,Submitting,Error} phase=Closed;
    uint32_t token=0,owner=0,item=0,total=0;
    uint64_t opened=0;
    std::string name,message;
    float anchorX=0.5f,anchorY=0.5f; // original item click, normalized client coordinates
    std::string iconCode; // copied item code, never a live unit pointer
};
SplitView GetSplitView();
void SubmitSplit(uint32_t token,int amount);
void CloseSplit(uint32_t token);
bool SplitWindowMessage(HWND window,UINT message,WPARAM key,LPARAM data,bool* outsideClick=nullptr);
bool SplitCapturesInput(UINT message,LPARAM data);
void SetSplitInputRegion(uint32_t token,RECT bounds,int displayWidth,int displayHeight,bool editing);
void DrawStackSplit();
}
