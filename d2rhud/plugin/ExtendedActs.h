#pragma once
class D2GameStrc;
namespace ExtendedActs {
bool Install();
void Uninstall();
void Tick(D2GameStrc* game);
const char* Status();
void SetRequested(bool enabled);
bool Requested();
bool Active();
}
