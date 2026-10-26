#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

// A reader belongs to the loading worker, never the render/game thread.
class GameAssets {
public:
    GameAssets(const std::filesystem::path& install, const std::string& mod);
    ~GameAssets();
    GameAssets(const GameAssets&) = delete;
    GameAssets& operator=(const GameAssets&) = delete;
    std::vector<uint8_t> Read(const std::string& relative);
    struct TableFiles { std::vector<uint8_t> text, binary; std::filesystem::path layer; };
    // Choose a data layer before choosing TXT/BIN, so a compiled mod table
    // wins over retail source text. Optional companion BIN stays in that layer.
    TableFiles ReadTable(const std::string& name, bool companionBinary = false);
    // BIN drops source symbols/comments and custom non-native columns. The
    // exporter preserves those in a checksum-bound, same-layer JSON companion.
    // Compiled consumers must not take symbols from unrelated/stale TXT when
    // a BIN exists. Symbols require matching companion metadata. Cube source
    // annotations are independent of the BIN checksum; recipe consumers check
    // the native fields they actually use instead of the whole table.
    std::vector<std::string> ReadTableNames(const std::string& name, bool preferBinary = false);
    std::vector<std::string> ReadTableLabels(const std::string& name);
    std::string ReadTableSource(const std::string& name, bool preferBinary = false);
    std::string ReadTableOverlay(const std::string& name);
    std::string ReadAuxiliaryTable(const std::string& name);
private:
    struct MetadataCache;
    std::shared_ptr<MetadataCache> metadataCache_;
    std::string MetadataField(const std::string& name, const TableFiles& files, const char* field);
    std::filesystem::path install_, mod_;
    void* storage_ = nullptr;
    bool triedStorage_ = false;
};
