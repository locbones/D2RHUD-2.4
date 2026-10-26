#pragma once
#include "../StartupDiagnostics.h"
namespace States512 {
bool Install(bool publishReady=true);
void StartupStage(StartupDiagnostics::Stage stage);
void FinishStartup(bool success,uint32_t failedFeatures=0,const char* detail=nullptr);
void Uninstall();
const char* Status();
}
