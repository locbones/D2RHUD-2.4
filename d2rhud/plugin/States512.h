#pragma once
namespace States512 {
bool Install(bool publishReady=true);
void FinishStartup(bool success);
void Uninstall();
const char* Status();
}
