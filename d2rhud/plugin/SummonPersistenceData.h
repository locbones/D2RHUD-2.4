#pragma once

#include "../d2/json.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#endif

// This companion format never writes to, or extends, a native character save.
namespace SummonPersistence::Data {
    inline constexpr size_t MaxPets = 1024;
    inline constexpr size_t MaxFileBytes = 256 * 1024;
    inline constexpr int32_t MaxSkillLevel = 1024;

    struct Identity {
        std::string name;
        uint32_t created = 0, classId = 0, flags = 0;
    };
    struct Record {
        int32_t skill = 0, level = 0, monster = 0, petType = 0;
        uint32_t seed = 0;
        std::array<uint8_t, 16> components{};
    };
    struct Snapshot {
        Identity identity;
        uint32_t checksum = 0, saveSize = 0;
        std::vector<Record> pets;
    };

    inline bool SameIdentity(const Identity& a, const Identity& b) noexcept {
        return a.name == b.name && a.created == b.created && a.classId == b.classId && a.flags == b.flags;
    }
    inline bool ValidIdentity(const Identity& value) noexcept {
        return !value.name.empty() && value.name.size() <= 64 && value.name.find('\0') == std::string::npos;
    }
    inline bool ValidRecord(const Record& value) noexcept {
        return value.skill >= 0 && value.skill <= 65535 && value.level >= 1 && value.level <= MaxSkillLevel
            && value.monster >= 0 && value.monster <= 65535 && value.petType >= 0 && value.petType <= 65535;
    }

    // Filename stem, with no path separators or lossy name normalization.
    inline std::string Key(const Identity& identity) {
        if (!ValidIdentity(identity)) return {};
        constexpr char hex[] = "0123456789abcdef";
        std::string key;
        key.reserve(identity.name.size() * 2 + 27);
        for (unsigned char value : identity.name) {
            key += hex[value >> 4];
            key += hex[value & 15];
        }
        for (uint32_t value : {identity.created, identity.classId, identity.flags}) {
            key += '-';
            for (int shift = 28; shift >= 0; shift -= 4) key += hex[(value >> shift) & 15];
        }
        return key;
    }

    inline std::string Encode(const Snapshot& snapshot) {
        if (!ValidIdentity(snapshot.identity) || snapshot.pets.size() > MaxPets)
            throw std::invalid_argument("Invalid summon roster");
        nlohmann::json pets = nlohmann::json::array();
        for (const auto& pet : snapshot.pets) {
            if (!ValidRecord(pet)) throw std::invalid_argument("Invalid summon record");
            pets.push_back({{"skill", pet.skill}, {"level", pet.level}, {"monster", pet.monster},
                {"petType", pet.petType}, {"seed", pet.seed}, {"components", pet.components}});
        }
        nlohmann::json document = {{"version", 1},
            {"identity", {{"name", snapshot.identity.name}, {"created", snapshot.identity.created},
                {"classId", snapshot.identity.classId}, {"flags", snapshot.identity.flags}}},
            {"checksum", snapshot.checksum}, {"saveSize", snapshot.saveSize}, {"pets", std::move(pets)}};
        auto encoded = document.dump();
        if (encoded.size() > MaxFileBytes) throw std::invalid_argument("Summon roster exceeds size limit");
        return encoded;
    }

    inline bool UnsignedValue(const nlohmann::json& input, uint32_t maximum, uint32_t& result) {
        if (!input.is_number_integer()) return false;
        if (input.is_number_unsigned()) {
            const auto value = input.get<uint64_t>();
            if (value > maximum) return false;
            result = static_cast<uint32_t>(value);
        } else {
            const auto value = input.get<int64_t>();
            if (value < 0 || static_cast<uint64_t>(value) > maximum) return false;
            result = static_cast<uint32_t>(value);
        }
        return true;
    }
    inline bool UnsignedInteger(const nlohmann::json& object, const char* key, uint32_t maximum, uint32_t& result) {
        const auto it = object.find(key);
        return it != object.end() && UnsignedValue(*it, maximum, result);
    }

