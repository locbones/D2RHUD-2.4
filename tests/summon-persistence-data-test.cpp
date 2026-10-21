#include "../d2rhud/plugin/SummonPersistenceData.h"
#include <cassert>
#include <iostream>

using namespace SummonPersistence::Data;

int main() {
    Snapshot original{{"Summoner", 1700000000, 2, 32}, 0xdeadbeef, 1234,
        {{70, 25, 363, 4, 0xfedcba98}, {65535, MaxSkillLevel, 65535, 65535, 0}}};
    original.pets[0].components = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 254, 255};
    const auto encoded = Encode(original);
    const auto decoded = Decode(encoded);
    assert(decoded && SameIdentity(original.identity, decoded->identity));
    assert(decoded->checksum == original.checksum && decoded->saveSize == original.saveSize);
    assert(decoded->pets.size() == 2 && decoded->pets[0].skill == 70 && decoded->pets[0].level == 25);
    assert(decoded->pets[0].monster == 363 && decoded->pets[0].petType == 4 && decoded->pets[0].seed == 0xfedcba98);
    assert(decoded->pets[1].skill == 65535 && decoded->pets[1].level == MaxSkillLevel);
    assert(decoded->pets[0].components == original.pets[0].components);
    assert((decoded->pets[1].components == std::array<uint8_t, 16>{}));

    assert(!Decode(""));
    assert(!Decode("{}"));
    assert(!Decode(encoded.substr(0, encoded.size() - 1)));
    assert(!Decode(encoded + " trailing garbage"));
    assert(!Decode(std::string(MaxFileBytes + 1, ' ')));
    auto document = nlohmann::json::parse(encoded);
    auto reject = [&](auto modify) { auto invalid = document; modify(invalid); assert(!Decode(invalid.dump())); };
    reject([](auto& j) { j["version"] = 2; });
    reject([](auto& j) { j["version"] = 1.0; });
    reject([](auto& j) { j["checksum"] = -1; });
    reject([](auto& j) { j["checksum"] = uint64_t{UINT32_MAX} + 1; });
    reject([](auto& j) { j["saveSize"] = "1234"; });
    reject([](auto& j) { j["identity"]["name"] = ""; });
    reject([](auto& j) { j["identity"]["name"] = std::string("a\0b", 3); });
    reject([](auto& j) { j["identity"]["name"] = std::string(65, 'a'); });
    reject([](auto& j) { j["identity"].erase("created"); });
    reject([](auto& j) { j["pets"][0]["skill"] = -1; });
    reject([](auto& j) { j["pets"][0]["skill"] = 65536; });
    reject([](auto& j) { j["pets"][0]["level"] = 0; });
    reject([](auto& j) { j["pets"][0]["level"] = MaxSkillLevel + 1; });
    reject([](auto& j) { j["pets"][0]["monster"] = true; });
    reject([](auto& j) { j["pets"][0]["petType"] = 65536; });
    reject([](auto& j) { j["pets"][0]["seed"] = 0.5; });
    reject([](auto& j) { j["pets"][0].erase("seed"); });
    reject([](auto& j) { j["pets"][0].erase("components"); });
    reject([](auto& j) { j["pets"][0]["components"] = "invalid"; });
    reject([](auto& j) { j["pets"][0]["components"] = std::vector<uint8_t>(15); });
    reject([](auto& j) { j["pets"][0]["components"] = std::vector<uint8_t>(17); });
    reject([](auto& j) { j["pets"][0]["components"][0] = -1; });
    reject([](auto& j) { j["pets"][0]["components"][0] = 256; });
    reject([](auto& j) { j["pets"][0]["components"][0] = 1.0; });
    reject([](auto& j) { j["pets"][0]["components"][0] = false; });
    reject([](auto& j) { j["pets"][0]["components"][0] = nullptr; });
    reject([](auto& j) { const auto pet = j["pets"][0]; j["pets"] = nlohmann::json::array();
        for (size_t i = 0; i <= MaxPets; ++i) j["pets"].push_back(pet); });
    Snapshot invalid = original;
    invalid.pets[0].level = 0;
    bool threw = false;
    try { Encode(invalid); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);

    const auto key = Key(original.identity);
    assert(!key.empty() && key.find_first_not_of("0123456789abcdef-") == std::string::npos);
    Identity other = original.identity;
    for (unsigned field = 0; field < 4; ++field) {
        other = original.identity;
        if (field == 0) other.name += "a";
        if (field == 1) ++other.created;
        if (field == 2) ++other.classId;
        if (field == 3) ++other.flags;
        assert(Key(other) != key);
    }
    assert(Key({"a/b", 1, 2, 3}) != Key({"a\\b", 1, 2, 3}));
    assert(Key({"a", 0x12, 0x3, 4}) != Key({"a", 0x1, 0x23, 4}));
    assert(Key({"A", 1, 2, 3}) != Key({"a", 1, 2, 3}));
    assert(Key({}).empty());

    // Only manipulate files inside a unique test directory owned by this test.
    const auto directory = std::filesystem::temp_directory_path()
        / ("d2rhud-summon-roster-test-" + std::to_string(GetCurrentProcessId()) + "-"
            + std::to_string(GetTickCount64()));
    assert(std::filesystem::create_directory(directory));
    const auto path = directory / (key + ".json");
    assert(!Read(path, original.identity, original.checksum, original.saveSize));
    assert(AtomicWrite(path, original));
    assert(Read(path, original.identity, original.checksum, original.saveSize)->pets.size() == 2);
    assert(Read(path, original.identity, original.checksum, original.saveSize)->pets[0].components == original.pets[0].components);
    assert(!Read(path, other, original.checksum, original.saveSize));
    assert(!Read(path, original.identity, original.checksum + 1, original.saveSize));
    assert(!Read(path, original.identity, original.checksum, original.saveSize + 1));
    assert(!AtomicWrite(path, invalid));
    assert(Read(path, original.identity, original.checksum, original.saveSize)->pets.size() == 2);
    std::error_code storageError;
    assert(!AtomicWrite(directory / "missing" / "roster.json", original, &storageError));
    assert(storageError.value() == ERROR_PATH_NOT_FOUND);
    const auto locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    assert(!AtomicWrite(path, original, &storageError));
    assert(storageError.value() == ERROR_SHARING_VIOLATION || storageError.value() == ERROR_ACCESS_DENIED);
    CloseHandle(locked);
    assert(Read(path, original.identity, original.checksum, original.saveSize)->pets.size() == 2);
    assert(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}) == 1);
    assert(AtomicWrite(path, original, &storageError) && !storageError);

    // Saving after every minion has died must erase the old surviving army.
    auto empty = original;
    empty.pets.clear();
    assert(AtomicWrite(path, empty));
    assert(Read(path, original.identity, original.checksum, original.saveSize)->pets.empty());
    assert(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}) == 1);
    { std::ofstream corrupt(path, std::ios::binary | std::ios::trunc); corrupt << encoded.substr(0, 10); }
    assert(!Read(path, original.identity, original.checksum, original.saveSize));
    { std::ofstream oversized(path, std::ios::binary | std::ios::trunc); oversized << std::string(MaxFileBytes + 1, ' '); }
    assert(!Read(path, original.identity, original.checksum, original.saveSize));
    std::filesystem::remove(path);
    std::filesystem::remove(directory);
    std::cout << "Summon companion roster storage tests passed\n";
}
