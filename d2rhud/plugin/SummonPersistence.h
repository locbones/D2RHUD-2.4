#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
class D2GameStrc;
class D2ClientStrc;
class D2UnitStrc;

namespace SummonPersistence {
bool Install();
void Uninstall();
bool Enabled();
void SetEnabled(bool enabled);
const char* Status();
std::string StorageStatus();
// Authoritative lifecycle notifications from the existing server hook owner.
void BeforeLoad(D2GameStrc*, D2ClientStrc*);
void AfterLoad(D2GameStrc*, D2ClientStrc*, D2UnitStrc*, bool success);
void Saved(D2GameStrc*, D2UnitStrc*, const uint8_t*, size_t);
void Tick(D2GameStrc*);
void Removed(D2GameStrc*, D2UnitStrc*);
void Destroyed(D2GameStrc*);
}