    inline std::optional<Snapshot> Decode(std::string_view encoded) noexcept {
        if (encoded.empty() || encoded.size() > MaxFileBytes) return std::nullopt;
        try {
            const auto document = nlohmann::json::parse(encoded.begin(), encoded.end());
            uint32_t version = 0;
            if (!document.is_object() || document.size() != 5
                || !UnsignedInteger(document, "version", 1, version) || version != 1) return std::nullopt;
            const auto& identity = document.at("identity");
            Snapshot snapshot;
            if (!identity.is_object() || identity.size() != 4 || !identity.at("name").is_string()) return std::nullopt;
            snapshot.identity.name = identity.at("name").get<std::string>();
            if (!ValidIdentity(snapshot.identity)
                || !UnsignedInteger(identity, "created", UINT32_MAX, snapshot.identity.created)
                || !UnsignedInteger(identity, "classId", UINT32_MAX, snapshot.identity.classId)
                || !UnsignedInteger(identity, "flags", UINT32_MAX, snapshot.identity.flags)
                || !UnsignedInteger(document, "checksum", UINT32_MAX, snapshot.checksum)
                || !UnsignedInteger(document, "saveSize", UINT32_MAX, snapshot.saveSize)) return std::nullopt;
            const auto& pets = document.at("pets");
            if (!pets.is_array() || pets.size() > MaxPets) return std::nullopt;
            snapshot.pets.reserve(pets.size());
            for (const auto& pet : pets) {
                uint32_t skill, level, monster, petType, seed;
                if (!pet.is_object() || pet.size() != 6
                    || !UnsignedInteger(pet, "skill", 65535, skill)
                    || !UnsignedInteger(pet, "level", MaxSkillLevel, level) || level == 0
                    || !UnsignedInteger(pet, "monster", 65535, monster)
                    || !UnsignedInteger(pet, "petType", 65535, petType)
                    || !UnsignedInteger(pet, "seed", UINT32_MAX, seed)) return std::nullopt;
                Record record{static_cast<int32_t>(skill), static_cast<int32_t>(level),
                    static_cast<int32_t>(monster), static_cast<int32_t>(petType), seed};
                const auto& components = pet.at("components");
                if (!components.is_array() || components.size() != record.components.size()) return std::nullopt;
                for (size_t i = 0; i < record.components.size(); ++i) {
                    uint32_t component;
                    if (!UnsignedValue(components[i], UINT8_MAX, component)) return std::nullopt;
                    record.components[i] = static_cast<uint8_t>(component);
                }
                snapshot.pets.push_back(record);
            }
            return snapshot;
        } catch (...) { return std::nullopt; }
    }

    // The checksum and size bind a roster to the actual saved character state.
    // A backup/replaced character must never receive a roster from another save.
    inline std::optional<Snapshot> Read(const std::filesystem::path& path, const Identity& expected,
        uint32_t checksum, uint32_t saveSize) noexcept {
        try {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) return std::nullopt;
            const auto size = input.tellg();
            if (size <= 0 || size > static_cast<std::streamoff>(MaxFileBytes)) return std::nullopt;
            std::string encoded(static_cast<size_t>(size), '\0');
            input.seekg(0);
            if (!input.read(encoded.data(), static_cast<std::streamsize>(encoded.size()))) return std::nullopt;
            // Reject a concurrent append as well as oversized/truncated documents.
            if (input.peek() != std::char_traits<char>::eof()) return std::nullopt;
            auto snapshot = Decode(encoded);
            if (!snapshot || !SameIdentity(snapshot->identity, expected)
                || snapshot->checksum != checksum || snapshot->saveSize != saveSize) return std::nullopt;
            return snapshot;
        } catch (...) { return std::nullopt; }
    }

    // The caller supplies a path inside its companion directory. An empty roster
    // is a real snapshot and must replace old data to prevent resurrecting pets.
    inline bool AtomicWrite(const std::filesystem::path& path, const Snapshot& snapshot, std::error_code* error = nullptr) noexcept {
        if (error) error->clear();
#ifdef _WIN32
        HANDLE file = INVALID_HANDLE_VALUE;
        std::filesystem::path temporary;
        bool ownsTemporary = false;
        try {
            const auto encoded = Encode(snapshot);
            static std::atomic<uint64_t> sequence{0};
            for (unsigned attempt = 0; attempt < 32; ++attempt) {
                temporary = path;
                temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence);
                file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file != INVALID_HANDLE_VALUE) { ownsTemporary = true; break; }
                const auto failure = GetLastError();
                if (failure != ERROR_FILE_EXISTS && failure != ERROR_ALREADY_EXISTS) {
                    if (error) *error = std::error_code(failure, std::system_category());
                    return false;
                }
            }
            if (file == INVALID_HANDLE_VALUE) {
                if (error) *error = std::error_code(ERROR_FILE_EXISTS, std::system_category());
                return false;
            }
            DWORD written = 0;
            DWORD failure = ERROR_SUCCESS;
            if (!WriteFile(file, encoded.data(), static_cast<DWORD>(encoded.size()), &written, nullptr)) failure = GetLastError();
            else if (written != encoded.size()) failure = ERROR_WRITE_FAULT;
            else if (!FlushFileBuffers(file)) failure = GetLastError();
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
            if (failure == ERROR_SUCCESS) {
                if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
                failure = GetLastError();
            }
            if (error) *error = std::error_code(failure, std::system_category());
            DeleteFileW(temporary.c_str());
        } catch (...) {
            if (error) *error = std::make_error_code(std::errc::invalid_argument);
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            if (ownsTemporary) DeleteFileW(temporary.c_str());
        }
#endif
        return false;
    }
}
