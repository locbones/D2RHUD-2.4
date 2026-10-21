#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

// A reader belongs to the loading worker, never the render/game thread.
class GameAssets {
public:
    GameAssets(const std::filesystem::path& install, const std::string& mod);
    ~GameAssets();
    GameAssets(const GameAssets&) = delete;
    GameAssets& operator=(const GameAssets&) = delete;
    std::vector<uint8_t> Read(const std::string& relative);
private:
    std::filesystem::path install_, mod_;
    void* storage_ = nullptr;
    bool triedStorage_ = false;
};
