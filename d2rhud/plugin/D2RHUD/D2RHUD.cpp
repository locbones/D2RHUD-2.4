#pragma region Includes

#include "D2RHUD.h"
#include <imgui.h>
#include "../../D2/D2Ptrs.h"
#include <sstream>
#include <windows.h>
#include <vector>
#include <queue>
#include <functional>
#include "../KeyMappings.h"
#include <fstream>
#include <string>
#include <detours/detours.h>
#include <iostream>
#include <tuple>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include "../../D3D12Hook.h"
#include "../ItemFilter/ItemFilter.h"
#include <regex>
#include <ctime>
#include "../../D2/json.hpp"
#include <random>
#include <unordered_set>
#include <mutex>

#pragma endregion

#pragma region Credits

/*
- Chat Detours/Structures by Killshot
- Base Implementation by DannyisGreat
- Monster Stats and Plugin Mods by Bonesy
- Special Thanks to those who have helped ^^
*/

#pragma endregion

#pragma region Global Static/Structs

std::string configFilePath = "config.json";
std::string filename = "../Launcher/D2RLAN_Config.txt";
std::string lootFile = "../D2R/lootfilter.lua";
std::string Version = "1.4.7";

using json = nlohmann::json;
static MonsterStatsDisplaySettings cachedSettings;
static D2Client* GetClientPtr();
static D2DataTablesStrc* sgptDataTables = reinterpret_cast<D2DataTablesStrc*>(Pattern::Address(0x1c9e980));
ItemFilter* itemFilter = new ItemFilter();

const uint32_t modNameOffset = 0x1BF084F;
static std::string GetModName() {
    uint64_t pModNameAddr = Pattern::Address(modNameOffset);
    if (pModNameAddr == 0) {
        return "";
    }

    const char* pModName = reinterpret_cast<const char*>(pModNameAddr);
    if (!pModName) {
        return "";
    }

    return std::string(pModName);
}
std::string modName = GetModName();

#pragma endregion

#pragma region *Currently Unused*

// UMod Offsets
constexpr uint32_t umod8a_Offsets[] = { 0x2FC7E4, 0xAF13F }; // Cold
constexpr uint32_t umod8b_Offsets[] = { 0x2FC8AD, 0xAF20B }; // Fire
constexpr uint32_t umod8c_Offsets[] = { 0x2FC967, 0xAF2CF }; // Light
constexpr uint32_t umod9_Offsets[] = { 0x2FC9EB, 0xAF35C }; // Fire
constexpr uint32_t umod18_Offsets[] = { 0x2FCA64, 0xAF3E9 }; // Cold
constexpr uint32_t umod17_Offsets[] = { 0x2FCADD, 0xAF479 }; // Lightning
constexpr uint32_t umod23_Offsets[] = { 0x2FCB56, 0xAF495 }; // Poison
constexpr uint32_t umod25_Offsets[] = { 0x2FCBCF, 0xAF522 }; // Magic
constexpr uint32_t umod27a_Offsets[] = { 0x2FCC56, 0xAF5B9 }; // Cold
constexpr uint32_t umod27b_Offsets[] = { 0x2FCD22, 0xAF698 }; // Fire
constexpr uint32_t umod27c_Offsets[] = { 0x2FCDDB, 0xAF75C }; // Light
constexpr uint32_t umod28_Offsets[] = { 0x2FCDEE, 0xAF778 }; // Physical

struct OriginalUMods {
    // Each element is a vector of original bytes for a single offsets-group.
    // Example: coldGroups[0] corresponds to umod8a_Offsets,
    //          coldGroups[1] -> umod18_Offsets, etc.
    std::vector<std::vector<uint8_t>> coldGroups;
    std::vector<std::vector<uint8_t>> fireGroups;
    std::vector<std::vector<uint8_t>> lightGroups;
    std::vector<std::vector<uint8_t>> poisonGroups;
    std::vector<std::vector<uint8_t>> damageGroups;
    std::vector<std::vector<uint8_t>> magicGroups;
};

OriginalUMods g_originalUModValues = {
    // coldGroups: each group displays original values for umodxx offsets
    {
        { 40, 40 },   // umod8a
        { 75, 75 },   // umod18
        { 20, 20 }    // umod27a
    },
    // fireGroups
    {
        { 40, 40 },   // umod8b
        { 75, 75 },   // umod9
        { 20, 20 }    // umod27b
    },
    // lightGroups
    {
        { 40, 40 },   // umod8c
        { 75, 75 },   // umod17
        { 20, 20 }    // umod27c
    },
    // poisonGroups
    {
        { 75, 75 }    // umod23
    },
    // damageGroups
    {
        { 50, 50 }    // umod28
    },
    // magicGroups
    {
        { 20, 20 }    // umod25
    }
};

void ApplyUModArray(const uint32_t* offsets, size_t count, uint32_t remainder, const std::vector<uint8_t>& groupOriginalValues, const std::string& statName)
{
    //LogSunder("ApplyUModArray count=" + std::to_string(count) + " remainder=" + std::to_string(remainder));

    for (size_t i = 0; i < count; ++i)
    {
        uint64_t addr = Pattern::Address(offsets[i]);
        if (!addr || addr < 0x10000)
            continue;

        uint8_t* pValue = reinterpret_cast<uint8_t*>(addr);
        uint8_t currentValue = *pValue;

        // Safely get original for this index (fallback to currentValue)
        uint8_t originalValue = (i < groupOriginalValues.size()) ? groupOriginalValues[i] : currentValue;

        // If memory doesn't match original, restore it first
        if (currentValue != originalValue)
        {
            //LogSunder(statName + " Restoring original UMod at 0x" + std::to_string(addr) + " index=" + std::to_string(i) + " current=" + std::to_string(currentValue) + " -> original=" + std::to_string(originalValue));

            DWORD oldProtect;
            if (VirtualProtect(pValue, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
            {
                *pValue = originalValue;
                VirtualProtect(pValue, 1, oldProtect, &oldProtect);
                currentValue = originalValue;
            }
            else
            {
                MessageBoxA(nullptr, "Failed to change memory protection (restore)!", "Error", MB_OK | MB_ICONERROR);
                continue;
            }
        }

        // Compute expected value from ORIGINAL minus remainder
        int expectedValue = static_cast<int>(originalValue) - static_cast<int>(remainder);
        if (expectedValue < 0) expectedValue = 0;

        if (static_cast<uint8_t>(expectedValue) == currentValue)
        {
            //LogSunder(statName + " UMod already at expected value at 0x" + std::to_string(addr) + " index=" + std::to_string(i) + " value=" + std::to_string(currentValue));
            continue;
        }

        //LogSunder(statName + " Applying remainder to UModAddr[" + std::to_string(i) + "] @0x" + std::to_string(addr) + " original=" + std::to_string(originalValue) + " old=" + std::to_string(currentValue) + " new=" + std::to_string(expectedValue));

        DWORD oldProtect;
        if (VirtualProtect(pValue, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            *pValue = static_cast<uint8_t>(expectedValue);
            VirtualProtect(pValue, 1, oldProtect, &oldProtect);
        }
        else
            MessageBoxA(nullptr, "Failed to change memory protection (apply)!", "Error", MB_OK | MB_ICONERROR);
    }
}

#pragma endregion

#pragma region D2I Parser

#include <stdexcept>

class BitReader {
public:
    BitReader(const std::vector<uint8_t>& buffer)
        : buf(buffer), bytePos(0), bitPos(0) {
    }

    uint32_t ReadBits(size_t bits) {
        if (bits > 32) throw std::runtime_error("Cannot read more than 32 bits at once");
        uint32_t result = 0;
        for (size_t i = 0; i < bits; ++i) {
            if (bytePos >= buf.size()) throw std::runtime_error("Buffer overflow");
            result <<= 1;
            result |= (buf[bytePos] >> (7 - bitPos)) & 1;
            bitPos++;
            if (bitPos == 8) { bitPos = 0; bytePos++; }
        }
        return result;
    }

    uint8_t ReadUInt8(size_t bits) { return static_cast<uint8_t>(ReadBits(bits)); }
    uint16_t ReadUInt16(size_t bits) { return static_cast<uint16_t>(ReadBits(bits)); }
    uint32_t ReadUInt32(size_t bits) { return ReadBits(bits); }
    bool ReadBit() { return ReadBits(1) != 0; }

    void SkipBits(size_t bits) { for (size_t i = 0; i < bits; ++i) ReadBit(); }
    void AlignToByte() { if (bitPos != 0) { bitPos = 0; bytePos++; } }

    size_t GetBytePos() const { return bytePos; }

private:
    const std::vector<uint8_t>& buf;
    size_t bytePos;
    uint8_t bitPos;
};

// -------------------- Item --------------------
struct EarAttributes {
    uint8_t clazz = 0;
    uint8_t level = 0;
    std::string name;
};

struct Item {
    // Existing fields
    uint32_t id = 0;
    uint8_t level = 0;
    uint8_t quality = 0;
    bool multiple_pictures = false;
    uint8_t picture_id = 0;
    bool class_specific = false;
    uint16_t auto_affix_id = 0;

    uint8_t low_quality_id = 0;
    uint8_t file_index = 0;
    uint16_t magic_prefix = 0;
    uint16_t magic_suffix = 0;
    uint16_t set_id = 0;
    uint16_t unique_id = 0;
    uint8_t rare_name_id = 0;
    uint8_t rare_name_id2 = 0;
    uint16_t magical_name_ids[6] = { 0 };

    // New fields from ReadSimpleBits
    bool identified = false;
    bool socketed = false;
    bool new_flag = false;  // 'new' is reserved in C++
    bool is_ear = false;
    bool starter_item = false;
    bool simple_item = false;
    bool ethereal = false;
    bool personalized = false;
    bool given_runeword = false;

    uint16_t version = 0;
    uint8_t location_id = 0;
    uint8_t equipped_id = 0;
    uint8_t position_x = 0;
    uint8_t position_y = 0;
    uint8_t alt_position_id = 0;

    std::string type;                // Item type
    uint8_t nr_of_items_in_sockets = 0;

    EarAttributes ear_attributes;    // Ear-specific data

    std::vector<uint8_t> unknown_bits; // Pre-ID unknown bits for debugging
};

struct HuffmanNode {
    char value = 0; // 0 = internal node
    HuffmanNode* left = nullptr;
    HuffmanNode* right = nullptr;
    ~HuffmanNode() { delete left; delete right; }
};

// Convert your JS array to a C++ tree
HuffmanNode* BuildHuffmanTree() {
    // Leaf nodes
    auto w = new HuffmanNode{ 'w' };
    auto u = new HuffmanNode{ 'u' };
    auto eight = new HuffmanNode{ '8' };
    auto y = new HuffmanNode{ 'y' };
    auto five = new HuffmanNode{ '5' };
    auto j = new HuffmanNode{ 'j' };
    auto h = new HuffmanNode{ 'h' };
    auto s = new HuffmanNode{ 's' };
    auto two = new HuffmanNode{ '2' };
    auto n = new HuffmanNode{ 'n' };
    auto x = new HuffmanNode{ 'x' };
    auto c = new HuffmanNode{ 'c' };
    auto k = new HuffmanNode{ 'k' };
    auto f = new HuffmanNode{ 'f' };
    auto b = new HuffmanNode{ 'b' };
    auto t = new HuffmanNode{ 't' };
    auto m = new HuffmanNode{ 'm' };
    auto nine = new HuffmanNode{ '9' };
    auto seven = new HuffmanNode{ '7' };
    auto space = new HuffmanNode{ ' ' };
    auto e = new HuffmanNode{ 'e' };
    auto d = new HuffmanNode{ 'd' };
    auto p = new HuffmanNode{ 'p' };
    auto g = new HuffmanNode{ 'g' };
    auto z = new HuffmanNode{ 'z' };
    auto q = new HuffmanNode{ 'q' };
    auto three = new HuffmanNode{ '3' };
    auto v = new HuffmanNode{ 'v' };
    auto r = new HuffmanNode{ 'r' };
    auto l = new HuffmanNode{ 'l' };
    auto a = new HuffmanNode{ 'a' };
    auto one = new HuffmanNode{ '1' };
    auto four = new HuffmanNode{ '4' };
    auto zero = new HuffmanNode{ '0' };
    auto i = new HuffmanNode{ 'i' };
    auto o = new HuffmanNode{ 'o' };

    // Internal nodes
    auto j_node = new HuffmanNode{ 0, j, nullptr };
    auto five_node = new HuffmanNode{ 0, five, j_node };
    auto y_node = new HuffmanNode{ 0, y, five_node };
    auto eight_node = new HuffmanNode{ 0, eight, y_node };
    auto h_node = new HuffmanNode{ 0, h, nullptr };
    auto left1 = new HuffmanNode{ 0, w, u };
    auto left2 = new HuffmanNode{ 0, eight_node, h_node };
    auto left3 = new HuffmanNode{ 0, left1, left2 };

    auto two_node = new HuffmanNode{ 0, two, n };
    auto right1 = new HuffmanNode{ 0, two_node, x };
    auto right2 = new HuffmanNode{ 0, c, new HuffmanNode{ 0, k, f } };
    auto right3 = new HuffmanNode{ 0, t, m };
    auto right4 = new HuffmanNode{ 0, nine, seven };
    auto right5 = new HuffmanNode{ 0, right3, right4 };
    auto right6 = new HuffmanNode{ 0, right2, right5 };
    auto left_root = new HuffmanNode{ 0, left3, right6 };

    auto e_node = new HuffmanNode{ 0, e, d };
    auto p_node = new HuffmanNode{ 0, e_node, p };
    auto z_node = new HuffmanNode{ 0, z, q };
    auto three_node = new HuffmanNode{ 0, z_node, three };
    auto six_node = new HuffmanNode{ 0, v, nullptr };
    auto g_node = new HuffmanNode{ 0, g, six_node };
    auto right_root_left = new HuffmanNode{ 0, p_node, g_node };

    auto r_node = new HuffmanNode{ 0, r, l };
    auto one_node = new HuffmanNode{ 0, one, four };
    auto io_node = new HuffmanNode{ 0, i, o };
    auto a_node = new HuffmanNode{ 0, a, new HuffmanNode{ 0, one_node, io_node } };
    auto right_root_right = new HuffmanNode{ 0, r_node, a_node };

    auto right_root = new HuffmanNode{ 0, right_root_left, right_root_right };

    auto root = new HuffmanNode{ 0, left_root, right_root };

    return root;
}

// Decode 1 character from BitReader using Huffman tree
char DecodeHuffmanChar(BitReader& reader, HuffmanNode* root) {
    HuffmanNode* node = root;
    while (node->value == 0) {
        bool bit = reader.ReadBit();
        node = bit ? node->right : node->left;
        if (!node) throw std::runtime_error("Invalid Huffman tree traversal");
    }
    return node->value;
}

std::string DecodeHuffmanString(BitReader& reader, HuffmanNode* root, int len) {
    std::string s;
    for (int i = 0; i < len; ++i) s += DecodeHuffmanChar(reader, root);
    return s;
}

void ReadSimpleBits(Item& item, BitReader& reader, uint32_t version, HuffmanNode* huffmanRoot) {
    item.unknown_bits.reserve(32);

    // Flags
    for (int i = 0; i < 4; i++) item.unknown_bits.push_back(reader.ReadBit() ? 1 : 0);
    item.identified = reader.ReadBit();
    for (int i = 0; i < 6; i++) item.unknown_bits.push_back(reader.ReadBit() ? 1 : 0);
    item.socketed = reader.ReadBit();
    item.unknown_bits.push_back(reader.ReadBit() ? 1 : 0);
    item.new_flag = reader.ReadBit();
    for (int i = 0; i < 2; i++) item.unknown_bits.push_back(reader.ReadBit() ? 1 : 0);
    item.is_ear = reader.ReadBit();
    item.starter_item = reader.ReadBit();
    for (int i = 0; i < 3; i++) item.unknown_bits.push_back(reader.ReadBit() ? 1 : 0);
    item.simple_item = reader.ReadBit();
    item.ethereal = reader.ReadBit();
    item.unknown_bits.push_back(reader.ReadBit() ? 1 : 0);
    item.personalized = reader.ReadBit();
    item.unknown_bits.push_back(reader.ReadBit() ? 1 : 0);
    item.given_runeword = reader.ReadBit();
    for (int i = 0; i < 5; i++) item.unknown_bits.push_back(reader.ReadBit() ? 1 : 0);

    // Version
    item.version = (version <= 0x60) ? reader.ReadUInt16(10) : reader.ReadUInt16(3);

    // Location / position
    item.location_id = reader.ReadUInt8(3);
    item.equipped_id = reader.ReadUInt8(4);
    item.position_x = reader.ReadUInt8(4);
    item.position_y = reader.ReadUInt8(4);
    item.alt_position_id = reader.ReadUInt8(3);

    // Ear or type
    if (item.is_ear) {
        item.ear_attributes.clazz = reader.ReadUInt8(3);
        item.ear_attributes.level = reader.ReadUInt8(7);
        item.ear_attributes.name.clear();
        for (int i = 0; i < 15; i++) {
            uint8_t ch = reader.ReadUInt8(7);
            if (ch == 0) break;
            item.ear_attributes.name += static_cast<char>(ch);
        }
    }
    else {
        if (version <= 0x60) {
            for (int i = 0; i < 4; i++) item.type += static_cast<char>(reader.ReadUInt8(8));
        }
        else {
            item.type = DecodeHuffmanString(reader, huffmanRoot, 4);
        }
        item.type.erase(std::remove(item.type.begin(), item.type.end(), '\0'), item.type.end());
        uint8_t bits = item.simple_item ? 1 : 3;
        item.nr_of_items_in_sockets = reader.ReadUInt8(bits);
    }
}

// -------------------- ParseItem --------------------
Item ParseItem(BitReader& reader, HuffmanNode* huffmanRoot, uint32_t version) {
    Item item;

    ReadSimpleBits(item, reader, version, huffmanRoot);

    // Parse remaining fields based on quality
    item.id = reader.ReadUInt32(32);
    item.level = reader.ReadUInt8(7);
    item.quality = reader.ReadUInt8(4);

    item.multiple_pictures = reader.ReadBit();
    if (item.multiple_pictures) item.picture_id = reader.ReadUInt8(3);

    item.class_specific = reader.ReadBit();
    if (item.class_specific) item.auto_affix_id = reader.ReadUInt16(11);

    switch (item.quality) {
    case 0: item.low_quality_id = reader.ReadUInt8(3); break;
    case 2: item.file_index = reader.ReadUInt8(3); break;
    case 3:
        item.magic_prefix = reader.ReadUInt16(11);
        item.magic_suffix = reader.ReadUInt16(11);
        break;
    case 4:
        item.rare_name_id = reader.ReadUInt8(8);
        item.rare_name_id2 = reader.ReadUInt8(8);
        for (int i = 0; i < 6; i++)
            if (reader.ReadBit()) item.magical_name_ids[i] = reader.ReadUInt16(11);
        break;
    case 5: item.set_id = reader.ReadUInt16(12); break;
    case 6: item.unique_id = reader.ReadUInt16(12); break;
    default: break;
    }

    return item;
}

// -------------------- Shared Stash Parser --------------------
void ParseSharedStash(const std::string& filePath) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f) { std::cerr << "Failed to open file: " << filePath << std::endl; return; }

    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());

    size_t offset = 0;
    size_t tabIndex = 1;

    while (offset + 8 <= buf.size()) {
        if (buf[offset] == 0x55 && buf[offset + 1] == 0xAA &&
            buf[offset + 2] == 0x55 && buf[offset + 3] == 0xAA &&
            buf[offset + 4] == 0x01 && buf[offset + 5] == 0x00 &&
            buf[offset + 6] == 0x00 && buf[offset + 7] == 0x00) {

            std::cout << "Found tab " << tabIndex << " at offset " << offset << std::endl;

            size_t versionOffset = offset + 8;
            std::cout << "  Version bytes at offset " << versionOffset << ": ";
            for (int i = 0; i < 4; ++i) std::cout << std::hex << (int)buf[versionOffset + i] << " ";
            std::cout << std::dec << std::endl;

            // Tab item count (1 byte)
            uint8_t numItems = buf[versionOffset + 4];
            std::cout << "  Tab item count at offset " << (versionOffset + 4) << ": " << (int)numItems << std::endl;

            // Skip unknown 32 bytes
            size_t itemDataStart = versionOffset + 4 + 32;

            BitReader reader(buf);
            reader.SkipBits(itemDataStart * 8); // Move to start of item data
            reader.AlignToByte();

            for (int i = 0; i < numItems; ++i) {
                size_t itemOffset = reader.GetBytePos();

                try {
                    // Assume version is first byte at versionOffset
                    uint32_t version = buf[versionOffset];

                    Item item = ParseItem(reader, BuildHuffmanTree(), version);

                    std::cout << "    Item at offset " << itemOffset
                        << " | Quality: " << (int)item.quality
                        << " | Level: " << (int)item.level
                        << " | ID: " << item.id;

                    if (item.set_id != 0)
                        std::cout << " | Set ID: " << item.set_id;
                    if (item.unique_id != 0)
                        std::cout << " | Unique ID: " << item.unique_id;

                    std::cout << std::endl;
                }
                catch (std::exception& e) {
                    std::cout << "    Failed to parse item at offset " << itemOffset
                        << ": " << e.what() << std::endl;

                    break; // stop parsing this tab
                }
            }


            tabIndex++;
            offset += 8;
        }
        else {
            offset++;
        }
    }
}

#pragma endregion

#pragma region Monster & Command Constants
const char* ResistanceNames[6] = { "   ", "   ", "   ", "   ", "   ", "   " };
constexpr  uint32_t ResistanceStats[6] = { 39, 41, 43, 45, 36, 37 };
constexpr  uint32_t Alignment = { 172 };
constexpr  uint32_t ImmunityCold = { 187 };
constexpr  uint32_t ImmunityFire = { 189 };
constexpr  uint32_t ImmunityLight = { 190 };
constexpr  uint32_t ImmunityPoison = { 191 };
constexpr  uint32_t ImmunityPhysical = { 192 };
constexpr  uint32_t ImmunityMagic = { 193 };
constexpr  ImU32 ResistanceColors[6] = { IM_COL32(170,50,50,255) ,IM_COL32(170,170,50,255) ,IM_COL32(50,90,170,255) ,IM_COL32(50,170,50,255),IM_COL32(255,255,255,255), IM_COL32(255,175,0,255) };
constexpr  const char* Seperator = "  ";
constexpr uint32_t Experience = { 21 };
std::string automaticCommand1;
std::string automaticCommand2;
std::string automaticCommand3;
std::string automaticCommand4;
std::string automaticCommand5;
std::string automaticCommand6;
std::time_t lastLogTimestamp = 0;
#pragma endregion

#pragma region Chat/Debug Structures
struct DebugCheatEntry
{ // cheats array at 1418026F0
    char name[32];
    bool(__fastcall* handler)(uint64_t pGame, uint64_t pPlayer, const char* arg);
    uint32_t alwaysEnabled;
    uint32_t hasArguments;
};

struct CMD_PACKET_BASE
{
    uint8_t opcode;
};

struct SCMD_CHATSTART_PACKET : CMD_PACKET_BASE
{
    uint8_t msgType;
    uint8_t langCode;
    uint8_t unitType;
    uint32_t id;
    uint8_t color;
    uint8_t subType;
    char sender[61];
    char message[256];
};

struct CCMD_DEBUGCHEAT_PACKET : CMD_PACKET_BASE
{
    uint8_t unk1;
    uint8_t unk2;
    char cheat[256];
};

const uint32_t mainMenuClickHandlerOffset = 0xBC88C0;
const uint32_t gameClientsOffset = 0x1D637F0;
const uint32_t sendPacketToServerOffset = 0x010EBF0;
const uint32_t executeDebugCheatOffset = 0x076BC0;
const uint32_t unitDataOffset = 0x1D442E0;
const uint32_t getUnitPtrFuncOffset = 0x066510;
const uint32_t gameChatMsgTypeOffset = 0x184ABE8;
const uint32_t CCMD_DEBUGCHEAT_HandlerOffset = 0x02AD6F0;
const uint32_t Process_SCMD_CHATSTARTOffset = 0x0107CA0;
const uint32_t GetChatManagerOffset = 0x056E440;
const uint32_t ChatManager_PushChatEntryOffset = 0x056E9A0;
const uint32_t GetUnitNameOffset = 0x027A920;
const uint32_t BroadcastChatMessageOffset = 0x02ADA90;
const uint32_t Send_SCMD_CHATSTARTOffset = 0x02CD280;
const uint32_t cheatsArrayOffset = 0x18026F0;
const uint8_t CCMD_DEBUGCHEAT = 0x15;
const uint8_t SCMD_CHATSTART = 0x26;
const uint64_t SaveAndExitButtonHash = 0x621D53C05FCA5A67;
const uint32_t detectGameStatusOffset = 0x1DC76F8; //unknown, just convenient



static D2Client* GetClientPtr()
{
    uint64_t pClients = Pattern::Address(gameClientsOffset);

    if (pClients != NULL)
    {
        D2Client* pGameClient = *(D2Client**)(pClients);
        return pGameClient;
    }

    return nullptr;
}

struct Widget
{
    uint64_t vtable;
    uint64_t hash;
};

struct QueuedAction
{
    const char* name;
    std::function<void()> action;
};

typedef bool(__fastcall* DebugCheatHandler)(uint64_t pGame, uint64_t pPlayer, const char* szArg);
typedef const char* (__fastcall* GetUnitNameFptr)(uint64_t pUnit, char* szName);
typedef bool(__fastcall* CCMD_HANDLER_Fptr)(uint64_t pGame, uint64_t pPlayer, CMD_PACKET_BASE* pPacket, uint32_t dwPacketLen);
typedef bool(__fastcall* Process_SCMD_CHATSTART_Fptr)(SCMD_CHATSTART_PACKET* pPacket);
typedef void(__fastcall* BroadcastChatMessageFptr)(uint64_t pGame, const char* szMsg, uint8_t color);
typedef void(__fastcall* GameMenuOnClickHandler)(uint64_t a1, Widget* pWidget);
typedef D2UnitStrc* (__fastcall* GetClientUnitByIdAndTypeNew)(uint64_t pTable, uint32_t id1, uint32_t id2, uint32_t dwType);
typedef void(__fastcall* SendPacketToServer)(void* pPacket);
typedef void(__fastcall* ExecuteDebugCheat)(const char* szCheat);
bool menuClickHookInstalled = false;
static GameMenuOnClickHandler mainMenuClickHandlerOrig = nullptr;
static CCMD_HANDLER_Fptr CCMD_DEBUGCHEAT_Handler_Orig = nullptr;
static Process_SCMD_CHATSTART_Fptr Process_SCMD_CHATSTART_Orig = nullptr;
static SendPacketToServer SendPacketFunc = reinterpret_cast<SendPacketToServer>(Pattern::Address(sendPacketToServerOffset));
static ExecuteDebugCheat ExecuteDebugCheatFunc = reinterpret_cast<ExecuteDebugCheat>(Pattern::Address(executeDebugCheatOffset));
static GetClientUnitByIdAndTypeNew GetClientUnitPtrFunc = reinterpret_cast<GetClientUnitByIdAndTypeNew>(Pattern::Address(getUnitPtrFuncOffset));
struct D2ChatManager;
static std::queue<QueuedAction> queuedActions;


struct handle_data
{
    DWORD process_id;
    HWND window_handle;
};

struct ChatMsg
{
    uint32_t type;
    blz_string str;
};

struct ChatOptionalStruct
{
    uint64_t value;
    bool hasValue;
};

typedef D2ChatManager* (__fastcall* GetChatManagerFptr)();
typedef D2ChatManager* (__fastcall* ChatManager_PushChatEntryFptr)(D2ChatManager* pMgr, blz_string& msg, uint32_t color, bool a4, ChatMsg& typeMsg, ChatOptionalStruct& a6, ChatOptionalStruct& a7, ChatOptionalStruct& a8, ChatOptionalStruct& a9);

static GetChatManagerFptr GetChatManager = reinterpret_cast<GetChatManagerFptr>(Pattern::Address(GetChatManagerOffset));
static ChatManager_PushChatEntryFptr ChatManager_PushChatEntry = reinterpret_cast<ChatManager_PushChatEntryFptr>(Pattern::Address(ChatManager_PushChatEntryOffset));

typedef void(__fastcall* D2GAME_UModInit_t)(D2UnitStrc* pUnit, int32_t nUMod, int32_t bUnique);
D2GAME_UModInit_t oD2GAME_UModInit = nullptr;

typedef void(__fastcall* D2GAME_SpawnChampUnique_t)(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, void* pRoomCoordList, D2UnitStrc* pUnit, int32_t bSpawnMinions, int32_t nMinGroup, int32_t nMaxGroup );
D2GAME_SpawnChampUnique_t oD2GAME_SpawnChampUnique_1402fddd0 = nullptr;

typedef void(__fastcall* D2GAME_SpawnMonsters_t)(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, void* pRoomCoordList, int32_t nX, int32_t nY, int32_t nUnitGUID, int32_t nClassId, void* a8);
D2GAME_SpawnMonsters_t oD2GAME_SpawnMonsters_140301b5f = nullptr;

typedef void(__fastcall* D2GAME_UMOD8Array_t)(D2UnitStrc* pUnit, int32_t nUMod, int32_t bUnique);
D2GAME_UMOD8Array_t oD2GAME_UMOD8Array_1402fc530 = nullptr;

typedef void* (__fastcall* DrawMonsterHPBar_t)(int param1, void* param2, void* param3);
static DrawMonsterHPBar_t DrawMonsterHPBar = reinterpret_cast<DrawMonsterHPBar_t>(Pattern::Address(0x830B0));

struct RemainderEntry
{
    int cold = 0;
    int fire = 0;
    int light = 0;
    int poison = 0;
    int damage = 0;
    int magic = 0;
};
static std::unordered_map<DWORD, RemainderEntry> g_resistRemainders;





#pragma endregion

#pragma region Bank Tabs
struct Message {
    uint64_t o1;
    uint64_t o2;
    uint64_t o3;
    uint64_t o4;
    uint64_t o5;
};

struct D2SaveSystemContainer {
    int64_t unk_0000;
    uint8_t* pData;
    uint64_t nSize;
    uint64_t nAllocated;
    small_string_opt<0x1F> tFileName;
    uint32_t unk_0058;
};
static_assert(offsetof(D2SaveSystemContainer, unk_0058) == 0x58);

#pragma pack(1)

// Both of these seem to be unused. Can use for our own uses.
struct CCMD_CUSTOM {
    uint8_t opcode;
    uint64_t magic;
    uint8_t opcode2;
};

struct SCMD_CUSTOM {
    uint8_t opcode;
    uint64_t magic;
    uint8_t opcode2;
};

// We might not use all these. Just documenting the steps mentally...
enum SharedStashPhase : uint8_t {
    PageChanged = 0,
    SaveStarted,
    SaveCompleted,
    OldSharedStashFreed,
    NewSharedStashLoadedAndAttachMessagesSent,
    NewSharedStashUnitsCreated
};

struct CCMD_STASH_PAGE_CHANGE : CCMD_CUSTOM {
    SharedStashPhase nPhase;
    uint32_t nPage;
};

struct SCMD_STASH_PAGE_CHANGE : SCMD_CUSTOM {
    SharedStashPhase nPhase;
    uint32_t nPage;
};
#pragma pack()

constexpr uint32_t bankPanelDraw = 0x18eb90;
constexpr uint32_t bankPanelMessage = 0x18ee50;
constexpr uint32_t saveSystemLoadFile = 0x6cba50;
constexpr uint32_t ccmdProcessClientSystemMessage = 0x2e1b60;
constexpr uint32_t nNumberOfTabs = 7;

// Easier to take over an unused opcode than to add to the end.
constexpr uint32_t CCMD_CUSTOM_OP_CODE = 0x3;  //CCMD_TRANSMUTE. variable length and can be large. we bake magic into it to let us know it is us.
constexpr uint32_t SCMD_CUSTOM_OP_CODE = 0x2B;  //SCMD_CORRECT_PATH. It's easier to hijack this packet as opposed to adding a new one.
constexpr uint32_t CCMD_SHARED_STASH_OP2 = 0x0;
constexpr uint32_t SCMD_SHARED_STASH_OP2 = 0x0;

struct D2BankPanelWidget;

typedef void(__fastcall* BankPanelDraw_t)(D2BankPanelWidget* pBankPanel);
static BankPanelDraw_t oBankPanelDraw = nullptr;

typedef D2SaveSystemContainer* (__fastcall* SaveSystemLoadFile_t)(int64_t* pContainer, const char* szFilename, char bFlag);
static SaveSystemLoadFile_t oSaveSystemLoadFile = nullptr;

typedef int64_t* (__fastcall* CCMD_ProcessClientGameMessage_t)(D2GameStrc* pGame, D2ClientStrc* pClient, uint8_t* pPacket, uint64_t nSize);
static CCMD_ProcessClientGameMessage_t oCCMD_ProcessClientGameMessage = nullptr; // D2GAME_PACKET_Handler_6FC89320 in D2MOO

typedef char(__fastcall* CCMD_ProcessClientSystemMessage_t)(uint8_t* pData, int64_t nSize);
static CCMD_ProcessClientSystemMessage_t oCCMD_ProcessClientSystemMessage = nullptr;

typedef void(__fastcall* SCMD_QueuePacket_t)(int64_t* pCMDManager, int32_t nClient, void** pPacketRange);
static SCMD_QueuePacket_t SCMD_QueuePacket = reinterpret_cast<SCMD_QueuePacket_t>(Pattern::Address(0x422ac0));

typedef void(__fastcall* CCMD_QueuePacket_t)(void* pPacket, int32_t nSize);
static CCMD_QueuePacket_t CCMD_QueuePacket = reinterpret_cast<CCMD_QueuePacket_t>(Pattern::Address(0x10dce0));

typedef void* (__fastcall* BankPanelOnMessage_t)(void* pWidget, Message& message);
static BankPanelOnMessage_t BankPanelMessage = reinterpret_cast<BankPanelOnMessage_t>(Pattern::Address(0x18ee50));

typedef void* (__fastcall* WidgetFindChild_t)(void* pWidget, const char* childName);
static WidgetFindChild_t WidgetFindChild = reinterpret_cast<WidgetFindChild_t>(Pattern::Address(0x576070));

typedef int64_t(__fastcall* D2GAME_PACKETCALLBACK_Rcv0x03_CCMD_RUNXY_t)(D2GameStrc* pGame, D2UnitStrc* pUnit, void* pPacket, int nPacketSize);
static D2GAME_PACKETCALLBACK_Rcv0x03_CCMD_RUNXY_t D2GAME_PACKETCALLBACK_Rcv0x20_CCMD_TRANSMUTE =
reinterpret_cast<D2GAME_PACKETCALLBACK_Rcv0x03_CCMD_RUNXY_t>(Pattern::Address(0x2ABE30));

typedef void(__fastcall* D2CLIENT_PACKETCALLBACK_Rcv0x2B_SCMD_CORRECT_PATH_t)(uint8_t* pPacket);
static D2CLIENT_PACKETCALLBACK_Rcv0x2B_SCMD_CORRECT_PATH_t D2CLIENT_PACKETCALLBACK_Rcv0x2B_SCMD_CORRECT_PATH =
reinterpret_cast<D2CLIENT_PACKETCALLBACK_Rcv0x2B_SCMD_CORRECT_PATH_t>(Pattern::Address(0xDA3B0));

static char* gpSharedStashString = reinterpret_cast<char*>(Pattern::Address(0x1577390));
static char* gpSharedStashHCString = reinterpret_cast<char*>(Pattern::Address(0x1577428));

static int64_t* gpSCMDManager = reinterpret_cast<int64_t*>(Pattern::Address(0x18682b0));
static int64_t* gpCCMDManager = reinterpret_cast<int64_t*>(Pattern::Address(0x1888310));

static D2CCMDStrc* gpCCMDHandlerTable = reinterpret_cast<D2CCMDStrc*>(Pattern::Address(0x14bfc50));
static D2SCMDStrc* gpSCMDHandlerTable = reinterpret_cast<D2SCMDStrc*>(Pattern::Address(0x1841b40));
static D2ClientStrc** gpClientList = reinterpret_cast<D2ClientStrc**>(Pattern::Address(0x1d637f0));
static D2Widget** gpPanelManager = reinterpret_cast<D2Widget**>(Pattern::Address(0x1d7c4e8));


static uint32_t gSelectedPage = 0;

const uint64_t CMD_MAGIC = 0xDEADBEEFDEADBEEF;
const std::string STASH_NAME = "Stash";
void __fastcall UpdateStashFileName(uint32_t nSelectedPage) {
    auto scString = std::format("{}_SC_Page{}\0", STASH_NAME, nSelectedPage + 1);
    auto hcString = std::format("{}_HC_Page{}\0", STASH_NAME, nSelectedPage + 1);
    DWORD oldProtect;
    // janky but does the job
    VirtualProtect(gpSharedStashString, 0x32, PAGE_READWRITE, &oldProtect);
    strcpy(gpSharedStashString, scString.c_str());
    VirtualProtect(gpSharedStashString, 0x32, oldProtect, &oldProtect);
    VirtualProtect(gpSharedStashHCString, 0x32, PAGE_READWRITE, &oldProtect);
    strcpy(gpSharedStashHCString, hcString.c_str());
    VirtualProtect(gpSharedStashHCString, 0x32, oldProtect, &oldProtect);

    /*
    static uint8_t* tcpipPatch = reinterpret_cast<uint8_t*>(Pattern::Address(0x749AC));
    VirtualProtect(tcpipPatch, 0x1, PAGE_READWRITE, &oldProtect);
    *tcpipPatch = 0xEB;
    VirtualProtect(tcpipPatch, 0x1, oldProtect, &oldProtect);
    */
}

const std::vector<uint8_t> emptyStashTab = std::vector<uint8_t>{ 0x55, 0xaa, 0x55, 0xaa, 0x01, 0x00, 0x00, 0x00, 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4a, 0x4d, 0x00, 0x00 };
void __fastcall CCMD_SendNewEmptyStashMessage() {
    byte pBuffer[0x47] = { 0x6c, 0x44, 0x1 };
    memcpy(pBuffer + 3, emptyStashTab.data(), emptyStashTab.size());
    CCMD_QueuePacket(&pBuffer, sizeof(pBuffer));
}

void __fastcall CCMD_SendSharedStashMessage(SharedStashPhase phase, uint32_t page) {
    // send 0x3. with our magic to let us know it's a custom packet
    // there seems to be validation on op code to size. need to make sure our packet is the same size of a 0x3 packet.
    CCMD_STASH_PAGE_CHANGE tStashPageChange = { CCMD_CUSTOM_OP_CODE, CMD_MAGIC, CCMD_SHARED_STASH_OP2, phase, page };
    byte pBuffer[0xD2] = { };
    memset(&pBuffer, 0, sizeof(pBuffer));
    memcpy(&pBuffer, &tStashPageChange, sizeof(tStashPageChange));
    CCMD_QueuePacket(&pBuffer, sizeof(pBuffer));
}

void __fastcall SCMD_SendSharedStashMessage(D2ClientStrc* pClient, SharedStashPhase phase, uint32_t page) {
    // send 0x9D w/ custom action. max size 257?
    SCMD_STASH_PAGE_CHANGE tStashPageChange = { SCMD_CUSTOM_OP_CODE, CMD_MAGIC, SCMD_SHARED_STASH_OP2, phase, page };
    byte pBuffer[0xCE] = { };
    memset(&pBuffer, 0, sizeof(pBuffer));
    memcpy(&pBuffer, &tStashPageChange, sizeof(tStashPageChange));
    void* packetRange[2] = {
        &pBuffer,
        (uint8_t*)&pBuffer + 0xCE
    };
    SCMD_QueuePacket(gpSCMDManager, pClient->dwClientId, packetRange);
}

void __fastcall SCMD_HandleSharedStashMessage(SCMD_STASH_PAGE_CHANGE* pData) {
    switch (pData->nPhase) {
    case SaveStarted:
        std::cout << "Client saved" << std::endl;
        CCMD_SendSharedStashMessage(SaveCompleted, pData->nPage);
        break;
    case OldSharedStashFreed:
        std::cout << "Sending new shared stash to server" << std::endl;
        UpdateStashFileName(pData->nPage);
        reinterpret_cast<void(__fastcall*)()>(Pattern::Address(0x10e080))(); // CLIENT_ReadSaveAndSend_D2CLTSYS_OPENCHAR()
        CCMD_SendSharedStashMessage(NewSharedStashLoadedAndAttachMessagesSent, pData->nPage);
        break;
    case NewSharedStashUnitsCreated:
        std::cout << "New shared stash units created" << std::endl;
        auto pBankPanel = WidgetFindChild(*gpPanelManager, "BankExpansionLayout");
        if (pBankPanel) {
            std::cout << "Selecting tab" << std::endl;
            int32_t nSelectedTab = 0;   //This doesnt really seem to matter...
            Message m = {
                "BankPanelMessage"_hash64,
                "SelectTab"_hash64,
                (uint64_t)&nSelectedTab
            };
            BankPanelMessage(pBankPanel, m);
        }
        break;
    }
}

int64_t __fastcall CCMD_HandleSharedStashMessage(D2GameStrc* pGame, D2UnitStrc* pUnit, CCMD_STASH_PAGE_CHANGE* pData) {
    auto pClient = pUnit->pPlayerData->pClient;
    switch (pData->nPhase) {
    case PageChanged:
        // First server packet
        // Think I could maybe mash this and the SUNIT_RemoveAllSharedStashes together...
        std::cout << "Saving all clients" << std::endl;
        reinterpret_cast<void(__fastcall*)(D2GameStrc*)>(Pattern::Address(0x28f510))(pClient->pGame);    // GAME_SaveClients
        //    SCMD_SendSharedStashMessage(pClient, SaveStarted, pData->nPage);
        //    break;
        //case SaveCompleted:
        std::cout << "Freeing current shared stash" << std::endl;
        reinterpret_cast<void(__fastcall*)(D2GameStrc*, D2UnitStrc*)>(Pattern::Address(0x28bf00))(pClient->pGame, pClient->pPlayer); // SUNIT_RemoveAllSharedStashes
        SCMD_SendSharedStashMessage(pClient, OldSharedStashFreed, pData->nPage);
        break;
    case NewSharedStashLoadedAndAttachMessagesSent:
        std::cout << "Loading new shared stash" << std::endl;
        std::cout << "# of saves attached: " << pClient->nSaveHeaderSize << std::endl;
        //pClient->dwClientState = 1; //CLIENTSTATE_GAME_INIT_SENT needed for D2CLTSYS_OPENCHAR to attach save to D2ClientStrc
        //pClient->dwFlags &= ~8;
        reinterpret_cast<void(__fastcall*)(D2GameStrc*, D2ClientStrc*, D2UnitStrc*)>(Pattern::Address(0x319b70))(pClient->pGame, pClient, pClient->pPlayer); // Parse shared stash save headers in D2ClientStrc
        reinterpret_cast<void(__fastcall*)(D2ClientStrc*)>(Pattern::Address(0x2a08a0))(pClient); // CLIENTS_FreeSaveHeader
        SCMD_SendSharedStashMessage(pClient, NewSharedStashUnitsCreated, pData->nPage);
        //pClient->dwFlags |= 8;
        //pClient->dwClientState = 4; //CLIENTSTATE_INGAME
        break;
    }
    return 1;
}

void __fastcall SCMDHANDLER_Custom(uint8_t* pPacket) {
    uint64_t magic = *(uint64_t*)(pPacket + 1);
    if (magic == CMD_MAGIC) {
        SCMD_CUSTOM* pData = (SCMD_CUSTOM*)pPacket;
        if (pData->opcode2 == SCMD_SHARED_STASH_OP2) {
            SCMD_HandleSharedStashMessage((SCMD_STASH_PAGE_CHANGE*)pPacket);
        }
    }
    else {
        D2CLIENT_PACKETCALLBACK_Rcv0x2B_SCMD_CORRECT_PATH(pPacket);
    }
}

int64_t __fastcall CCMDHANDLER_Custom(D2GameStrc* pGame, D2UnitStrc* pUnit, uint8_t* pPacket, int nPacketSize) {
    uint64_t magic = *(uint64_t*)(pPacket + 1);
    if (magic == CMD_MAGIC) {
        CCMD_CUSTOM* pData = (CCMD_CUSTOM*)pPacket;
        if (pData->opcode2 == CCMD_SHARED_STASH_OP2) {
            return CCMD_HandleSharedStashMessage(pGame, pUnit, (CCMD_STASH_PAGE_CHANGE*)pPacket);
        }
        return 1;
    }
    else {
        return D2GAME_PACKETCALLBACK_Rcv0x20_CCMD_TRANSMUTE(pGame, pUnit, pPacket, nPacketSize);
    }
}

char __fastcall CCMD_ProcessClientSystemMessageHook(uint8_t* pData, int64_t nSize) {
    auto result = oCCMD_ProcessClientSystemMessage(pData, nSize);;
    const int32_t nClientId = *(int32_t*)pData;
    uint8_t* pPacket = ((uint8_t*)pData + 4);
    if (*pPacket == 0x6C) { //D2CLTSYS_OPENCHAR
        auto pClient = gpClientList[nClientId];
        if (!pClient) {
            return result;
        }
        if (pClient->dwClientState == 0x4) {
            reinterpret_cast<bool(__fastcall*)(int32_t, uint8_t*, int64_t, char)>(Pattern::Address(0x2a06f0))(nClientId, pPacket + 3, pPacket[1], pPacket[2] != 0); //CLIENTS_AttachSaveFile
            return 1;
        }
    }
    return result;
}

void __fastcall GenerateSharedStash() {
    std::cout << "Generating new stash with " << nNumberOfTabs << " tabs" << std::endl;
    for (int i = 0; i < nNumberOfTabs; i++) {
        CCMD_SendNewEmptyStashMessage();
    }
}

void OnStashPageChanged(uint32_t nSelectedPage) {
    std::cout << "Sending CCMD_STASH_PAGE_CHANGE" << std::endl;
    gSelectedPage = nSelectedPage;
    CCMD_SendSharedStashMessage(PageChanged, nSelectedPage);
}

// Might be better than global vars
/*
void __fastcall HookedBankPanelMessage(D2BankPanelWidget* pBankPanel, int64_t* pMessages) {
    oBankPanelMessage(pBankPanel, pMessages);
    if (pMessages[0] == "DropdownListWidgetMessage"_hash64
        && pMessages[1] == "OptionSelected"_hash64) {
        auto pBankPages = WidgetFindChild(pBankPanel, "BankPages");
        if (pBankPages) {
            auto nSelectedPage = *(uint32_t*)((int64_t)pBankPages + 0x178C);
            OnStashPageChanged(nSelectedPage);
        }
    }
}
*/

void __fastcall HookedBankPanelDraw(D2BankPanelWidget* pBankPanel) {
    oBankPanelDraw(pBankPanel);
    //todo hook OnSelectionChange for widget?
    auto pBankPages = WidgetFindChild(pBankPanel, "BankPages");
    if (pBankPages) {
        auto nSelectedPage = *(uint32_t*)((int64_t)pBankPages + 0x178C);
        if (nSelectedPage != gSelectedPage) {
            OnStashPageChanged(nSelectedPage);
        }
    }
}

#pragma endregion

#pragma region Window/Detour Handlers
BOOL is_main_window(HWND handle)
{
    return GetWindow(handle, GW_OWNER) == (HWND)0;
}

BOOL CALLBACK enum_windows_callback(HWND handle, LPARAM lParam)
{
    handle_data* data = (handle_data*)lParam;
    unsigned long process_id = 0;
    GetWindowThreadProcessId(handle, &process_id);
    if (data->process_id != process_id || !is_main_window(handle))
        return TRUE;
    data->window_handle = handle;
    return FALSE;
}

HWND find_main_window(DWORD process_id)
{
    handle_data data;
    data.process_id = process_id;
    data.window_handle = 0;
    EnumWindows(enum_windows_callback, (LPARAM)&data);
    return data.window_handle;
}

VOID CALLBACK DelayedActionsTimerProc(
    HWND hwnd,
    UINT message,
    UINT_PTR idTimer,
    DWORD dwTime)
{
    if (idTimer != 1234)
        return;

    printf("delayed actions timer proc\n");

    if (!queuedActions.empty())
    {
        printf("%lld delayed actions in queue\n", queuedActions.size());
        auto action = queuedActions.front();
        printf("action %s will be called\n", action.name);
        action.action();
        queuedActions.pop();
        printf("action called\n");
    }
    else
    {
        printf("queue is empty, stopping timer\n");
        KillTimer(hwnd, idTimer);
    }
}
#pragma endregion

#pragma region Helper Functions
std::string TrimWhitespace(const std::string& str) {
    size_t first = str.find_first_not_of(" \n\r\t");
    size_t last = str.find_last_not_of(" \n\r\t");
    return str.substr(first, (last - first + 1));
}

std::string ReadCommandFromFile(const std::string& filename, const std::string& searchString) {
    if (!std::filesystem::exists(filename)) {
        return {};
    }
    return TrimWhitespace(readTextFollowingString(filename, searchString));
}

void ReadCommandWithValuesFromFile(const std::string& filename, const std::string& searchString, std::string& commandKey, std::string& commandValue) {
    std::string commandLine = ReadCommandFromFile(filename, searchString);

    size_t commaPos = commandLine.find(',');
    if (commaPos != std::string::npos) {
        commandKey = TrimWhitespace(commandLine.substr(0, commaPos));
        std::string valuePart = TrimWhitespace(commandLine.substr(commaPos + 1));
        size_t startQuote = valuePart.find('\"');
        size_t endQuote = valuePart.find('\"', startQuote + 1);

        if (startQuote != std::string::npos && endQuote != std::string::npos) {
            commandValue = TrimWhitespace(valuePart.substr(startQuote + 1, endQuote - startQuote - 1));
        }
        else {
            commandValue.clear();
        }
    }
    else {
        commandKey.clear();
        commandValue.clear();
    }
}

std::string Trim(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\n\r");
    size_t last = str.find_last_not_of(" \t\n\r");
    return (first == std::string::npos || last == std::string::npos) ? "" : str.substr(first, last - first + 1);
}

std::vector<std::string> ReadAutomaticCommandsFromFile(const std::string& filename)
{
    std::vector<std::string> automaticCommands;
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Error: Failed to open file " << filename << std::endl;
        return automaticCommands;
    }

    std::string line;

    while (std::getline(file, line)) {
        if (line.find("Startup Commands:") != std::string::npos) {
            size_t pos = line.find("Startup Commands:") + std::string("Startup Commands:").length();
            std::string commands = line.substr(pos);

            while (std::getline(file, line) && !line.empty()) {
                commands += line;
            }

            size_t commaPos = 0;
            while ((commaPos = commands.find(",")) != std::string::npos) {
                std::string command = Trim(commands.substr(0, commaPos));
                if (!command.empty()) {
                    automaticCommands.push_back(command);
                }
                commands.erase(0, commaPos + 1);
            }

            std::string command = Trim(commands);
            if (!command.empty()) {
                automaticCommands.push_back(command);
            }

            break;
        }
    }

    while (automaticCommands.size() < 6) {
        automaticCommands.push_back("");
    }

    if (automaticCommands.size() >= 6) {
        automaticCommand1 = automaticCommands[0];
        automaticCommand2 = automaticCommands[1];
        automaticCommand3 = automaticCommands[2];
        automaticCommand4 = automaticCommands[3];
        automaticCommand5 = automaticCommands[4];
        automaticCommand6 = automaticCommands[5];
    }
    else {
        std::cerr << "Error: Not enough automatic commands in the config file!" << std::endl;
    }

    return automaticCommands;
}

void WriteToDebugLog(const std::string& message) {
    const std::string logDirectory = "../Launcher/";
    if (!std::filesystem::exists(logDirectory)) {
        std::filesystem::create_directories(logDirectory);
    }
    const std::string logFilePath = logDirectory + "D2RHUD_Logs.txt";

    std::ofstream logFile(logFilePath, std::ios::app);
    if (logFile.is_open()) {
        std::time_t now = std::time(nullptr);
        std::tm* localTime = std::localtime(&now);
        logFile << "[" << std::put_time(localTime, "%a %b %d %T %Y") << "] " << message << std::endl;

        logFile.close();
    }
}
#pragma endregion

#pragma region Startup Options Control

std::string GetExecutableDir()
{
    char buffer[MAX_PATH];
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    std::filesystem::path exePath(buffer);
    return exePath.parent_path().string();
}

static int GetClientStatus() {
    uint64_t pClients = Pattern::Address(detectGameStatusOffset);
    return (pClients != NULL) ? static_cast<int>(*(uint8_t*)pClients) : -1;
}

void ExecuteCommand(const std::string& command) {
    if (command == "disabled" || command.empty()) return;

    bool isPlayerCommand = (command.find("/") != std::string::npos);
    if (isPlayerCommand) {
        CLIENT_playerCommand(command, command);
    }
    else {
        ExecuteDebugCheatFunc(command.c_str());
    }
}

int GetPlayerDifficulty(D2UnitStrc* pPlayer) {
    if (!pPlayer || !pPlayer->pDynamicPath) return -1;
    auto pRoom = pPlayer->pDynamicPath->pRoom;
    if (!pRoom || !pRoom->pDrlgRoom) return -1;
    auto pLevel = pRoom->pDrlgRoom->pLevel;
    if (!pLevel || !pLevel->pDrlg) return -1;
    return pLevel->pDrlg->nDifficulty;
}

void OnClientStatusChange() {
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::vector<std::string> commands = { automaticCommand1, automaticCommand2, automaticCommand3,
                                         automaticCommand4, automaticCommand5, automaticCommand6 };
    for (const auto& command : commands) {
        if (!command.empty() && command != "disabled") {
            ExecuteCommand(command);
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int CheckClientStatusChange() {
    static int previousValue = -1;
    int currentValue = GetClientStatus();

    if (previousValue != 1 && currentValue == 1) {
        std::thread(OnClientStatusChange).detach();
    }

    previousValue = currentValue;
    return currentValue;
}

std::atomic<bool> keepPolling{ true };

void PollClientStatus() {
    while (keepPolling) {
        auto result = CheckClientStatusChange();
        //std::cout << "Current Status: " << result << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void StartPolling() {
    std::thread(PollClientStatus).detach();
}

void StopPolling() {
    keepPolling = false;
}
#pragma endregion

#pragma region Monster Stats Structure

int mapColorToInt(const std::string& colorCode) {
    static const std::unordered_map<std::string, int> colorMap = {
        {"ÿc0", 0},
        {"ÿc1", 1},
        {"ÿc2", 2},
        {"ÿc3", 3},
        {"ÿc4", 4},
        {"ÿc5", 5},
        {"ÿc6", 6},
        {"ÿc7", 7},
        {"ÿc8", 8},
        {"ÿc9", 9},
        {"ÿc;", 11},
        {"ÿcA", 17},
        {"ÿcN", 25},
        {"ÿcO", 31}
    };

    auto it = colorMap.find(colorCode);
    if (it != colorMap.end()) {
        return it->second;
    }
    else {
        return -1;
    }
}


MonsterStatsDisplaySettings getMonsterStatsDisplaySetting(const std::string& configFilePath) {
    StartPolling();
    std::vector<std::string> automaticCommands = ReadAutomaticCommandsFromFile(filename);
    static bool isCached = false;

    if (isCached) {
        return cachedSettings;
    }

    std::ifstream configFile(configFilePath);
    if (!configFile.is_open()) {
        std::cout << "Error: Could not open the config file: " << configFilePath << std::endl;
        return cachedSettings;
    }

    // Helper to clean a value (trim spaces, quotes, etc.)
    auto cleanValue = [](std::string value) -> std::string {
        value.erase(0, value.find_first_not_of(" \t\""));
        value.erase(value.find_last_not_of(" \t\n\r\",") + 1);
        return value;
        };

    // Helper to strip inline comments
    auto stripComments = [](std::string& line) {
        std::size_t commentPos = line.find("//");
        if (commentPos == std::string::npos)
            commentPos = line.find("#");
        if (commentPos != std::string::npos)
            line = line.substr(0, commentPos);
        };

    std::unordered_map<std::string, std::string> settings;
    std::string line;
    while (std::getline(configFile, line)) {
        stripComments(line);

        std::size_t pos = line.find(":");
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string value = cleanValue(line.substr(pos + 1));
        settings[cleanValue(key)] = value;
    }

    if (settings["MonsterStatsDisplay"] == "true") {
        cachedSettings.monsterStatsDisplay = true;
    }
    else if (settings["MonsterStatsDisplay"] == "false") {
        cachedSettings.monsterStatsDisplay = false;
    }
    else {
        std::cerr << "Error: 'MonsterStatsDisplay' must be true or false." << std::endl;
    }

    cachedSettings.channelColor = settings["Channel Color"];
    cachedSettings.playerNameColor = settings["Player Name Color"];
    cachedSettings.messageColor = settings["Message Color"];
    cachedSettings.HPRollover = settings["HPRolloverMods"] == "true";

    try {
        cachedSettings.HPRolloverAmt = std::stoi(settings["HPRollover%"]);
        cachedSettings.HPRolloverDiff = std::stoi(settings["HPRolloverDifficulty"]);
    }
    catch (const std::exception& e) {
        std::cerr << "Error parsing numeric values: " << e.what() << std::endl;
        cachedSettings.HPRolloverAmt = 0;
        cachedSettings.HPRolloverDiff = 0;
    }
    cachedSettings.sunderedMonUMods = settings["SunderedMonUMods"] == "true";
    cachedSettings.minionEquality = settings["MinionEquality"] == "true";
    cachedSettings.gambleForce = settings["GambleCostControl"] == "true";
    cachedSettings.SunderValue = std::stoi(settings["SunderValue"]);
    cachedSettings.CombatLog = settings["CombatLog"] == "true";

    isCached = true;
    return cachedSettings;
}

MonsterStatsDisplaySettings settings = getMonsterStatsDisplaySetting(configFilePath);
#pragma endregion

#pragma region Chat/Command Functions
void SendDebugCheat(const char* cheat) {
    CCMD_DEBUGCHEAT_PACKET debugCheat = {};
    debugCheat.opcode = CCMD_DEBUGCHEAT;
    debugCheat.unk1 = 1;
    strcpy_s(debugCheat.cheat, cheat);
    SendPacketFunc(&debugCheat);
}

typedef void(__fastcall* Send_SCMD_CHATSTART_Fptr)(uint64_t pPlayer, SCMD_CHATSTART_PACKET* pChatStart);
static GetUnitNameFptr GetUnitName = reinterpret_cast<GetUnitNameFptr>(Pattern::Address(GetUnitNameOffset));
static BroadcastChatMessageFptr BroadcastChatMessage = reinterpret_cast<BroadcastChatMessageFptr>(Pattern::Address(BroadcastChatMessageOffset));
static Send_SCMD_CHATSTART_Fptr Send_SCMD_CHATSTART = reinterpret_cast<Send_SCMD_CHATSTART_Fptr>(Pattern::Address(Send_SCMD_CHATSTARTOffset));
static std::vector<std::string> g_automaticCommands;
typedef void(__fastcall* MONSTER_InitializeStatsAndSkills_t)(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, D2UnitStrc* pMonster, int64_t* pMonRegData);
static MONSTER_InitializeStatsAndSkills_t oMONSTER_InitializeStatsAndSkills = nullptr;
static std::unordered_set<DWORD> g_unitsEdited;

void BroadcastChatMessageCustom(uint64_t pGame, const char* szSender, const char* szMsg) {
    SCMD_CHATSTART_PACKET chatStart = {};
    chatStart.opcode = SCMD_CHATSTART;
    chatStart.msgType = 0xFF;
    chatStart.color = 4;

#undef min
    strncpy(chatStart.sender, szSender, std::min(strlen(szSender), size_t(60)));
    strncpy(chatStart.message, szMsg, std::min(strlen(szMsg), size_t(255)));

    uint64_t pClient = *reinterpret_cast<uint64_t*>(pGame + 336);
    while (pClient) {
        if (*reinterpret_cast<uint32_t*>(pClient + 4) == 4) {
            Send_SCMD_CHATSTART(pClient, &chatStart);
        }
        pClient = *reinterpret_cast<uint64_t*>(pClient + 1368);
    }
}

bool __fastcall CCMD_DEBUGCHEAT_Hook(uint64_t pGame, uint64_t pPlayer, CCMD_DEBUGCHEAT_PACKET* pCheat, uint32_t dwDataLen) {
    printf("CCMD_DEBUGCHEAT_Hook: %s\n", pCheat->cheat);

    char cheatBuf[256];
    strcpy_s(cheatBuf, pCheat->cheat);

    char* next_token = nullptr;
    const char* cheatName = strtok_s(cheatBuf, " =", &next_token);
    const char* cheatArg = strtok_s(nullptr, "", &next_token);

    DebugCheatEntry* cheatsArray = reinterpret_cast<DebugCheatEntry*>(Pattern::Address(cheatsArrayOffset));
    bool isChatMessage = true;

    for (uint32_t i = 0; i < 128; i++) {
        if (strstr(cheatName, cheatsArray[i].name) == cheatName &&
            (!cheatsArray[i].hasArguments || (cheatArg != nullptr))) {
            isChatMessage = false;
            break;
        }
    }

    if (isChatMessage) {
        char nameBuf[128] = {};
        const char* playerName = GetUnitName(pPlayer, nameBuf);
        char messageBuf[512];
        sprintf_s(messageBuf, "%s%s: %s%s", settings.playerNameColor.c_str(), playerName, settings.messageColor.c_str(), pCheat->cheat);
        BroadcastChatMessageCustom(pGame, playerName, messageBuf);
        return false;
    }

    return CCMD_DEBUGCHEAT_Handler_Orig(pGame, pPlayer, pCheat, dwDataLen);
}

bool __fastcall Process_SCMD_CHATSTART_Hook(SCMD_CHATSTART_PACKET* pPacket) {
    if (pPacket->msgType == 0xFF) {
        auto pChatMgr = GetChatManager();
        blz_string msg = { pPacket->message, strlen(pPacket->message), strlen(pPacket->message) };
        int colorValue = mapColorToInt(settings.channelColor);
        ChatMsg gameChatMsgType = *reinterpret_cast<ChatMsg*>(Pattern::Address(gameChatMsgTypeOffset));
        ChatOptionalStruct opt = {};

        ChatManager_PushChatEntry(pChatMgr, msg, colorValue, false, gameChatMsgType, opt, opt, opt, opt);
        return true;
    }
    return Process_SCMD_CHATSTART_Orig(pPacket);
}

void __fastcall GameMenuOnClickHandlerHook(uint64_t a1, Widget* pWidget) {
    WriteToDebugLog("GameMenuOnClickHandlerHook called");

    if (!pWidget) {
        WriteToDebugLog("pWidget is null. Exiting function.");
        return;
    }

    if (pWidget->hash == SaveAndExitButtonHash) {
        WriteToDebugLog("Save and Exit button detected. Executing save command");

        ExecuteDebugCheatFunc("save 1");
        WriteToDebugLog("Executed: 'save 1'");
        g_unitsEdited.clear();

        queuedActions.push({ "delayexit", [a1, pWidget] {
            WriteToDebugLog("Executing delayed exit action");
            mainMenuClickHandlerOrig(a1, pWidget);
        } });

        HWND mainWindow = find_main_window(GetCurrentProcessId());
        if (mainWindow) {
            WriteToDebugLog("Main window found. Setting timer");
            SetTimer(mainWindow, 1234, 1000, DelayedActionsTimerProc);
        }
        else {
            WriteToDebugLog("Failed to find main window");
        }

        return;
    }

    mainMenuClickHandlerOrig(a1, pWidget);
    WriteToDebugLog("Original handler executed");
}
#pragma endregion

#pragma region Monster Stats

typedef BOOL(__fastcall* STATLISTEX_SetStatListExStat_t)(D2StatListExStrc* pStatListEx, D2C_ItemStats nStat, int32_t nValue, uint16_t nLayer);
static STATLISTEX_SetStatListExStat_t STATLISTEX_SetStatListExStat = reinterpret_cast<STATLISTEX_SetStatListExStat_t>(Pattern::Address(0x1e5270));

typedef void(__fastcall* MONSTER_GetPlayerCountBonus_t)(D2GameStrc* pGame, D2PlayerCountBonusStrc* pPlayerCountBonus, D2ActiveRoomStrc* pRoom, D2UnitStrc* pMonster);
static MONSTER_GetPlayerCountBonus_t oMONSTER_GetPlayerCountBonus = nullptr;

typedef void(__fastcall* SUNITDMG_ApplyResistancesAndAbsorb_t)(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pDamageStatTableRecord, int32_t bDontAbsorb);
static SUNITDMG_ApplyResistancesAndAbsorb_t oSUNITDMG_ApplyResistancesAndAbsorb = nullptr;

static int32_t* gnVirtualPlayerCount = reinterpret_cast<int32_t*>(Pattern::Address(0x1d637e4));
int32_t playerCountGlobal;


D2MonStatsTxt* __fastcall MONSTERMODE_GetMonStatsTxtRecord(int32_t nMonsterId)
{
    if (nMonsterId >= 0 && nMonsterId < sgptDataTables->nMonStatsTxtRecordCount)
        return &sgptDataTables->pMonStatsTxt[nMonsterId];

    return nullptr;
}


void __fastcall HookedMONSTER_GetPlayerCountBonus(D2GameStrc* pGame, D2PlayerCountBonusStrc* pPlayerCountBonus, D2ActiveRoomStrc* pRoom, D2UnitStrc* pMonster) {
    oMONSTER_GetPlayerCountBonus(pGame, pPlayerCountBonus, pRoom, pMonster);
    playerCountGlobal = pPlayerCountBonus->nPlayerCount;

    if (GetModName() != "RMD-MP")
    {
        // cap max hp bonus at 300%. once it gets to 500% + it can rollover quickly causing monsters to have negative hp.
        if (pPlayerCountBonus->nPlayerCount > 8 && pGame->nDifficulty > cachedSettings.HPRolloverDiff)
            pPlayerCountBonus->nHP = 300;
    }   
}

const int32_t nMaxPlayerCount = 65535;
float nMaxDamageReductionPercent = cachedSettings.HPRolloverAmt; // e.g., 90 means 90% max reduction

void __fastcall ScaleDamage(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pDamageStatTableRecord) {
    if (pDamageInfo->bDefenderIsMonster && *pDamageStatTableRecord->pOffsetInDamageStrc > 0) {
        int nPlayerCount = STATLIST_GetUnitStatSigned(pDamageInfo->pDefender, STAT_MONSTER_PLAYERCOUNT, 0);

        if (nPlayerCount > 8) {
            // Calculate how far we are past 8 players, normalized
            float playerRatio = static_cast<float>(nPlayerCount - 8) / (nMaxPlayerCount - 8);

            // Exponential scaling: the higher the exponent, the faster the reduction accelerates
            float exponent = 2000.5f;  // adjust this to make scaling more aggressive (>2 is quite steep)
            float scaleFactor = pow(1.0f - playerRatio, exponent); // approaches 0 rapidly

            // Final reduction capped by configured percentage
            float reduction = (1.0f - scaleFactor) * (nMaxDamageReductionPercent / 100.0f);

            float damageScale = 1.0f - reduction;
            *pDamageStatTableRecord->pOffsetInDamageStrc *= damageScale;


            std::ofstream log("d2r_hp.txt", std::ios::app);
            if (log.is_open()) {
                log << "Player count: " << nPlayerCount
                    << " | Reduction: " << (1.0f - damageScale) * 100.0f << "%"
                    << " | Damage scale: " << damageScale << std::endl;
                log.close();
            }

        }
    }
}

void __fastcall HookedSUNITDMG_ApplyResistancesAndAbsorb(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pDamageStatTableRecord, int32_t bDontAbsorb) {
    oSUNITDMG_ApplyResistancesAndAbsorb(pDamageInfo, pDamageStatTableRecord, bDontAbsorb);

    if (pDamageInfo->pGame->nDifficulty > cachedSettings.HPRolloverDiff) {
        ScaleDamage(pDamageInfo, pDamageStatTableRecord);
    }
}

#pragma endregion

#pragma region Sunder Mechanic

constexpr uint16_t UNIQUE_LAYER = 1337;

static void LogSunder(const std::string& msg)
{
    std::ofstream log("debug_sunder_log.txt", std::ios::app);
    if (!log.is_open()) return;

    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char timebuf[32];
    std::strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S] ", &tm);
    log << timebuf << msg << "\n";
}

void StoreRemainder(D2UnitStrc* pUnit, const RemainderEntry& remainders)
{
    if (!pUnit) return;
    g_resistRemainders[pUnit->dwUnitId] = remainders;
}

RemainderEntry GetRemainder(D2UnitStrc* pUnit)
{
    if (!pUnit) return {};

    auto it = g_resistRemainders.find(pUnit->dwUnitId);
    if (it != g_resistRemainders.end())
        return it->second;

    return {};
}

uint32_t SubtractResistances(D2UnitStrc* pUnit, D2C_ItemStats nStatId, uint32_t nValue, uint16_t nLayer = 0)
{
    if (!pUnit) return 0;

    auto nCurrentValue = STATLIST_GetUnitStatSigned(pUnit, nStatId, nLayer);
    int newValue = nCurrentValue - nValue;
    int remainder = 0;

    if (nCurrentValue >= 100)
    {
        // Calculate overshoot based on cachedSettings.SunderValue
        if (newValue < cachedSettings.SunderValue) {
            remainder = cachedSettings.SunderValue - newValue;
            newValue = cachedSettings.SunderValue;
        }

        STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, newValue, nLayer);
    }
    else
    {
        remainder = nValue;
        newValue = nCurrentValue;
    }

    STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, newValue, nLayer);

    // Update the proper remainder entry
    auto& re = g_resistRemainders[pUnit->dwUnitId];
    switch (nStatId)
    {
    case STAT_COLDRESIST:  re.cold = remainder; break;
    case STAT_FIRERESIST:  re.fire = remainder; break;
    case STAT_LIGHTRESIST: re.light = remainder; break;
    case STAT_POISONRESIST: re.poison = remainder; break;
    case STAT_DAMAGERESIST: re.damage = remainder; break;
    case STAT_MAGICRESIST:  re.magic = remainder; break;
    default: break; // ignore other stats
    }

    LogSunder("SU Monster Value: " + std::to_string(nCurrentValue) +
        ", SU Function Value: " + std::to_string(nValue) +
        ", SU New Value: " + std::to_string(newValue) +
        ", SU Remainder: " + std::to_string(remainder));

    return remainder;
}

void SetStat(D2UnitStrc* pUnit, D2C_ItemStats nStatId, uint32_t nValue) {
    int currentValue = STATLIST_GetUnitStatSigned(pUnit, nStatId, 0);

    if (currentValue >= static_cast<int>(nValue))
        return;

    int offset = static_cast<int>(nValue) - currentValue;

    STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, currentValue + offset, 0);
    STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, offset, UNIQUE_LAYER);
}

void AddToCurrentStat(D2UnitStrc* pUnit, D2C_ItemStats nStatId, uint32_t nValue) {
    int currentValue = STATLIST_GetUnitStatSigned(pUnit, nStatId, 0);

    if (STATLIST_GetUnitStatSigned(pUnit, STAT_ALIGNMENT, 0) != 1)
        STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, currentValue + nValue, 0);
}

static void ApplySunderForStat(D2UnitStrc* pUnit, D2C_ItemStats statId, int maxVal, const std::vector<std::pair<const char*, int>>& umodCaps, const std::vector<const uint32_t*>& umodArrays, const std::vector<size_t>& umodSizes, const std::string& statName, const std::vector<std::vector<uint8_t>>& originalGroups)   // per-group originals
{
    if (maxVal <= INT_MIN)
        return;

    int rem = SubtractResistances(pUnit, statId, maxVal);
    LogSunder(statName + " max=" + std::to_string(maxVal) + " SubtractResistances rem=" + std::to_string(rem));

    for (size_t i = 0; i < umodArrays.size(); ++i)
    {
        LogSunder(statName + " UMod[" + std::string(umodCaps[i].first) +
            "] cap=" + std::to_string(umodCaps[i].second) +
            " final=" + std::to_string(rem));

        const std::vector<uint8_t>& groupOriginal = (i < originalGroups.size()) ? originalGroups[i] : std::vector<uint8_t>{};

        if (cachedSettings.sunderedMonUMods)
            ApplyUModArray(umodArrays[i], umodSizes[i], rem, groupOriginal, statName);
    }
}

void __fastcall ApplyGhettoSunder(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, D2UnitStrc* pUnit, int64_t* pMonRegData, D2MonStatsInitStrc* monStatsInit)
{
    if (!pGame || !pUnit)
    {
        LogSunder("Invalid game/unit pointer in ApplyGhettoSunder");
        return;
    }

    LogSunder("=== Begin ApplyGhettoSunder ===");

    int maxCold = INT_MIN, maxFire = INT_MIN, maxLight = INT_MIN;
    int maxPoison = INT_MIN, maxDamage = INT_MIN, maxMagic = INT_MIN;

    for (int i = 0; i < 8; ++i)
    {
        auto pClient = gpClientList[i];
        if (!pClient) continue;

        uint32_t guid = pClient->dwUnitGUID;
        D2UnitStrc* pPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, guid);
        if (!pPlayer) continue;

        int cold = STATLIST_GetUnitStatSigned(pPlayer, 187, 0);
        int fire = STATLIST_GetUnitStatSigned(pPlayer, 189, 0);
        int light = STATLIST_GetUnitStatSigned(pPlayer, 190, 0);
        int poison = STATLIST_GetUnitStatSigned(pPlayer, 191, 0);
        int damage = STATLIST_GetUnitStatSigned(pPlayer, 192, 0);
        int magic = STATLIST_GetUnitStatSigned(pPlayer, 193, 0);

        if (cold > maxCold)     maxCold = cold;
        if (fire > maxFire)     maxFire = fire;
        if (light > maxLight)   maxLight = light;
        if (poison > maxPoison) maxPoison = poison;
        if (damage > maxDamage) maxDamage = damage;
        if (magic > maxMagic)   maxMagic = magic;
    }

    RemainderEntry remainders{};
    remainders.cold = SubtractResistances(pUnit, STAT_COLDRESIST, maxCold);
    remainders.fire = SubtractResistances(pUnit, STAT_FIRERESIST, maxFire);
    remainders.light = SubtractResistances(pUnit, STAT_LIGHTRESIST, maxLight);
    remainders.poison = SubtractResistances(pUnit, STAT_POISONRESIST, maxPoison);
    remainders.damage = SubtractResistances(pUnit, STAT_DAMAGERESIST, maxDamage);
    remainders.magic = SubtractResistances(pUnit, STAT_MAGICRESIST, maxMagic);

    StoreRemainder(pUnit, remainders);
    LogSunder("=== End ApplyGhettoSunder ===");
}



#pragma endregion

#pragma region Terror Zones

#pragma region - Static/Structs

struct DifficultySettings {
    std::optional<int> bound_incl_min;
    std::optional<int> bound_incl_max;
    std::optional<int> boost_level;
    std::optional<int> difficulty_scale;
    std::optional<int> boost_experience_percent;
};

struct WarningInfo {
    int announce_time_min = 0;
    int tier = 0;
};

struct LevelName {
    int id;
    std::string name;
};

struct LevelGroup {
    std::string name;
    std::vector<int> levels;
};

struct Stat {
    D2C_ItemStats id;
    int minValue;
    int maxValue;
    bool allowNegative;
    bool isBinary;
};

struct MonsterTreasureClass
{
    std::string MonsterName;
    std::string TCChecker1a;
    std::string TCChecker1b;
    std::string TCChecker1c;
    std::string Desecrated;
    std::string DesecratedChamp;
    std::string DesecratedUnique;
    std::string TCChecker2a;
    std::string TCChecker2b;
    std::string TCChecker2c;
    std::string Desecrated_N;
    std::string DesecratedChamp_N;
    std::string DesecratedUnique_N;
    std::string TCChecker3a;
    std::string TCChecker3b;
    std::string TCChecker3c;
    std::string Desecrated_H;
    std::string DesecratedChamp_H;
    std::string DesecratedUnique_H;
};

struct MonsterTreasureClassSU
{
    std::string MonsterName;
    std::string BaseMonster;
    std::string TCChecker1;
    std::string Desecrated;
    std::string TCChecker2;
    std::string Desecrated_N;
    std::string TCChecker3;
    std::string Desecrated_H;
};

struct StatValue
{
    D2C_ItemStats stat;
    std::vector<int> value;
};

struct StatAdjustment
{
    bool random;
    int minStats = 0;
    int maxStats = 0;
    std::vector<StatValue> stats_values;
};

struct StatNameEntry {
    int id;
    std::string name;
};

struct ZoneLevel {
    std::optional<int> level_id;
    bool allLevels = false;

    // Optional per-difficulty overrides
    std::optional<DifficultySettings> normal;
    std::optional<DifficultySettings> nightmare;
    std::optional<DifficultySettings> hell;
    std::vector<StatAdjustment> stat_adjustments;
};

struct ZoneGroup {
    int id = 0;
    std::vector<ZoneLevel> levels;
};

struct DesecratedZone {
    std::time_t start_time_utc = 0;
    std::time_t end_time_utc = 0;
    int terror_duration_min = 0;
    int terror_break_min = 0;
    uint64_t seed = 0;
    DifficultySettings default_normal;
    DifficultySettings default_nightmare;
    DifficultySettings default_hell;
    std::vector<WarningInfo> warnings;
    std::vector<ZoneGroup> zones;
};

struct TerrorZoneDisplayData
{
    int cycleLengthMin = 0;
    int terrorDurationMin = 0;
    int groupCount = 0;
    int activeGroupIndex = -1;
    time_t zoneStartUtc = 0;
    std::vector<ZoneLevel> activeLevels;
};

struct MonsterTreasureResult {
    int treasureIndex;
    int tcCheckIndex;
};

int playerLevel = 0;
static std::string g_ActiveZoneInfoText;
int g_ManualZoneGroupOverride = -1;
time_t g_LastToggleTime = 0;
TerrorZoneDisplayData g_TerrorZoneData;
std::vector<DesecratedZone> gDesecratedZones;
std::vector<LevelName> level_names;
std::vector<LevelGroup> level_groups;
typedef BOOL(__fastcall* QUESTRECORD_GetQuestState_t)(D2BitBufferStrc* pQuestRecord, int32_t nQuestId, int32_t nState);
static QUESTRECORD_GetQuestState_t oQUESTRECORD_GetQuestState = reinterpret_cast<QUESTRECORD_GetQuestState_t>(Pattern::Address(0x243880));
typedef int64_t(__fastcall* HUDWarnings__PopulateHUDWarnings_t)(void* pWidget);
static HUDWarnings__PopulateHUDWarnings_t oHUDWarnings__PopulateHUDWarnings = nullptr;
typedef void(__fastcall* Widget__OnClose_t)(void* pWidget);
static Widget__OnClose_t oWidget__OnClose = nullptr;
static char pCustom[1024];
typedef BOOL(__stdcall* DATATBLS_CalculateMonsterStatsByLevel_t)(int nMonsterId, int nGameType, int nDifficulty, int nLevel, short nFlags, D2MonStatsInitStrc* pMonStatsInit);
static DATATBLS_CalculateMonsterStatsByLevel_t oAdjustMonsterStats = reinterpret_cast<DATATBLS_CalculateMonsterStatsByLevel_t>(Pattern::Address(0x2356B0));
typedef void(__fastcall* DropTCTest_t)(D2GameStrc* pGame, D2UnitStrc* pMonster, D2UnitStrc* pPlayer, int32_t nTCId, int32_t nQuality, int32_t nItemLevel, int32_t a7, D2UnitStrc** ppItems, int32_t* pnItemsDropped, int32_t nMaxItems);
static DropTCTest_t oDropTCTest = nullptr;
typedef uint32_t(__fastcall* GambleForce_t)(D2UnitStrc* pItem, int nPlayerLevel);
static GambleForce_t oGambleForce = nullptr;
bool isTerrorized = false;
std::vector<StatAdjustment> gStatAdjustments;
std::unordered_map<int, std::string> gStatNames;
static std::vector<std::pair<D2C_ItemStats, int>> g_randomStats;
std::unordered_map<D2C_ItemStats, int> gRandomStatsForMonsters;
bool showStatAdjusts = true;
std::string g_ItemFilterStatusMessage = "";
bool g_ShouldShowItemFilterMessage = false;
std::chrono::steady_clock::time_point g_ItemFilterMessageStartTime;
bool initialized = false;
static char gTZInfoText[256] = { 0 };
static char gTZStatAdjText[256] = { 0 };
static std::mutex g_LogMutex;
static std::unordered_map<uint32_t, std::unordered_set<D2C_ItemStats>> g_unitsEditedStats;
static std::once_flag gZonesLoadedFlag;
static std::atomic<bool> gZonesLoaded = false;
static std::string gZonesFilePath;
namespace {
    static double gLastManualToggleTime = 0;
}

#pragma endregion

#pragma region - Helper Functions

static void LogDebug(const std::string& msg)
{
    std::ofstream log("debug_log.txt", std::ios::app);
    if (!log.is_open())
        return;

    log << msg << "\n";
}

void LogSpawnDebug(const char* fmt, ...)
{
    std::lock_guard<std::mutex> lock(g_LogMutex);

    FILE* f = fopen("spawn_debug.log", "a");
    if (!f)
        return;

    // Timestamp
    std::time_t t = std::time(nullptr);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    tmBuf = *std::localtime(&t);
#endif

    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] ",
        tmBuf.tm_year + 1900,
        tmBuf.tm_mon + 1,
        tmBuf.tm_mday,
        tmBuf.tm_hour,
        tmBuf.tm_min,
        tmBuf.tm_sec);

    // Format the message
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);

    fprintf(f, "\n");

    fclose(f);
}

int32_t __fastcall MONSTERUNIQUE_GetSuperUniqueBossHcIdx(D2GameStrc* pGame, D2UnitStrc* pUnit)
{
    if (pUnit && pUnit->dwUnitType == UNIT_MONSTER && pUnit->pMonsterData && pUnit->pMonsterData->nTypeFlag & MONTYPEFLAG_SUPERUNIQUE)
        return pUnit->pMonsterData->wBossHcIdx;

    return -1;
}

int32_t __fastcall MONSTERUNIQUE_CheckMonTypeFlag(D2UnitStrc* pUnit, uint16_t nFlag)
{
    if (pUnit && pUnit->dwUnitType == UNIT_MONSTER && pUnit->pMonsterData)
        return (pUnit->pMonsterData->nTypeFlag & nFlag) != 0;

    return 0;
}

BOOL CalculateMonsterStats(int monsterId, int gameType, int difficulty, int level, short flags, D2MonStatsInitStrc& outStats)
{
    if (!oAdjustMonsterStats)
        return FALSE;

    return oAdjustMonsterStats(monsterId, gameType, difficulty, level, flags, &outStats);
}

inline int D2_ApplyRatio(int32_t nValue, int32_t nMultiplier, int32_t nDivisor)
{
    if (nDivisor)
    {
        if (nValue <= 0x100'000)
        {
            if (nMultiplier <= 0x10'000)
                return nMultiplier * nValue / nDivisor;

            if (nDivisor <= (nMultiplier >> 4))
                return nValue * (nMultiplier / nDivisor);
        }
        else
        {
            if (nDivisor <= (nValue >> 4))
                return nMultiplier * (nValue / nDivisor);
        }

        return ((int64_t)nMultiplier * (int64_t)nValue) / nDivisor;
    }

    return 0;
}

inline int32_t D2_ComputePercentage(int32_t nValue, int32_t nPercentage)
{
    return D2_ApplyRatio(nValue, nPercentage, 100);
}

inline uint64_t __fastcall SEED_RollRandomNumber(D2SeedStrc* pSeed)
{
    uint64_t lSeed = static_cast<uint64_t>(pSeed->dwSeed[1]) + 0x6AC690C5i64 * static_cast<uint64_t>(pSeed->dwSeed[0]);
    pSeed->lSeed = lSeed;
    return lSeed;
}

inline uint32_t __fastcall SEED_RollLimitedRandomNumber(D2SeedStrc* pSeed, int nMax)
{
    if (nMax > 0)
    {
        if ((nMax - 1) & nMax)
            return (unsigned int)SEED_RollRandomNumber(pSeed) % nMax;
        else
            return SEED_RollRandomNumber(pSeed) & (nMax - 1);
    }

    return 0;
}

uint32_t __fastcall ITEMS_RollLimitedRandomNumber(D2SeedStrc* pSeed, int32_t nMax)
{
    return SEED_RollLimitedRandomNumber(pSeed, nMax);
}

std::string StripComments(const std::string& jsonWithComments) {
    std::istringstream iss(jsonWithComments);
    std::ostringstream oss;
    std::string line;
    bool in_block_comment = false;

    while (std::getline(iss, line)) {
        std::string newLine;
        bool in_string = false;

        for (size_t i = 0; i < line.length(); ++i) {
            char c = line[i];

            if (c == '\"') {
                bool escaped = (i > 0 && line[i - 1] == '\\');
                if (!escaped) in_string = !in_string;
            }

            if (in_block_comment) {
                if (c == '*' && i + 1 < line.length() && line[i + 1] == '/') {
                    in_block_comment = false;
                    ++i;
                }
                continue;
            }

            if (!in_string && c == '/' && i + 1 < line.length() && line[i + 1] == '*') {
                in_block_comment = true;
                ++i;
                continue;
            }

            if (!in_string && c == '/' && i + 1 < line.length() && line[i + 1] == '/') {
                size_t trimEnd = newLine.find_last_not_of(" \t");
                if (trimEnd != std::string::npos) {
                    newLine = newLine.substr(0, trimEnd + 1);
                }
                else {
                    newLine.clear();
                }
                break;
            }

            newLine += c;
        }

        if (!in_block_comment)
            oss << newLine << "\n";
    }

    return oss.str();
}

bool GetBaalQuest(D2UnitStrc* pPlayer, D2GameStrc* pGame) {
    if (!pPlayer || !pPlayer->pPlayerData || !pGame)
        return false;

    auto pQuestData = pPlayer->pPlayerData->pQuestData[pGame->nDifficulty];
    if (!pQuestData)
        return false;

    return pGame->bExpansion & oQUESTRECORD_GetQuestState(pQuestData, QUESTSTATEFLAG_A5Q6, QFLAG_REWARDGRANTED);
}

int GetLevelIdFromRoom(D2ActiveRoomStrc* pRoom)
{
    if (!pRoom || !pRoom->pDrlgRoom || !pRoom->pDrlgRoom->pLevel)
        return -1;

    return pRoom->pDrlgRoom->pLevel->nLevelId;
}

std::string GetMonsterTypeFlags(uint8_t flags)
{
    std::string out;

    if (flags & MONTYPEFLAG_OTHER)        out += "OTHER ";
    if (flags & MONTYPEFLAG_SUPERUNIQUE)  out += "SUPERUNIQUE ";
    if (flags & MONTYPEFLAG_CHAMPION)     out += "CHAMPION ";
    if (flags & MONTYPEFLAG_UNIQUE)       out += "UNIQUE ";
    if (flags & MONTYPEFLAG_MINION)       out += "MINION ";
    if (flags & MONTYPEFLAG_POSSESSED)    out += "POSSESSED ";
    if (flags & MONTYPEFLAG_GHOSTLY)      out += "GHOSTLY ";
    if (flags & MONTYPEFLAG_MULTISHOT)    out += "MULTISHOT ";

    if (out.empty())
        return "NONE";

    return out;
}

#pragma endregion

#pragma region - JSON Parsers

inline std::time_t parse_time_utc(const std::string& s) {
    std::tm tm = {};
    std::istringstream ss(s);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        return 0;
    }
    return _mkgmtime(&tm);
}

void from_json(const json& j, DifficultySettings& d) {

    //Optional
    if (j.contains("bound_incl_min"))
        d.bound_incl_min = j.at("bound_incl_min").get<int>();

    if (j.contains("bound_incl_max"))
        d.bound_incl_max = j.at("bound_incl_max").get<int>();

    if (j.contains("boost_level"))
        d.boost_level = j.at("boost_level").get<int>();

    if (j.contains("difficulty_scale"))
        d.difficulty_scale = j.at("difficulty_scale").get<int>();

    if (j.contains("boost_experience_percent"))
        d.boost_experience_percent = j.at("boost_experience_percent").get<int>();
}

void from_json(const json& j, WarningInfo& w) {
    w.announce_time_min = j.at("announce_time_min").get<int>();
    w.tier = j.at("tier").get<int>();
}

void from_json(const nlohmann::json& j, LevelName& ln) {
    j.at("id").get_to(ln.id);
    j.at("name").get_to(ln.name);
}

void from_json(const nlohmann::json& j, LevelGroup& lg) {
    j.at("name").get_to(lg.name);
    j.at("levels").get_to(lg.levels);
}

inline void from_json(const nlohmann::json& j, StatValue& sv)
{
    sv.stat = static_cast<D2C_ItemStats>(j.at("stat").get<int>());
    j.at("value").get_to(sv.value);
}

inline void from_json(const nlohmann::json& j, StatAdjustment& sa)
{
    j.at("random").get_to(sa.random);
    j.at("stats_values").get_to(sa.stats_values);

    if (j.contains("minmaxStats") && j["minmaxStats"].is_array() && j["minmaxStats"].size() == 2)
    {
        sa.minStats = j["minmaxStats"][0].get<int>();
        sa.maxStats = j["minmaxStats"][1].get<int>();
    }
    else
    {
        sa.minStats = 0;
        sa.maxStats = 0;
    }
}

struct Config
{
    std::vector<StatAdjustment> stat_adjustments;
};

inline void from_json(const nlohmann::json& j, Config& cfg)
{
    if (j.contains("stat_adjustments"))
        j.at("stat_adjustments").get_to(cfg.stat_adjustments);
    else
        cfg.stat_adjustments.clear();
}

void from_json(const nlohmann::json& j, StatNameEntry& entry) {
    j.at("id").get_to(entry.id);
    j.at("name").get_to(entry.name);
}

struct StatNamesConfig {
    std::vector<StatNameEntry> stat_names;
};

void from_json(const nlohmann::json& j, StatNamesConfig& config) {
    if (j.contains("stat_names") && j["stat_names"].is_array()) {
        config.stat_names = j.at("stat_names").get<std::vector<StatNameEntry>>();
    }
}

void from_json(const json& j, ZoneLevel& zl) {
    if (j.contains("all") && j.at("all").get<bool>() == true) {
        zl.allLevels = true;
        zl.level_id.reset();  // clear any level_id
    }
    else if (j.contains("level_id")) {
        zl.level_id = j.at("level_id").get<int>();
    }
    else {
        throw std::runtime_error("ZoneLevel must have either 'level_id' or 'all: true'");
    }

    // Optional per-difficulty overrides
    if (j.contains("normal")) zl.normal = j.at("normal").get<DifficultySettings>();
    if (j.contains("nightmare")) zl.nightmare = j.at("nightmare").get<DifficultySettings>();
    if (j.contains("hell")) zl.hell = j.at("hell").get<DifficultySettings>();

    if (j.contains("stat_adjustments") && j["stat_adjustments"].is_array()) {
        zl.stat_adjustments = j.at("stat_adjustments").get<std::vector<StatAdjustment>>();
    }
    else {
        zl.stat_adjustments.clear();
    }
}

void from_json(const json& j, ZoneGroup& zg) {
    zg.id = j.at("id").get<int>();
    zg.levels = j.at("levels").get<std::vector<ZoneLevel>>();
}

void from_json(const json& j, DesecratedZone& dz) {
    dz.start_time_utc = parse_time_utc(j.at("start_time_utc").get<std::string>());
    dz.end_time_utc = parse_time_utc(j.at("end_time_utc").get<std::string>());
    dz.terror_duration_min = j.at("terror_duration_min").get<int>();
    dz.terror_break_min = j.at("terror_break_min").get<int>();
    dz.default_normal = j.at("default_normal").get<DifficultySettings>();
    dz.default_nightmare = j.at("default_nightmare").get<DifficultySettings>();
    dz.default_hell = j.at("default_hell").get<DifficultySettings>();
    dz.warnings = j.at("warnings").get<std::vector<WarningInfo>>();
    dz.zones = j.at("zones").get<std::vector<ZoneGroup>>();
    // Optional: seed
    if (j.contains("seed")) {
        dz.seed = j.at("seed").get<uint64_t>();

        if (dz.seed != 0) {
            std::mt19937_64 rng(dz.seed);
            std::shuffle(dz.zones.begin(), dz.zones.end(), rng);
        }
    }
    else {
        dz.seed = 0;
    }
}

#pragma endregion

#pragma region - Load/Save Functions

std::vector<std::string> ReadTCexFile(const std::string& filename)
{
    std::vector<std::string> treasureClasses;
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error opening file: " << filename << "\n";
        return treasureClasses;
    }

    std::string line;
    while (std::getline(file, line))
    {
        std::stringstream ss(line);
        std::string firstColumn;
        if (std::getline(ss, firstColumn, '\t'))
            treasureClasses.push_back(firstColumn);
    }
    return treasureClasses;
}

std::vector<MonsterTreasureClass> ReadMonsterTreasureFile(const std::string& filename)
{
    std::vector<MonsterTreasureClass> results;
    std::ifstream file(filename);

    if (!file.is_open())
    {
        std::cerr << "Error: Unable to open file: " << filename << "\n";
        return results;
    }

    std::string line;
    bool isHeader = true;
    int idxMonsterName = -1;
    int idxTCChecker1a = -1;
    int idxTCChecker1b = -1;
    int idxTCChecker1c = -1;
    int idxDesecrated = -1;
    int idxDesecratedChamp = -1;
    int idxDesecratedUnique = -1;
    int idxTCChecker2a = -1;
    int idxTCChecker2b = -1;
    int idxTCChecker2c = -1;
    int idxDesecrated_N = -1;
    int idxDesecratedChamp_N = -1;
    int idxDesecratedUnique_N = -1;
    int idxTCChecker3a = -1;
    int idxTCChecker3b = -1;
    int idxTCChecker3c = -1;
    int idxDesecrated_H = -1;
    int idxDesecratedChamp_H = -1;
    int idxDesecratedUnique_H = -1;

    while (std::getline(file, line))
    {
        std::stringstream ss(line);
        std::vector<std::string> cols;
        std::string cell;

        while (std::getline(ss, cell, '\t'))
            cols.push_back(cell);

        if (isHeader)
        {
            for (size_t i = 0; i < cols.size(); ++i)
            {
                if (cols[i] == "Id") idxMonsterName = static_cast<int>(i);
                else if (cols[i] == "TreasureClass1") idxTCChecker1a = static_cast<int>(i);
                else if (cols[i] == "TreasureClass2") idxTCChecker1b = static_cast<int>(i);
                else if (cols[i] == "TreasureClass3") idxTCChecker1c = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecrated") idxDesecrated = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecratedChamp") idxDesecratedChamp = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecratedUnique") idxDesecratedUnique = static_cast<int>(i);
                else if (cols[i] == "TreasureClass1(N)") idxTCChecker2a = static_cast<int>(i);
                else if (cols[i] == "TreasureClass2(N)") idxTCChecker2b = static_cast<int>(i);
                else if (cols[i] == "TreasureClass3(N)") idxTCChecker2c = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecrated(N)") idxDesecrated_N = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecratedChamp(N)") idxDesecratedChamp_N = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecratedUnique(N)") idxDesecratedUnique_N = static_cast<int>(i);
                else if (cols[i] == "TreasureClass1(H)") idxTCChecker3a = static_cast<int>(i);
                else if (cols[i] == "TreasureClass2(H)") idxTCChecker3b = static_cast<int>(i);
                else if (cols[i] == "TreasureClass3(H)") idxTCChecker3c = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecrated(H)") idxDesecrated_H = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecratedChamp(H)") idxDesecratedChamp_H = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecratedUnique(H)") idxDesecratedUnique_H = static_cast<int>(i);
            }

            isHeader = false;
            continue;
        }

        MonsterTreasureClass entry;

        if (idxMonsterName >= 0 && idxMonsterName < (int)cols.size())
            entry.MonsterName = cols[idxMonsterName];
        if (idxTCChecker1a >= 0 && idxTCChecker1a < (int)cols.size())
            entry.TCChecker1a = cols[idxTCChecker1a];
        if (idxTCChecker1b >= 0 && idxTCChecker1b < (int)cols.size())
            entry.TCChecker1b = cols[idxTCChecker1b];
        if (idxTCChecker1c >= 0 && idxTCChecker1c < (int)cols.size())
            entry.TCChecker1c = cols[idxTCChecker1c];
        if (idxDesecrated >= 0 && idxDesecrated < (int)cols.size())
            entry.Desecrated = cols[idxDesecrated];
        if (idxDesecratedChamp >= 0 && idxDesecratedChamp < (int)cols.size())
            entry.DesecratedChamp = cols[idxDesecratedChamp];
        if (idxDesecratedUnique >= 0 && idxDesecratedUnique < (int)cols.size())
            entry.DesecratedUnique = cols[idxDesecratedUnique];
        if (idxTCChecker2a >= 0 && idxTCChecker2a < (int)cols.size())
            entry.TCChecker2a = cols[idxTCChecker2a];
        if (idxTCChecker2b >= 0 && idxTCChecker2b < (int)cols.size())
            entry.TCChecker2b = cols[idxTCChecker2b];
        if (idxTCChecker2c >= 0 && idxTCChecker2c < (int)cols.size())
            entry.TCChecker2c = cols[idxTCChecker2c];
        if (idxDesecrated_N >= 0 && idxDesecrated_N < (int)cols.size())
            entry.Desecrated_N = cols[idxDesecrated_N];
        if (idxDesecratedChamp_N >= 0 && idxDesecratedChamp_N < (int)cols.size())
            entry.DesecratedChamp_N = cols[idxDesecratedChamp_N];
        if (idxDesecratedUnique_N >= 0 && idxDesecratedUnique_N < (int)cols.size())
            entry.DesecratedUnique_N = cols[idxDesecratedUnique_N];
        if (idxTCChecker3a >= 0 && idxTCChecker3a < (int)cols.size())
            entry.TCChecker3a = cols[idxTCChecker3a];
        if (idxTCChecker3b >= 0 && idxTCChecker3b < (int)cols.size())
            entry.TCChecker3b = cols[idxTCChecker3b];
        if (idxTCChecker3c >= 0 && idxTCChecker3c < (int)cols.size())
            entry.TCChecker3c = cols[idxTCChecker3c];
        if (idxDesecrated_H >= 0 && idxDesecrated_H < (int)cols.size())
            entry.Desecrated_H = cols[idxDesecrated_H];
        if (idxDesecratedChamp_H >= 0 && idxDesecratedChamp_H < (int)cols.size())
            entry.DesecratedChamp_H = cols[idxDesecratedChamp_H];
        if (idxDesecratedUnique_H >= 0 && idxDesecratedUnique_H < (int)cols.size())
            entry.DesecratedUnique_H = cols[idxDesecratedUnique_H];

        results.push_back(entry);
    }

    return results;
}

std::vector<MonsterTreasureClassSU> ReadMonsterTreasureFileSU(const std::string& filename)
{
    std::vector<MonsterTreasureClassSU> results;
    std::ifstream file(filename);

    if (!file.is_open())
    {
        std::cerr << "Error: Unable to open file: " << filename << "\n";
        return results;
    }

    std::string line;
    bool isHeader = true;
    int idxMonsterName = -1;
    int idxBaseMonster = -1;
    int idxTCChecker1 = -1;
    int idxDesecrated = -1;
    int idxTCChecker2 = -1;
    int idxDesecrated_N = -1;
    int idxTCChecker3 = -1;
    int idxDesecrated_H = -1;

    while (std::getline(file, line))
    {
        std::stringstream ss(line);
        std::vector<std::string> cols;
        std::string cell;

        while (std::getline(ss, cell, '\t'))
            cols.push_back(cell);

        if (isHeader)
        {
            for (size_t i = 0; i < cols.size(); ++i)
            {
                if (cols[i] == "Superunique") idxMonsterName = static_cast<int>(i);
                else if (cols[i] == "Class") idxBaseMonster = static_cast<int>(i);
                else if (cols[i] == "TC") idxTCChecker1 = static_cast<int>(i);
                else if (cols[i] == "TC Desecrated") idxDesecrated = static_cast<int>(i);
                else if (cols[i] == "TC(N)") idxTCChecker2 = static_cast<int>(i);
                else if (cols[i] == "TC(N) Desecrated") idxDesecrated_N = static_cast<int>(i);
                else if (cols[i] == "TC(H)") idxTCChecker3 = static_cast<int>(i);
                else if (cols[i] == "TC(H) Desecrated") idxDesecrated_H = static_cast<int>(i);
            }

            isHeader = false;
            continue;
        }

        MonsterTreasureClassSU entry;

        if (idxMonsterName >= 0 && idxMonsterName < (int)cols.size())
            entry.MonsterName = cols[idxMonsterName];
        if (idxBaseMonster >= 0 && idxBaseMonster < (int)cols.size())
            entry.BaseMonster = cols[idxBaseMonster];
        if (idxTCChecker1 >= 0 && idxTCChecker1 < (int)cols.size())
            entry.TCChecker1 = cols[idxTCChecker1];
        if (idxDesecrated >= 0 && idxDesecrated < (int)cols.size())
            entry.Desecrated = cols[idxDesecrated];
        if (idxTCChecker2 >= 0 && idxTCChecker2 < (int)cols.size())
            entry.TCChecker2 = cols[idxTCChecker2];
        if (idxDesecrated_N >= 0 && idxDesecrated_N < (int)cols.size())
            entry.Desecrated_N = cols[idxDesecrated_N];
        if (idxTCChecker3 >= 0 && idxTCChecker3 < (int)cols.size())
            entry.TCChecker3 = cols[idxTCChecker3];
        if (idxDesecrated_H >= 0 && idxDesecrated_H < (int)cols.size())
            entry.Desecrated_H = cols[idxDesecrated_H];

        results.push_back(entry);
    }

    return results;
}

MonsterTreasureResult GetMonsterTreasure(const std::vector<MonsterTreasureClass>& monsters, size_t rowIndex, int diff, int monType, const std::vector<std::string>& tcexEntries)
{
    MonsterTreasureResult result{ -1, -1 };

    if (rowIndex >= monsters.size()) {
        LogDebug("Error: Row index out of range");
        return result;
    }

    const auto& m = monsters[rowIndex];
    std::string treasureClassValue;
    std::string tcCheck;

    LogDebug(std::format("---------------------\nMonster: {}", m.MonsterName));
    LogDebug(std::format("Monstats Row: {}, Difficulty: {}", rowIndex, diff));

    if (rowIndex >= 410)
        rowIndex++;

    if (diff == 0) {
        if (monType == 0) { tcCheck = m.TCChecker1a; treasureClassValue = m.Desecrated; }
        else if (monType == 1) { tcCheck = m.TCChecker1b; treasureClassValue = m.DesecratedChamp; }
        else if (monType == 2) { tcCheck = m.TCChecker1c; treasureClassValue = m.DesecratedUnique; }
    }
    else if (diff == 1) {
        if (monType == 0) { tcCheck = m.TCChecker2a; treasureClassValue = m.Desecrated_N; }
        else if (monType == 1) { tcCheck = m.TCChecker2b; treasureClassValue = m.DesecratedChamp_N; }
        else if (monType == 2) { tcCheck = m.TCChecker2c; treasureClassValue = m.DesecratedUnique_N; }
    }
    else if (diff == 2) {
        if (monType == 0) { tcCheck = m.TCChecker3a; treasureClassValue = m.Desecrated_H; }
        else if (monType == 1) { tcCheck = m.TCChecker3b; treasureClassValue = m.DesecratedChamp_H; }
        else if (monType == 2) { tcCheck = m.TCChecker3c; treasureClassValue = m.DesecratedUnique_H; }
    }

    for (size_t i = 0; i < tcexEntries.size(); ++i) {
        if (tcexEntries[i] == treasureClassValue) {
            result.treasureIndex = static_cast<int>(i) + 1;
            break;
        }
    }

    for (size_t i = 0; i < tcexEntries.size(); ++i) {
        if (tcexEntries[i] == tcCheck) {
            result.tcCheckIndex = static_cast<int>(i) + 1;
            break;
        }
    }

    LogDebug(std::format("Treasure Class: {}", tcCheck));
    LogDebug(std::format("TZ Treasure Class: {}", treasureClassValue));
    LogDebug(std::format("Base TC Row: {}, Terror TC Row: {}", result.tcCheckIndex, result.treasureIndex));

    return result;
}

MonsterTreasureResult GetMonsterTreasureSU(const std::vector<MonsterTreasureClassSU>& monsters, size_t rowIndex, int diff, const std::vector<std::string>& tcexEntries)
{
    MonsterTreasureResult result{ -1, -1 };

    if (rowIndex >= monsters.size()) {
        std::cerr << "Error: Row index out of range\n";
        return result;
    }

    const auto& m = monsters[rowIndex];
    std::string treasureClassValue;
    std::string tcCheck;

    LogDebug(std::format("---------------------\nSuperUnique: {}", m.BaseMonster));
    LogDebug(std::format("GetMonsterTreasureSU called with: rowIndex={}", rowIndex));

    if (diff == 0) {
        tcCheck = m.TCChecker1;
        treasureClassValue = m.Desecrated;
    }
    else if (diff == 1) {
        tcCheck = m.TCChecker2;
        treasureClassValue = m.Desecrated_N;
    }
    else if (diff == 2) {
        tcCheck = m.TCChecker3;
        treasureClassValue = m.Desecrated_H;
    }
    else {
        return result;
    }

    for (size_t i = 0; i < tcexEntries.size(); ++i) {
        if (tcexEntries[i] == treasureClassValue) {
            result.treasureIndex = static_cast<int>(i);
            break;
        }
    }

    for (size_t i = 0; i < tcexEntries.size(); ++i) {
        if (tcexEntries[i] == tcCheck) {
            result.tcCheckIndex = static_cast<int>(i);
            break;
        }
    }

    LogDebug(std::format("SU Treasure Class: {}", tcCheck));
    LogDebug(std::format("SU TZ Treasure Class: {}", treasureClassValue));
    LogDebug(std::format("SuperUniques Base TC Row: {}, SuperUniques Terror TC Row: {}", result.tcCheckIndex, result.treasureIndex));

    return result;
}

bool LoadDesecratedZones(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        MessageBoxA(nullptr, "Failed to open desecrated zones config file.", "Error", MB_ICONERROR);
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = StripComments(buffer.str());

    json j;
    try {
        j = json::parse(content);
    }
    catch (const std::exception& e) {
        MessageBoxA(nullptr, ("JSON parse error: " + std::string(e.what())).c_str(), "Error", MB_ICONERROR);
        return false;
    }

    if (!j.contains("desecrated_zones")) {
        MessageBoxA(nullptr, "Unable to locate a valid TZ config", "Error", MB_ICONERROR);
        return false;
    }

    if (!j.contains("level_names")) {
        MessageBoxA(nullptr, "Unable to locate valid level names for TZ", "Error", MB_ICONERROR);
        return false;
    }

    try {
        gDesecratedZones = j.at("desecrated_zones").get<std::vector<DesecratedZone>>();
        level_names = j.at("level_names").get<std::vector<LevelName>>();

        if (j.contains("level_groups") && j["level_groups"].is_array()) {
            level_groups = j.at("level_groups").get<std::vector<LevelGroup>>();
        }
        else {
            level_groups.clear();
        }

        if (j.contains("stat_adjustments") && j["stat_adjustments"].is_array()) {
            gStatAdjustments = j.at("stat_adjustments").get<std::vector<StatAdjustment>>();
        }
        else {
            gStatAdjustments.clear();
        }

        if (j.contains("stat_names") && j["stat_names"].is_array()) {
            std::vector<StatNameEntry> statEntries = j.at("stat_names").get<std::vector<StatNameEntry>>();
            gStatNames.clear();
            for (const auto& entry : statEntries) {
                gStatNames[entry.id] = entry.name;
            }
        }
        else {
            gStatNames.clear();
        }
    }
    catch (const std::exception& e) {
        MessageBoxA(nullptr, ("JSON field parse error: " + std::string(e.what())).c_str(), "Error", MB_ICONERROR);
        return false;
    }

    return true;
}

#pragma endregion

#pragma region - Terror Zone Adjustments

void AdjustMonsterLevel(D2UnitStrc* pUnit, D2C_ItemStats nStatId, uint32_t nValue, uint16_t nLayer = 0) {
    auto monsterLevel = STATLIST_GetUnitStatSigned(pUnit, nStatId, nLayer);

    if (playerLevel >= monsterLevel)
        STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, nValue, nLayer);
}

void ApplyMonsterDifficultyScaling(D2UnitStrc* pUnit, const DesecratedZone& zone, const ZoneLevel* matchingZoneLevel, int difficulty, int playerLevel, int playerCountGlobal, D2GameStrc* pGame)
{
    // Get global defaults
    const DifficultySettings* globalDefaults = nullptr;
    switch (difficulty) {
    case 0: globalDefaults = &zone.default_normal; break;
    case 1: globalDefaults = &zone.default_nightmare; break;
    case 2: globalDefaults = &zone.default_hell; break;
    default: return; // invalid
    }
    if (!globalDefaults) return;

    // Get optional level-specific override
    const std::optional<DifficultySettings>* levelOverride = nullptr;
    if (matchingZoneLevel) {
        switch (difficulty) {
        case 0: levelOverride = &matchingZoneLevel->normal; break;
        case 1: levelOverride = &matchingZoneLevel->nightmare; break;
        case 2: levelOverride = &matchingZoneLevel->hell; break;
        }
    }

    // Merge values
    int boostLevel = globalDefaults->boost_level.value_or(0);
    int boundMin = globalDefaults->bound_incl_min.value_or(1);
    int boundMax = globalDefaults->bound_incl_max.value_or(99);

    if (levelOverride && levelOverride->has_value()) {
        const DifficultySettings & override = levelOverride->value();
        if (override.boost_level)     boostLevel = override.boost_level.value();
        if (override.bound_incl_min)  boundMin = override.bound_incl_min.value();
        if (override.bound_incl_max)  boundMax = override.bound_incl_max.value();
    }

    // Clamp boosted level and Apply
    int boostedLevel = std::clamp(playerLevel + boostLevel, boundMin, boundMax);
    AdjustMonsterLevel(pUnit, STAT_LEVEL, boostedLevel);
    int32_t playerCountModifier = (playerCountGlobal >= 9) ? (playerCountGlobal - 2) * 50 : (playerCountGlobal - 1) * 50;

    // Calculate base monster stats
    D2MonStatsInitStrc monStatsInit = {};
    CalculateMonsterStats(pUnit->dwClassId, 1, pGame->nDifficulty, STATLIST_GetUnitStatSigned(pUnit, STAT_LEVEL, 0), 7, monStatsInit);
    const int32_t nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1);
    const int32_t nHp = nBaseHp + D2_ComputePercentage(nBaseHp, playerCountModifier);
    const int32_t nShiftedHp = nHp << 8;

    // Apply core stats
    SetStat(pUnit, STAT_MAXHP, nShiftedHp);
    SetStat(pUnit, STAT_HITPOINTS, nShiftedHp);
    SetStat(pUnit, STAT_ARMORCLASS, monStatsInit.nAC);
    SetStat(pUnit, STAT_EXPERIENCE, D2_ComputePercentage(monStatsInit.nExp, ((playerCountGlobal - 8) * 100) / 5));

    if (pUnit->dwClassId != 156 && pUnit->dwClassId != 211 && pUnit->dwClassId != 242 && pUnit->dwClassId != 243 && pUnit->dwClassId != 544) //Ignore Act Bosses
        SetStat(pUnit, STAT_HPREGEN, (nShiftedHp * 2) >> 12);
}

void ApplyMonsterDifficultyScalingNonTZ(D2UnitStrc* pUnit, int difficulty, int playerLevel, int playerCountGlobal, D2GameStrc* pGame)
{
    int32_t playerCountModifier = (playerCountGlobal >= 9) ? (playerCountGlobal - 2) * 50 : (playerCountGlobal - 1) * 50;

    // Calculate base monster stats
    D2MonStatsInitStrc monStatsInit = {};
    CalculateMonsterStats(pUnit->dwClassId, 1, pGame->nDifficulty, STATLIST_GetUnitStatSigned(pUnit, STAT_LEVEL, 0), 7, monStatsInit);
    int32_t nBaseHp = 0;

    D2UnitStrc* pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);

    if (difficulty == 0)
    {
        if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_UNIQUE)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 4;
        else if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_CHAMPION)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 3;
        else if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_MINION)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 2;
        else
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1);
    }
    if (difficulty == 1)
    {
        if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_UNIQUE)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 3;
        else if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_CHAMPION)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 2.5;
        else if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_MINION)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 1.75;
        else
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1);
    }
    if (difficulty == 2)
    {
        if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_UNIQUE)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 1;
        else if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_CHAMPION)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 1;
        else if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_MINION)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 1;
        else
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1);
    }


    const int32_t nHp = nBaseHp + D2_ComputePercentage(nBaseHp, playerCountModifier);
    const int32_t nShiftedHp = nHp << 8;

    // Apply core stats
    SetStat(pUnit, STAT_MAXHP, nShiftedHp);
    SetStat(pUnit, STAT_HITPOINTS, nShiftedHp);
    SetStat(pUnit, STAT_ARMORCLASS, monStatsInit.nAC);
    SetStat(pUnit, STAT_EXPERIENCE, D2_ComputePercentage(monStatsInit.nExp, ((playerCountGlobal - 8) * 100) / 5));

    if (pUnit->dwClassId != 156 && pUnit->dwClassId != 211 && pUnit->dwClassId != 242 && pUnit->dwClassId != 243 && pUnit->dwClassId != 544) //Ignore Act Bosses
        SetStat(pUnit, STAT_HPREGEN, (nShiftedHp * 2) >> 12);
}

void ApplyStatsToMonster(D2UnitStrc* pUnit)
{
    std::ostringstream msgStream;

    for (const auto& [stat, value] : gRandomStatsForMonsters)
    {
        AddToCurrentStat(pUnit, stat, value);

        msgStream << "Stat ID " << static_cast<int>(stat)
            << " Applied Value: " << value << "\n";
    }
}

void InitRandomStatsForAllMonsters(bool forceNew = false) {
    static bool initialized = false;
    static std::random_device rd;
    static std::mt19937 rng(rd());

    if (!initialized || forceNew) {
        gRandomStatsForMonsters.clear();

        auto processAdjustments = [&](const std::vector<StatAdjustment>& adjustments) {
            for (const auto& adjustment : adjustments) {
                std::vector<const StatValue*> statsToApply;

                if (adjustment.random) {
                    size_t available = adjustment.stats_values.size();
                    size_t lower = 1;
                    size_t upper = available;

                    if (adjustment.minStats > 0 && static_cast<size_t>(adjustment.minStats) <= available)
                        lower = adjustment.minStats;

                    if (adjustment.maxStats > 0 && static_cast<size_t>(adjustment.maxStats) < upper)
                        upper = adjustment.maxStats;

                    if (lower > upper)
                        lower = upper;

                    std::uniform_int_distribution<size_t> countDist(lower, upper);
                    size_t numStats = countDist(rng);

                    std::vector<const StatValue*> shuffled;
                    shuffled.reserve(available);
                    for (const auto& sv : adjustment.stats_values)
                        shuffled.push_back(&sv);

                    std::shuffle(shuffled.begin(), shuffled.end(), rng);
                    statsToApply.assign(shuffled.begin(), shuffled.begin() + numStats);
                }
                else {
                    for (const auto& sv : adjustment.stats_values)
                        statsToApply.push_back(&sv);
                }

                for (const auto* statVal : statsToApply) {
                    int appliedValue = 0;

                    if (!statVal->value.empty()) {
                        if (statVal->value.size() == 1) {
                            appliedValue = statVal->value[0];
                        }
                        else {
                            int minVal = *std::min_element(statVal->value.begin(), statVal->value.end());
                            int maxVal = *std::max_element(statVal->value.begin(), statVal->value.end());
                            std::uniform_int_distribution<int> valueDist(minVal, maxVal);

                            do {
                                appliedValue = valueDist(rng);
                            } while (appliedValue == 0);
                        }
                    }

                    if (appliedValue != 0) {
                        gRandomStatsForMonsters[statVal->stat] = appliedValue;
                    }
                }
            }
            };

        processAdjustments(gStatAdjustments);

        // Apply per-level stat adjustments for all zones
        for (const auto& dz : gDesecratedZones) {
            for (const auto& zg : dz.zones) {
                for (const auto& lvl : zg.levels) {
                    if (!lvl.stat_adjustments.empty()) {
                        processAdjustments(lvl.stat_adjustments);
                    }
                }
            }
        }

        initialized = true;
    }
}

void __fastcall ForceTCDrops(D2GameStrc* pGame, D2UnitStrc* pMonster, D2UnitStrc* pPlayer, int32_t nTCId, int32_t nQuality, int32_t nItemLevel, int32_t a7, D2UnitStrc** ppItems, int32_t* pnItemsDropped, int32_t nMaxItems)
{
    // Cached file data
    static bool filesLoaded = false;
    static decltype(ReadMonsterTreasureFile("")) monsters;
    static decltype(ReadMonsterTreasureFileSU("")) superuniques;
    static std::vector<std::string> TCEx;

    if (!filesLoaded)
    {
        std::string basePath = std::format("{}/Mods/{}/{}.mpq/data/global/excel/", GetExecutableDir(), GetModName(), GetModName());
        std::string MONFile = basePath + "monstats.txt";
        std::string SUFile = basePath + "superuniques.txt";
        std::string TCEXFile = basePath + "treasureclassex.txt";

        monsters = ReadMonsterTreasureFile(MONFile);
        superuniques = ReadMonsterTreasureFileSU(SUFile);
        TCEx = ReadTCexFile(TCEXFile);

        filesLoaded = true;
    }

    D2MonStatsTxt* pMonStatsTxtRecord = MONSTERMODE_GetMonStatsTxtRecord(pMonster->dwClassId);
    if (!pMonStatsTxtRecord)
    {
        MessageBoxA(nullptr, "Failed to get monster stats record.", "Debug", MB_OK | MB_ICONERROR);
        return;
    }

    auto pMonsterFlag = pMonster->pMonsterData->nTypeFlag;
    const int32_t nSuperUniqueId = MONSTERUNIQUE_GetSuperUniqueBossHcIdx(pGame, pMonster);

    if (pMonStatsTxtRecord->nId >= monsters.size())
    {
        MessageBoxA(nullptr, "Monster ID out of range in monsters vector.", "Debug", MB_OK | MB_ICONERROR);
        return;
    }

    D2UnitStrc* pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);
    if (!pUnitPlayer)
    {
        MessageBoxA(nullptr, "Failed to get player unit.", "Debug", MB_OK | MB_ICONERROR);
        return;
    }

    int difficulty = GetPlayerDifficulty(pUnitPlayer);
    if (difficulty < 0 || difficulty > 2)
    {
        MessageBoxA(nullptr, "Invalid difficulty detected.", "Debug", MB_OK | MB_ICONERROR);
        return;
    }

    // Adjust for Expansion Row
    int adjustedSID = (nSuperUniqueId >= 42) ? nSuperUniqueId + 1 : nSuperUniqueId;
    int adjustedNID = (pMonStatsTxtRecord->nId >= 410) ? pMonStatsTxtRecord->nId + 1 : pMonStatsTxtRecord->nId;

    MonsterTreasureResult regResult = GetMonsterTreasure(monsters, adjustedNID, difficulty, 0, TCEx);
    MonsterTreasureResult champResult = GetMonsterTreasure(monsters, adjustedNID, difficulty, 1, TCEx);
    MonsterTreasureResult uniqResult = GetMonsterTreasure(monsters, adjustedNID, difficulty, 2, TCEx);
    MonsterTreasureResult superuniqResult = GetMonsterTreasureSU(superuniques, adjustedSID, difficulty, TCEx);

    // Base TC
    int tcCheckRegular = regResult.tcCheckIndex;
    int tcCheckChamp = champResult.tcCheckIndex;
    int tcCheckUnique = uniqResult.tcCheckIndex;
    int tcCheckSuperUnique = superuniqResult.tcCheckIndex;

    // Terror TC
    int indexRegular = (regResult.treasureIndex == -1) ? tcCheckRegular : regResult.treasureIndex;
    int indexChamp = (champResult.treasureIndex == -1) ? tcCheckChamp : champResult.treasureIndex;
    int indexUnique = (uniqResult.treasureIndex == -1) ? tcCheckUnique : uniqResult.treasureIndex;
    int indexSuperUnique = (superuniqResult.treasureIndex == -1) ? tcCheckSuperUnique : superuniqResult.treasureIndex;

    LogDebug(std::format("---------------------\nnTCId: {}, indexRegular: {}, indexChamp: {},  indexUnique: {}, indexSuperUnique: {},", nTCId, indexRegular, indexChamp, indexUnique, indexSuperUnique));

    if (nTCId == 0)
        oDropTCTest(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);

    if ((regResult.treasureIndex == -1 || champResult.treasureIndex == -1 || uniqResult.treasureIndex == -1) && superuniqResult.treasureIndex == -1)
        oDropTCTest(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);

    else
    {
        // Force Boss Drops
        if (pMonStatsTxtRecord->nId == 156 || pMonStatsTxtRecord->nId == 211 || pMonStatsTxtRecord->nId == 242 || pMonStatsTxtRecord->nId == 243 || pMonStatsTxtRecord->nId == 544)
        {
            nTCId = nTCId + (uniqResult.treasureIndex - tcCheckUnique);
            oDropTCTest(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);
            LogDebug(std::format("nTCId Applied to Monster: {}\n---------------------\n", nTCId));
        }
        else
        {
            if (pMonsterFlag & MONTYPEFLAG_SUPERUNIQUE)
                nTCId = nTCId + (superuniqResult.treasureIndex - tcCheckSuperUnique);
            else if (pMonsterFlag & (MONTYPEFLAG_CHAMPION | MONTYPEFLAG_POSSESSED | MONTYPEFLAG_GHOSTLY))
                nTCId = nTCId + (champResult.treasureIndex - tcCheckChamp);
            else if ((pMonsterFlag & MONTYPEFLAG_UNIQUE) || (cachedSettings.minionEquality && (pMonsterFlag & MONTYPEFLAG_MINION)))
                nTCId = nTCId + (uniqResult.treasureIndex - tcCheckUnique);
            else nTCId = nTCId + (regResult.treasureIndex - tcCheckRegular);

            LogDebug(std::format("nTCId Applied to Monster: {}\n---------------------\n", nTCId));
            oDropTCTest(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);
        }
    }

}

void UpdateActiveZoneInfoText(time_t currentUtc)
{
    g_ActiveZoneInfoText.clear();
    g_TerrorZoneData.activeLevels.clear();

    std::stringstream ss;

    for (const auto& zone : gDesecratedZones)
    {
        if (currentUtc < zone.start_time_utc || currentUtc > zone.end_time_utc)
            continue;

        int groupCount = static_cast<int>(zone.zones.size());
        if (groupCount == 0)
            continue;

        int cycleLengthMin = zone.terror_duration_min + zone.terror_break_min;
        if (cycleLengthMin <= 0)
            continue;

        int activeGroupIndex = g_ManualZoneGroupOverride;
        if (activeGroupIndex == -1)
        {
            int minutesSinceStart = static_cast<int>((currentUtc - zone.start_time_utc) / 60);
            int cyclePos = minutesSinceStart % (cycleLengthMin * groupCount);
            activeGroupIndex = cyclePos / cycleLengthMin;
        }
        else if (activeGroupIndex >= groupCount)
        {
            activeGroupIndex = groupCount - 1;
        }

        if (activeGroupIndex >= groupCount)
            continue;

        const ZoneGroup& activeGroup = zone.zones[activeGroupIndex];

        g_TerrorZoneData.cycleLengthMin = cycleLengthMin;
        g_TerrorZoneData.terrorDurationMin = zone.terror_duration_min;
        g_TerrorZoneData.groupCount = groupCount;
        g_TerrorZoneData.activeGroupIndex = activeGroupIndex;
        g_TerrorZoneData.zoneStartUtc = zone.start_time_utc;

        bool hasAllLevels = false;
        for (const auto& zl : activeGroup.levels)
        {
            if (zl.allLevels)
            {
                hasAllLevels = true;
                break;
            }
        }

        if (hasAllLevels)
        {
            ss << "All Levels have been Terrorized!\n";
            continue; // don't break, continue to next zone
        }

        // Build set of active level IDs
        std::unordered_set<int> activeLevelIds;
        for (const auto& zl : activeGroup.levels)
        {
            if (zl.level_id.has_value())
                activeLevelIds.insert(zl.level_id.value());
        }

        // Map level ID -> group index
        std::unordered_map<int, int> levelToGroupIndex;
        for (size_t gi = 0; gi < level_groups.size(); ++gi)
        {
            for (int lvlId : level_groups[gi].levels)
                levelToGroupIndex[lvlId] = static_cast<int>(gi);
        }

        // Determine fully present groups
        std::vector<bool> groupFullyPresent(level_groups.size(), false);
        for (size_t gi = 0; gi < level_groups.size(); ++gi)
        {
            const auto& grp = level_groups[gi];
            bool allPresent = !grp.levels.empty();
            for (int lvlId : grp.levels)
            {
                if (activeLevelIds.find(lvlId) == activeLevelIds.end())
                {
                    allPresent = false;
                    break;
                }
            }
            groupFullyPresent[gi] = allPresent;
        }

        // Keep track of levels already printed via a fully present group
        std::unordered_set<int> printedLevelIds;

        // Print fully present groups first
        for (size_t gi = 0; gi < level_groups.size(); ++gi)
        {
            if (groupFullyPresent[gi])
            {
                ss << level_groups[gi].name << "\n";
                // mark all levels in this group as printed
                for (int lvlId : level_groups[gi].levels)
                    printedLevelIds.insert(lvlId);
            }
        }

        // Print individual levels not part of fully present groups
        for (const auto& zl : activeGroup.levels)
        {
            g_TerrorZoneData.activeLevels.push_back(zl);

            if (!zl.level_id.has_value())
                continue;

            int lvlId = zl.level_id.value();

            if (printedLevelIds.find(lvlId) != printedLevelIds.end())
                continue; // already printed via a fully present group

            auto it = std::find_if(level_names.begin(), level_names.end(),
                [&](const LevelName& ln) { return ln.id == lvlId; });

            if (it != level_names.end())
                ss << it->name << "\n";
            else
                ss << "(Unknown Level ID: " << lvlId << ")\n";
        }
    }

    g_ActiveZoneInfoText = ss.str();
}

void __fastcall ApplyGhettoTerrorZone(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, D2UnitStrc* pUnit, int64_t* pMonRegData, D2MonStatsInitStrc* monStatsInit)
{
    time_t currentUtc = std::time(nullptr);
    g_ActiveZoneInfoText.clear();

    if (!pGame || !pRoom || !pUnit)
    {
        isTerrorized = false;
        return;
    }

    int levelId = GetLevelIdFromRoom(pRoom);
    if (levelId == -1)
    {
        isTerrorized = false;
        return;
    }

    D2UnitStrc* pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);
    if (!pUnitPlayer)
    {
        isTerrorized = false;
        return;
    }

    playerLevel = STATLIST_GetUnitStatSigned(pUnitPlayer, STAT_LEVEL, 0);
    int difficulty = GetPlayerDifficulty(pUnitPlayer);
    if (difficulty < 0 || difficulty > 2)
    {
        isTerrorized = false;
        return;
    }

    // Loop through desecrated zones
    for (const auto& zone : gDesecratedZones)
    {
        if (currentUtc < zone.start_time_utc || currentUtc > zone.end_time_utc)
            continue;

        int groupCount = static_cast<int>(zone.zones.size());
        if (groupCount == 0)
            continue;

        int cycleLengthMin = zone.terror_duration_min + zone.terror_break_min;
        if (cycleLengthMin <= 0)
            continue;

        int minutesSinceStart = static_cast<int>((currentUtc - zone.start_time_utc) / 60);
        int totalCycle = cycleLengthMin * groupCount;
        int cyclePos = minutesSinceStart % totalCycle;

        int activeGroupIndex = (g_ManualZoneGroupOverride == -1) ? (cyclePos / cycleLengthMin) : g_ManualZoneGroupOverride;

        int posWithinGroup = cyclePos % cycleLengthMin;
        if (activeGroupIndex < 0 || activeGroupIndex >= groupCount)
            continue;

        // Only active during terror duration, not break
        bool isInActivePhase = (posWithinGroup < zone.terror_duration_min);
        if (!isInActivePhase)
        {
            isTerrorized = false;
            continue;
        }

        const ZoneGroup& activeGroup = zone.zones[activeGroupIndex];

        // Store terror zone timing info
        g_TerrorZoneData.cycleLengthMin = cycleLengthMin;
        g_TerrorZoneData.terrorDurationMin = zone.terror_duration_min;
        g_TerrorZoneData.groupCount = groupCount;
        g_TerrorZoneData.activeGroupIndex = activeGroupIndex;
        g_TerrorZoneData.zoneStartUtc = zone.start_time_utc;
        int totalSecondsInPhase = g_TerrorZoneData.terrorDurationMin * 60;
        int secondsIntoPhase = (currentUtc - g_TerrorZoneData.zoneStartUtc) % (g_TerrorZoneData.cycleLengthMin * 60) % totalSecondsInPhase;
        int secondsRemaining = totalSecondsInPhase - secondsIntoPhase;
        int remainingMinutes = secondsRemaining / 60;
        int remainingSeconds = secondsRemaining % 60;

        double now = static_cast<double>(std::time(nullptr));
        UpdateActiveZoneInfoText(static_cast<time_t>(now));
        InitRandomStatsForAllMonsters(false);


        // Match level overrides
        const ZoneLevel* matchingZoneLevel = nullptr;
        for (const auto& zl : activeGroup.levels)
        {
            if (zl.allLevels || (zl.level_id.has_value() && zl.level_id.value() == levelId))
            {
                matchingZoneLevel = &zl;
                isTerrorized = true;

                ApplyMonsterDifficultyScaling(pUnit, zone, matchingZoneLevel, difficulty, playerLevel, playerCountGlobal, pGame);
                ApplyStatsToMonster(pUnit);
                break;
            }
        }

        if (!matchingZoneLevel)
        {
            isTerrorized = false;
            continue;
        }

        return; // success
    }

}

#pragma endregion

#pragma region - Terror Zone Controls



static void ToggleManualZoneGroupInternal(bool forward)
{
    std::string path = std::format("{0}/Mods/{1}/{1}.mpq/data/hd/global/excel/desecratedzones.json", GetExecutableDir(), GetModName());
    LoadDesecratedZones(path);
    double now = static_cast<double>(std::time(nullptr));

    if ((now - gLastManualToggleTime) <= 0.3)
        return;

    gLastManualToggleTime = now;
    int maxGroups = 0;

    // Find the first active zone with groups
    for (const auto& zone : gDesecratedZones)
    {
        if (now < zone.start_time_utc || now > zone.end_time_utc)
            continue;

        maxGroups = static_cast<int>(zone.zones.size());
        break;
    }

    if (maxGroups == 0)
        return; // no active groups

    if (g_ManualZoneGroupOverride == -1)
        g_ManualZoneGroupOverride = forward ? 0 : maxGroups - 1;
    else
    {
        if (forward)
        {
            g_ManualZoneGroupOverride++;
            if (g_ManualZoneGroupOverride >= maxGroups)
                g_ManualZoneGroupOverride = 0; // wrap to first
        }
        else
        {
            g_ManualZoneGroupOverride--;
            if (g_ManualZoneGroupOverride < 0)
                g_ManualZoneGroupOverride = maxGroups - 1; // wrap to last
        }
    }

    InitRandomStatsForAllMonsters(true);
    UpdateActiveZoneInfoText(static_cast<time_t>(now));
}

void CheckToggleForward()
{
    ToggleManualZoneGroupInternal(true);
}

void CheckToggleBackward()
{
    ToggleManualZoneGroupInternal(false);
}

std::string BuildTerrorZoneStatAdjustmentsText()
{
    if (initialized == false)
    {
        InitRandomStatsForAllMonsters(true);
        initialized = true;
    }

    if (gRandomStatsForMonsters.empty() || showStatAdjusts == false)
        return "";

    std::string finalText = "Stat Adjustments:\n";

    for (const auto& [statID, value] : gRandomStatsForMonsters)
    {
        std::string statFormat;
        auto it = gStatNames.find(statID);

        if (it != gStatNames.end())
            statFormat = it->second;
        else
            statFormat = "Unknown Stat (%d)";

        if (value < 0) {
            for (char& c : statFormat) {
                if (c == '+') { c = '-'; break; }
                if (c == '-') { c = '+'; break; }
            }
        }

        size_t pos = statFormat.find("%d");
        if (pos != std::string::npos)
        {
            statFormat.replace(pos, 2, std::to_string(std::abs(value)));
        }

        finalText += statFormat + "\n";
    }

    return finalText;
}

std::string BuildTerrorZoneInfoText()
{
    if (g_ActiveZoneInfoText.empty())
        return "";

    time_t currentUtc = std::time(nullptr);
    int remainingMinutes = 0;
    int remainingSeconds = 0;

    if (g_TerrorZoneData.cycleLengthMin > 0 &&
        g_TerrorZoneData.groupCount > 0 &&
        g_TerrorZoneData.activeGroupIndex >= 0)
    {
        int totalCycleMinutes = g_TerrorZoneData.cycleLengthMin * g_TerrorZoneData.groupCount;
        int minutesSinceStart = static_cast<int>((currentUtc - g_TerrorZoneData.zoneStartUtc) / 60);
        int cyclePos = minutesSinceStart % totalCycleMinutes;
        int positionInCycle = cyclePos % g_TerrorZoneData.cycleLengthMin;

        if (positionInCycle < g_TerrorZoneData.terrorDurationMin)
        {
            // In terror phase
            int totalSecondsInPhase = g_TerrorZoneData.terrorDurationMin * 60;
            int secondsIntoPhase = (currentUtc - g_TerrorZoneData.zoneStartUtc) % (g_TerrorZoneData.cycleLengthMin * 60) % totalSecondsInPhase;
            int secondsRemaining = totalSecondsInPhase - secondsIntoPhase;
            remainingMinutes = secondsRemaining / 60;
            remainingSeconds = secondsRemaining % 60;

            if (remainingSeconds == 0 && remainingMinutes == g_TerrorZoneData.terrorDurationMin)
                ToggleManualZoneGroupInternal(true);
        }
        else
        {
            // In break phase
            int totalSecondsInCycle = g_TerrorZoneData.cycleLengthMin * 60;
            int secondsIntoCycle = (currentUtc - g_TerrorZoneData.zoneStartUtc) % totalSecondsInCycle;
            int secondsRemaining = totalSecondsInCycle - secondsIntoCycle;
            remainingMinutes = secondsRemaining / 60;
            remainingSeconds = secondsRemaining % 60;

        }
    }

    std::string finalText = g_ActiveZoneInfoText +
        "Next Rotation In: " + std::to_string(remainingMinutes) + "m " +
        std::to_string(remainingSeconds) + "s\n";

    return finalText;
}

#pragma endregion

#pragma endregion

#pragma region Grail Tracker

#pragma region - Static/Structs

struct SetItemEntry {
    std::string name;
    int id;
    std::string setName;
    std::string code;
    bool enabled = false;
    bool collected = false;
};

struct UniqueItemEntry {
    int index;
    int id;
    std::string name;
    std::string code;
    bool enabled = false;
    bool collected = false;
};

std::vector<UniqueItemEntry> g_UniqueItems;
std::vector<SetItemEntry>    g_SetItems;
static std::unordered_set<std::string> g_ExcludedGrailItems;
static bool autoBackups = false;
static bool backupWithTimestamps = false;
static bool overwriteOldBackup = true;
static int backupIntervalMinutes = 10;
static bool triggerBackupNow = false;
static std::mutex backupMutex;
static char backupPath[260] = "C:\\MyGrailBackup";

#pragma endregion

#pragma region - RMD/Retail Grail Data

static UniqueItemEntry g_StaticUniqueItemsRMD[] = {
{ 0, 0, "Amulet of the Viper", "vip", false }, { 1, 1, "Staff of Kings", "msf", false }, { 2, 2, "Horadric Staff", "hst", false }, { 3, 3, "Hell Forge Hammer", "hfh", false }, { 4, 4, "KhalimFlail", "qf1", false }, { 5, 5, "SuperKhalimFlail", "qf2", false }, { 6, 6, "The Gnasher", "hax", false }, { 7, 7, "Deathspade", "axe", false }, { 8, 8, "Bladebone", "2ax", false }, { 9, 9, "Skull Splitter", "mpi", false },
{ 10, 10, "Rakescar", "wax", false }, { 11, 11, "Fechmars Axe", "lax", false }, { 12, 12, "Goreshovel", "bax", false }, { 13, 13, "The Chieftan", "btx", false }, { 14, 14, "Brainhew", "gax", false }, { 15, 15, "The Humongous", "gix", false }, { 16, 16, "Iros Torch", "wnd", false }, { 17, 17, "Maelstromwrath", "ywn", false }, { 18, 18, "Gravenspine", "bwn", false }, { 19, 19, "Umes Lament", "gwn", false },
{ 20, 20, "Felloak", "clb", false }, { 21, 21, "Knell Striker", "scp", false }, { 22, 22, "Rusthandle", "gsc", false }, { 23, 23, "Stormeye", "wsp", false }, { 24, 24, "Stoutnail", "spc", false }, { 25, 25, "Crushflange", "mac", false }, { 26, 26, "Bloodrise", "mst", false }, { 27, 27, "The Generals Tan Do Li Ga", "fla", false }, { 28, 28, "Ironstone", "whm", false }, { 29, 29, "Bonesnap", "mau", false },
{ 30, 30, "Steeldriver", "gma", false }, { 31, 31, "Rixots Keen", "ssd", false }, { 32, 32, "Blood Crescent", "scm", false }, { 33, 33, "Krintizs Skewer", "sbr", false }, { 34, 34, "Gleamscythe", "flc", false }, { 35, 35, "Light's Beacon", "crs", false }, { 36, 36, "Griswold's Edge", "bsd", false }, { 37, 37, "Hellplague", "lsd", false }, { 38, 38, "Culwens Point", "wsd", false }, { 39, 39, "Shadowfang", "2hs", false },
{ 40, 40, "Soulflay", "clm", false }, { 41, 41, "Kinemils Awl", "gis", false }, { 42, 42, "Blacktongue", "bsw", false }, { 43, 43, "Ripsaw", "flb", false }, { 44, 44, "The Patriarch", "gsd", false }, { 45, 45, "Gull", "dgr", false }, { 46, 46, "The Diggler", "dir", false }, { 47, 47, "The Jade Tan Do", "kri", false }, { 48, 48, "Irices Shard", "bld", false }, { 49, 49, "Shadow Strike", "tkf", false },
{ 50, 50, "Madawc's First", "tax", false }, { 51, 51, "Carefully", "bkf", false }, { 52, 52, "Ancient's Assualt", "bal", false }, { 53, 53, "Harpoonist's Training", "jav", false }, { 54, 54, "Glorious Point", "pil", false }, { 55, 55, "Not So", "ssp", false }, { 56, 56, "Double Trouble", "glv", false }, { 57, 57, "Straight Shot", "tsp", false }, { 58, 58, "The Dragon Chang", "spr", false }, { 59, 59, "Razortine", "tri", false },
{ 60, 60, "Bloodthief", "brn", false }, { 61, 61, "Lance of Yaggai", "spt", false }, { 62, 62, "The Tannr Gorerod", "pik", false }, { 63, 63, "Dimoaks Hew", "bar", false }, { 64, 64, "Steelgoad", "vou", false }, { 65, 65, "Soul Harvest", "scy", false }, { 66, 66, "The Battlebranch", "pax", false }, { 67, 67, "Woestave", "hal", false }, { 68, 68, "The Grim Reaper", "wsc", false }, { 69, 69, "Bane Ash", "sst", false },
{ 70, 70, "Serpent Lord", "lst", false }, { 71, 71, "Lazarus Spire", "cst", false }, { 72, 72, "The Salamander", "bst", false }, { 73, 73, "The Iron Jang Bong", "wst", false }, { 74, 74, "Pluckeye", "sbw", false }, { 75, 75, "Witherstring", "hbw", false }, { 76, 76, "Rimeraven", "lbw", false }, { 77, 77, "Piercerib", "cbw", false }, { 78, 78, "Pullspite", "sbb", false }, { 79, 79, "Wizendraw", "lbb", false },
{ 80, 80, "Hellclap", "swb", false }, { 81, 81, "Blastbark", "lwb", false }, { 82, 82, "Leadcrow", "lxb", false }, { 83, 83, "Ichorsting", "mxb", false }, { 84, 84, "Hellcast", "hxb", false }, { 85, 85, "Doomspittle", "rxb", false }, { 86, 86, "Coldkill", "9ha", false }, { 87, 87, "Butcher's Pupil", "9ax", false }, { 88, 88, "Islestrike", "92a", false }, { 89, 89, "Pompeii's Wrath", "9mp", false },
{ 90, 90, "Guardian Naga", "9wa", false }, { 91, 91, "Warlord's Trust", "9la", false }, { 92, 92, "Spellsteel", "9ba", false }, { 93, 93, "Stormrider", "9bt", false }, { 94, 94, "Boneslayer Blade", "9ga", false }, { 95, 95, "The Minataur", "9gi", false }, { 96, 96, "Suicide Branch", "9wn", false }, { 97, 97, "Carin Shard", "9yw", false }, { 98, 98, "Arm of King Leoric", "9bw", false }, { 99, 99, "Blackhand Key", "9gw", false },
{ 100, 100, "Dark Clan Crusher", "9sp", false }, { 101, 101, "Zakarum's Hand", "9sc", false }, { 102, 102, "The Fetid Sprinkler", "9qs", false }, { 103, 103, "Hand of Blessed Light", "9ws", false }, { 104, 104, "Fleshrender", "9cl", false }, { 105, 105, "Sureshrill Frost", "9ma", false }, { 106, 106, "Moonfall", "9mt", false }, { 107, 107, "Baezil's Vortex", "9fl", false }, { 108, 108, "Earthshaker", "9wh", false }, { 109, 109, "Bloodtree Stump", "9m9", false },
{ 110, 110, "The Gavel of Pain", "9gm", false }, { 111, 111, "Bloodletter", "9ss", false }, { 112, 112, "Coldsteel Eye", "9sm", false }, { 113, 113, "Hexfire", "9sb", false }, { 114, 114, "Blade of Ali Baba", "9fc", false }, { 115, 115, "Ginther's Rift", "9cr", false }, { 116, 116, "Headstriker", "9bs", false }, { 117, 117, "Plague Bearer", "9ls", false }, { 118, 118, "The Atlantian", "9wd", false }, { 119, 119, "Crainte Vomir", "92h", false },
{ 120, 120, "Bing Sz Wang", "9cm", false }, { 121, 121, "The Vile Husk", "9gs", false }, { 122, 122, "Cloudcrack", "9b9", false }, { 123, 123, "Todesfaelle Flamme", "9fb", false }, { 124, 124, "Swordguard", "9gd", false }, { 125, 125, "Spineripper", "9dg", false }, { 126, 126, "Heart Carver", "9di", false }, { 127, 127, "Blackbog's Sharp", "9kr", false }, { 128, 128, "Stormspike", "9bl", false }, { 129, 129, "Deathbit", "9tk", false },
{ 130, 130, "The Scalper", "9ta", false }, { 131, 131, "Constantly Waging", "9bk", false }, { 132, 132, "Realm Crusher", "9b8", false }, { 133, 133, "Quickening Strikes", "9ja", false }, { 134, 134, "Shrapnel Impact", "9pi", false }, { 135, 135, "Tempest Flash", "9s9", false }, { 136, 136, "Untethered", "9gl", false }, { 137, 137, "Unrelenting Will", "9ts", false }, { 138, 138, "The Impaler", "9sr", false }, { 139, 139, "Kelpie Snare", "9tr", false },
{ 140, 140, "Soulfeast Tine", "9br", false }, { 141, 141, "Hone Sundan", "9st", false }, { 142, 142, "Spire of Honor", "9p9", false }, { 143, 143, "The Meat Scraper", "9b7", false }, { 144, 144, "Blackleach Blade", "9vo", false }, { 145, 145, "Athena's Wrath", "9s8", false }, { 146, 146, "Pierre Tombale Couant", "9pa", false }, { 147, 147, "Husoldal Evo", "9h9", false }, { 148, 148, "Grim's Burning Dead", "9wc", false }, { 149, 149, "Razorswitch", "8ss", false },
{ 150, 150, "Ribcracker", "8ls", false }, { 151, 151, "Chromatic Ire", "8cs", false }, { 152, 152, "Warpspear", "8bs", false }, { 153, 153, "Skullcollector", "8ws", false }, { 154, 154, "Skystrike", "8sb", false }, { 155, 155, "Riphook", "8hb", false }, { 156, 156, "Kuko Shakaku", "8lb", false }, { 157, 157, "Endlesshail", "8cb", false }, { 158, 158, "Whichwild String", "8s8", false }, { 159, 159, "Cliffkiller", "8l8", false },
{ 160, 160, "Magewrath", "8sw", false }, { 161, 161, "Godstrike Arch", "8lw", false }, { 162, 162, "Langer Briser", "8lx", false }, { 163, 163, "Pus Spiter", "8mx", false }, { 164, 164, "Buriza-Do Kyanon", "8hx", false }, { 165, 165, "Demon Machine", "8rx", false }, { 166, 166, "Untrained Eye", "ktr", false }, { 167, 167, "Redemption", "wrb", false }, { 168, 168, "Ancient Hand", "axf", false }, { 169, 169, "Willbreaker", "ces", false },
{ 170, 170, "Skyfall Grip", "clw", false }, { 171, 171, "Oathbinder", "btl", false }, { 172, 172, "Pride's Fan", "skr", false }, { 173, 173, "Burning Sun", "9ar", false }, { 174, 174, "Severance", "9wb", false }, { 175, 175, "Hand of Madness", "9xf", false }, { 176, 176, "Vanquisher", "9cs", false }, { 177, 177, "Wind-Forged Blade", "9lw", false }, { 178, 178, "Bartuc's Cut-Throat", "9tw", false }, { 179, 179, "Void Ripper", "9qr", false },
{ 180, 180, "Soul-Forged Grip", "7ar", false }, { 181, 181, "Jadetalon", "7wb", false }, { 182, 182, "Malignant Touch", "7xf", false }, { 183, 183, "Shadowkiller", "7cs", false }, { 184, 184, "Firelizard's Talons", "7lw", false }, { 185, 185, "Viz-Jaq'taar Order", "7tw", false }, { 186, 186, "Mage Crusher", "7qr", false }, { 187, 187, "Razoredge", "7ha", false }, { 188, 188, "Glittering Crescent", "7ax", false }, { 189, 189, "Runemaster", "72a", false },
{ 190, 190, "Cranebeak", "7mp", false }, { 191, 191, "Deathcleaver", "7wa", false }, { 192, 192, "Blessed Beheader", "7la", false }, { 193, 193, "Ethereal Edge", "7ba", false }, { 194, 194, "Hellslayer", "7bt", false }, { 195, 195, "Messerschmidt's Reaver", "7ga", false }, { 196, 196, "Executioner's Justice", "7gi", false }, { 197, 197, "Bane Glow", "7wn", false }, { 198, 198, "Malthael Touch", "7yw", false }, { 199, 199, "Boneshade", "7bw", false },
{ 200, 200, "Deaths's Web", "7gw", false }, { 201, 201, "Nord's Tenderizer", "7cl", false }, { 202, 202, "Heaven's Light", "7sc", false }, { 203, 203, "The Redeemer", "7qs", false }, { 204, 204, "Ironward", "7ws", false }, { 205, 205, "Demonlimb", "7sp", false }, { 206, 206, "Stormlash", "7ma", false }, { 207, 207, "Baranar's Star", "7mt", false }, { 208, 208, "Horizon's Tornado", "7fl", false }, { 209, 209, "Schaefer's Hammer", "7wh", false },
{ 210, 210, "Windhammer", "7m7", false }, { 211, 211, "The Cranium Basher", "7gm", false }, { 212, 212, "Vows of Promise", "7ss", false }, { 213, 213, "Djinnslayer", "7sm", false }, { 214, 214, "Bloodmoon", "7sb", false }, { 215, 215, "Starward Fencer", "7fc", false }, { 216, 216, "Lightsabre", "7cr", false }, { 217, 217, "Azurewrath", "7bs", false }, { 218, 218, "Frostwind", "7ls", false }, { 219, 219, "Last Legend", "7wd", false },
{ 220, 220, "Oashi", "72h", false }, { 221, 221, "Gleam Rod", "7cm", false }, { 222, 222, "Flamebellow", "7gs", false }, { 223, 223, "Doombringer", "7b7", false }, { 224, 224, "Burning Bane", "7fb", false }, { 225, 225, "The Grandfather", "7gd", false }, { 226, 226, "Wizardspike", "7dg", false }, { 227, 227, "Rapid Strike", "7di", false }, { 228, 228, "Fleshripper", "7kr", false }, { 229, 229, "Ghostflame", "7bl", false },
{ 230, 230, "Sentinels Call", "7tk", false }, { 231, 231, "Gimmershred", "7ta", false }, { 232, 232, "Warshrike", "7bk", false }, { 233, 233, "Lacerator", "7b8", false }, { 234, 234, "Contemplation", "7ja", false }, { 235, 235, "Main Hand", "7pi", false }, { 236, 236, "Demon's Arch", "7s7", false }, { 237, 237, "Wraithflight", "7gl", false }, { 238, 238, "Gargoyle's Bite", "7ts", false }, { 239, 239, "Arioc's Needle", "7sr", false },
{ 240, 240, "Rock Piercer", "7tr", false }, { 241, 241, "Viperfork", "7br", false }, { 242, 242, "Flash Forward", "7st", false }, { 243, 243, "Steelpillar", "7p7", false }, { 244, 244, "Bonehew", "7o7", false }, { 245, 245, "Tundra Tamer", "7vo", false }, { 246, 246, "The Reaper's Toll", "7s8", false }, { 247, 247, "Tomb Reaver", "7pa", false }, { 248, 248, "Wind Shatter", "l17", false }, { 249, 249, "Bonespire", "7wc", false },
{ 250, 250, "Natures Intention", "6bs", false }, { 251, 251, "Thermite Quicksand", "6ls", false }, { 252, 252, "Ondal's Wisdom", "6cs", false }, { 253, 253, "Stone Crusher", "6bs", false }, { 254, 254, "Mang Song's Lesson", "6ws", false }, { 255, 255, "Cold Crow's Caw", "6sb", false }, { 256, 256, "Trembling Vortex", "6hb", false }, { 257, 257, "Corrupted String", "6lb", false }, { 258, 258, "Gyro Blaster", "6cb", false }, { 259, 259, "Underground", "6s7", false },
{ 260, 260, "Eaglehorn", "6l7", false }, { 261, 261, "Widowmaker", "6sw", false }, { 262, 262, "Windforce", "6lw", false }, { 263, 263, "Shadow Hunter", "6lx", false }, { 264, 264, "Amnestys Glare", "6mx", false }, { 265, 265, "Hellrack", "6hx", false }, { 266, 266, "Gutsiphon", "6rx", false }, { 267, 267, "Enlightener", "ob1", false }, { 268, 268, "Endothermic Stone", "ob2", false }, { 269, 269, "Sensor", "ob3", false },
{ 270, 270, "Lightning Rod", "ob4", false }, { 271, 271, "Energizer", "ob5", false }, { 272, 272, "The Artemis String", "am1", false }, { 273, 273, "Pinaka", "am2", false }, { 274, 274, "The Pain Producer", "am3", false }, { 275, 275, "The Poking Pike", "am4", false }, { 276, 276, "Skovos Striker", "am5", false }, { 277, 277, "Risen Phoenix", "ob6", false }, { 278, 278, "Glacial Oasis", "ob7", false }, { 279, 279, "Thunderous", "ob8", false },
{ 280, 280, "Magic", "ob9", false }, { 281, 281, "The Oculus", "oba", false }, { 282, 282, "Windraven", "am6", false }, { 283, 285, "Lycander's Aim", "am7", false }, { 284, 286, "Titan's Revenge", "ama", false }, { 285, 287, "Lycander's Flank", "am9", false }, { 286, 288, "Above All", "obb", false }, { 287, 289, "Eschuta's Temper", "obc", false }, { 288, 290, "Belphegor's Beating", "obd", false }, { 289, 291, "Tempest Firey", "obe", false },
{ 290, 292, "Death's Fathom", "obf", false }, { 291, 293, "Bloodraven's Charge", "amb", false }, { 292, 294, "Shredwind Hell", "amc", false }, { 293, 295, "Thunderstroke", "amf", false }, { 294, 296, "Stoneraven", "amd", false }, { 295, 297, "Biggin's Bonnet", "cap", false }, { 296, 298, "Tarnhelm", "skp", false }, { 297, 299, "Coif of Glory", "hlm", false }, { 298, 300, "Duskdeep", "fhl", false }, { 299, 301, "Howltusk", "ghm", false },
{ 300, 302, "Undead Crown", "crn", false }, { 301, 303, "The Face of Horror", "msk", false }, { 302, 304, "Greyform", "qui", false }, { 303, 305, "Blinkbats Form", "lea", false }, { 304, 306, "The Centurion", "hla", false }, { 305, 307, "Twitchthroe", "stu", false }, { 306, 308, "Darkglow", "rng", false }, { 307, 309, "Hawkmail", "scl", false }, { 308, 310, "Sparking Mail", "chn", false }, { 309, 311, "Venomsward", "brs", false },
{ 310, 312, "Iceblink", "spl", false }, { 311, 313, "Boneflesh", "plt", false }, { 312, 314, "Rockfleece", "fld", false }, { 313, 315, "Rattlecage", "gth", false }, { 314, 316, "Goldskin", "ful", false }, { 315, 317, "Victors Silk", "aar", false }, { 316, 318, "Heavenly Garb", "ltp", false }, { 317, 319, "Pelta Lunata", "buc", false }, { 318, 320, "Umbral Disk", "sml", false }, { 319, 321, "Stormguild", "lrg", false },
{ 320, 322, "Steelclash", "kit", false }, { 321, 323, "Bverrit Keep", "tow", false }, { 322, 324, "The Ward", "gts", false }, { 323, 325, "The Hand of Broc", "lgl", false }, { 324, 326, "Bloodfist", "vgl", false }, { 325, 327, "Chance Guards", "mgl", false }, { 326, 328, "Magefist", "tgl", false }, { 327, 329, "Frostburn", "hgl", false }, { 328, 330, "Hotspur", "lbt", false }, { 329, 331, "Gorefoot", "vbt", false },
{ 330, 332, "Treads of Cthon", "mbt", false }, { 331, 333, "Goblin Toe", "tbt", false }, { 332, 334, "Tearhaunch", "hbt", false }, { 333, 335, "Lenyms Cord", "lbl", false }, { 334, 336, "Snakecord", "vbl", false }, { 335, 337, "Nightsmoke", "mbl", false }, { 336, 338, "Goldwrap", "tbl", false }, { 337, 339, "Bladebuckle", "hbl", false }, { 338, 340, "Wormskull", "bhm", false }, { 339, 341, "Wall of the Eyeless", "bsh", false },
{ 340, 342, "Swordback Hold", "spk", false }, { 341, 343, "Peasent Crown", "xap", false }, { 342, 344, "Rockstopper", "xkp", false }, { 343, 345, "Stealskull", "xlm", false }, { 344, 346, "Darksight Helm", "xhl", false }, { 345, 347, "Valkyrie Wing", "xhm", false }, { 346, 348, "Crown of Thieves", "xrn", false }, { 347, 349, "Blackhorn's Face", "xsk", false }, { 348, 350, "The Spirit Shroud", "xui", false }, { 349, 351, "Skin of the Vipermagi", "xea", false },
{ 350, 352, "Skin of the Flayerd One", "xla", false }, { 351, 353, "Ironpelt", "xtu", false }, { 352, 354, "Spiritforge", "xng", false }, { 353, 355, "Crow Caw", "xcl", false }, { 354, 356, "Shaftstop", "xhn", false }, { 355, 357, "Duriel's Shell", "xrs", false }, { 356, 358, "Skullder's Ire", "xpl", false }, { 357, 359, "Guardian Angel", "xlt", false }, { 358, 360, "Toothrow", "xld", false }, { 359, 361, "Atma's Wail", "xth", false },
{ 360, 362, "Black Hades", "xul", false }, { 361, 363, "Corpsemourn", "xar", false }, { 362, 364, "Que-Hegan's Wisdon", "xtp", false }, { 363, 365, "Visceratuant", "xuc", false }, { 364, 366, "Mosers Blessed Circle", "xml", false }, { 365, 367, "Stormchaser", "xrg", false }, { 366, 368, "Tiamat's Rebuke", "xit", false }, { 367, 369, "Kerke's Sanctuary", "xow", false }, { 368, 370, "Radimant's Sphere", "xts", false }, { 369, 371, "Venom Grip", "xlg", false },
{ 370, 372, "Gravepalm", "xvg", false }, { 371, 373, "Ghoulhide", "xmg", false }, { 372, 374, "Lavagout", "xtg", false }, { 373, 375, "Hellmouth", "xhg", false }, { 374, 376, "Infernostride", "xlb", false }, { 375, 377, "Waterwalk", "xvb", false }, { 376, 378, "Silkweave", "xmb", false }, { 377, 379, "Wartraveler", "xtb", false }, { 378, 380, "Gorerider", "xhb", false }, { 379, 381, "String of Ears", "zlb", false },
{ 380, 382, "Razortail", "zvb", false }, { 381, 383, "Gloomstrap", "zmb", false }, { 382, 384, "Snowclash", "ztb", false }, { 383, 385, "Thudergod's Vigor", "zhb", false }, { 384, 386, "Vampiregaze", "xh9", false }, { 385, 387, "Lidless Wall", "xsh", false }, { 386, 388, "Lance Guard", "xpk", false }, { 387, 389, "Primal Power", "dr1", false }, { 388, 390, "Murder of Crows", "dr2", false }, { 389, 391, "Cheetah Stance", "dr3", false },
{ 390, 392, "Uproar", "dr4", false }, { 391, 393, "Flame Spirit", "dr5", false }, { 392, 394, "Toothless Maw", "ba1", false }, { 393, 395, "Darkfear", "ba2", false }, { 394, 396, "Thermal Shock", "ba3", false }, { 395, 397, "Nature's Protector", "ba4", false }, { 396, 398, "Reckless Fury", "ba5", false }, { 397, 399, "Sigurd's Staunch", "pa1", false }, { 398, 400, "Caster's Courage", "pa2", false }, { 399, 401, "Briar Patch", "pa3", false },
{ 400, 402, "Ricochet", "pa4", false }, { 401, 403, "Favored Path", "pa5", false }, { 402, 404, "Old Friend", "ne1", false }, { 403, 405, "Decomposed Leader", "ne2", false }, { 404, 406, "Tangled Fellow", "ne3", false }, { 405, 407, "Stubborn Stone", "ne4", false }, { 406, 408, "Spiked Dreamcatcher", "ne5", false }, { 407, 409, "Journeyman's Band", "ci0", false }, { 408, 410, "Hygieia's Purity", "ci1", false }, { 409, 411, "Kira's Guardian", "ci2", false },
{ 410, 412, "Griffon's Eye", "ci3", false }, { 411, 413, "Harlequin Crest", "uap", false }, { 412, 414, "Tarnhelm's Revenge", "ukp", false }, { 413, 415, "Steelshade", "ulm", false }, { 414, 416, "Veil of Steel", "uhl", false }, { 415, 417, "Nightwing's Veil", "uhm", false }, { 416, 418, "Crown of Ages", "urn", false }, { 417, 419, "Andariel's Visage", "usk", false }, { 418, 420, "Ormus' Robes", "uui", false }, { 419, 421, "Arcane Protector", "uea", false },
{ 420, 422, "Spell Splitter", "ula", false }, { 421, 423, "The Gladiator's Bane", "utu", false }, { 422, 424, "Balled Lightning", "ung", false }, { 423, 425, "Giant Crusher", "ucl", false }, { 424, 426, "Chained Lightning", "uhn", false }, { 425, 427, "Savitr's Garb", "urs", false }, { 426, 428, "Arkaine's Valor", "upl", false }, { 427, 429, "Strength Unleashed", "ult", false }, { 428, 430, "Leviathan", "uld", false }, { 429, 431, "Duality", "uth", false },
{ 430, 432, "Steel Carapice", "uul", false }, { 431, 433, "Tyrael's Might", "uar", false }, { 432, 434, "Spiritual Protector", "utp", false }, { 433, 435, "Cleansing Ward", "uuc", false }, { 434, 436, "Blackoak Shield", "uml", false }, { 435, 437, "Astrogha's Web", "urg", false }, { 436, 438, "Stormshield", "uit", false }, { 437, 439, "Medusa's Gaze", "uow", false }, { 438, 440, "Spirit Ward", "uts", false }, { 439, 441, "Indra's Mark", "ulg", false },
{ 440, 442, "Dracul's Grasp", "uvg", false }, { 441, 443, "Souldrain", "umg", false }, { 442, 444, "Carthas's Presence", "utg", false }, { 443, 445, "Steelrend", "uhg", false }, { 444, 446, "Mana Wyrm", "ulb", false }, { 445, 447, "Sandstorm Trek", "uvb", false }, { 446, 448, "Marrowwalk", "umb", false }, { 447, 449, "Crimson Shift", "utb", false }, { 448, 450, "Lelantus's Frenzy", "uhb", false }, { 449, 451, "Arachnid Mesh", "ulc", false },
{ 450, 452, "Nosferatu's Coil", "uvc", false }, { 451, 453, "Verdugo's Hearty Cord", "umc", false }, { 452, 454, "Magni's Warband", "utc", false }, { 453, 455, "Arcanist's Safeguard", "uhc", false }, { 454, 456, "Giantskull", "uh9", false }, { 455, 457, "Headhunter's Glory", "ush", false }, { 456, 458, "Spike Thorn", "upk", false }, { 457, 459, "Flame of Combat", "dr6", false }, { 458, 460, "Mystic Command", "dr7", false }, { 459, 461, "Rama's Protector", "dr8", false },
{ 460, 462, "Snow Spirit", "dr9", false }, { 461, 463, "Efreeti's Fury", "dra", false }, { 462, 464, "Combat Visor", "ba6", false }, { 463, 465, "Strength of Pride", "ba7", false }, { 464, 466, "Fighter's Stance", "ba8", false }, { 465, 467, "Piercing Cold", "ba9", false }, { 466, 468, "Arreat's Face", "baa", false }, { 467, 469, "Fara's Defender", "pa6", false }, { 468, 470, "Rakkis's Guard", "pa7", false }, { 469, 471, "Assaulter's Armament", "pa8", false },
{ 470, 472, "Herald of Zakarum", "pa9", false }, { 471, 473, "Blackheart's Barrage", "paa", false }, { 472, 474, "Mehtan's Carrion", "ne6", false }, { 473, 475, "Venom Storm", "ne7", false }, { 474, 476, "Bone Zone", "ne8", false }, { 475, 477, "Contagion", "ne9", false }, { 476, 478, "Homunculus", "nea", false }, { 477, 479, "Cerebus", "drb", false }, { 478, 480, "Pack Mentality", "drc", false }, { 479, 481, "Spiritkeeper", "drd", false },
{ 480, 482, "Cavern Dweller", "dre", false }, { 481, 483, "Jalal's Mane", "dra", false }, { 482, 484, "Berserker's Stance", "bab", false }, { 483, 485, "Wolfhowl", "bac", false }, { 484, 486, "Demonhorn's Edge", "bad", false }, { 485, 487, "Halaberd's Reign", "bae", false }, { 486, 488, "Warrior's Resolve", "baf", false }, { 487, 489, "Primordial Punisher", "pab", false }, { 488, 490, "Alma Negra", "pac", false }, { 489, 491, "Faithful Guardian", "pad", false },
{ 490, 492, "Dragonscale", "pae", false }, { 491, 493, "Shield of Forsaken Light", "paf", false }, { 492, 494, "Onikuma", "neb", false }, { 493, 495, "Bone Parade", "neg", false }, { 494, 496, "Elanuzuru", "ned", false }, { 495, 497, "Boneflame", "nee", false }, { 496, 498, "Darkforce Spawn", "nef", false }, { 497, 504, "Earthshifter", "Wp3", false }, { 498, 510, "Shadowdancer", "Ab3", false }, { 499, 513, "Templar's Might", "Bp3", false },
{ 500, 516, "Nature's Nurture", "Oa3", false }, { 501, 519, "Firebelr", "Vg3", false }, { 502, 520, "Flightless", "aqv", false }, { 503, 521, "Pinpoint", "aqv", false }, { 504, 522, "Nokozan Relic", "amu", false }, { 505, 523, "The Eye of Etlich", "amu", false }, { 506, 524, "The Mahim-Oak Curio", "amu", false }, { 507, 525, "Nagelring", "rin", false }, { 508, 526, "Manald Heal", "rin", false }, { 509, 527, "The Stone of Jordan", "rin", false },
{ 510, 528, "Bul Katho's Wedding Band", "rin", false }, { 511, 529, "The Cat's Eye", "amu", false }, { 512, 530, "The Rising Sun", "amu", false }, { 513, 531, "Crescent Moon", "amu", false }, { 514, 532, "Mara's Kaleidoscope", "amu", false }, { 515, 533, "Atma's Scarab", "amu", false }, { 516, 534, "Dwarf Star", "rin", false }, { 517, 535, "Raven Frost", "rin", false }, { 518, 536, "Highlord's Wrath", "amu", false }, { 519, 537, "Saracen's Chance", "amu", false },
{ 520, 538, "Nature's Peace", "rin", false }, { 521, 539, "Seraph's Hymn", "amu", false }, { 522, 540, "Wisp Projector", "rin", false }, { 523, 541, "Constricting Ring", "rin", false }, { 524, 542, "Gheed's Fortune", "cm3", false }, { 525, 543, "Annihilus", "cm1", false }, { 526, 544, "Carrion Wind", "rin", false }, { 527, 545, "Metalgrid", "amu", false }, { 528, 550, "Rainbow Facet1", "jew", false }, { 529, 551, "Rainbow Facet2", "jew", false },
{ 530, 552, "Rainbow Facet3", "jew", false }, { 531, 553, "Rainbow Facet4", "jew", false }, { 532, 554, "Rainbow Facet5", "jew", false }, { 533, 555, "Rainbow Facet6", "jew", false }, { 534, 556, "Hellfire Torch", "cm2", false }, { 535, 557, "Beacon of Hope", "BoH", false }, { 536, 558, "MythosLog", "y08", false }, { 537, 559, "Storage Bag", "Z01", false }, { 538, 560, "Magefist", "tgl", false }, { 539, 561, "Magefist", "tgl", false },
{ 540, 562, "Magefist", "tgl", false }, { 541, 563, "Magefist", "tgl", false }, { 542, 564, "IceClone Armor", "St1", false }, { 543, 565, "IceClone Armor2", "St2", false }, { 544, 566, "Hydra Master", "6ls", false }, { 545, 567, "Spiritual Savior", "utp", false }, { 546, 568, "IceClone Armor3", "St3", false }, { 547, 569, "Fletcher's Fury", "Ag1", false }, { 548, 570, "Indra's Guidance", "Ag2", false }, { 549, 572, "Robbin's Temple", "ci1", false },
{ 550, 573, "Trials Charm c1", "a59", false }, { 551, 574, "Trials Charm c2", "a60", false }, { 552, 575, "Trials Charm c3", "a61", false }, { 553, 576, "Trials Charm c4", "a62", false }, { 554, 577, "Trials Charm c5", "a63", false }, { 555, 578, "Trials Charm c6", "a64", false }, { 556, 579, "Trials Charm c7", "a65", false }, { 557, 580, "MegaCharm", "a66", false }, { 558, 581, "Spirit Striker", "aqv", false }, { 559, 582, "Aim of Indra", "aqv", false },
{ 560, 583, "Enchanted Flame", "aqv", false }, { 561, 584, "Mageflight", "aqv", false }, { 562, 585, "Energy Manipulator", "amu", false }, { 563, 586, "Trinity", "amu", false }, { 564, 587, "Quintessence", "amu", false }, { 565, 588, "Life Everlasting", "rin", false }, { 566, 589, "Hunter's Mark", "rin", false }, { 567, 590, "Unholy Commander", "cm3", false }, { 568, 591, "Tommy's Enlightener", "7qs", false }, { 569, 592, "Curtis's Fortifier", "uhc", false },
{ 570, 593, "Kurec's Pride", "drd", false }, { 571, 594, "Spiritual Guardian", "utp", false }, { 572, 595, "Blackmaw's Brutality", "xld", false }, { 573, 596, "Spencer's Dispenser", "oba", false }, { 574, 597, "Fletching of Frostbite", "aqv", false }, { 575, 598, "Healthy Breakfast", "cm1", false }, { 576, 599, "MythosLogAmazon", "y01", false }, { 577, 600, "MythosLogAssassin", "y02", false }, { 578, 601, "MythosLogBarbarian", "y03", false }, { 579, 602, "MythosLogDruid", "y04", false },
{ 580, 603, "MythosLogNecromancer", "y05", false }, { 581, 604, "MythosLogPaladin", "y06", false }, { 582, 605, "MythosLogSorceress", "y07", false }, { 583, 606, "Cola Cube", "cm1", false }, { 584, 607, "Soul Stompers", "umb", false }, { 585, 608, "MapReceipt01", "m27", false }, { 586, 609, "Kingdom's Heart", "uar", false }, { 587, 610, "Prismatic Facet", "j00", false }, { 588, 611, "Null Charm", "cm3", false }, { 589, 612, "SS Full Plate", "St4", false },
{ 590, 613, "SS Full Plate", "St5", false }, { 591, 614, "SS Full Plate", "St6", false }, { 592, 615, "SS Full Plate", "St7", false }, { 593, 616, "SS Full Plate", "St8", false }, { 594, 617, "SS Full Plate", "St9", false }, { 595, 618, "SS Full Plate", "St0", false }, { 596, 619, "Messerschmidt's Reaver SS", "Ss1", false }, { 597, 620, "Lightsabre SS", "Ss2", false }, { 598, 621, "Crainte Vomir", "Ss3", false }, { 599, 622, "Crainte Vomir", "Ss4", false },
{ 600, 623, "Spiritual Sentinel", "utp", false }, { 601, 624, "Spiritual Warden", "utp", false }, { 602, 626, "Harlequin Crest Legacy", "uap", false }, { 603, 627, "The Cat's Eye Legacy", "amu", false }, { 604, 628, "Arkaine's Valor Bugged", "upl", false }, { 605, 629, "String of Ears Bugged", "zlb", false }, { 606, 630, "Wizardspike Fused", "tgl", false }, { 607, 631, "Exsanguinate", "vgl", false }, { 608, 632, "Monar's Gale", "xts", false }, { 609, 633, "MythosLogAmazonA", "y34", false },
{ 610, 634, "MythosLogAssassinA", "y35", false }, { 611, 635, "MythosLogBarbarianA", "y36", false }, { 612, 636, "MythosLogDruidA", "y37", false }, { 613, 637, "MythosLogNecromancerA", "y38", false }, { 614, 638, "MythosLogPaladinA", "y39", false }, { 615, 639, "MythosLogSorceressA", "y40", false }, { 616, 640, "MythosLogAmazonB", "y34", false }, { 617, 641, "MythosLogAssassinB", "y35", false }, { 618, 642, "MythosLogBarbarianB", "y36", false }, { 619, 643, "MythosLogDruidB", "y37", false },
{ 620, 644, "MythosLogNecromancerB", "y38", false }, { 621, 645, "MythosLogPaladinB", "y39", false }, { 622, 646, "MythosLogSorceressB", "y40", false }, { 623, 647, "MythosLogAmazonC", "y34", false }, { 624, 648, "MythosLogAssassinC", "y35", false }, { 625, 649, "MythosLogBarbarianC", "y36", false }, { 626, 650, "MythosLogDruidC", "y37", false }, { 627, 651, "MythosLogNecromancerC", "y38", false }, { 628, 652, "MythosLogPaladinC", "y39", false }, { 629, 653, "MythosLogSorceressC", "y40", false },
{ 630, 654, "Black Cats Secret", "cm3", false }, { 631, 655, "Dustdevil", "l18", false }, { 632, 656, "Improvise", "6sw", false }, { 633, 657, "Ken'Juk's Blighted Visage", "usk", false }, { 634, 658, "Philios Prophecy", "amc", false }, { 635, 659, "Whisper", "cqv", false }, { 636, 660, "Dragon's Cinder", "cqv", false }, { 637, 661, "Serpent's Fangs", "cqv", false }, { 638, 662, "Valkyrie Wing Legacy", "xhm", false }, { 639, 663, "War Traveler Bugged", "xtb", false },
{ 640, 664, "Undead Crown Fused", "rin", false },
};

static SetItemEntry g_StaticSetItemsRMD[] = {
    { "Civerb's Ward", 0, "Civerb's Vestments", "lrg", false },     { "Civerb's Icon", 1, "Civerb's Vestments", "amu", false },     { "Civerb's Cudgel", 2, "Civerb's Vestments", "gsc", false },     { "Hsarus' Iron Heel", 3, "Hsarus' Defense", "mbt", false },     { "Hsarus' Iron Fist", 4, "Hsarus' Defense", "buc", false },     { "Hsarus' Iron Stay", 5, "Hsarus' Defense", "mbl", false },     { "Cleglaw's Tooth", 6, "Cleglaw's Brace", "lsd", false },     { "Cleglaw's Claw", 7, "Cleglaw's Brace", "sml", false },     { "Cleglaw's Pincers", 8, "Cleglaw's Brace", "mgl", false },     { "Iratha's Collar", 9, "Iratha's Finery", "amu", false },
    { "Iratha's Cuff", 10, "Iratha's Finery", "tgl", false },     { "Iratha's Coil", 11, "Iratha's Finery", "crn", false },     { "Iratha's Cord", 12, "Iratha's Finery", "tbl", false },     { "Isenhart's Lightbrand", 13, "Isenhart's Armory", "bsd", false },     { "Isenhart's Parry", 14, "Isenhart's Armory", "gts", false },     { "Isenhart's Case", 15, "Isenhart's Armory", "brs", false },     { "Isenhart's Horns", 16, "Isenhart's Armory", "fhl", false },     { "Vidala's Barb", 17, "Vidala's Rig", "lbb", false },     { "Vidala's Fetlock", 18, "Vidala's Rig", "tbt", false },     { "Vidala's Ambush", 19, "Vidala's Rig", "lea", false },
    { "Vidala's Snare", 20, "Vidala's Rig", "amu", false },     { "Milabrega's Orb", 21, "Milabrega's Regalia", "kit", false },     { "Milabrega's Rod", 22, "Milabrega's Regalia", "wsp", false },     { "Milabrega's Diadem", 23, "Milabrega's Regalia", "crn", false },     { "Milabrega's Robe", 24, "Milabrega's Regalia", "aar", false },     { "Cathan's Rule", 25, "Cathan's Traps", "bst", false },     { "Cathan's Mesh", 26, "Cathan's Traps", "chn", false },     { "Cathan's Visage", 27, "Cathan's Traps", "msk", false },     { "Cathan's Sigil", 28, "Cathan's Traps", "amu", false },     { "Cathan's Seal", 29, "Cathan's Traps", "rin", false },
    { "Tancred's Crowbill", 30, "Tancred's Battlegear", "mpi", false },     { "Tancred's Spine", 31, "Tancred's Battlegear", "ful", false },     { "Tancred's Hobnails", 32, "Tancred's Battlegear", "lbt", false },     { "Tancred's Weird", 33, "Tancred's Battlegear", "amu", false },     { "Tancred's Skull", 34, "Tancred's Battlegear", "bhm", false },     { "Sigon's Gage", 35, "Sigon's Complete Steel", "hgl", false },     { "Sigon's Visor", 36, "Sigon's Complete Steel", "ghm", false },     { "Sigon's Shelter", 37, "Sigon's Complete Steel", "gth", false },     { "Sigon's Sabot", 38, "Sigon's Complete Steel", "hbt", false },     { "Sigon's Wrap", 39, "Sigon's Complete Steel", "hbl", false },
    { "Sigon's Guard", 40, "Sigon's Complete Steel", "tow", false },     { "Infernal Cranium", 41, "Infernal Tools", "cap", false },     { "Infernal Torch", 42, "Infernal Tools", "gwn", false },     { "Infernal Sign", 43, "Infernal Tools", "tbl", false },     { "Berserker's Headgear", 44, "Berserker's Garb", "hlm", false },     { "Berserker's Hauberk", 45, "Berserker's Garb", "spl", false },     { "Berserker's Hatchet", 46, "Berserker's Garb", "2ax", false },     { "Death's Hand", 47, "Death's Disguise", "lgl", false },     { "Death's Guard", 48, "Death's Disguise", "lbl", false },     { "Death's Touch", 49, "Death's Disguise", "wsd", false },
    { "Angelic Sickle", 50, "Angelical Raiment", "sbr", false },     { "Angelic Mantle", 51, "Angelical Raiment", "rng", false },     { "Angelic Halo", 52, "Angelical Raiment", "rin", false },     { "Angelic Wings", 53, "Angelical Raiment", "amu", false },     { "Arctic Horn", 54, "Arctic Gear", "swb", false },     { "Arctic Furs", 55, "Arctic Gear", "qui", false },     { "Arctic Binding", 56, "Arctic Gear", "vbl", false },     { "Arctic Mitts", 57, "Arctic Gear", "tgl", false },     { "Arcanna's Sign", 58, "Arcanna's Tricks", "amu", false },     { "Arcanna's Deathwand", 59, "Arcanna's Tricks", "wst", false },
    { "Arcanna's Head", 60, "Arcanna's Tricks", "skp", false },     { "Arcanna's Flesh", 61, "Arcanna's Tricks", "ltp", false },     { "Natalya's Totem", 62, "Natalya's Odium", "xlm", false },     { "Natalya's Mark", 63, "Natalya's Odium", "7qr", false },     { "Natalya's Shadow", 64, "Natalya's Odium", "Ca2", false },     { "Natalya's Soul", 65, "Natalya's Odium", "xmb", false },     { "Aldur's Stony Gaze", 66, "Aldur's Watchtower", "dr8", false },     { "Aldur's Deception", 67, "Aldur's Watchtower", "uul", false },     { "Aldur's Gauntlet", 68, "Aldur's Watchtower", "9mt", false },     { "Aldur's Advance", 69, "Aldur's Watchtower", "xtb", false },
    { "Immortal King's Will", 70, "Immortal King", "ba5", false },     { "Immortal King's Soul Cage", 71, "Immortal King", "uar", false },     { "Immortal King's Detail", 72, "Immortal King", "zhb", false },     { "Immortal King's Forge", 73, "Immortal King", "xhg", false },     { "Immortal King's Pillar", 74, "Immortal King", "xhb", false },     { "Immortal King's Stone Crusher", 75, "Immortal King", "7m7", false },     { "Tal Rasha's Fire-Spun Cloth", 76, "Tal Rasha's Wrappings", "zmb", false },     { "Tal Rasha's Adjudication", 77, "Tal Rasha's Wrappings", "amu", false },     { "Tal Rasha's Lidless Eye", 78, "Tal Rasha's Wrappings", "oba", false },     { "Tal Rasha's Howling Wind", 79, "Tal Rasha's Wrappings", "uth", false },
    { "Tal Rasha's Horadric Crest", 80, "Tal Rasha's Wrappings", "xsk", false },     { "Griswold's Valor", 81, "Griswold's Legacy", "Pc3", false },     { "Griswold's Heart", 82, "Griswold's Legacy", "xar", false },     { "Griswolds's Redemption", 83, "Griswold's Legacy", "7ws", false },     { "Griswold's Honor", 84, "Griswold's Legacy", "paf", false },     { "Trang-Oul's Guise", 85, "Trang-Oul's Avatar", "uh9", false },     { "Trang-Oul's Scales", 86, "Trang-Oul's Avatar", "xul", false },     { "Trang-Oul's Wing", 87, "Trang-Oul's Avatar", "ne9", false },     { "Trang-Oul's Claws", 88, "Trang-Oul's Avatar", "xmg", false },     { "Trang-Oul's Girth", 89, "Trang-Oul's Avatar", "utc", false },
    { "M'avina's True Sight", 90, "M'avina's Battle Hymn", "ci3", false },     { "M'avina's Embrace", 91, "M'avina's Battle Hymn", "uld", false },     { "M'avina's Icy Clutch", 92, "M'avina's Battle Hymn", "xtg", false },     { "M'avina's Tenet", 93, "M'avina's Battle Hymn", "zvb", false },     { "M'avina's Caster", 94, "M'avina's Battle Hymn", "amc", false },     { "Telling of Beads", 95, "The Disciple", "amu", false },     { "Laying of Hands", 96, "The Disciple", "ulg", false },     { "Rite of Passage", 97, "The Disciple", "xlb", false },     { "Spiritual Custodian", 98, "The Disciple", "uui", false },     { "Credendum", 99, "The Disciple", "umc", false },
    { "Dangoon's Teaching", 100, "Heaven's Brethren", "7ma", false },     { "Heaven's Taebaek", 101, "Heaven's Brethren", "uts", false },     { "Haemosu's Adament", 102, "Heaven's Brethren", "xrs", false },     { "Ondal's Almighty", 103, "Heaven's Brethren", "uhm", false },     { "Guillaume's Face", 104, "Orphan's Call", "xhm", false },     { "Wilhelm's Pride", 105, "Orphan's Call", "ztb", false },     { "Magnus' Skin", 106, "Orphan's Call", "xvg", false },     { "Wihtstan's Guard", 107, "Orphan's Call", "xml", false },     { "Hwanin's Splendor", 108, "Hwanin's Majesty", "xrn", false },     { "Hwanin's Refuge", 109, "Hwanin's Majesty", "xcl", false },
    { "Hwanin's Seal", 110, "Hwanin's Majesty", "mbl", false },     { "Hwanin's Justice", 111, "Hwanin's Majesty", "9vo", false },     { "Sazabi's Cobalt Redeemer", 112, "Sazabi's Grand Tribute", "7ls", false },     { "Sazabi's Ghost Liberator", 113, "Sazabi's Grand Tribute", "upl", false },     { "Sazabi's Mental Sheath", 114, "Sazabi's Grand Tribute", "xhl", false },     { "Bul-Kathos' Sacred Charge", 115, "Bul-Kathos' Children", "7gd", false },     { "Bul-Kathos' Tribal Guardian", 116, "Bul-Kathos' Children", "7wd", false },     { "Cow King's Horns", 117, "Cow King's Leathers", "xap", false },     { "Cow King's Hide", 118, "Cow King's Leathers", "stu", false },     { "Cow King's Hoofs", 119, "Cow King's Leathers", "vbt", false },
    { "Naj's Puzzler", 120, "Naj's Ancient Set", "6cs", false },     { "Naj's Light Plate", 121, "Naj's Ancient Set", "ult", false },     { "Naj's Circlet", 122, "Naj's Ancient Set", "ci0", false },     { "McAuley's Paragon", 123, "McAuley's Folly", "cap", false },     { "McAuley's Riprap", 124, "McAuley's Folly", "vbt", false },     { "McAuley's Taboo", 125, "McAuley's Folly", "vgl", false },     { "McAuley's Superstition", 126, "McAuley's Folly", "bwn", false },     { "Vessel's Atonment", 127, "Holy Vessel", "Bp1", false },     { "Vessel's Fufillment", 128, "Holy Vessel", "pa3", false },     { "Vessel's Anointment", 129, "Holy Vessel", "Pc1", false },
    { "Vessel's Armament", 130, "Holy Vessel", "scp", false },     { "Pointed Justice", 131, "Majestic Lancer", "am5", false },     { "True Parry", 132, "Majestic Lancer", "lrg", false },     { "Solidarity", 133, "Majestic Lancer", "Zc1", false },     { "Island Shore", 134, "Skovos Storm", "am1", false },     { "Raging Seas", 135, "Skovos Storm", "vgl", false },     { "Eye of the Storm", 136, "Skovos Storm", "aqv", false },     { "Sturdy Garment", 137, "Wonder Wear", "zmb", false },     { "True Deflector", 138, "Wonder Wear", "xkp", false },     { "Encased Corset", 139, "Wonder Wear", "xtu", false },
    { "Silver Bracers", 140, "Wonder Wear", "xtg", false },     { "Outreach", 141, "Vizjerei Vocation", "Vg1", false },     { "Masterful Teachings", 142, "Vizjerei Vocation", "ob2", false },     { "Inner Focus", 143, "Vizjerei Vocation", "ci0", false },     { "Disruptor", 144, "Beyond Battlemage", "9cr", false },     { "Bursting Desire", 145, "Beyond Battlemage", "xit", false },     { "Underestimated", 146, "Beyond Battlemage", "xea", false },     { "Tundra Storm", 147, "Glacial Plains", "xsk", false },     { "Enduring Onslaught", 148, "Glacial Plains", "zlb", false },     { "Frozen Goliath", 149, "Glacial Plains", "xpk", false },
    { "Rathma's Reaper", 150, "Rathma's Calling", "9mp", false },     { "Rathma's Shelter", 151, "Rathma's Calling", "ush", false },     { "Rathma's Vestage", 152, "Rathma's Calling", "uh9", false },     { "Rathma's Fortress", 153, "Rathma's Calling", "uea", false },     { "Stacato's Sigil", 154, "Stacatomamba's Guidance", "rin", false },     { "Mamba's Circle", 155, "Stacatomamba's Guidance", "rin", false },     { "Kreigur's Will", 156, "Kreigur's Mastery", "72h", false },     { "Kreigur's Judgement", 157, "Kreigur's Mastery", "72h", false },     { "Kami", 158, "Scarlet Sukami", "7fb", false },     { "Su", 159, "Scarlet Sukami", "7b7", false },
    { "Ysenob's Blood", 160, "Mirrored Flames", "uhb", false },     { "Noertap's Pride", 161, "Mirrored Flames", "uhc", false },     { "Olbaid's Deceipt", 162, "Mirrored Flames", "uhg", false },     { "Gale Strength", 163, "Unstoppable Force", "7ts", false },     { "Assault Prowess", 164, "Unstoppable Force", "7ts", false },     { "Thirst for Blood", 165, "Underworld's Unrest", "umb", false },     { "Rotting Reaper", 166, "Underworld's Unrest", "mpi", false },     { "Siphon String", 167, "Underworld's Unrest", "uvc", false },     { "Crown of Cold", 168, "Elemental Blueprints", "ci3", false },     { "Blazing Band", 169, "Elemental Blueprints", "rin", false },
    { "Lightning Locket", 170, "Elemental Blueprints", "amu", false },     { "Brewing Storm", 171, "Raijin's Rebellion", "uts", false },     { "Charged Chaos", 172, "Raijin's Rebellion", "uld", false },     { "Electron Emitter", 173, "Raijin's Rebellion", "urn", false },     { "Achyls' Armament", 174, "Mikael's Toxicity", "uhn", false },     { "Pendant of Pestilence", 175, "Mikael's Toxicity", "amu", false },     { "Plague Protector", 176, "Mikael's Toxicity", "ush", false },     { "Meat Masher", 177, "Warrior's Wrath", "7gm", false },     { "Supreme Strength", 178, "Warrior's Wrath", "utg", false },     { "Repeating Reaper", 179, "Blessings of Artemis", "6lw", false },
    { "Fletcher's Friend", 180, "Blessings of Artemis", "Ag3", false },     { "Band of Brothers", 181, "Artio's Calling", "rin", false },     { "Grizzlepaw's Hide", 182, "Artio's Calling", "Gg3", false },     { "Animal Instinct", 183, "Artio's Calling", "umc", false },     { "Justitia's Anger", 184, "Justitia's Divinity", "7ws", false },     { "Justitia's Embrace", 185, "Justitia's Divinity", "paf", false },     { "Hand of Efreeti", 186, "Pulsing Presence", "ulg", false },     { "Morning Frost", 187, "Pulsing Presence", "uvb", false },     { "Thunderlord's Vision", 188, "Pulsing Presence", "usk", false },     { "Coil of Heaven", 189, "Celestial Caress", "rin", false },
    { "Band of Divinity", 190, "Celestial Caress", "rin", false },     { "Godly Locket", 191, "Celestial Caress", "amu", false },     { "Chains of Bondage", 192, "Breaker of Chains", "ci3", false },     { "Chains of Force", 193, "Breaker of Chains", "utp", false },     { "Night's Disguise", 194, "Silhouette of Silence", "Ca3", false },     { "Silent Stalkers", 195, "Silhouette of Silence", "Ab3", false },     { "Toxic Grasp", 196, "Silhouette of Silence", "uvg", false },     { "Blade Binding", 197, "Mangala's Teachings", "uvc", false },     { "Murderous Intent", 198, "Mangala's Teachings", "uap", false },     { "Band of Suffering", 199, "Sacrificial Trinity", "rin", false },
    { "Loop of Regret", 200, "Sacrificial Trinity", "rin", false },     { "Locket of Burden", 201, "Sacrificial Trinity", "amu", false },     { "Bulwark of Defiance", 202, "Plates of Protection", "uow", false },     { "Marauder's Mark", 203, "Plates of Protection", "7wa", false },     { "Girdle of Resilience", 204, "Plates of Protection", "uhc", false },     { "Crippling Conch", 205, "Black Tempest", "uhl", false },     { "Sub-Zero Sash", 206, "Black Tempest", "ulc", false },     { "Band of Permafrost", 207, "Black Tempest", "rin", false },     { "Morality", 208, "Memento Mori", "7pa", false },     { "Remembrance", 209, "Memento Mori", "uh9", false },
    { "Harbinger", 210, "Memento Mori", "uhn", false },     { "Promethium", 211, "Cascading Caldera", "amu", false },     { "Searing Step", 212, "Cascading Caldera", "uvb", false },     { "Flameward", 213, "Cascading Caldera", "gts", false },     { "Vortex1", 214, "Path of the Vortex", "7qr", false },     { "Maelstrom1", 215, "Path of the Vortex", "7qr", false },     { "Vortex2", 216, "Path of the Vortex2", "7gi", false },     { "Maelstrom2", 217, "Path of the Vortex2", "7gi", false },     { "Great Warrior", 218, "Blacklight", "baf", false },     { "Great Defender", 219, "Blacklight", "upk", false },
    { "Great Warrior2", 220, "Blacklight2", "urn", false },     { "Great Defender2", 221, "Blacklight2", "pae", false },
};

static UniqueItemEntry g_StaticUniqueItems[] = {
{ 0, 0, "The Gnasher", "hax", false }, { 1, 1, "Deathspade", "axe", false }, { 2, 2, "Bladebone", "2ax", false }, { 3, 3, "Mindrend", "mpi", false }, { 4, 4, "Rakescar", "wax", false }, { 5, 5, "Fechmars Axe", "lax", false }, { 6, 6, "Goreshovel", "bax", false }, { 7, 7, "The Chieftan", "btx", false }, { 8, 8, "Brainhew", "gax", false }, { 9, 9, "The Humongous", "gix", false },
{ 10, 10, "Iros Torch", "wnd", false }, { 11, 11, "Maelstromwrath", "ywn", false }, { 12, 12, "Gravenspine", "bwn", false }, { 13, 13, "Umes Lament", "gwn", false }, { 14, 14, "Felloak", "clb", false }, { 15, 15, "Knell Striker", "scp", false }, { 16, 16, "Rusthandle", "gsc", false }, { 17, 17, "Stormeye", "wsp", false }, { 18, 18, "Stoutnail", "spc", false }, { 19, 19, "Crushflange", "mac", false },
{ 20, 20, "Bloodrise", "mst", false }, { 21, 21, "The Generals Tan Do Li Ga", "fla", false }, { 22, 22, "Ironstone", "whm", false }, { 23, 23, "Bonesob", "mau", false }, { 24, 24, "Steeldriver", "gma", false }, { 25, 25, "Rixots Keen", "ssd", false }, { 26, 26, "Blood Crescent", "scm", false }, { 27, 27, "Krintizs Skewer", "sbr", false }, { 28, 28, "Gleamscythe", "flc", false }, { 29, 30, "Griswolds Edge", "bsd", false },
{ 30, 31, "Hellplague", "lsd", false }, { 31, 32, "Culwens Point", "wsd", false }, { 32, 33, "Shadowfang", "2hs", false }, { 33, 34, "Soulflay", "clm", false }, { 34, 35, "Kinemils Awl", "gis", false }, { 35, 36, "Blacktongue", "bsw", false }, { 36, 37, "Ripsaw", "flb", false }, { 37, 38, "The Patriarch", "gsd", false }, { 38, 39, "Gull", "dgr", false }, { 39, 40, "The Diggler", "dir", false },
{ 40, 41, "The Jade Tan Do", "kri", false }, { 41, 42, "Irices Shard", "bld", false }, { 42, 43, "The Dragon Chang", "spr", false }, { 43, 44, "Razortine", "tri", false }, { 44, 45, "Bloodthief", "brn", false }, { 45, 46, "Lance of Yaggai", "spt", false }, { 46, 47, "The Tannr Gorerod", "pik", false }, { 47, 48, "Dimoaks Hew", "bar", false }, { 48, 49, "Steelgoad", "vou", false }, { 49, 50, "Soul Harvest", "scy", false },
{ 50, 51, "The Battlebranch", "pax", false }, { 51, 52, "Woestave", "hal", false }, { 52, 53, "The Grim Reaper", "wsc", false }, { 53, 54, "Bane Ash", "sst", false }, { 54, 55, "Serpent Lord", "lst", false }, { 55, 56, "Lazarus Spire", "cst", false }, { 56, 57, "The Salamander", "bst", false }, { 57, 58, "The Iron Jang Bong", "wst", false }, { 58, 59, "Pluckeye", "sbw", false }, { 59, 60, "Witherstring", "hbw", false },
{ 60, 61, "Rimeraven", "lbw", false }, { 61, 62, "Piercerib", "cbw", false }, { 62, 63, "Pullspite", "sbb", false }, { 63, 64, "Wizendraw", "lbb", false }, { 64, 65, "Hellclap", "swb", false }, { 65, 66, "Blastbark", "lwb", false }, { 66, 67, "Leadcrow", "lxb", false }, { 67, 68, "Ichorsting", "mxb", false }, { 68, 69, "Hellcast", "hxb", false }, { 69, 70, "Doomspittle", "rxb", false },
{ 70, 71, "War Bonnet", "cap", false }, { 71, 72, "Tarnhelm", "skp", false }, { 72, 73, "Coif of Glory", "hlm", false }, { 73, 74, "Duskdeep", "fhl", false }, { 74, 75, "Wormskull", "bhm", false }, { 75, 76, "Howltusk", "ghm", false }, { 76, 77, "Undead Crown", "crn", false }, { 77, 78, "The Face of Horror", "msk", false }, { 78, 79, "Greyform", "qui", false }, { 79, 80, "Blinkbats Form", "lea", false },
{ 80, 81, "The Centurion", "hla", false }, { 81, 82, "Twitchthroe", "stu", false }, { 82, 83, "Darkglow", "rng", false }, { 83, 84, "Hawkmail", "scl", false }, { 84, 85, "Sparking Mail", "chn", false }, { 85, 86, "Venomsward", "brs", false }, { 86, 87, "Iceblink", "spl", false }, { 87, 88, "Boneflesh", "plt", false }, { 88, 89, "Rockfleece", "fld", false }, { 89, 90, "Rattlecage", "gth", false },
{ 90, 91, "Goldskin", "ful", false }, { 91, 92, "Victors Silk", "aar", false }, { 92, 93, "Heavenly Garb", "ltp", false }, { 93, 94, "Pelta Lunata", "buc", false }, { 94, 95, "Umbral Disk", "sml", false }, { 95, 96, "Stormguild", "lrg", false }, { 96, 97, "Wall of the Eyeless", "bsh", false }, { 97, 98, "Swordback Hold", "spk", false }, { 98, 99, "Steelclash", "kit", false }, { 99, 100, "Bverrit Keep", "tow", false },
{ 100, 101, "The Ward", "gts", false }, { 101, 102, "The Hand of Broc", "lgl", false }, { 102, 103, "Bloodfist", "vgl", false }, { 103, 104, "Chance Guards", "mgl", false }, { 104, 105, "Magefist", "tgl", false }, { 105, 106, "Frostburn", "hgl", false }, { 106, 107, "Hotspur", "lbt", false }, { 107, 108, "Gorefoot", "vbt", false }, { 108, 109, "Treads of Cthon", "mbt", false }, { 109, 110, "Goblin Toe", "tbt", false },
{ 110, 111, "Tearhaunch", "hbt", false }, { 111, 112, "Lenyms Cord", "lbl", false }, { 112, 113, "Snakecord", "vbl", false }, { 113, 114, "Nightsmoke", "mbl", false }, { 114, 115, "Goldwrap", "tbl", false }, { 115, 116, "Bladebuckle", "hbl", false }, { 116, 117, "Nokozan Relic", "amu", false }, { 117, 118, "The Eye of Etlich", "amu", false }, { 118, 119, "The Mahim-Oak Curio", "amu", false }, { 119, 120, "Nagelring", "rin", false },
{ 120, 121, "Manald Heal", "rin", false }, { 121, 122, "The Stone of Jordan", "rin", false }, { 122, 123, "Amulet of the Viper", "vip", false }, { 123, 124, "Staff of Kings", "msf", false }, { 124, 125, "Horadric Staff", "hst", false }, { 125, 126, "Hell Forge Hammer", "hfh", false }, { 126, 127, "KhalimFlail", "qf1", false }, { 127, 128, "SuperKhalimFlail", "qf2", false }, { 128, 129, "Coldkill", "9ha", false }, { 129, 130, "Butcher's Pupil", "9ax", false },
{ 130, 131, "Islestrike", "92a", false }, { 131, 132, "Pompe's Wrath", "9mp", false }, { 132, 133, "Guardian Naga", "9wa", false }, { 133, 134, "Warlord's Trust", "9la", false }, { 134, 135, "Spellsteel", "9ba", false }, { 135, 136, "Stormrider", "9bt", false }, { 136, 137, "Boneslayer Blade", "9ga", false }, { 137, 138, "The Minataur", "9gi", false }, { 138, 139, "Suicide Branch", "9wn", false }, { 139, 140, "Carin Shard", "9yw", false },
{ 140, 141, "Arm of King Leoric", "9bw", false }, { 141, 142, "Blackhand Key", "9gw", false }, { 142, 143, "Dark Clan Crusher", "9cl", false }, { 143, 144, "Zakarum's Hand", "9sc", false }, { 144, 145, "The Fetid Sprinkler", "9qs", false }, { 145, 146, "Hand of Blessed Light", "9ws", false }, { 146, 147, "Fleshrender", "9sp", false }, { 147, 148, "Sureshrill Frost", "9ma", false }, { 148, 149, "Moonfall", "9mt", false }, { 149, 150, "Baezil's Vortex", "9fl", false },
{ 150, 151, "Earthshaker", "9wh", false }, { 151, 152, "Bloodtree Stump", "9m9", false }, { 152, 153, "The Gavel of Pain", "9gm", false }, { 153, 154, "Bloodletter", "9ss", false }, { 154, 155, "Coldsteel Eye", "9sm", false }, { 155, 156, "Hexfire", "9sb", false }, { 156, 157, "Blade of Ali Baba", "9fc", false }, { 157, 158, "Ginther's Rift", "9cr", false }, { 158, 159, "Headstriker", "9bs", false }, { 159, 160, "Plague Bearer", "9ls", false },
{ 160, 161, "The Atlantian", "9wd", false }, { 161, 162, "Crainte Vomir", "92h", false }, { 162, 163, "Bing Sz Wang", "9cm", false }, { 163, 164, "The Vile Husk", "9gs", false }, { 164, 165, "Cloudcrack", "9b9", false }, { 165, 166, "Todesfaelle Flamme", "9fb", false }, { 166, 167, "Swordguard", "9gd", false }, { 167, 168, "Spineripper", "9dg", false }, { 168, 169, "Heart Carver", "9di", false }, { 169, 170, "Blackbog's Sharp", "9kr", false },
{ 170, 171, "Stormspike", "9bl", false }, { 171, 172, "The Impaler", "9sr", false }, { 172, 173, "Kelpie Snare", "9tr", false }, { 173, 174, "Soulfeast Tine", "9br", false }, { 174, 175, "Hone Sundan", "9st", false }, { 175, 176, "Spire of Honor", "9p9", false }, { 176, 177, "The Meat Scraper", "9b7", false }, { 177, 178, "Blackleach Blade", "9vo", false }, { 178, 179, "Athena's Wrath", "9s8", false }, { 179, 180, "Pierre Tombale Couant", "9pa", false },
{ 180, 181, "Husoldal Evo", "9h9", false }, { 181, 182, "Grim's Burning Dead", "9wc", false }, { 182, 183, "Razorswitch", "8ss", false }, { 183, 184, "Ribcracker", "8ls", false }, { 184, 185, "Chromatic Ire", "8cs", false }, { 185, 186, "Warpspear", "8bs", false }, { 186, 187, "Skullcollector", "8ws", false }, { 187, 188, "Skystrike", "8sb", false }, { 188, 189, "Riphook", "8hb", false }, { 189, 190, "Kuko Shakaku", "8lb", false },
{ 190, 191, "Endlesshail", "8cb", false }, { 191, 192, "Whichwild String", "8s8", false }, { 192, 193, "Cliffkiller", "8l8", false }, { 193, 194, "Magewrath", "8sw", false }, { 194, 195, "Godstrike Arch", "8lw", false }, { 195, 196, "Langer Briser", "8lx", false }, { 196, 197, "Pus Spiter", "8mx", false }, { 197, 198, "Buriza-Do Kyanon", "8hx", false }, { 198, 199, "Demon Machine", "8rx", false }, { 199, 201, "Peasent Crown", "xap", false },
{ 200, 202, "Rockstopper", "xkp", false }, { 201, 203, "Stealskull", "xlm", false }, { 202, 204, "Darksight Helm", "xhl", false }, { 203, 205, "Valkiry Wing", "xhm", false }, { 204, 206, "Crown of Thieves", "xrn", false }, { 205, 207, "Blackhorn's Face", "xsk", false }, { 206, 208, "Vampiregaze", "xh9", false }, { 207, 209, "The Spirit Shroud", "xui", false }, { 208, 210, "Skin of the Vipermagi", "xea", false }, { 209, 211, "Skin of the Flayerd One", "xla", false },
{ 210, 212, "Ironpelt", "xtu", false }, { 211, 213, "Spiritforge", "xng", false }, { 212, 214, "Crow Caw", "xcl", false }, { 213, 215, "Shaftstop", "xhn", false }, { 214, 216, "Duriel's Shell", "xrs", false }, { 215, 217, "Skullder's Ire", "xpl", false }, { 216, 218, "Guardian Angel", "xlt", false }, { 217, 219, "Toothrow", "xld", false }, { 218, 220, "Atma's Wail", "xth", false }, { 219, 221, "Black Hades", "xul", false },
{ 220, 222, "Corpsemourn", "xar", false }, { 221, 223, "Que-Hegan's Wisdon", "xtp", false }, { 222, 224, "Visceratuant", "xuc", false }, { 223, 225, "Mosers Blessed Circle", "xml", false }, { 224, 226, "Stormchaser", "xrg", false }, { 225, 227, "Tiamat's Rebuke", "xit", false }, { 226, 228, "Kerke's Sanctuary", "xow", false }, { 227, 229, "Radimant's Sphere", "xts", false }, { 228, 230, "Lidless Wall", "xsh", false }, { 229, 231, "Lance Guard", "xpk", false },
{ 230, 232, "Venom Grip", "xlg", false }, { 231, 233, "Gravepalm", "xvg", false }, { 232, 234, "Ghoulhide", "xmg", false }, { 233, 235, "Lavagout", "xtg", false }, { 234, 236, "Hellmouth", "xhg", false }, { 235, 237, "Infernostride", "xlb", false }, { 236, 238, "Waterwalk", "xvb", false }, { 237, 239, "Silkweave", "xmb", false }, { 238, 240, "Wartraveler", "xtb", false }, { 239, 241, "Gorerider", "xhb", false },
{ 240, 242, "String of Ears", "zlb", false }, { 241, 243, "Razortail", "zvb", false }, { 242, 244, "Gloomstrap", "zmb", false }, { 243, 245, "Snowclash", "ztb", false }, { 244, 246, "Thudergod's Vigor", "zhb", false }, { 245, 248, "Harlequin Crest", "uap", false }, { 246, 249, "Veil of Steel", "uhm", false }, { 247, 250, "The Gladiator's Bane", "utu", false }, { 248, 251, "Arkaine's Valor", "upl", false }, { 249, 252, "Blackoak Shield", "uml", false },
{ 250, 253, "Stormshield", "uit", false }, { 251, 254, "Hellslayer", "7bt", false }, { 252, 255, "Messerschmidt's Reaver", "7ga", false }, { 253, 256, "Baranar's Star", "7mt", false }, { 254, 257, "Schaefer's Hammer", "7wh", false }, { 255, 258, "The Cranium Basher", "7gm", false }, { 256, 259, "Lightsabre", "7cr", false }, { 257, 260, "Doombringer", "7b7", false }, { 258, 261, "The Grandfather", "7gd", false }, { 259, 262, "Wizardspike", "7dg", false },
{ 260, 264, "Stormspire", "7wc", false }, { 261, 265, "Eaglehorn", "6l7", false }, { 262, 266, "Windforce", "6lw", false }, { 263, 268, "Bul Katho's Wedding Band", "rin", false }, { 264, 269, "The Cat's Eye", "amu", false }, { 265, 270, "The Rising Sun", "amu", false }, { 266, 271, "Crescent Moon", "amu", false }, { 267, 272, "Mara's Kaleidoscope", "amu", false }, { 268, 273, "Atma's Scarab", "amu", false }, { 269, 274, "Dwarf Star", "rin", false },
{ 270, 275, "Raven Frost", "rin", false }, { 271, 276, "Highlord's Wrath", "amu", false }, { 272, 277, "Saracen's Chance", "amu", false }, { 273, 279, "Arreat's Face", "baa", false }, { 274, 280, "Homunculus", "nea", false }, { 275, 281, "Titan's Revenge", "ama", false }, { 276, 282, "Lycander's Aim", "am7", false }, { 277, 283, "Lycander's Flank", "am9", false }, { 278, 284, "The Oculus", "oba", false }, { 279, 285, "Herald of Zakarum", "pa9", false },
{ 280, 286, "Cutthroat1", "9tw", false }, { 281, 287, "Jalal's Mane", "dra", false }, { 282, 288, "The Scalper", "9ta", false }, { 283, 289, "Bloodmoon", "7sb", false }, { 284, 290, "Djinnslayer", "7sm", false }, { 285, 291, "Deathbit", "9tk", false }, { 286, 292, "Warshrike", "7bk", false }, { 287, 293, "Gutsiphon", "6rx", false }, { 288, 294, "Razoredge", "7ha", false }, { 289, 296, "Demonlimb", "7sp", false },
{ 290, 297, "Steelshade", "ulm", false }, { 291, 298, "Tomb Reaver", "7pa", false }, { 292, 299, "Deaths's Web", "7gw", false }, { 293, 300, "Nature's Peace", "rin", false }, { 294, 301, "Azurewrath", "7cr", false }, { 295, 302, "Seraph's Hymn", "amu", false }, { 296, 304, "Fleshripper", "7kr", false }, { 297, 306, "Horizon's Tornado", "7fl", false }, { 298, 307, "Stone Crusher", "7wh", false }, { 299, 308, "Jadetalon", "7wb", false },
{ 300, 309, "Shadowdancer", "uhb", false }, { 301, 310, "Cerebus", "drb", false }, { 302, 311, "Tyrael's Might", "uar", false }, { 303, 312, "Souldrain", "umg", false }, { 304, 313, "Runemaster", "72a", false }, { 305, 314, "Deathcleaver", "7wa", false }, { 306, 315, "Executioner's Justice", "7gi", false }, { 307, 316, "Stoneraven", "amd", false }, { 308, 317, "Leviathan", "uld", false }, { 309, 319, "Wisp", "rin", false },
{ 310, 320, "Gargoyle's Bite", "7ts", false }, { 311, 321, "Lacerator", "7b8", false }, { 312, 322, "Mang Song's Lesson", "6ws", false }, { 313, 323, "Viperfork", "7br", false }, { 314, 324, "Ethereal Edge", "7ba", false }, { 315, 325, "Demonhorn's Edge", "bad", false }, { 316, 326, "The Reaper's Toll", "7s8", false }, { 317, 327, "Spiritkeeper", "drd", false }, { 318, 328, "Hellrack", "6hx", false }, { 319, 329, "Alma Negra", "pac", false },
{ 320, 330, "Darkforge Spawn", "nef", false }, { 321, 331, "Widowmaker", "6sw", false }, { 322, 332, "Bloodraven's Charge", "amb", false }, { 323, 333, "Ghostflame", "7bl", false }, { 324, 334, "Shadowkiller", "7cs", false }, { 325, 335, "Gimmershred", "7ta", false }, { 326, 336, "Griffon's Eye", "ci3", false }, { 327, 337, "Windhammer", "7m7", false }, { 328, 338, "Thunderstroke", "amf", false }, { 329, 340, "Demon's Arch", "7s7", false },
{ 330, 341, "Boneflame", "nee", false }, { 331, 342, "Steelpillar", "7p7", false }, { 332, 343, "Nightwing's Veil", "uhm", false }, { 333, 344, "Crown of Ages", "urn", false }, { 334, 345, "Andariel's Visage", "usk", false }, { 335, 347, "Dragonscale", "pae", false }, { 336, 348, "Steel Carapice", "uul", false }, { 337, 349, "Medusa's Gaze", "uow", false }, { 338, 350, "Ravenlore", "dre", false }, { 339, 351, "Boneshade", "7bw", false },
{ 340, 353, "Flamebellow", "7gs", false }, { 341, 354, "Fathom", "obf", false }, { 342, 355, "Wolfhowl", "bac", false }, { 343, 356, "Spirit Ward", "uts", false }, { 344, 357, "Kira's Guardian", "ci2", false }, { 345, 358, "Ormus' Robes", "uui", false }, { 346, 359, "Gheed's Fortune", "cm3", false }, { 347, 360, "Stormlash", "7fl", false }, { 348, 361, "Halaberd's Reign", "bae", false }, { 349, 363, "Spike Thorn", "upk", false },
{ 350, 364, "Dracul's Grasp", "uvg", false }, { 351, 365, "Frostwind", "7ls", false }, { 352, 366, "Templar's Might", "uar", false }, { 353, 367, "Eschuta's temper", "obc", false }, { 354, 368, "Firelizard's Talons", "7lw", false }, { 355, 369, "Sandstorm Trek", "uvb", false }, { 356, 370, "Marrowwalk", "umb", false }, { 357, 371, "Heaven's Light", "7sc", false }, { 358, 373, "Arachnid Mesh", "ulc", false }, { 359, 374, "Nosferatu's Coil", "uvc", false },
{ 360, 375, "Metalgrid", "amu", false }, { 361, 376, "Verdugo's Hearty Cord", "umc", false }, { 362, 378, "Carrion Wind", "rin", false }, { 363, 379, "Giantskull", "uh9", false }, { 364, 380, "Ironward", "7ws", false }, { 365, 381, "Annihilus", "cm1", false }, { 366, 382, "Arioc's Needle", "7sr", false }, { 367, 383, "Cranebeak", "7mp", false }, { 368, 384, "Nord's Tenderizer", "7cl", false }, { 369, 385, "Earthshifter", "7gm", false },
{ 370, 386, "Wraithflight", "7gl", false }, { 371, 387, "Bonehew", "7o7", false }, { 372, 388, "Ondal's Wisdom", "6cs", false }, { 373, 389, "The Reedeemer", "7sc", false }, { 374, 390, "Headhunter's Glory", "ush", false }, { 375, 391, "Steelrend", "uhg", false }, { 376, 392, "Rainbow Facet", "jew", false }, { 377, 393, "Rainbow Facet", "jew", false }, { 378, 394, "Rainbow Facet", "jew", false }, { 379, 395, "Rainbow Facet", "jew", false },
{ 380, 396, "Rainbow Facet", "jew", false }, { 381, 397, "Rainbow Facet", "jew", false }, { 382, 398, "Rainbow Facet", "jew", false }, { 383, 399, "Rainbow Facet", "jew", false }, { 384, 400, "Hellfire Torch", "cm2", false }, { 385, 401, "Cold Rupture", "cm3", false }, { 386, 402, "Flame Rift", "cm3", false }, { 387, 403, "Crack of the Heavens", "cm3", false }, { 388, 404, "Rotting Fissure", "cm3", false }, { 389, 405, "Bone Break", "cm3", false },
{ 390, 406, "Black Cleft", "cm3", false },
};

static SetItemEntry g_StaticSetItems[] = {
    { "Civerb's Ward", 0, "Civerb's Vestments", "lrg", false },     { "Civerb's Icon", 1, "Civerb's Vestments", "amu", false },     { "Civerb's Cudgel", 2, "Civerb's Vestments", "gsc", false },     { "Hsarus' Iron Heel", 3, "Hsarus' Defense", "mbt", false },     { "Hsarus' Iron Fist", 4, "Hsarus' Defense", "buc", false },     { "Hsarus' Iron Stay", 5, "Hsarus' Defense", "mbl", false },     { "Cleglaw's Tooth", 6, "Cleglaw's Brace", "lsd", false },     { "Cleglaw's Claw", 7, "Cleglaw's Brace", "sml", false },     { "Cleglaw's Pincers", 8, "Cleglaw's Brace", "mgl", false },     { "Iratha's Collar", 9, "Iratha's Finery", "amu", false },
    { "Iratha's Cuff", 10, "Iratha's Finery", "tgl", false },     { "Iratha's Coil", 11, "Iratha's Finery", "crn", false },     { "Iratha's Cord", 12, "Iratha's Finery", "tbl", false },     { "Isenhart's Lightbrand", 13, "Isenhart's Armory", "bsd", false },     { "Isenhart's Parry", 14, "Isenhart's Armory", "gts", false },     { "Isenhart's Case", 15, "Isenhart's Armory", "brs", false },     { "Isenhart's Horns", 16, "Isenhart's Armory", "fhl", false },     { "Vidala's Barb", 17, "Vidala's Rig", "lbb", false },     { "Vidala's Fetlock", 18, "Vidala's Rig", "tbt", false },     { "Vidala's Ambush", 19, "Vidala's Rig", "lea", false },
    { "Vidala's Snare", 20, "Vidala's Rig", "amu", false },     { "Milabrega's Orb", 21, "Milabrega's Regalia", "kit", false },     { "Milabrega's Rod", 22, "Milabrega's Regalia", "wsp", false },     { "Milabrega's Diadem", 23, "Milabrega's Regalia", "crn", false },     { "Milabrega's Robe", 24, "Milabrega's Regalia", "aar", false },     { "Cathan's Rule", 25, "Cathan's Traps", "bst", false },     { "Cathan's Mesh", 26, "Cathan's Traps", "chn", false },     { "Cathan's Visage", 27, "Cathan's Traps", "msk", false },     { "Cathan's Sigil", 28, "Cathan's Traps", "amu", false },     { "Cathan's Seal", 29, "Cathan's Traps", "rin", false },
    { "Tancred's Crowbill", 30, "Tancred's Battlegear", "mpi", false },     { "Tancred's Spine", 31, "Tancred's Battlegear", "ful", false },     { "Tancred's Hobnails", 32, "Tancred's Battlegear", "lbt", false },     { "Tancred's Weird", 33, "Tancred's Battlegear", "amu", false },     { "Tancred's Skull", 34, "Tancred's Battlegear", "bhm", false },     { "Sigon's Gage", 35, "Sigon's Complete Steel", "hgl", false },     { "Sigon's Visor", 36, "Sigon's Complete Steel", "ghm", false },     { "Sigon's Shelter", 37, "Sigon's Complete Steel", "gth", false },     { "Sigon's Sabot", 38, "Sigon's Complete Steel", "hbt", false },     { "Sigon's Wrap", 39, "Sigon's Complete Steel", "hbl", false },
    { "Sigon's Guard", 40, "Sigon's Complete Steel", "tow", false },     { "Infernal Cranium", 41, "Infernal Tools", "cap", false },     { "Infernal Torch", 42, "Infernal Tools", "gwn", false },     { "Infernal Sign", 43, "Infernal Tools", "tbl", false },     { "Berserker's Headgear", 44, "Berserker's Garb", "hlm", false },     { "Berserker's Hauberk", 45, "Berserker's Garb", "spl", false },     { "Berserker's Hatchet", 46, "Berserker's Garb", "2ax", false },     { "Death's Hand", 47, "Death's Disguise", "lgl", false },     { "Death's Guard", 48, "Death's Disguise", "lbl", false },     { "Death's Touch", 49, "Death's Disguise", "wsd", false },
    { "Angelic Sickle", 50, "Angelical Raiment", "sbr", false },     { "Angelic Mantle", 51, "Angelical Raiment", "rng", false },     { "Angelic Halo", 52, "Angelical Raiment", "rin", false },     { "Angelic Wings", 53, "Angelical Raiment", "amu", false },     { "Arctic Horn", 54, "Arctic Gear", "swb", false },     { "Arctic Furs", 55, "Arctic Gear", "qui", false },     { "Arctic Binding", 56, "Arctic Gear", "vbl", false },     { "Arctic Mitts", 57, "Arctic Gear", "tgl", false },     { "Arcanna's Sign", 58, "Arcanna's Tricks", "amu", false },     { "Arcanna's Deathwand", 59, "Arcanna's Tricks", "wst", false },
    { "Arcanna's Head", 60, "Arcanna's Tricks", "skp", false },     { "Arcanna's Flesh", 61, "Arcanna's Tricks", "ltp", false },     { "Natalya's Totem", 62, "Natalya's Odium", "xh9", false },     { "Natalya's Mark", 63, "Natalya's Odium", "7qr", false },     { "Natalya's Shadow", 64, "Natalya's Odium", "ucl", false },     { "Natalya's Soul", 65, "Natalya's Odium", "xmb", false },     { "Aldur's Stony Gaze", 66, "Aldur's Watchtower", "dr8", false },     { "Aldur's Deception", 67, "Aldur's Watchtower", "uul", false },     { "Aldur's Gauntlet", 68, "Aldur's Watchtower", "9mt", false },     { "Aldur's Advance", 69, "Aldur's Watchtower", "xtb", false },
    { "Immortal King's Will", 70, "Immortal King", "ba5", false },     { "Immortal King's Soul Cage", 71, "Immortal King", "uar", false },     { "Immortal King's Detail", 72, "Immortal King", "zhb", false },     { "Immortal King's Forge", 73, "Immortal King", "xhg", false },     { "Immortal King's Pillar", 74, "Immortal King", "xhb", false },     { "Immortal King's Stone Crusher", 75, "Immortal King", "7m7", false },     { "Tal Rasha's Fire-Spun Cloth", 76, "Tal Rasha's Wrappings", "zmb", false },     { "Tal Rasha's Adjudication", 77, "Tal Rasha's Wrappings", "amu", false },     { "Tal Rasha's Lidless Eye", 78, "Tal Rasha's Wrappings", "oba", false },     { "Tal Rasha's Howling Wind", 79, "Tal Rasha's Wrappings", "uth", false },
    { "Tal Rasha's Horadric Crest", 80, "Tal Rasha's Wrappings", "xsk", false },     { "Griswold's Valor", 81, "Griswold's Legacy", "urn", false },     { "Griswold's Heart", 82, "Griswold's Legacy", "xar", false },     { "Griswolds's Redemption", 83, "Griswold's Legacy", "7ws", false },     { "Griswold's Honor", 84, "Griswold's Legacy", "paf", false },     { "Trang-Oul's Guise", 85, "Trang-Oul's Avatar", "uh9", false },     { "Trang-Oul's Scales", 86, "Trang-Oul's Avatar", "xul", false },     { "Trang-Oul's Wing", 87, "Trang-Oul's Avatar", "ne9", false },     { "Trang-Oul's Claws", 88, "Trang-Oul's Avatar", "xmg", false },     { "Trang-Oul's Girth", 89, "Trang-Oul's Avatar", "utc", false },
    { "M'avina's True Sight", 90, "M'avina's Battle Hymn", "ci3", false },     { "M'avina's Embrace", 91, "M'avina's Battle Hymn", "uld", false },     { "M'avina's Icy Clutch", 92, "M'avina's Battle Hymn", "xtg", false },     { "M'avina's Tenet", 93, "M'avina's Battle Hymn", "zvb", false },     { "M'avina's Caster", 94, "M'avina's Battle Hymn", "amc", false },     { "Telling of Beads", 95, "The Disciple", "amu", false },     { "Laying of Hands", 96, "The Disciple", "ulg", false },     { "Rite of Passage", 97, "The Disciple", "xlb", false },     { "Spiritual Custodian", 98, "The Disciple", "uui", false },     { "Credendum", 99, "The Disciple", "umc", false },
    { "Dangoon's Teaching", 100, "Heaven's Brethren", "7ma", false },     { "Heaven's Taebaek", 101, "Heaven's Brethren", "uts", false },     { "Haemosu's Adament", 102, "Heaven's Brethren", "xrs", false },     { "Ondal's Almighty", 103, "Heaven's Brethren", "uhm", false },     { "Guillaume's Face", 104, "Orphan's Call", "xhm", false },     { "Wilhelm's Pride", 105, "Orphan's Call", "ztb", false },     { "Magnus' Skin", 106, "Orphan's Call", "xvg", false },     { "Wihtstan's Guard", 107, "Orphan's Call", "xml", false },     { "Hwanin's Splendor", 108, "Hwanin's Majesty", "xrn", false },     { "Hwanin's Refuge", 109, "Hwanin's Majesty", "xcl", false },
    { "Hwanin's Seal", 110, "Hwanin's Majesty", "mbl", false },     { "Hwanin's Justice", 111, "Hwanin's Majesty", "9vo", false },     { "Sazabi's Cobalt Redeemer", 112, "Sazabi's Grand Tribute", "7ls", false },     { "Sazabi's Ghost Liberator", 113, "Sazabi's Grand Tribute", "upl", false },     { "Sazabi's Mental Sheath", 114, "Sazabi's Grand Tribute", "xhl", false },     { "Bul-Kathos' Sacred Charge", 115, "Bul-Kathos' Children", "7gd", false },     { "Bul-Kathos' Tribal Guardian", 116, "Bul-Kathos' Children", "7wd", false },     { "Cow King's Horns", 117, "Cow King's Leathers", "xap", false },     { "Cow King's Hide", 118, "Cow King's Leathers", "stu", false },     { "Cow King's Hoofs", 119, "Cow King's Leathers", "vbt", false },
    { "Naj's Puzzler", 120, "Naj's Ancient Set", "6cs", false },     { "Naj's Light Plate", 121, "Naj's Ancient Set", "ult", false },     { "Naj's Circlet", 122, "Naj's Ancient Set", "ci0", false },     { "McAuley's Paragon", 123, "McAuley's Folly", "cap", false },     { "McAuley's Riprap", 124, "McAuley's Folly", "vbt", false },     { "McAuley's Taboo", 125, "McAuley's Folly", "vgl", false },     { "McAuley's Superstition", 126, "McAuley's Folly", "bwn", false },
};

#pragma endregion

#pragma region - Helper Functions

static std::vector<std::string> SplitTab(const std::string& line)
{
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string field;

    while (std::getline(ss, field, '\t'))
        result.push_back(field);

    return result;
}

static int FindColumn(const std::vector<std::string>& header, const std::string& name)
{
    for (size_t i = 0; i < header.size(); i++)
        if (header[i] == name)
            return (int)i;
    return -1;
}

static int MaxInt(int a, int b)
{
    return (a > b) ? a : b;
}

static int MaxInt3(int a, int b, int c)
{
    return MaxInt(a, MaxInt(b, c));
}

static int MaxInt4(int a, int b, int c, int d)
{
    return MaxInt(a, MaxInt(b, MaxInt(c, d)));
}

static bool SafeStringToInt(const std::string& s, int& out)
{
    try {
        size_t idx = 0;
        out = std::stoi(s, &idx);
        return idx == s.size(); // ensure entire string was numeric
    }
    catch (...) {
        out = -1;
        return false;
    }
}

std::string EscapeString(const std::string& input) {
    std::string out;
    for (char c : input) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        default: out += c;
        }
    }
    return out;
}

bool GenerateStaticArrays(const std::string& filepath, int itemsPerLine = 5)
{
    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    std::ofstream outFile("StaticGrailArrays.txt");
    if (!outFile.is_open())
        return false;

    std::string line;
    std::getline(file, line); // header
    auto header = SplitTab(line);

    int colUniqueName = FindColumn(header, "index");
    int colUniqueID = FindColumn(header, "*ID");
    int colUniqueCode = FindColumn(header, "code");
    int colUniqueEnabled = FindColumn(header, "enabled");

    int colSetName = FindColumn(header, "set");
    int colSetItemCode = FindColumn(header, "item");

    // ------------------------
    // Unique items
    // ------------------------
    outFile << "static UniqueItemEntry g_StaticUniqueItems[] = {\n";
    int count = 0;
    int runningIndex = 0;
    while (std::getline(file, line))
    {
        auto cols = SplitTab(line);

        if (colUniqueName >= 0 && colUniqueID >= 0 && colUniqueCode >= 0 && colUniqueEnabled >= 0)
        {
            int maxCol = MaxInt4(colUniqueName, colUniqueID, colUniqueCode, colUniqueEnabled);
            if (cols.size() <= maxCol) continue;

            int idVal;
            if (!SafeStringToInt(cols[colUniqueID], idVal)) continue;

            std::string enabledStr = cols[colUniqueEnabled];
            bool enabled = (enabledStr == "1" || enabledStr == "true");
            if (!enabled) continue;

            outFile << "{ " << runningIndex << ", "  // index
                << idVal << ", \""                     // id
                << cols[colUniqueName] << "\", \""     // name
                << cols[colUniqueCode] << "\", "      // code
                << "false" << " }, ";             // always default to false
            count++;
            runningIndex++;  // increment index

            if (count % itemsPerLine == 0)
                outFile << "\n"; // newline after X items
        }
    }
    outFile << "\n};\n\n";

    // ------------------------
    // Set items
    // ------------------------
    file.clear();
    file.seekg(0, std::ios::beg);
    std::getline(file, line);

    outFile << "static SetItemEntry g_StaticSetItems[] = {\n";
    count = 0;
    while (std::getline(file, line))
    {
        auto cols = SplitTab(line);

        if (colSetName >= 0 && colUniqueID >= 0 && colUniqueName >= 0 && colSetItemCode >= 0)
        {
            int maxCol = MaxInt4(colUniqueName, colUniqueID, colSetName, colSetItemCode);
            if (cols.size() <= maxCol) continue;

            int idVal;
            if (!SafeStringToInt(cols[colUniqueID], idVal)) continue;

            outFile << "    { \"" << cols[colUniqueName] << "\", "
                << idVal << ", \"" << cols[colSetName] << "\", \""
                << cols[colSetItemCode] << "\", false }, ";
            count++;
            if (count % itemsPerLine == 0)
                outFile << "\n";
        }
    }
    outFile << "\n};\n";

    outFile.close();
    file.close();

    std::cout << "Static arrays generated in StaticGrailArrays.txt with " << itemsPerLine << " items per line.\n";
    return true;
}

void SortItemLists()
{
    std::sort(g_UniqueItems.begin(), g_UniqueItems.end(),
        [](const UniqueItemEntry& a, const UniqueItemEntry& b)
        {
            return a.name < b.name;
        });

    std::sort(g_SetItems.begin(), g_SetItems.end(),
        [](const SetItemEntry& a, const SetItemEntry& b)
        {
            return a.setName < b.setName;
        });
}

void WriteResultsToFile(const std::string& output)
{
    std::ofstream out(output);

    // --- Unique Items ---
    out << "=== UNIQUE ITEMS ===\n";
    for (auto& u : g_UniqueItems)
        out << u.id << "\t" << u.name << "\t" << u.code << "\t" << u.enabled << "\n";

    // --- Set Items ---
    out << "\n=== SET ITEMS ===\n";
    for (auto& s : g_SetItems)
        out << s.id << "\t" << s.setName << "\t" << s.name << "\t" << s.code << "\t" << s.enabled << "\n";
}

#pragma endregion

#pragma region - Load/Save Functions

void SaveGrailProgress(const std::string& userPath, bool isAutoBackup)
{
    std::filesystem::path path;
    json j;
    std::string uniqueJsonStr;

    try
    {
        std::vector<UniqueItemEntry> uniqueCopy = g_UniqueItems;
        std::vector<SetItemEntry> setCopy = g_SetItems;
        std::unordered_set<std::string> excludedCopy = g_ExcludedGrailItems;

        // --- Determine base path ---
        if (userPath.empty())
            path = std::filesystem::current_path();
        else
            path = userPath;

        std::string filename = "Grail_Settings_" + GetModName() + ".json";

        if (isAutoBackup)
        {
            if (!path.has_extension())
            {
                if (backupWithTimestamps)
                {
                    auto t = std::chrono::system_clock::to_time_t(
                        std::chrono::system_clock::now());
                    std::tm tm{};
#if defined(_WIN32)
                    localtime_s(&tm, &t);
#else
                    localtime_r(&t, &tm);
#endif
                    char buf[64];
                    strftime(buf, sizeof(buf), "GrailBackup_%Y%m%d_%H%M%S.json", &tm);
                    filename = buf;
                }
                else if (!overwriteOldBackup)
                {
                    filename = "GrailBackup.json";
                }
                path /= filename;
            }
        }
        else
        {
            path = filename;
        }

        auto parent = path.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);

        // --- Build JSON ---
        // UNIQUE ITEMS
        {
            std::stringstream uniqueStream;
            uniqueStream << "[";

            bool first = true;
            int count = 0;

            for (auto& u : uniqueCopy)
            {
                if (!u.collected) continue;

                if (!first) uniqueStream << ", ";
                first = false;

                uniqueStream << "\"" << u.name << "\"";
                count++;

                if (count % 10 == 0)
                    uniqueStream << "\n  ";
            }

            uniqueStream << "]";
            uniqueJsonStr = uniqueStream.str();
            j["Unique Items"] = json::parse(uniqueJsonStr);
        }


        // SET ITEMS
        j["Set Items"] = json::object();
        for (auto& s : setCopy)
        {
            if (!s.collected) continue;
            j["Set Items"][s.setName].push_back(s.name);
        }

        // EXCLUDED ITEMS
        j["Excluded Grail Items"] = json::array();
        for (auto& x : excludedCopy)
            j["Excluded Grail Items"].push_back(x);

        // AUTO BACKUP SETTINGS
        j["AutoBackups"] = {
            { "On", autoBackups },
            { "Timestamps", backupWithTimestamps },
            { "Overwrite", overwriteOldBackup },
            { "Interval", backupIntervalMinutes },
            { "Path", backupPath }
        };

        // --- Write file ---
        std::ofstream out(path);
        if (!out.is_open())
        {
            std::cout << "[Backup ERROR] Failed to open file: " << path << std::endl;
            return;
        }

        out << j.dump(4);
        std::cout << "[Backup] Grail saved to: " << path << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cout << "\n[Backup ERROR] Exception encountered.\n";
        std::cout << "  Path: " << path << "\n";
        std::cout << "  Error: " << e.what() << "\n";
        std::cout << "  Unique JSON string was:\n" << uniqueJsonStr << "\n";

        try
        {
            std::cout << "\n  JSON dump so far:\n" << j.dump(4) << "\n";
        }
        catch (...)
        {
            std::cout << "  JSON dump failed.\n";
        }
    }
}

void LoadGrailProgress(const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    json j;
    try { file >> j; }
    catch (...) { return; }

    // --- Load uniques ---
    if (j.contains("Unique Items"))
    {
        std::unordered_set<std::string> collectedSet;
        for (auto& x : j["Unique Items"])
            collectedSet.insert(x.get<std::string>());

        for (auto& u : g_UniqueItems)
            u.collected = collectedSet.count(u.name) > 0;
    }

    // --- Load set items ---
    if (j.contains("Set Items"))
    {
        for (auto& s : g_SetItems)
        {
            s.collected = false;
            if (j["Set Items"].contains(s.setName))
            {
                for (auto& nm : j["Set Items"][s.setName])
                    if (nm.get<std::string>() == s.name)
                        s.collected = true;
            }
        }
    }

    // --- Load excluded ---
    g_ExcludedGrailItems.clear();
    if (j.contains("Excluded Grail Items"))
    {
        for (auto& x : j["Excluded Grail Items"])
            g_ExcludedGrailItems.insert(x.get<std::string>());
    }

    // --- Load AutoBackup settings ---
    if (j.contains("AutoBackups"))
    {
        auto& a = j["AutoBackups"];
        autoBackups = a.value("On", false);
        backupWithTimestamps = a.value("Timestamps", false);
        overwriteOldBackup = a.value("Overwrite", false);
        backupIntervalMinutes = a.value("Interval", 10);

        std::string pathStr = a.value("Path", "GrailBackup.json");
        std::strncpy(backupPath, pathStr.c_str(), sizeof(backupPath));
        backupPath[sizeof(backupPath) - 1] = '\0';
    }
}

bool LoadUniqueItems(const std::string& filepath)
{
    g_UniqueItems.clear();

    // Use static array for Retail Mods if file doesn't exist
    if (!std::filesystem::exists("Mods/" + modName + "/" + modName + ".mpq/data/global/excel/uniqueitems.txt"))
    {
        int arraySize = sizeof(g_StaticUniqueItems) / sizeof(g_StaticUniqueItems[0]);
        for (int i = 0; i < arraySize; ++i)
        {
            g_UniqueItems.push_back(g_StaticUniqueItems[i]);
        }
        return true;
    }

    // Use static array for RMD
    if (modName == "RMD-MP")
    {
        int arraySize = sizeof(g_StaticUniqueItemsRMD) / sizeof(g_StaticUniqueItemsRMD[0]);
        for (int i = 0; i < arraySize; ++i)
        {
            g_UniqueItems.push_back(g_StaticUniqueItemsRMD[i]);
        }
        return true;
    }

    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    std::string line;
    std::getline(file, line); // read header
    auto header = SplitTab(line);

    int colIndex = FindColumn(header, "index");
    int colID = FindColumn(header, "*ID");
    int colEnabled = FindColumn(header, "enabled");
    int colCode = FindColumn(header, "code");

    if (colIndex < 0 || colID < 0 || colEnabled < 0 || colCode < 0)
        return false;

    while (std::getline(file, line))
    {
        auto cols = SplitTab(line);
        int maxCol = MaxInt4(colIndex, colID, colEnabled, colCode);
        if (cols.size() <= maxCol)
            continue;

        UniqueItemEntry entry;

        int indexVal;
        if (!SafeStringToInt(cols[colID], indexVal)) // ID
            continue;
        entry.id = indexVal;

        entry.name = cols[colIndex];   // Name from index column
        entry.code = cols[colCode];    // Code

        std::string enabledStr = cols[colEnabled];
        entry.enabled = (enabledStr == "1" || enabledStr == "true");

        // Skip items not enabled in the file
        if (!(enabledStr == "1" || enabledStr == "true"))
            continue;

        g_UniqueItems.push_back(entry);
    }

    return true;
}

bool LoadSetItems(const std::string& filepath)
{
    g_SetItems.clear();

    // Use static array for Retail Mods if file doesn't exist
    if (!std::filesystem::exists("Mods/" + modName + "/" + modName + ".mpq/data/global/excel/setitems.txt"))
    {
        int arraySize = sizeof(g_StaticSetItems) / sizeof(g_StaticSetItems[0]);
        for (int i = 0; i < arraySize; ++i)
        {
            g_SetItems.push_back(g_StaticSetItems[i]);
        }
        return true;
    }

    // Use static array for RMD
    if (modName == "RMD-MP")
    {
        int arraySize = sizeof(g_StaticSetItemsRMD) / sizeof(g_StaticSetItemsRMD[0]);
        for (int i = 0; i < arraySize; ++i)
        {
            g_SetItems.push_back(g_StaticSetItemsRMD[i]);
        }
        return true;
    }

    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    std::string line;
    std::getline(file, line);
    auto header = SplitTab(line);

    int colIndex = FindColumn(header, "index");
    int colID = FindColumn(header, "*ID");
    int colSet = FindColumn(header, "set");
    int colItem = FindColumn(header, "item");

    if (colIndex < 0 || colID < 0 || colSet < 0 || colItem < 0)
        return false;

    while (std::getline(file, line))
    {
        auto cols = SplitTab(line);

        int maxCol = MaxInt4(colIndex, colID, colSet, colItem);
        if (cols.size() <= maxCol)
            continue;

        SetItemEntry entry;
        entry.name = cols[colIndex];

        int idVal;
        if (!SafeStringToInt(cols[colID], idVal))
            continue;
        entry.id = idVal;
        entry.setName = cols[colSet];
        entry.code = cols[colItem];
        entry.enabled = false;
        g_SetItems.push_back(entry);
    }

    return true;
}

void LoadExcludedGrailItems(const std::string& filepath)
{
    g_ExcludedGrailItems.clear();

    std::ifstream file(filepath);
    if (!file.is_open())
        return;

    try
    {
        nlohmann::json j;
        file >> j;

        if (j.contains("Excluded Grail Items") && j["Excluded Grail Items"].is_array())
        {
            for (auto& item : j["Excluded Grail Items"])
            {
                if (item.is_string())
                    g_ExcludedGrailItems.insert(item.get<std::string>());
            }
        }
    }
    catch (...)
    {
        // failed to parse, just skip
    }
}

void LoadAllItemData()
{
    g_UniqueItems.clear();
    g_SetItems.clear();
    
    // Load Functions  
    LoadUniqueItems("Mods/" + modName + "/" + modName + ".mpq/data/global/excel/uniqueitems.txt");
    LoadSetItems("Mods/" + modName + "/" + modName + ".mpq/data/global/excel/setitems.txt");
    SortItemLists();
    LoadGrailProgress("Grail_Settings_" + GetModName() + ".json");

    //GenerateStaticArrays("Mods/" + modName + "/" + modName + ".mpq/data/global/excel/uniqueitems.txt", 10);
    //GenerateStaticArrays("Mods/" + modName + "/" + modName + ".mpq/data/global/excel/setitems.txt", 10);
    //WriteResultsToFile("ParsedItemData_Output.txt");
    
}

#pragma endregion

#pragma endregion

#pragma region Menu System

#pragma region - Static/Structs

struct D2RHUDConfig
{
    bool MonsterStatsDisplay = true;
    std::string ChannelColor = "ÿc5";
    std::string PlayerNameColor = "ÿc8";
    std::string MessageColor = "ÿc0";
    bool HPRolloverMods = true;
    int HPRolloverPercent = 99;
    int HPRolloverDifficulty = -1;
    bool SunderedMonUMods = true;
    int SunderValue = 99;
    bool MinionEquality = true;
    bool GambleCostControl = true;
    bool CombatLog = true;
    std::vector<std::string> DLLsToLoad = { "D2RHUD.dll" };
};

std::vector<std::string> priorityOrder = {
        "reload", "Debug", "allowOverrides", "audioPlayback", "modTips",
        "audioVoice", "filter_titles", "filter_level", "language"
};

std::unordered_map<std::string, std::string> displayNames = {
    { "reload", "Reload Message" }, { "allowOverrides", "Allow Overrides" },
    { "modTips", "Mod Tips" }, { "Debug", "Debug Mode" },
    { "audioPlayback", "Audio Playback" }, { "audioVoice", "Audio Voice" },
    { "filter_titles", "Filter Titles" }, { "filter_level", "Filter Level" },
    { "language", "Language" }
};

std::unordered_map<std::string, std::string> g_LuaDescriptions = {
    { "reload", "Displays an in-game chat message whenever the filter is reloaded" },
    { "Debug", "Outputs details of matched/failed filter rules using in-game chat" },
    { "allowOverrides", "Allows the override_rules.lua to be loaded in addition to your own filter\nUsually used by mod authors to add 'default' content as a baseload" },
    { "audioPlayback", "Enables or disables both the Audio Playback and TTS options\nRequires Win10+ and Windows Media framework installed" },
    { "modTips", "Displays helpful mod-related tips at the bottom of item tooltips\nThese tips must be defined by the filter or mod author" },
    { "audioVoice", "Selects which TTS voice is used for audio playback (numeric ID)\nThis entry uses your own Windows TTS voices, they will differ between players" },
    { "filter_titles", "Define what the varying filter_levels should be displayed as in-game\nEntries such as 1,2,3 or Early-Game, Mid-Game, End-Game, etc are accepted" },
    { "filter_level", "Define your currently selected filter_level\nThis controls what filter rules will apply to your session (on supported filters)" },
    { "language", "Sets the filter language, such as 'enUS' or 'frFR'. Defaults to enUS if not defined\nLanguage support must be added by the filter author for proper functionality" }
};

struct LootFilterHeader
{
    std::string Title;
    std::string Version;
};

struct MemoryConfigEntry
{
    std::string Description;
    std::string Address;
    std::vector<std::string> Addresses;
    int Length = 1;
    std::string Type = "Hex";
    std::string Values;
};

static char searchBuffer[128] = "";
static bool filterUncollected = false;
static bool showMainMenu = false;
static bool showHUDSettingsMenu = false;
static bool showD2RHUDMenu = false;
static bool showMemoryMenu = false;
static bool showLootMenu = false;
static bool showHotkeyMenu = false;
static bool showGrailMenu = false;
LootFilterHeader g_LootFilterHeader;
std::unordered_map<std::string, std::string> g_LuaVariables;
std::unordered_map<std::string, std::pair<std::string, std::string>> g_Hotkeys;
std::vector<MemoryConfigEntry> g_MemoryConfigs;
static D2RHUDConfig d2rHUDConfig;
bool lootConfigLoaded = false;
bool lootLogicLoaded = false;
bool showExcluded = false;

#pragma endregion

#pragma region - Helper Funcs

void ImGuiTextCentered(const char* text, ImFont* font = nullptr, ImVec4 color = ImVec4(1, 1, 1, 1))
{
    ImGuiIO& io = ImGui::GetIO();
    float windowWidth = ImGui::GetWindowWidth();
    if (font) ImGui::PushFont(font);
    ImVec2 textSize = font ? font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text) : ImGui::CalcTextSize(text);
    ImGui::SetCursorPosX((windowWidth - textSize.x) * 0.5f);
    ImGui::TextColored(color, "%s", text);
    if (font) ImGui::PopFont();
}

std::string RemoveComments(const std::string& input)
{
    std::string output;
    size_t i = 0;
    while (i < input.size())
    {
        // Handle single-line comments //
        if (i + 1 < input.size() && input[i] == '/' && input[i + 1] == '/')
        {
            i += 2;
            while (i < input.size() && input[i] != '\n') i++;
        }
        // Handle multi-line comments
        else if (i + 1 < input.size() && input[i] == '/' && input[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < input.size() && !(input[i] == '*' && input[i + 1] == '/')) i++;
            i += 2;
        }
        else
        {
            output += input[i];
            i++;
        }
    }

    return output;
}

std::string CleanJsonFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    text = RemoveComments(text);
    text = std::regex_replace(text, std::regex(R"(,\s*([}\]]))"), "$1");

    return text;
}

ImVec2 CenterWindow(ImVec2 size)
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 centerPos = ImVec2((io.DisplaySize.x - size.x) * 0.5f,
        (io.DisplaySize.y - size.y) * 0.5f);
    ImGui::SetNextWindowPos(centerPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(size, ImGuiCond_Once);
    return centerPos;
}

ImFont* GetFont(int index)
{
    ImGuiIO& io = ImGui::GetIO();
    return (index >= 0 && index < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[index] : nullptr;
}

void PushFontSafe(int index)
{
    if (ImFont* f = GetFont(index))
        ImGui::PushFont(f);
}

void PopFontSafe(int index)
{
    if (GetFont(index))
        ImGui::PopFont();
}

bool DrawWindowTitleAndClose(const char* title, bool* open)
{
    float closeBtnSize = 20.0f;
    float padding = 5.0f;
    ImVec2 contentSize = ImGui::GetContentRegionAvail();

    float titleWidth = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((contentSize.x - titleWidth) * 0.5f);
    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "%s", title);

    ImGui::SameLine(contentSize.x - closeBtnSize - padding);
    ImVec2 btnPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("CloseBtn", ImVec2(closeBtnSize, closeBtnSize));
    if (ImGui::IsItemClicked() && open) *open = false;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 textSize = ImGui::CalcTextSize("X");
    ImVec2 textPos = ImVec2(btnPos.x + (closeBtnSize - textSize.x) * 0.5f,
        btnPos.y + (closeBtnSize - textSize.y) * 0.5f);
    drawList->AddText(textPos, IM_COL32(255, 80, 80, 255), "X");

    return true;
}

void DrawBottomDescription(const std::string& desc)
{
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 165, 0, 255));
    float textWidth = ImGui::CalcTextSize(desc.c_str()).x;
    float availWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX((availWidth - textWidth) * 0.5f);
    ImGui::TextWrapped("%s", desc.c_str());
    ImGui::PopStyleColor();
}

std::string DisplayKey(const std::string& key)
{
    std::string temp = key;
    size_t pos = 0;
    while ((pos = temp.find("VK_", pos)) != std::string::npos)
        temp.erase(pos, 3);
    return temp;
}

bool CaseInsensitiveContains(const std::string& str, const std::string& substr)
{
    auto it = std::search(
        str.begin(), str.end(),
        substr.begin(), substr.end(),
        [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
    );
    return (it != str.end());
}

void EnableAllInput() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NoKeyboard;
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableSetMousePos;
}

bool D2RHUD::IsAnyMenuOpen() {
    return showGrailMenu || showHotkeyMenu || showLootMenu || showD2RHUDMenu;
}

void ProcessBackups()
{
    static std::chrono::steady_clock::time_point lastBackup = std::chrono::steady_clock::now();

    // --- Manual backup triggered by button ---
    {
        std::lock_guard<std::mutex> lock(backupMutex);
        if (triggerBackupNow)
        {
            triggerBackupNow = false;
            SaveGrailProgress(std::string(backupPath), true);
        }
    }

    // --- Auto-backup logic ---
    if (autoBackups)
    {
        auto now = std::chrono::steady_clock::now();
        int intervalMs = backupIntervalMinutes * 60 * 1000;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBackup).count() >= intervalMs)
        {
            lastBackup = now;
            SaveGrailProgress(std::string(backupPath), true);
        }
    }
}

#pragma endregion

#pragma region - File Load/Save

void LoadHotkeys(const std::string& filename)
{
    g_Hotkeys.clear();
    std::ifstream file(filename);
    if (!file.is_open()) return;
    std::string line;
    std::regex multiRegex(R"delim(^\s*([^:]+):\s*(.+?),\s*"([^"]*)")delim"); // Name: Key combo, "string"
    std::regex singleRegex(R"(^\s*([^:]+):\s*(.+))"); // Name: Key combo

    while (std::getline(file, line))
    {
        std::smatch match;
        if (std::regex_match(line, match, multiRegex))
        {
            std::string name = match[1].str();
            std::string key = match[2].str();
            std::string extra = match[3].str();
            g_Hotkeys[name] = { key, extra };
        }
        else if (std::regex_match(line, match, singleRegex))
        {
            std::string name = match[1].str();
            std::string key = match[2].str();
            g_Hotkeys[name] = { key, "" };
        }
    }

    file.close();
}

void LoadLootFilterConfig(const std::string& path)
{
    g_LuaVariables.clear();
    g_LootFilterHeader = {}; // reset

    std::ifstream file(path);
    if (!file.is_open())
        return;

    std::string line;
    std::regex assignRegex(R"(^\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*(.+?),\s*$)");
    int nestingLevel = 0; // track { } nesting

    while (std::getline(file, line))
    {
        // --- Parse top comment header ---
        if (line.rfind("---", 0) == 0)
        {
            if (line.find("Filter Title:") != std::string::npos)
                g_LootFilterHeader.Title = line.substr(line.find(":") + 1);

            // Trim leading spaces
            for (auto* str : { &g_LootFilterHeader.Title })
            {
                if (!str->empty() && (*str)[0] == ' ') *str = str->substr(1);
            }
            continue;
        }

        // --- Track nesting for Lua table ---
        for (char c : line)
        {
            if (c == '{') nestingLevel++;
            if (c == '}') nestingLevel--;
        }

        // Only parse top-level assignments
        if (nestingLevel == 1)
        {
            std::smatch match;
            if (std::regex_match(line, match, assignRegex))
            {
                std::string key = match[1].str();
                std::string value = match[2].str();

                // Strip surrounding quotes for strings
                if (!value.empty() && value.front() == '"' && value.back() == '"')
                    value = value.substr(1, value.size() - 2);

                g_LuaVariables[key] = value;
            }
        }
    }
}

void LoadLootFilterLogic(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        g_LootFilterHeader.Version.clear();
        return;
    }

    std::string line;
    std::regex versionRegex(R"delim(local\s+version\s*=\s*"([^"]*)")delim");

    while (std::getline(file, line))
    {
        std::smatch match;
        if (std::regex_search(line, match, versionRegex))
        {
            g_LootFilterHeader.Version = match[1].str();
            break; // found
        }
    }
}

void LoadMemoryConfigs(const std::string& path)
{
    // Clear log
    std::ofstream("debug_memoryedits.log", std::ios::trunc);

    std::ifstream file(path);
    if (!file.is_open())
        return;

    // Parse JSON
    json j;
    try
    {
        std::string cleanJson = CleanJsonFile(path);
        if (cleanJson.empty())
            return;

        j = json::parse(cleanJson);
    }
    catch (const std::exception& e)
    {
        return;
    }

    g_MemoryConfigs.clear();

    if (!j.contains("MemoryConfigs") || !j["MemoryConfigs"].is_array())
    {
        return;
    }

    int index = 0;

    for (auto& entry : j["MemoryConfigs"])
    {
        MemoryConfigEntry m;
        m.Description = entry.value("Description", "");
        m.Address = entry.value("Address", "");

        if (entry.contains("Addresses") && entry["Addresses"].is_array())
        {
            for (auto& a : entry["Addresses"])
                m.Addresses.push_back(a.get<std::string>());
        }

        m.Length = entry.value("Length", 1);
        m.Type = entry.value("Type", "Hex");
        m.Values = entry.value("Values", "");

        g_MemoryConfigs.push_back(m);
        index++;
    }

    // --- Process entries (like C#) ---
    int totalOperations = 0;
    int successfulOperations = 0;

    for (auto& entry : g_MemoryConfigs)
    {
        totalOperations++;
        bool allSucceeded = true;

        try
        {
            if (!entry.Addresses.empty())
            {
                for (auto& addr : entry.Addresses)
                    allSucceeded = false;
            }
            else if (!entry.Address.empty())
            {
                allSucceeded = false;
            }
        }
        catch (const std::exception& ex)
        {
            allSucceeded = false;
        }

        if (allSucceeded)
            successfulOperations++;
    }
}

void LoadD2RHUDConfig(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) return;

    nlohmann::json j;
    file >> j;

    d2rHUDConfig.MonsterStatsDisplay = j.value("MonsterStatsDisplay", d2rHUDConfig.MonsterStatsDisplay);
    d2rHUDConfig.ChannelColor = j.value("ChannelColor", d2rHUDConfig.ChannelColor);
    d2rHUDConfig.PlayerNameColor = j.value("PlayerNameColor", d2rHUDConfig.PlayerNameColor);
    d2rHUDConfig.MessageColor = j.value("MessageColor", d2rHUDConfig.MessageColor);
    d2rHUDConfig.HPRolloverMods = j.value("HPRolloverMods", d2rHUDConfig.HPRolloverMods);
    d2rHUDConfig.HPRolloverPercent = j.value("HPRolloverPercent", d2rHUDConfig.HPRolloverPercent);
    d2rHUDConfig.HPRolloverDifficulty = j.value("HPRolloverDifficulty", d2rHUDConfig.HPRolloverDifficulty);
    d2rHUDConfig.SunderedMonUMods = j.value("SunderedMonUMods", d2rHUDConfig.SunderedMonUMods);
    d2rHUDConfig.SunderValue = j.value("SunderValue", d2rHUDConfig.SunderValue);
    d2rHUDConfig.MinionEquality = j.value("MinionEquality", d2rHUDConfig.MinionEquality);
    d2rHUDConfig.GambleCostControl = j.value("GambleCostControl", d2rHUDConfig.GambleCostControl);
    d2rHUDConfig.CombatLog = j.value("CombatLog", d2rHUDConfig.CombatLog);

    if (j.contains("DLLsToLoad"))
        d2rHUDConfig.DLLsToLoad = j["DLLsToLoad"].get<std::vector<std::string>>();
}

bool SaveD2RHUDConfig(const std::string& path)
{
    std::ifstream inFile(path, std::ios::binary);
    if (!inFile.is_open())
    {
        MessageBoxA(nullptr, ("Failed to open config file: " + path).c_str(), "Error", MB_OK | MB_ICONERROR);
        return false;
    }

    std::ostringstream oss;
    oss << inFile.rdbuf();
    std::string raw = oss.str();
    inFile.close();

    // Helper lambdas...
    auto replaceValue = [&](const std::string& key, const std::string& newValue)
        {
            size_t pos = raw.find("\"" + key + "\"");
            if (pos != std::string::npos)
            {
                size_t colon = raw.find(":", pos);
                if (colon != std::string::npos)
                {
                    size_t end = raw.find_first_of(",\n", colon);
                    if (end != std::string::npos)
                    {
                        raw.replace(colon + 1, end - colon - 1, " " + newValue);
                    }
                }
            }
        };

    auto replaceStringValue = [&](const std::string& key, const std::string& newValue)
        {
            size_t pos = raw.find("\"" + key + "\"");
            if (pos != std::string::npos)
            {
                size_t colon = raw.find(":", pos);
                size_t quoteStart = raw.find("\"", colon);
                size_t quoteEnd = raw.find("\"", quoteStart + 1);
                if (quoteStart != std::string::npos && quoteEnd != std::string::npos)
                {
                    raw.replace(quoteStart + 1, quoteEnd - quoteStart - 1, newValue);
                }
            }
        };

    // Replace all configurable values
    replaceValue("MonsterStatsDisplay", d2rHUDConfig.MonsterStatsDisplay ? "true" : "false");
    replaceStringValue("Channel Color", d2rHUDConfig.ChannelColor);
    replaceStringValue("Player Name Color", d2rHUDConfig.PlayerNameColor);
    replaceStringValue("Message Color", d2rHUDConfig.MessageColor);
    replaceValue("HPRolloverMods", d2rHUDConfig.HPRolloverMods ? "true" : "false");
    replaceValue("HPRollover%", std::to_string(d2rHUDConfig.HPRolloverPercent));
    replaceValue("HPRolloverDifficulty", std::to_string(d2rHUDConfig.HPRolloverDifficulty));
    replaceValue("SunderedMonUMods", d2rHUDConfig.SunderedMonUMods ? "true" : "false");
    replaceValue("SunderValue", std::to_string(d2rHUDConfig.SunderValue));
    replaceValue("MinionEquality", d2rHUDConfig.MinionEquality ? "true" : "false");
    replaceValue("GambleCostControl", d2rHUDConfig.GambleCostControl ? "true" : "false");
    replaceValue("CombatLog", d2rHUDConfig.CombatLog ? "true" : "false");

    // Write back to file
    std::ofstream outFile(path, std::ios::binary);
    if (!outFile.is_open())
        return false;

    outFile << raw;
    outFile.close();

    return true;
}

void SaveHotkeys(const std::string& filename)
{
    std::ofstream file(filename);
    if (!file.is_open()) return;

    for (auto& [name, pair] : g_Hotkeys)
    {
        if (pair.second.empty())
            file << name << ": " << pair.first << "\n"; // Name: Key combo
        else
            file << name << ": " << pair.first << ", \"" << pair.second << "\"\n"; // Name: Key combo, "string"
    }

    file.close();
}

#pragma endregion

#pragma region - Menu Displays

void ShowGrailMenu()
{
    static bool wasOpen = false;

    if (!showGrailMenu)
    {
        // Menu just closed? Save progress including AutoBackup settings
        if (wasOpen)
            SaveGrailProgress("Grail_Settings_" + GetModName() + ".json", false);

        wasOpen = false;
        return;
    }
    wasOpen = true;


    static bool itemsLoaded = false;
    if (!itemsLoaded)
    {
        LoadAllItemData();
        //ParseSharedStash("C:\\Users\\djsch\\Saved Games\\Diablo II Resurrected\\Mods\\RMD-MP\\Stash_SC_Page1.d2i");
        itemsLoaded = true;
    }

    // ------- Tooltip helper -------
    auto ShowOffsetTooltip = [](const char* text)
        {
            ImVec2 mousePos = ImGui::GetIO().MousePos;
            ImGui::SetNextWindowPos(ImVec2(mousePos.x + 70, mousePos.y), ImGuiCond_Always);
            ImGui::BeginTooltip();
            float tooltipWidth = 600.0f;
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + tooltipWidth);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        };

    CenterWindow(ImVec2(850, 500));

    PushFontSafe(3);
    ImGui::Begin("Grail Tracker", &showGrailMenu,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    DrawWindowTitleAndClose("Grail Tracker", &showGrailMenu);
    PopFontSafe(3);

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    // --------------------------------------------------------
    // Persistent State
    // --------------------------------------------------------
    static int selectedCategory = 0;  // 0 = Sets, 1 = Uniques
    static int selectedType = -1;     // For types later (Goal 4)
    static int selectedSet = -1;      // For future navigation
    static int selectedUnique = -1;
    static char searchBuffer[128] = "";
    static bool showUncollectedOnly = false;

    // --------------------------------------------------------
    // Layout: Left Panel / Right Panel
    // --------------------------------------------------------
    ImVec2 full = ImGui::GetContentRegionAvail();
    float leftWidth = 240.0f;

    // LEFT PANEL
    ImGui::BeginChild("left_panel", ImVec2(leftWidth, full.y), true);
    ImVec4 darkRed = ImVec4(0.6f, 0.1f, 0.1f, 1.0f);

    // --- Search ---
    ImGui::PushStyleColor(ImGuiCol_Text, darkRed);
    {
        float avail = ImGui::GetContentRegionAvail().x;
        float textWidth = ImGui::CalcTextSize("Item Search:").x;
        ImGui::SetCursorPosX((avail - textWidth) * 0.5f + ImGui::GetCursorPosX());
    }
    ImGui::Text("Item Search:");
    ImGui::PopStyleColor();

    // Center the input box under the label
    ImGui::PushItemWidth(leftWidth - 55);
    {
        float inputWidth = leftWidth - 55;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((avail - inputWidth) * 0.5f + ImGui::GetCursorPosX());
    }
    ImGui::InputText("##Search", searchBuffer, IM_ARRAYSIZE(searchBuffer));
    ImGui::PopItemWidth();
    ImGui::Dummy(ImVec2(0, 5));

    // --- Filter --- Centered checkbox ---
    {
        const char* label = "Hide Collected";
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        float checkboxWidth = ImGui::GetFrameHeight();
        float totalWidth = checkboxWidth + 4 + labelSize.x;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((avail - totalWidth) * 0.5f + ImGui::GetCursorPosX());
    }
    ImGui::Checkbox("Hide Collected", &showUncollectedOnly);

    // --- Show Excluded --- Centered checkbox ---
    {
        const char* label = "Show Excluded";
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        float checkboxWidth = ImGui::GetFrameHeight();
        float totalWidth = checkboxWidth + 4 + labelSize.x;
        float avail = ImGui::GetContentRegionAvail().x;

        // center horizontally
        ImGui::SetCursorPosX((avail - totalWidth) * 0.5f + ImGui::GetCursorPosX());
    }

    if (ImGui::IsItemHovered())
        ShowOffsetTooltip("displays excluded items in the list, but with grey text");

    // --- Backup Section ---
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 3));

    ImGui::PushStyleColor(ImGuiCol_Text, darkRed);
    {
        float avail = ImGui::GetContentRegionAvail().x;
        float textWidth = ImGui::CalcTextSize("Backup Path:").x;
        ImGui::SetCursorPosX((avail - textWidth) * 0.5f + ImGui::GetCursorPosX());
    }
    ImGui::Text("Backup Path:");
    ImGui::PopStyleColor();

    ImGui::PushItemWidth(leftWidth - 55);
    {
        float inputWidth = leftWidth - 55;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((avail - inputWidth) * 0.5f + ImGui::GetCursorPosX());
    }
    ImGui::InputText("##BackupPath", backupPath, IM_ARRAYSIZE(backupPath));
    ImGui::PopItemWidth();
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));

    // --- Centered Auto-Backup Checkboxes Block ---
    const char* labels[] = { "Auto-Backups", "Use Timestamps", "Overwrite Old" };
    float widest = 0.0f;
    for (auto label : labels) {
        float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2 + 20;
        if (w > widest) widest = w;
    }
    float avail = ImGui::GetContentRegionAvail().x;
    float startX = (avail - widest) * 0.5f + ImGui::GetCursorPosX();

    ImGui::SetCursorPosX(startX);
    ImGui::Checkbox("Auto-Backups", &autoBackups);
    ImGui::BeginDisabled(!autoBackups);
    if (ImGui::IsItemHovered()) ShowOffsetTooltip("Enable automatic backups on the specified interval.");

    ImGui::SetCursorPosX(startX);
    if (ImGui::Checkbox("Use Timestamps", &backupWithTimestamps))
        if (backupWithTimestamps) overwriteOldBackup = false;
    if (ImGui::IsItemHovered()) ShowOffsetTooltip("Append current date/time to backup filename to avoid overwriting.");

    ImGui::SetCursorPosX(startX);
    if (ImGui::Checkbox("Overwrite Old", &overwriteOldBackup))
        if (overwriteOldBackup) backupWithTimestamps = false;
    if (ImGui::IsItemHovered()) ShowOffsetTooltip("Overwrite previous backup file instead of creating a new one.");

    ImGui::Dummy(ImVec2(0, 2));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));

    // --- Centered Backup Interval ---
    ImGui::PushStyleColor(ImGuiCol_Text, darkRed);
    const char* intervalLabel = "Backup Interval:";
    ImVec2 labelSize = ImGui::CalcTextSize(intervalLabel);
    avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX((avail - labelSize.x) * 0.5f + ImGui::GetCursorPosX());
    ImGui::Text("%s", intervalLabel);
    ImGui::PopStyleColor();

    float inputWidth = 100;
    avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX((avail - inputWidth) * 0.5f + ImGui::GetCursorPosX());
    ImGui::SetNextItemWidth(inputWidth);
    ImGui::InputInt("##BackupInterval", &backupIntervalMinutes);

    if (ImGui::IsItemHovered())
        ShowOffsetTooltip("How often to save automatic backups.\nMeasured in minutes.");

    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::EndChild();

    // -----------------------------
    // RIGHT PANEL
    // -----------------------------
    ImGui::SameLine();
    ImGui::BeginChild("right_panel", ImVec2(0, full.y), true);

    std::string searchStr = searchBuffer;
    auto Trim = [](std::string s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(),
            [](unsigned char c) { return !std::isspace(c); }));
        s.erase(std::find_if(s.rbegin(), s.rend(),
            [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
        return s;
        };

    // --- Category Buttons (Sets / Uniques) ---
    auto CategoryButton = [&](const char* label, int id)
        {
            bool selected = (selectedCategory == id);
            ImVec4 textColor = (id == 0) ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.84f, 0.2f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, textColor);

            // Use Button instead of Selectable to avoid full-width
            if (ImGui::Button(label, ImVec2(0, 0)))
                selectedCategory = id;

            ImGui::PopStyleColor();

            // Tooltip for collection progress
            if (ImGui::IsItemHovered())
            {
                ImVec2 mousePos = ImGui::GetIO().MousePos;
                ImGui::SetNextWindowPos(ImVec2(mousePos.x + 70, mousePos.y), ImGuiCond_Always);
                ImGui::BeginTooltip();

                ImVec4 labelColor = ImVec4(0.6f, 0.8f, 1.0f, 1.0f);
                ImVec4 valueColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

                if (id == 0) // Sets
                {
                    std::unordered_map<std::string, std::pair<int, int>> setProgress;
                    for (auto& s : g_SetItems)
                    {
                        auto& p = setProgress[s.setName];
                        p.second++;
                        if (s.collected) p.first++;
                    }

                    int collectedSets = 0;
                    for (auto& [name, p] : setProgress)
                        if (p.first == p.second)
                            collectedSets++;

                    int totalSets = (int)setProgress.size();
                    int collectedItems = 0;
                    for (auto& s : g_SetItems) if (s.collected) collectedItems++;
                    int totalItems = (int)g_SetItems.size();

                    ImGui::PushStyleColor(ImGuiCol_Text, labelColor);
                    ImGui::Text("Items Collected:");
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, valueColor);
                    ImGui::Text("%d/%d", collectedItems, totalItems);
                    ImGui::PopStyleColor();

                    ImGui::PushStyleColor(ImGuiCol_Text, labelColor);
                    ImGui::Text("Sets Completed:");
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, valueColor);
                    ImGui::Text("%d/%d", collectedSets, totalSets);
                    ImGui::PopStyleColor();
                }
                else // Uniques
                {
                    int collectedItems = 0;
                    for (auto& u : g_UniqueItems) if (u.collected) collectedItems++;
                    int totalItems = (int)g_UniqueItems.size();

                    ImGui::PushStyleColor(ImGuiCol_Text, labelColor);
                    ImGui::Text("Unique Items:");
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, valueColor);
                    ImGui::Text("%d/%d", collectedItems, totalItems);
                    ImGui::PopStyleColor();
                }

                ImGui::EndTooltip();
            }
        };

    // Sets button
    CategoryButton("Sets", 0);

    // Compute button widths
    float setsWidth = ImGui::CalcTextSize("Sets").x + ImGui::GetStyle().FramePadding.x * 2;
    float uniquesWidth = ImGui::CalcTextSize("Uniques").x + ImGui::GetStyle().FramePadding.x * 2;
    float panelWidth = ImGui::GetContentRegionAvail().x;

    // Compute label width
    std::string trackerLabel = "<  Choose your collection type  >";
    float labelWidth = ImGui::CalcTextSize(trackerLabel.c_str()).x;
    float spacing = (panelWidth - setsWidth - uniquesWidth - labelWidth - 25) / 2.0f;

    // Move cursor after Sets button + spacing
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(spacing, 0));
    ImGui::SameLine();

    // Draw the label
    ImGui::PushStyleColor(ImGuiCol_Text, darkRed);
    ImGui::Text("%s", trackerLabel.c_str());
    ImGui::PopStyleColor();

    // Keep Uniques button on same line, at the right
    ImGui::SameLine(panelWidth - uniquesWidth + 10);
    CategoryButton("Uniques", 1);

    ImGui::Dummy(ImVec2(0, 5));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    // --- Display the selected list ---
    if (selectedCategory == 0)
    {
        // SET ITEM LIST (GROUPED BY SET NAME)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.988f, 0.0f, 1.0f));
        ImGui::Text("Set Items");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4));

        // Build map of sets
        std::unordered_map<std::string, std::vector<SetItemEntry*>> sets;
        for (auto& s : g_SetItems)
        {
            if (!searchStr.empty() &&
                !CaseInsensitiveContains(s.name, searchStr) &&
                !CaseInsensitiveContains(s.setName, searchStr))
                continue;

            if (showUncollectedOnly && s.collected)
                continue;

            sets[s.setName].push_back(&s);
        }

        // Collect set names and sort alphabetically
        std::vector<std::string> sortedSetNames;
        for (auto& [setName, items] : sets)
            sortedSetNames.push_back(setName);

        std::sort(sortedSetNames.begin(), sortedSetNames.end(),
            [](const std::string& a, const std::string& b)
            {
                return a < b;
            });

        // Display sets in sorted order
        for (auto& setName : sortedSetNames)
        {
            auto& items = sets[setName];

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.988f, 0.0f, 1.0f));
            if (ImGui::TreeNode(setName.c_str()))
            {
                ImGui::PopStyleColor();

                for (auto* s : items)
                {
                    std::string label = s->name + " (" + s->code + ")";
                    bool* checked = &s->collected;

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.988f, 0.0f, 1.0f));
                    if (ImGui::Checkbox(label.c_str(), checked))
                    {
                        // TODO: save state to file later
                    }
                    ImGui::PopStyleColor();
                }

                ImGui::TreePop();
            }
            else
            {
                ImGui::PopStyleColor();
            }
        }
    }
    else
    {
        // =====================================================
        // UNIQUE ITEM LIST
        // =====================================================
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.780f, 0.702f, 0.467f, 1.0f));
        ImGui::Text("Unique Items");

        // Button for excluded items
        ImGui::SameLine();
        if (ImGui::SmallButton("Excluded"))
            ImGui::OpenPopup("ExcludedItemsPopup");

        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4));

        if (ImGui::BeginPopup("ExcludedItemsPopup"))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "Excluded Items:");
            ImGui::Separator();
            for (const auto& item : g_ExcludedGrailItems)
                ImGui::BulletText("%s", item.c_str());
            ImGui::EndPopup();
        }

        for (size_t i = 0; i < g_UniqueItems.size(); ++i)
        {
            auto& u = g_UniqueItems[i];
            std::string trimmedName = Trim(u.name);
            bool isExcluded = g_ExcludedGrailItems.count(trimmedName) > 0;

            // FILTERS
            if (!showExcluded && isExcluded)
                continue;

            if (!searchStr.empty() && !CaseInsensitiveContains(u.name, searchStr))
                continue;

            if (showUncollectedOnly && u.collected && !isExcluded)
                continue;

            bool* checked = &u.collected;
            std::string label = u.name + " (" + u.code + ")";
            std::string checkboxID = label + "##" + std::to_string(i);

            // Begin horizontal line
            ImGui::BeginGroup();

            // -------------------------------
            // TEXT COLOR: GOLD OR GREY
            // -------------------------------
            if (isExcluded)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f)); // grey
            else
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.780f, 0.702f, 0.467f, 1.0f));   // gold

            // Checkbox for collected/uncollected
            if (ImGui::Checkbox(checkboxID.c_str(), checked))
            {
                // TODO: functionality if needed
            }
            ImGui::PopStyleColor();

            // -------------------------------
            // ACTION BUTTON (X or ✓)
            // -------------------------------
            float offsetX = ImGui::GetContentRegionAvail().x - 80.0f;
            if (offsetX < 0) offsetX = 0;

            ImGui::SameLine(offsetX);

            std::string buttonID;

            if (isExcluded)
            {
                //--------------------------------------------------
                // ✓ GREEN BUTTON FOR EXCLUDED ITEMS
                //--------------------------------------------------
                buttonID = "Include##" + std::to_string(i);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.2f, 1.0f)); // green

                if (ImGui::SmallButton(buttonID.c_str()))
                {
                    g_ExcludedGrailItems.erase(trimmedName);
                    SaveGrailProgress("Grail_Settings_" + GetModName(), false);
                }

                if (ImGui::IsItemHovered())
                    ShowOffsetTooltip("Include this item back in your Grail hunt");

                ImGui::PopStyleColor();
            }
            else
            {
                //--------------------------------------------------
                // X RED BUTTON FOR NORMAL ITEMS
                //--------------------------------------------------
                buttonID = "Exclude##" + std::to_string(i);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.15f, 0.15f, 1.0f)); // red

                if (ImGui::SmallButton(buttonID.c_str()))
                {
                    g_ExcludedGrailItems.insert(trimmedName);
                    u.enabled = false;
                    SaveGrailProgress("Grail_Settings_" + GetModName() + ".json", false);
                }

                if (ImGui::IsItemHovered())
                    ShowOffsetTooltip("Exclude this item from your Grail hunt");

                ImGui::PopStyleColor();
            }

            ImGui::EndGroup();
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

void ShowHotkeyMenu()
{
    if (!showHotkeyMenu)
        return;

    static bool hotkeysLoaded = false;
    if (!hotkeysLoaded)
    {
        LoadHotkeys("../Launcher/D2RLAN_Config.txt");
        hotkeysLoaded = true;
    }

    EnableAllInput();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 windowSize = ImVec2(800, 450);
    CenterWindow(windowSize);
    PushFontSafe(3);
    ImGui::Begin("D2R Hotkeys", &showHotkeyMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    DrawWindowTitleAndClose("D2R Hotkeys", &showHotkeyMenu);
    if (GetFont(3)) ImGui::PopFont();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // --- Split hotkeys ---
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> singleKeys;
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> customKeys;
    std::pair<std::string, std::pair<std::string, std::string>> startupCommandEntry;
    bool hasStartupCommand = false;

    for (auto& [name, pair] : g_Hotkeys)
    {
        if (name == "Startup Commands")
        {
            startupCommandEntry = { name, pair };
            hasStartupCommand = true;
        }
        else if (pair.second.empty())
            singleKeys.push_back({ name, pair });
        else
            customKeys.push_back({ name, pair });
    }

    static std::string hoveredHotkey;
    hoveredHotkey.clear();

    // --- 2-column layout ---
    ImGui::Columns(2, nullptr, true);
    ImGui::SetColumnWidth(0, 350);
    ImGui::SetColumnWidth(1, 450);

    ImFont* inputFont = GetFont(1);

    // --- LEFT COLUMN: Single Hotkeys ---
    float leftColumnStartY = ImGui::GetCursorPosY();
    {
        PushFontSafe(2);
        const char* colTitle = "Standard Hotkeys";
        float colWidth = ImGui::GetColumnWidth();

        float textW = ImGui::CalcTextSize(colTitle).x;
        ImGui::SetCursorPosX((colWidth - textW) * 0.5f);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "%s", colTitle);
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        PopFontSafe(2);

        for (auto& [name, pair] : singleKeys)
        {
            std::string displayName = (name == "Toggle Stat Adjustments Display") ? "TZ Stat Display" : name;

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(2, 130, 199, 255));
            ImGui::Text("%s:", displayName.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();

            std::string hotkeyDisplay = pair.first;
            std::string boolPart;
            if (name == "Toggle Stat Adjustments Display")
            {
                size_t commaPos = hotkeyDisplay.find(',');
                if (commaPos != std::string::npos)
                {
                    boolPart = hotkeyDisplay.substr(0, commaPos);
                    hotkeyDisplay = hotkeyDisplay.substr(commaPos + 1);
                }
                hotkeyDisplay.erase(0, hotkeyDisplay.find_first_not_of(" \t"));
            }

            char keyBuffer[128];
            strncpy(keyBuffer, DisplayKey(hotkeyDisplay).c_str(), sizeof(keyBuffer));
            keyBuffer[sizeof(keyBuffer) - 1] = '\0';

            ImGui::PushItemWidth(120);
            if (inputFont) ImGui::PushFont(inputFont);
            if (ImGui::InputText(("##key_" + name).c_str(), keyBuffer, sizeof(keyBuffer)))
            {
                std::string edited = keyBuffer;
                std::string restored;
                size_t start = 0;
                while (start < edited.length())
                {
                    size_t plusPos = edited.find('+', start);
                    std::string part = (plusPos == std::string::npos) ? edited.substr(start) : edited.substr(start, plusPos - start);
                    part.erase(0, part.find_first_not_of(" "));
                    part.erase(part.find_last_not_of(" ") + 1);
                    if (part.find("VK_") != 0) part = "VK_" + part;
                    restored += part;
                    if (plusPos != std::string::npos) restored += " + ";
                    start = (plusPos == std::string::npos) ? edited.length() : plusPos + 1;
                }
                pair.first = (name == "Toggle Stat Adjustments Display") ? boolPart + ", " + restored : restored;
            }
            if (inputFont) ImGui::PopFont();
            ImGui::PopItemWidth();

            if (name == "Toggle Stat Adjustments Display")
            {
                ImGui::SameLine();
                bool enabled = (boolPart.find("true") != std::string::npos);
                if (ImGui::Checkbox(("##toggle_" + name).c_str(), &enabled))
                    pair.first = (enabled ? "true" : "false") + std::string(", ") + hotkeyDisplay;
            }

            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            max.x += ImGui::CalcTextSize(displayName.c_str()).x + 5.0f;
            if (ImGui::IsMouseHoveringRect(min, max)) hoveredHotkey = displayName;
        }
    }

    // --- RIGHT COLUMN: Custom Commands ---
    ImGui::NextColumn();
    ImGui::SetCursorPosY(leftColumnStartY);
    {
        const char* colTitle = "Command Hotkeys";
        float colWidth = ImGui::GetColumnWidth();
        float colStartX = ImGui::GetCursorPosX();

        float textW = ImGui::CalcTextSize(colTitle).x;
        ImGui::SetCursorPosX(colStartX + (colWidth - textW) * 0.5f);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "%s", colTitle);
        ImGui::Dummy(ImVec2(0.0f, 15.0f));

        int cmdIndex = 1;
        for (auto& [name, pair] : customKeys)
        {
            std::string displayName = "Command " + std::to_string(cmdIndex++);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(2, 130, 199, 255));
            ImGui::Text("%s:", displayName.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();

            char keyBuffer[128];
            strncpy(keyBuffer, DisplayKey(pair.first).c_str(), sizeof(keyBuffer));
            keyBuffer[sizeof(keyBuffer) - 1] = '\0';

            ImGui::PushItemWidth(120);
            if (inputFont) ImGui::PushFont(inputFont);
            if (ImGui::InputText(("##key_" + name).c_str(), keyBuffer, sizeof(keyBuffer)))
                pair.first = keyBuffer;
            if (inputFont) ImGui::PopFont();
            ImGui::PopItemWidth();

            ImGui::SameLine();

            char extraBuffer[256];
            strncpy(extraBuffer, pair.second.c_str(), sizeof(extraBuffer));
            extraBuffer[sizeof(extraBuffer) - 1] = '\0';

            ImGui::PushItemWidth(180);
            if (inputFont) ImGui::PushFont(inputFont);
            if (ImGui::InputText(("##extra_" + name).c_str(), extraBuffer, sizeof(extraBuffer)))
                pair.second = extraBuffer;
            if (inputFont) ImGui::PopFont();
            ImGui::PopItemWidth();

            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            max.x += 60 + 180;
            if (ImGui::IsMouseHoveringRect(min, max)) hoveredHotkey = displayName;
        }

        if (hasStartupCommand)
        {
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(2, 130, 199, 255));
            ImGui::Text("Startup Commands");
            ImGui::PopStyleColor();

            ImGui::PushItemWidth(-1);
            if (inputFont) ImGui::PushFont(inputFont);

            char cmdBuffer[512];
            strncpy(cmdBuffer, startupCommandEntry.second.first.c_str(), sizeof(cmdBuffer));
            cmdBuffer[sizeof(cmdBuffer) - 1] = '\0';

            if (ImGui::InputTextMultiline("##startup_commands", cmdBuffer, sizeof(cmdBuffer),
                ImVec2(-1, 80)))
            {
                startupCommandEntry.second.first = cmdBuffer;
                g_Hotkeys["Startup Commands"] = startupCommandEntry.second;
            }

            if (inputFont) ImGui::PopFont();
            ImGui::PopItemWidth();
        }
    }

    ImGui::Columns(1);

    std::string desc = hoveredHotkey.empty() ? "Hover over a hotkey to see details."
        : "Set the hotkey for " + hoveredHotkey + ".";
    DrawBottomDescription(desc);

    ImGui::End();
    io.ConfigFlags = ImGui::GetIO().ConfigFlags;
}

void ShowLootMenu()
{
    if (!showLootMenu)
    {
        lootConfigLoaded = false;
        lootLogicLoaded = false;
        return;
    }

    if (!lootConfigLoaded)
    {
        LoadLootFilterConfig("lootfilter_config.lua");
        LoadLootFilterLogic("lootfilter.lua");
        lootConfigLoaded = true;
        lootLogicLoaded = true;
    }

    EnableAllInput();
    CenterWindow(ImVec2(800, 400));
    static std::string hoveredKey;
    hoveredKey.clear();
    PushFontSafe(3);
    ImGui::Begin("D2RLoot Settings", &showLootMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    DrawWindowTitleAndClose("D2RLoot Settings", &showLootMenu);
    PopFontSafe(3);
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // --- Centered Wrapped Text Helper ---
    auto CenteredWrappedText = [&](const std::string& prefix, const std::string& text,
        const ImVec4& prefixColor = ImVec4(1, 0.7f, 0.3f, 1.0f),
        const ImVec4& valueColor = ImVec4(1, 1, 1, 1))
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float leftOffset = 10.0f;
            float wrapWidth = avail.x - leftOffset;

            ImVec2 prefixSize = ImGui::CalcTextSize(prefix.c_str(), nullptr, false, wrapWidth);
            ImVec2 valueSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrapWidth);
            float totalWidth = prefixSize.x + valueSize.x;
            float cursorX = leftOffset + (wrapWidth - totalWidth) * 0.5f;
            if (cursorX < leftOffset) cursorX = leftOffset;

            ImGui::SetCursorPosX(cursorX);
            ImGui::TextColored(prefixColor, "%s", prefix.c_str());
            ImGui::SameLine(0, 0);
            ImGui::TextColored(valueColor, "%s", text.c_str());
        };

    if (!g_LootFilterHeader.Version.empty())
        CenteredWrappedText("My D2RLoot Version: ", g_LootFilterHeader.Version);
    CenteredWrappedText("My Selected Filter: ", g_LootFilterHeader.Title);

    ImGui::Dummy(ImVec2(0.0f, 3.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // --- Boolean Checkboxes ---
    auto RenderCheckboxLine = [&](const std::vector<std::pair<std::string, std::string>>& items)
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float spacing = 10.0f;
            float totalWidth = 0.0f;

            for (auto& item : items)
            {
                totalWidth += ImGui::CalcTextSize(item.first.c_str()).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetFrameHeight();
            }
            totalWidth += spacing * (items.size() - 1);

            float startX = (avail.x - totalWidth) * 0.5f;
            if (startX < 0.0f) startX = 0.0f;
            ImGui::SetCursorPosX(startX);

            for (size_t i = 0; i < items.size(); ++i)
            {
                if (i > 0) ImGui::SameLine(0.0f, spacing);

                std::string key = items[i].second;
                std::string val = "";
                auto it = g_LuaVariables.find(key);
                if (it != g_LuaVariables.end()) val = it->second;

                bool boolValue = (val == "true");

                if (ImGui::Checkbox(items[i].first.c_str(), &boolValue))
                {
                    auto it2 = g_LuaVariables.find(key);
                    if (it2 != g_LuaVariables.end()) it2->second = boolValue ? "true" : "false";
                    else g_LuaVariables.insert({ key, boolValue ? "true" : "false" });
                }

                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 itemMax = ImGui::GetItemRectMax();
                float textWidth = ImGui::CalcTextSize(items[i].first.c_str()).x;
                itemMax.x += textWidth;
                if (ImGui::IsMouseHoveringRect(itemMin, itemMax)) hoveredKey = key;
            }

            ImGui::Dummy(ImVec2(0.0f, 3.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 3.0f));
        };

    std::vector<std::pair<std::string, std::string>> bools = {
        { "Allow Overrides", "allowOverrides" },
        { "Mod Tips", "modTips" },
        { "Debug Mode", "Debug" },
        { "Audio Playback", "audioPlayback" }
    };
    RenderCheckboxLine(bools);

    // --- Input Text Helper ---
    auto RenderInputText = [&](const std::string& key, const std::string& label, const std::string& defaultVal = "Not Defined")
        {
            std::string value = defaultVal;
            auto it = g_LuaVariables.find(key);
            if (it != g_LuaVariables.end()) value = it->second;
            if (!value.empty() && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);

            std::string fullLabel = label + " = ";
            float labelWidth = ImGui::CalcTextSize(fullLabel.c_str()).x;
            float valueWidth = ImGui::CalcTextSize(value.c_str()).x + 8.0f;

            ImVec2 cursorPos = ImGui::GetCursorPos();
            ImGui::SetCursorPosY(cursorPos.y - 2.0f);

            ImGui::Text("%s", fullLabel.c_str());
            ImGui::SameLine(labelWidth + 10.0f, -3.0f);

            char buffer[256];
            strncpy(buffer, value.c_str(), sizeof(buffer));
            buffer[sizeof(buffer) - 1] = '\0';

            std::string inputID = "##val_" + key;
            ImGui::PushItemWidth(valueWidth);
            bool changed = ImGui::InputText(inputID.c_str(), buffer, sizeof(buffer));
            if (ImGui::IsItemActivated()) ImGui::SetKeyboardFocusHere(-1);
            if (changed)
            {
                auto it2 = g_LuaVariables.find(key);
                if (it2 != g_LuaVariables.end()) it2->second = buffer;
                else g_LuaVariables.insert({ key, buffer });
            }
            ImGui::PopItemWidth();

            ImVec2 itemMin = ImGui::GetItemRectMin();
            ImVec2 itemMax = ImGui::GetItemRectMax();
            itemMin.x -= labelWidth;
            if (ImGui::IsMouseHoveringRect(itemMin, itemMax)) hoveredKey = key;
        };

    RenderInputText("reload", "Reload Message");
    RenderInputText("audioVoice", "Audio Voice");
    RenderInputText("filter_level", "Filter Level");
    RenderInputText("language", "Language");

    // --- Filter Titles ---
    auto RenderFilterTitles = [&]()
        {
            std::string key = "filter_titles";
            std::string value = "";
            auto it = g_LuaVariables.find(key);
            if (it != g_LuaVariables.end()) value = it->second;

            std::vector<std::string> titles;
            if (!value.empty())
            {
                std::regex titleRegex(R"delim("([^"]*)")delim");
                for (auto i = std::sregex_iterator(value.begin(), value.end(), titleRegex);
                    i != std::sregex_iterator(); ++i)
                    titles.push_back((*i)[1].str());
            }
            if (titles.empty()) titles.push_back("Not Defined");

            std::string ftLabel = "Filter Titles = ";
            float labelWidth = ImGui::CalcTextSize(ftLabel.c_str()).x;
            ImGui::Text("%s", ftLabel.c_str());
            ImGui::SameLine(labelWidth + 10.0f);

            for (size_t idx = 0; idx < titles.size(); ++idx)
            {
                char buffer[256];
                strncpy(buffer, titles[idx].c_str(), sizeof(buffer));
                buffer[sizeof(buffer) - 1] = '\0';

                float textWidth = ImGui::CalcTextSize(buffer).x;
                ImGui::PushItemWidth(textWidth + 8.0f);

                std::string inputID = "##filter_title_" + std::to_string(idx);
                bool changed = ImGui::InputText(inputID.c_str(), buffer, sizeof(buffer));
                ImGui::PopItemWidth();

                if (changed) titles[idx] = buffer;
                if (idx + 1 < titles.size()) { ImGui::SameLine(0, 2); ImGui::Text(", "); ImGui::SameLine(0, 0); }

                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 itemMax = ImGui::GetItemRectMax();
                itemMin.x -= labelWidth;
                if (ImGui::IsMouseHoveringRect(itemMin, itemMax)) hoveredKey = key;
            }

            std::string newValue = "{ ";
            for (size_t i = 0; i < titles.size(); ++i)
            {
                newValue += "\"" + titles[i] + "\"";
                if (i + 1 < titles.size()) newValue += ", ";
            }
            newValue += " }";

            auto it2 = g_LuaVariables.find(key);
            if (it2 != g_LuaVariables.end()) it2->second = newValue;
            else g_LuaVariables.insert({ key, newValue });
        };
    RenderFilterTitles();

    // --- Bottom Description ---
    std::string desc = "Hover over an option to see its description.";
    if (!hoveredKey.empty() && g_LuaDescriptions.count(hoveredKey))
        desc = g_LuaDescriptions.at(hoveredKey);

    DrawBottomDescription(desc);

    ImGui::End();
}

void ShowMemoryMenu()
{
    if (!showMemoryMenu) return;

    LoadMemoryConfigs("../Launcher/config.json");

    EnableAllInput();
    CenterWindow(ImVec2(850, 420));

    PushFontSafe(3);
    ImGui::Begin("Memory Edit Info", &showMemoryMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

    DrawWindowTitleAndClose("Memory Edit Info", &showMemoryMenu);

    // --- Window Description ---
    PushFontSafe(2);
    std::string desc = "Shows the currently enabled memory edits being applied\nThis panel is read-only at this time - Editing supported later";
    DrawBottomDescription(desc);
    PopFontSafe(2);

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 contentSize = ImGui::GetContentRegionAvail();

    // --- Display Memory Config Entries ---
    static std::unordered_map<std::string, int> selectedIndexMap;

    for (auto& entry : g_MemoryConfigs)
    {
        ImGui::Dummy(ImVec2(0.0f, 5.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 5.0f));

        // Centered Description
        float descWidth = ImGui::CalcTextSize(entry.Description.c_str()).x;
        ImGui::SetCursorPosX((contentSize.x - descWidth) * 0.5f);
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "%s", entry.Description.c_str());

        // Type / Length
        PushFontSafe(1);
        std::string typeLen = "Type: " + entry.Type + ", Length: " + std::to_string(entry.Length);
        float typeLenWidth = ImGui::CalcTextSize(typeLen.c_str()).x;
        ImGui::SetCursorPosX((contentSize.x - typeLenWidth) * 0.5f);
        ImGui::Text("%s", typeLen.c_str());

        // --- Address Combo ---
        std::vector<std::string> addressList;
        if (!entry.Address.empty()) addressList.push_back(entry.Address);
        if (!entry.Addresses.empty()) addressList.insert(addressList.end(), entry.Addresses.begin(), entry.Addresses.end());

        if (!addressList.empty())
        {
            int& selectedIndex = selectedIndexMap[entry.Description];
            if (selectedIndex >= addressList.size()) selectedIndex = 0;

            std::string current = addressList[selectedIndex];
            std::string label = "Address: ";
            float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
            float comboWidth = 150.0f;
            float totalWidth = labelWidth + comboWidth + 5.0f;
            ImGui::SetCursorPosX((contentSize.x - totalWidth) * 0.5f);

            ImGui::Text("%s", label.c_str());
            ImGui::SameLine();
            ImGui::PushItemWidth(comboWidth);
            std::string comboID = "##addr_" + entry.Description;
            if (ImGui::BeginCombo(comboID.c_str(), current.c_str(), ImGuiComboFlags_HeightLarge))
            {
                for (int i = 0; i < addressList.size(); ++i)
                {
                    bool isSelected = (selectedIndex == i);
                    if (ImGui::Selectable(addressList[i].c_str(), isSelected))
                        selectedIndex = i;
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
        }

        // --- Value Input ---
        char valueBuffer[64];
        strncpy(valueBuffer, entry.Values.c_str(), sizeof(valueBuffer));
        valueBuffer[sizeof(valueBuffer) - 1] = '\0';

        std::string label = "Value: ";
        float textWidth = ImGui::CalcTextSize(label.c_str()).x;
        float inputWidth = ImGui::CalcTextSize(valueBuffer).x + 10.0f;
        float totalWidth = textWidth + inputWidth + 5.0f;
        ImGui::SetCursorPosX((contentSize.x - totalWidth) * 0.5f);

        ImVec2 cursorPos = ImGui::GetCursorPos();
        ImGui::SetCursorPosY(cursorPos.y + 3.0f);
        ImGui::Text("%s", label.c_str());
        ImGui::SameLine();
        ImGui::SetCursorPosY(cursorPos.y);

        ImGui::PushItemWidth(inputWidth);
        std::string inputID = "##val" + entry.Description;
        if (ImGui::InputText(inputID.c_str(), valueBuffer, sizeof(valueBuffer)))
            entry.Values = valueBuffer;
        ImGui::PopItemWidth();

        PopFontSafe(1);
    }

    PopFontSafe(3);
    ImGui::End();
}

void ShowD2RHUDMenu()
{
    if (!showD2RHUDMenu) return;

    static std::string saveStatusMessage = "";
    static ImVec4 saveStatusColor = ImVec4(0, 1, 0, 1); // default green
    static bool initialized = false;
    if (!initialized)
    {
        // Initialize from cachedSettings
        d2rHUDConfig.MonsterStatsDisplay = cachedSettings.monsterStatsDisplay;
        d2rHUDConfig.HPRolloverMods = cachedSettings.HPRollover;
        d2rHUDConfig.HPRolloverPercent = cachedSettings.HPRolloverAmt;
        d2rHUDConfig.SunderedMonUMods = cachedSettings.sunderedMonUMods;
        d2rHUDConfig.SunderValue = cachedSettings.SunderValue;
        d2rHUDConfig.MinionEquality = cachedSettings.minionEquality;
        d2rHUDConfig.GambleCostControl = cachedSettings.gambleForce;
        d2rHUDConfig.CombatLog = cachedSettings.CombatLog;

        initialized = true;
    }

    ImVec2 windowSize = ImVec2(850, 420);
    ImVec2 centerPos = ImVec2((ImGui::GetIO().DisplaySize.x - windowSize.x) * 0.5f, (ImGui::GetIO().DisplaySize.y - windowSize.y) * 0.5f);
    ImGui::SetNextWindowPos(centerPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NoKeyboard;

    int fontIndex = 3;
    ImFont* fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);

    if (ImGui::Begin("D2RHUD Options", &showD2RHUDMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
    {
        // --- CENTERED WINDOW TITLE WITH TOP-RIGHT CLOSE BUTTON ---
        ImGuiIO& io = ImGui::GetIO();
        int fontIndex = 3;
        ImFont* fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
        const char* windowTitle = "D2RHUD Options";
        float closeBtnSize = 20.0f;
        float padding = -10.0f;

        // Compute window content size
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        float titleWidth = ImGui::CalcTextSize(windowTitle).x;
        ImGui::SetCursorPosX((contentSize.x - titleWidth) * 0.5f);
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "%s", windowTitle);

        // --- Same line: position close button at top-right ---
        ImGui::SameLine(contentSize.x - closeBtnSize - padding);
        ImVec2 btnPos = ImGui::GetCursorScreenPos();

        // Invisible button to capture clicks
        ImGui::InvisibleButton("CloseBtn", ImVec2(closeBtnSize, closeBtnSize));
        if (ImGui::IsItemClicked())
        {
            showD2RHUDMenu = false;
        }

        // Draw centered "X" in button
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 textSize = ImGui::CalcTextSize("X");
        ImVec2 textPos = ImVec2(
            btnPos.x + (closeBtnSize - textSize.x) * 0.5f,
            btnPos.y + (closeBtnSize - textSize.y) * 0.5f
        );
        drawList->AddText(textPos, IM_COL32(255, 80, 80, 255), "X");
        if (fontSize) ImGui::PopFont();
        ImGui::Separator();

        // --- two columns: left = controls, right = descriptions ---
        ImGui::Columns(2, nullptr, true);
        ImGui::SetColumnWidth(0, 280.0f); // width of control column

        static std::string descriptionTitle = "";
        static std::string descriptionText = "";
        static std::string descriptionNote = "";

        // --- SUBHEADER (centered in first column) ---
        ImVec2 columnStartPos = ImGui::GetCursorPos();
        float columnWidth = ImGui::GetColumnWidth();
        float headerWidth = ImGui::CalcTextSize("Current Options").x;
        ImGui::SetCursorPosX(columnStartPos.x + (columnWidth - headerWidth) * 0.5f);
        ImGui::TextColored(ImVec4(0.0157f, 0.380f, 0.8f, 1.0f), "Current Options");
        ImGui::Separator();

        // --- MENU CONTROLS (LEFT COLUMN) ---
        // helper lambda for checkboxes (keeps your original behavior)
        auto drawCheckbox = [&](const char* label, bool* value, const char* title, const char* desc, const char* descnote)
            {
                if (ImGui::Checkbox(label, value)) {}
                if (ImGui::IsItemHovered()) { descriptionTitle = title; descriptionText = desc; descriptionNote = descnote; }
            };

        auto drawLabeledInput = [&](const char* label, auto widgetFunc, const char* title, const char* desc, const char* descnote, bool sameLine = true, int slValue = 35)
            {
                ImGui::BeginGroup();
                ImGui::Text("%s", label);

                if (sameLine)
                    ImGui::SameLine(0.0f, slValue);

                widgetFunc();
                ImGui::EndGroup();

                // only store hover info — actual drawing happens in the description panel
                if (ImGui::IsItemHovered())
                {
                    descriptionTitle = title;
                    descriptionText = desc;
                    descriptionNote = descnote;
                }
            };

        drawCheckbox("Monster Stats Display", &d2rHUDConfig.MonsterStatsDisplay, "Monster Stats Display", "- Displays HP and Resistances of Monsters in real-time\n- Uses data retrieved directly from the game for accuracy\n- Values for -Enemy Resistance% (when applied by items) are not shown, they don't actually affect monsters\n\n", "This setting is best displayed using either of the Advanced Display Modes in D2RLAN");
        drawCheckbox("Sundered Monster UMods", &d2rHUDConfig.SunderedMonUMods, "Sundered Monster UMods", "- Allows Sundering mechanic to reduce MonUMod bonuses\n(Bonuses such as Cold Enchanted, Stone Skin, etc)\n- This edit applies when the monster is spawned\n\n", "If the monster is under 100 resistance (sunder threshold), then this edit will not apply");
        drawCheckbox("Minion Equality", &d2rHUDConfig.MinionEquality, "Minion Equality", "- Champ/Uniques minions assume their master's drops\n(Determined by your TreasureClassEx.txt entries)\n\n", "Requires TreasureClassEx.txt, Levels.txt, Monstats.txt and SuperUniques.txt to correctly map the drops between types");
        drawCheckbox("Gamble Cost Control", &d2rHUDConfig.GambleCostControl, "Gamble Cost Control", "- Allows the gamble cost column to be used by AMW.txt\n- If a value of -1 is specified, it will use retail cost logic\n- If Disabled, only ring and amulet costs may be changed\n\n", "The maximum gamble cost will be determined by your in-game gold limits (defined in Memory Edits)");
        drawCheckbox("Combat Log", &d2rHUDConfig.CombatLog, "Combat Log", "- Displays real-time combat logs in system chat\n    - RNG rolls for Hit, Block and Dodge results\n    - Elemental Type, +DMG%, Length and Final Damage\n    - Remaining Monster Health\n- Uses data retrieved directly from the game for accuracy\n\n", "This feature is also utilized by the 'attackinfo 1' cheat\nExpect formatting and usage to adapt over time");
        drawCheckbox("HP Rollover Mods", &d2rHUDConfig.HPRolloverMods, "HP Rollover Mods", "- Prevents HP rollovers on high player counts by:\n- Capping the maximum health bonus % applied to monsters\n- Applying damage reduction logic to the Player\n- Scales Reduction % by the calculated rollover amount\n\n", "This feature cannot guarantee no rollovers\nHowever, it should work for mods with retail-ish values");

        // --- HPRolloverDifficulty ---
        const char* difficultyItems[] = { "Normal or Higher", "Nightmare or Higher", "Hell Only" };
        int difficultyIndex = d2rHUDConfig.HPRolloverDifficulty + 1;

        // label hover detection:
        drawLabeledInput(
            "HP Rollover Difficulty",
            [&]() {
                ImGui::PushItemWidth(250);
                if (ImGui::Combo("##HPRolloverDifficulty", &difficultyIndex,
                    difficultyItems, IM_ARRAYSIZE(difficultyItems)))
                {
                    d2rHUDConfig.HPRolloverDifficulty = difficultyIndex - 1;
                }
                ImGui::PopItemWidth();
            },
            "HP Rollover Difficulty", "- Control the applied Difficulty of HP Rollover Mods\n\n", "Only valid if HP Rollover Mods are enabled", false, 35.0f
        );

        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        drawLabeledInput(
            "HP Rollover %",
            [&]() {
                ImGui::PushItemWidth(100);
                if (ImGui::InputInt("##HPRolloverPercent", &d2rHUDConfig.HPRolloverPercent, 1, 10))
                {
                    d2rHUDConfig.HPRolloverPercent =
                        std::clamp(d2rHUDConfig.HPRolloverPercent, 0, 100);
                }
                ImGui::PopItemWidth();
            },
            "HP Rollover %", "- Controls the maximum amount of Damage reduction\n\n", "Only valid if HP Rollover Mods are enabled", true, 35.0f
        );

        drawLabeledInput(
            "Sunder Value",
            [&]() {
                ImGui::PushItemWidth(100);
                if (ImGui::InputInt("##SunderValue", &d2rHUDConfig.SunderValue, 1, 10))
                {
                    d2rHUDConfig.SunderValue =
                        std::clamp(d2rHUDConfig.SunderValue, 0, 100);
                }
                ImGui::PopItemWidth();
            },
            "Sunder Value", "- Controls the Sundered monster value\n(When specified value is reached, reduction stops)\n- Only applies when monster is above 100 resistance\n- Applies edit at the time of monster spawn\n- States like Conviction will apply at full effect\n(Instead of by 1/5 if the monster is immune)\n- For TCPIP, the highest sunder value for each element\nFound among all players will be applied\n\n", "Requires compatible sunder-edited mod files to use\nMore info available at D2RModding Discord", true, 42.0f
        );

        // --- END LEFT COLUMN ---
        ImGui::NextColumn();

        // --- DESCRIPTION PANEL (RIGHT COLUMN) ---
        float colX = ImGui::GetColumnOffset(1);
        float colW = ImGui::GetColumnWidth(1);
        fontIndex = 3;
        fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
        if (fontSize) ImGui::PushFont(fontSize);

        // get column-local cursor X and width
        float colLocalX = ImGui::GetCursorPosX();
        float colWidthLocal = ImGui::GetColumnWidth(1);

        // measure title and center it (use local coords)
        float titleW = ImGui::CalcTextSize(descriptionTitle.c_str()).x;
        ImGui::SetCursorPosX((colLocalX + (colWidthLocal - titleW) * 0.5f) - 10.0f);
        ImGui::TextColored(ImVec4(0.0157f, 0.380f, 0.8f, 1.0f), "%s", descriptionTitle.c_str());
        if (fontSize) ImGui::PopFont();

        // --- Description (right column, fixed width 570px) ---
        if (!descriptionText.empty())
        {
            const float colWidth = 550.0f;
            const float paddingRight = 10.0f;
            const float wrapPos = ImGui::GetCursorPosX() + colWidth - paddingRight;
            ImGui::PushTextWrapPos(wrapPos);

            // Center the wrapped main description text
            ImVec2 textSize = ImGui::CalcTextSize(descriptionText.c_str(), nullptr, false, colWidth);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (colWidth - textSize.x) * 0.5f);
            ImGui::TextWrapped("%s", descriptionText.c_str());

            // Center the wrapped description note in orangish-yellow
            ImVec2 noteSize = ImGui::CalcTextSize(descriptionNote.c_str(), nullptr, false, colWidth);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (colWidth - noteSize.x) * 0.5f);
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "%s", descriptionNote.c_str());

            ImGui::PopTextWrapPos();
        }

        ImGui::Columns(1);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        // --- Centered buttons on the same line ---
        float windowWidth = ImGui::GetWindowSize().x;
        float buttonWidth = 150.0f;
        float spacing = 20.0f;
        float totalWidth = buttonWidth * 2 + spacing;
        ImGui::SetCursorPosX((windowWidth - totalWidth) * 0.5f);

        // --- Test Settings Button ---
        ImGui::PushID("TestSettings");
        if (ImGui::Button("Test Settings", ImVec2(buttonWidth, 0)))
        {
            // Update cachedSettings with the latest values
            cachedSettings.HPRollover = d2rHUDConfig.HPRolloverMods;
            cachedSettings.SunderValue = d2rHUDConfig.SunderValue;
            cachedSettings.monsterStatsDisplay = d2rHUDConfig.MonsterStatsDisplay;
            cachedSettings.HPRolloverAmt = d2rHUDConfig.HPRolloverPercent;
            cachedSettings.sunderedMonUMods = d2rHUDConfig.SunderedMonUMods;
            cachedSettings.minionEquality = d2rHUDConfig.MinionEquality;
            cachedSettings.gambleForce = d2rHUDConfig.GambleCostControl;
            cachedSettings.CombatLog = d2rHUDConfig.CombatLog;

            saveStatusMessage = "New Settings Applied!";
            saveStatusColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // green
        }

        if (ImGui::IsItemHovered())
        {
            descriptionTitle = "Test Settings";
            descriptionText = "- Applies your current settings to the active game session\n- These settings will not persist for future game launches\n\n";
            descriptionNote = "This applies only to the options in this panel";
        }
        ImGui::PopID();
        ImGui::SameLine(0.0f, spacing);
        ImGui::PushID("SaveConfig");

        // Make the button green
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.3f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.5f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.6f, 0.1f, 1.0f));

        if (ImGui::Button("Save Config", ImVec2(buttonWidth, 0)))
        {
            namespace fs = std::filesystem;
            std::string modName = GetModName();
            fs::path relativePath = fs::path("Mods") / modName / (modName + ".mpq") / "data" / "D2RLAN" / "config_override.json";
            fs::path configPath = fs::absolute(relativePath);

            if (fs::exists(configPath)) {
                bool result = SaveD2RHUDConfig(configPath.string());

                if (result)
                {
                    saveStatusMessage = "Config saved successfully!";
                    saveStatusColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // green
                }
                else
                {
                    saveStatusMessage = "Failed to save config!";
                    saveStatusColor = ImVec4(0.8f, 0.3f, 0.0f, 1.0f); // dark orange/red
                }
            }
            else {
                bool result = SaveD2RHUDConfig("../Launcher/config.json");

                if (result)
                {
                    saveStatusMessage = "Config saved successfully!";
                    saveStatusColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // green
                }
                else
                {
                    saveStatusMessage = "Failed to save config!";
                    saveStatusColor = ImVec4(0.8f, 0.3f, 0.0f, 1.0f); // dark orange/red
                }
            }

            // Update cached settings as before...
            cachedSettings.HPRollover = d2rHUDConfig.HPRolloverMods;
            cachedSettings.SunderValue = d2rHUDConfig.SunderValue;
            cachedSettings.monsterStatsDisplay = d2rHUDConfig.MonsterStatsDisplay;
            cachedSettings.HPRolloverAmt = d2rHUDConfig.HPRolloverPercent;
            cachedSettings.sunderedMonUMods = d2rHUDConfig.SunderedMonUMods;
            cachedSettings.minionEquality = d2rHUDConfig.MinionEquality;
            cachedSettings.gambleForce = d2rHUDConfig.GambleCostControl;
            cachedSettings.CombatLog = d2rHUDConfig.CombatLog;

            // Reload HUD config
            d2rHUDConfig.HPRolloverPercent = cachedSettings.HPRolloverAmt;
            d2rHUDConfig.SunderValue = cachedSettings.SunderValue;
            d2rHUDConfig.MonsterStatsDisplay = cachedSettings.monsterStatsDisplay;
            d2rHUDConfig.HPRolloverMods = cachedSettings.HPRollover;
            d2rHUDConfig.SunderedMonUMods = cachedSettings.sunderedMonUMods;
            d2rHUDConfig.MinionEquality = cachedSettings.minionEquality;
            d2rHUDConfig.GambleCostControl = cachedSettings.gambleForce;
            d2rHUDConfig.CombatLog = cachedSettings.CombatLog;
        }
        // Assign hover description
        if (ImGui::IsItemHovered())
        {
            descriptionTitle = "Save Config";
            descriptionText = "- Applies your current settings and updates the config file\n- This ensures your settings are kept for future launches\n\n";
            descriptionNote = "This applies only to the options in this panel";
        }

        ImGui::PopStyleColor(3);
        ImGui::PopID();

        // --- Status message below buttons ---
        if (!saveStatusMessage.empty())
        {
            ImGui::Spacing();
            ImGui::SetCursorPosX((windowWidth - ImGui::CalcTextSize(saveStatusMessage.c_str()).x) * 0.5f);
            ImGui::TextColored(saveStatusColor, "%s", saveStatusMessage.c_str());
        }

        ImGui::End();
    }
}

void ShowHUDSettingsMenu()
{
    if (!showHUDSettingsMenu)
        return;


    EnableAllInput();
    CenterWindow(ImVec2(800, 400));
    static std::string hoveredKey;
    hoveredKey.clear();
    PushFontSafe(3);
    ImGui::Begin("HUD Settings", &showHUDSettingsMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    DrawWindowTitleAndClose("HUD Settings", &showHUDSettingsMenu);
    PopFontSafe(3);
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // --- Centered Wrapped Text Helper ---
    auto CenteredWrappedText = [&](const std::string& prefix, const std::string& text,
        const ImVec4& prefixColor = ImVec4(1, 0.7f, 0.3f, 1.0f),
        const ImVec4& valueColor = ImVec4(1, 1, 1, 1))
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float leftOffset = 10.0f;
            float wrapWidth = avail.x - leftOffset;

            ImVec2 prefixSize = ImGui::CalcTextSize(prefix.c_str(), nullptr, false, wrapWidth);
            ImVec2 valueSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrapWidth);
            float totalWidth = prefixSize.x + valueSize.x;
            float cursorX = leftOffset + (wrapWidth - totalWidth) * 0.5f;
            if (cursorX < leftOffset) cursorX = leftOffset;

            ImGui::SetCursorPosX(cursorX);
            ImGui::TextColored(prefixColor, "%s", prefix.c_str());
            ImGui::SameLine(0, 0);
            ImGui::TextColored(valueColor, "%s", text.c_str());
        };

    if (!g_LootFilterHeader.Version.empty())
        CenteredWrappedText("My D2RLoot Version: ", g_LootFilterHeader.Version);
    CenteredWrappedText("My Selected Filter: ", g_LootFilterHeader.Title);

    ImGui::Dummy(ImVec2(0.0f, 3.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // --- Boolean Checkboxes ---
    auto RenderCheckboxLine = [&](const std::vector<std::pair<std::string, std::string>>& items)
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float spacing = 10.0f;
            float totalWidth = 0.0f;

            for (auto& item : items)
            {
                totalWidth += ImGui::CalcTextSize(item.first.c_str()).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetFrameHeight();
            }
            totalWidth += spacing * (items.size() - 1);

            float startX = (avail.x - totalWidth) * 0.5f;
            if (startX < 0.0f) startX = 0.0f;
            ImGui::SetCursorPosX(startX);

            for (size_t i = 0; i < items.size(); ++i)
            {
                if (i > 0) ImGui::SameLine(0.0f, spacing);

                std::string key = items[i].second;
                std::string val = "";
                auto it = g_LuaVariables.find(key);
                if (it != g_LuaVariables.end()) val = it->second;

                bool boolValue = (val == "true");

                if (ImGui::Checkbox(items[i].first.c_str(), &boolValue))
                {
                    auto it2 = g_LuaVariables.find(key);
                    if (it2 != g_LuaVariables.end()) it2->second = boolValue ? "true" : "false";
                    else g_LuaVariables.insert({ key, boolValue ? "true" : "false" });
                }

                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 itemMax = ImGui::GetItemRectMax();
                float textWidth = ImGui::CalcTextSize(items[i].first.c_str()).x;
                itemMax.x += textWidth;
                if (ImGui::IsMouseHoveringRect(itemMin, itemMax)) hoveredKey = key;
            }

            ImGui::Dummy(ImVec2(0.0f, 3.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 3.0f));
        };

    std::vector<std::pair<std::string, std::string>> bools = {
        { "Allow Overrides", "allowOverrides" },
        { "Mod Tips", "modTips" },
        { "Debug Mode", "Debug" },
        { "Audio Playback", "audioPlayback" }
    };
    RenderCheckboxLine(bools);

    // --- Input Text Helper ---
    auto RenderInputText = [&](const std::string& key, const std::string& label, const std::string& defaultVal = "Not Defined")
        {
            std::string value = defaultVal;
            auto it = g_LuaVariables.find(key);
            if (it != g_LuaVariables.end()) value = it->second;
            if (!value.empty() && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);

            std::string fullLabel = label + " = ";
            float labelWidth = ImGui::CalcTextSize(fullLabel.c_str()).x;
            float valueWidth = ImGui::CalcTextSize(value.c_str()).x + 8.0f;

            ImVec2 cursorPos = ImGui::GetCursorPos();
            ImGui::SetCursorPosY(cursorPos.y - 2.0f);

            ImGui::Text("%s", fullLabel.c_str());
            ImGui::SameLine(labelWidth + 10.0f, -3.0f);

            char buffer[256];
            strncpy(buffer, value.c_str(), sizeof(buffer));
            buffer[sizeof(buffer) - 1] = '\0';

            std::string inputID = "##val_" + key;
            ImGui::PushItemWidth(valueWidth);
            bool changed = ImGui::InputText(inputID.c_str(), buffer, sizeof(buffer));
            if (ImGui::IsItemActivated()) ImGui::SetKeyboardFocusHere(-1);
            if (changed)
            {
                auto it2 = g_LuaVariables.find(key);
                if (it2 != g_LuaVariables.end()) it2->second = buffer;
                else g_LuaVariables.insert({ key, buffer });
            }
            ImGui::PopItemWidth();

            ImVec2 itemMin = ImGui::GetItemRectMin();
            ImVec2 itemMax = ImGui::GetItemRectMax();
            itemMin.x -= labelWidth;
            if (ImGui::IsMouseHoveringRect(itemMin, itemMax)) hoveredKey = key;
        };

    RenderInputText("reload", "Reload Message");
    RenderInputText("audioVoice", "Audio Voice");
    RenderInputText("filter_level", "Filter Level");
    RenderInputText("language", "Language");

    // --- Filter Titles ---
    auto RenderFilterTitles = [&]()
        {
            std::string key = "filter_titles";
            std::string value = "";
            auto it = g_LuaVariables.find(key);
            if (it != g_LuaVariables.end()) value = it->second;

            std::vector<std::string> titles;
            if (!value.empty())
            {
                std::regex titleRegex(R"delim("([^"]*)")delim");
                for (auto i = std::sregex_iterator(value.begin(), value.end(), titleRegex);
                    i != std::sregex_iterator(); ++i)
                    titles.push_back((*i)[1].str());
            }
            if (titles.empty()) titles.push_back("Not Defined");

            std::string ftLabel = "Filter Titles = ";
            float labelWidth = ImGui::CalcTextSize(ftLabel.c_str()).x;
            ImGui::Text("%s", ftLabel.c_str());
            ImGui::SameLine(labelWidth + 10.0f);

            for (size_t idx = 0; idx < titles.size(); ++idx)
            {
                char buffer[256];
                strncpy(buffer, titles[idx].c_str(), sizeof(buffer));
                buffer[sizeof(buffer) - 1] = '\0';

                float textWidth = ImGui::CalcTextSize(buffer).x;
                ImGui::PushItemWidth(textWidth + 8.0f);

                std::string inputID = "##filter_title_" + std::to_string(idx);
                bool changed = ImGui::InputText(inputID.c_str(), buffer, sizeof(buffer));
                ImGui::PopItemWidth();

                if (changed) titles[idx] = buffer;
                if (idx + 1 < titles.size()) { ImGui::SameLine(0, 2); ImGui::Text(", "); ImGui::SameLine(0, 0); }

                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 itemMax = ImGui::GetItemRectMax();
                itemMin.x -= labelWidth;
                if (ImGui::IsMouseHoveringRect(itemMin, itemMax)) hoveredKey = key;
            }

            std::string newValue = "{ ";
            for (size_t i = 0; i < titles.size(); ++i)
            {
                newValue += "\"" + titles[i] + "\"";
                if (i + 1 < titles.size()) newValue += ", ";
            }
            newValue += " }";

            auto it2 = g_LuaVariables.find(key);
            if (it2 != g_LuaVariables.end()) it2->second = newValue;
            else g_LuaVariables.insert({ key, newValue });
        };
    RenderFilterTitles();

    // --- Bottom Description ---
    std::string desc = "Hover over an option to see its description.";
    if (!hoveredKey.empty() && g_LuaDescriptions.count(hoveredKey))
        desc = g_LuaDescriptions.at(hoveredKey);

    DrawBottomDescription(desc);

    ImGui::End();
}

void ShowMainMenu()
{
    if (!showMainMenu)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 windowSize = ImVec2(640, 400);

    // Center the window only on the first run
    static bool firstRun = true;
    if (firstRun)
    {
        ImVec2 centerPos = ImVec2((io.DisplaySize.x - windowSize.x) * 0.5f, (io.DisplaySize.y - windowSize.y) * 0.5f);
        ImGui::SetNextWindowPos(centerPos, ImGuiCond_Once);
        firstRun = false;
    }

    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::Begin("D2RHUD Control Center", &showMainMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

    // --- HEADER ---
    int fontIndex = 3;
    ImFont* fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);

    /*
    // ---- Hud Settings Button ----
    {
        const char* gearIcon = reinterpret_cast<const char*>(u8"\u216C"); // or u8"⚙🔧"
        float btnSize = 20.0f;
        float padding = 35.0f;

        ImVec2 startPos = ImGui::GetCursorScreenPos();  // top-left of window content

        // Position gear on far left
        ImGui::SetCursorPosX(padding);

        // Button hitbox
        ImGui::InvisibleButton("SettingsBtn", ImVec2(btnSize, btnSize));
        bool gearClicked = ImGui::IsItemClicked();

        // Draw the icon centered in the hitbox
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 gearPos = ImGui::GetItemRectMin();
        ImVec2 gearSize = ImGui::CalcTextSize(gearIcon);

        dl->AddText(ImVec2(
            gearPos.x + (btnSize - gearSize.x) * 0.5f,
            gearPos.y + (btnSize - gearSize.y) * 0.5f
        ), IM_COL32(230, 230, 230, 255), gearIcon);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Open D2RHUDCC Settings");

        if (gearClicked)
        {
            showHUDSettingsMenu = true;
            
        }
        ShowHUDSettingsMenu();
    }
    */

    // --- TITLE AND CLOSE BUTTON ---
    const char* windowTitle = "D2RHUD Control Center";
    float closeBtnSize = 20.0f;
    float padding = 5.0f;
    ImVec2 contentSize = ImGui::GetContentRegionAvail();

    float titleWidth = ImGui::CalcTextSize(windowTitle).x;
    ImGui::SetCursorPosX((contentSize.x - titleWidth) * 0.5f);
    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "%s", windowTitle);

    ImGui::SameLine(contentSize.x - closeBtnSize - padding);
    ImVec2 btnPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("CloseBtn", ImVec2(closeBtnSize, closeBtnSize));
    if (ImGui::IsItemClicked()) showMainMenu = false;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 textSize = ImGui::CalcTextSize("X");
    ImVec2 textPos = ImVec2(btnPos.x + (closeBtnSize - textSize.x) * 0.5f,
        btnPos.y + (closeBtnSize - textSize.y) * 0.5f);
    drawList->AddText(textPos, IM_COL32(255, 80, 80, 255), "X");
    if (fontSize) ImGui::PopFont();

    fontIndex = 1;
    fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);
    ImGuiTextCentered("Quickly view or command custom options offered by D2RHUD");
    ImGuiTextCentered("This menu can be toggled using the hotkey set in D2RLAN");
    if (fontSize) ImGui::PopFont();

    fontIndex = 2;
    fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);

    // Prepare version line
    std::string label = "Version:";
    std::string number = Version;
    ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
    ImVec2 numberSize = ImGui::CalcTextSize(number.c_str());
    float totalWidth = labelSize.x + numberSize.x;
    float contentWidth = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
    ImGui::SetCursorPosX((contentWidth - totalWidth) * 0.5f);

    // Draw the two colored segments
    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "%s", label.c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.0157f, 0.380f, 0.8f, 1.0f), "%s", number.c_str());

    if (fontSize) ImGui::PopFont();
    ImGui::Separator();

    // --- MENU BUTTONS AND DESCRIPTION AREA ---
    float buttonWidth = 200.0f;
    float buttonHeight = 50.0f;
    float separatorX = buttonWidth + 20.0f;
    float descriptionWidth = windowSize.x - separatorX - 15.0f;
    const char* descriptionTitle = "";
    const char* descriptionText = "";
    const char* descriptionNote = "";

    fontIndex = 2;
    fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);
    
    if (ImGui::Button("D2RHUD Options", ImVec2(buttonWidth, buttonHeight)))
        showD2RHUDMenu = true;
    ShowD2RHUDMenu();
    if (ImGui::IsItemHovered()) { descriptionTitle = "D2RHUD Options"; descriptionText = "Explore and Control enabled D2RHUD Options\n\n- Values are retrieved and stored in D2RLAN/Launcher/config.json\n- Overrides can be applied by the author in data/D2RLAN/config_override.json\n- Expect implementation changes over the next updates"; }
    
    if (ImGui::Button("Memory Edit Info", ImVec2(buttonWidth, buttonHeight)))
        showMemoryMenu = true;
    ShowMemoryMenu();
    if (ImGui::IsItemHovered()) { descriptionTitle = "Memory Edit Info"; descriptionText = "View your currently active memory edits\n\n- Values are retrieved and stored in D2RLAN/Launcher/config.json\n- These edits provide additional 'hardcode only' options to the game\n- For some entries, game restart will be needed for them to apply\n- This panel is currently read-only during early development"; }
    
    if (ImGui::Button("D2RLoot Settings", ImVec2(buttonWidth, buttonHeight)))
        showLootMenu = true;
    ShowLootMenu();
    if (ImGui::IsItemHovered()) { descriptionTitle = "D2RLoot Settings"; descriptionText = "Explore and control your currently active loot filter\n\n- Filters operate in real-time with user-defined rules\n- Accessible in D2RLAN > Options > Loot Filter\n- Rules are defined in D2RLAN/D2R/lootfilter_config.lua"; }
    
    if (ImGui::Button("Hotkey Controls", ImVec2(buttonWidth, buttonHeight)))
        showHotkeyMenu = true;
    ShowHotkeyMenu();
    if (ImGui::IsItemHovered()) { descriptionTitle = "Hotkey Controls"; descriptionText = "Manage your hotkeys used by various tools\n\n- Hotkeys are achieved by utilizing internal game functions\n- They can also be used to dynamically control your loot filter\n- Hotkeys are defined in D2RLAN/Launcher/D2RLAN_Config.txt"; }

    if (ImGui::Button("Grail Tracker", ImVec2(buttonWidth, buttonHeight)))
        showGrailMenu = true;
    ShowGrailMenu();
    if (ImGui::IsItemHovered()) { descriptionTitle = "Grail Tracker"; descriptionText = "View the progress of your Set/Unique item hunting\n\n- Grail Entries are manually stored for now\n- This feature works for all mods* (or TCP)\n(Mod must have included set/unique items.txt files)\n- Grail Progress/Settings are stored in D2RLAN/D2R/Grail_Settings_ModName.json"; }
    if (fontSize) ImGui::PopFont();

    // Vertical separator + Description Panel
    ImGui::GetWindowDrawList()->AddLine(ImVec2(ImGui::GetWindowPos().x + separatorX, ImGui::GetWindowPos().y + 100.0f), ImVec2(ImGui::GetWindowPos().x + separatorX, ImGui::GetWindowPos().y + windowSize.y - 3.0f), IM_COL32(180, 150, 80, 255), 2.0f);
    ImGui::SetCursorPosX(separatorX - 200.0f);
    ImGui::SetCursorPosY(100.0f);

    // Title style
    fontIndex = 3;
    fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);
    if (descriptionTitle && descriptionTitle[0] != '\0')
    {
        ImVec2 textSize = ImGui::CalcTextSize(descriptionTitle);
        ImGui::SetCursorPosX(separatorX + 5.0f + (descriptionWidth - textSize.x) * 0.5f);
        ImGui::TextColored(ImVec4(0.0157f, 0.380f, 0.8f, 1.0f), descriptionTitle);
    }
    if (fontSize) ImGui::PopFont();

    // Description style
    fontIndex = 1;
    fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);
    if (descriptionText && descriptionText[0] != '\0')
    {
        ImVec2 textSize = ImGui::CalcTextSize(descriptionText, nullptr, false, descriptionWidth);
        ImGui::SetCursorPosX(separatorX + 5.0f + (descriptionWidth - textSize.x) * 0.5f);
        ImGui::TextWrapped("%s", descriptionText);
    }
    if (fontSize) ImGui::PopFont();

    ImGui::End();
}

#pragma endregion

#pragma endregion

#pragma region Game Hooks

void __fastcall Hooked_D2GAME_UMOD8Array_1402fc530(D2UnitStrc* pUnit, int32_t nUMod, int32_t bUnique)
{
    // Call the original function
    oD2GAME_UMOD8Array_1402fc530(pUnit, nUMod, bUnique);

    if (!pUnit)
        return;

    // Get stored remainders
    RemainderEntry remainders = GetRemainder(pUnit);

    auto& processedStats = g_unitsEditedStats[pUnit->dwUnitId];

    // Helper lambda to handle each stat
    auto ApplyFinalValue = [&](D2C_ItemStats statId, int remainder, const char* name)
        {
            // Skip if this stat was already processed for this unit
            if (processedStats.find(statId) != processedStats.end())
                return;

            int nCurrentValue = STATLIST_GetUnitStatSigned(pUnit, statId, 0);
            int finalValue = 0;
            int wastedValue = 0;

            if ((nCurrentValue >= cachedSettings.SunderValue + 1) && cachedSettings.sunderedMonUMods == true)
            {
                finalValue = nCurrentValue - remainder;
                LogSpawnDebug("  Stat %s: nCurrentValue=%d, finalValue=%d, wastedValue=%d, remainder=%d", name, nCurrentValue, finalValue, wastedValue, remainder);

                if (finalValue < cachedSettings.SunderValue)
                {
                    wastedValue = cachedSettings.SunderValue - finalValue;
                    finalValue = cachedSettings.SunderValue;
                    LogSpawnDebug("  Stat %s: nCurrentValue=%d, finalValue=%d, wastedValue=%d, remainder=%d", name, nCurrentValue, finalValue, wastedValue, remainder);
                }

                STATLISTEX_SetStatListExStat(pUnit->pStatListEx, statId, finalValue, 0);

                // Mark this stat as processed for this unit
                processedStats.insert(statId);
            }
        };

    // Apply for all six resistances
    ApplyFinalValue(STAT_COLDRESIST, remainders.cold, "Cold");
    ApplyFinalValue(STAT_FIRERESIST, remainders.fire, "Fire");
    ApplyFinalValue(STAT_LIGHTRESIST, remainders.light, "Light");
    ApplyFinalValue(STAT_POISONRESIST, remainders.poison, "Poison");
    ApplyFinalValue(STAT_DAMAGERESIST, remainders.damage, "Damage");
    ApplyFinalValue(STAT_MAGICRESIST, remainders.magic, "Magic");
}

void __fastcall HookedMONSTER_InitializeStatsAndSkills(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, D2UnitStrc* pUnit, int64_t* pMonRegData)
{
    oMONSTER_InitializeStatsAndSkills(pGame, pRoom, pUnit, pMonRegData);

    if (!pUnit || pUnit->dwUnitType != UNIT_MONSTER || !pUnit->pMonsterData || !pUnit->pMonsterData->pMonstatsTxt)
        return;

    int32_t nClassId = pUnit->dwClassId;
    auto pMonStatsTxtRecord = pUnit->pMonsterData->pMonstatsTxt;
    auto wMonStatsEx = sgptDataTables->pMonStatsTxt[nClassId].wMonStatsEx;

    if (wMonStatsEx >= sgptDataTables->nMonStats2TxtRecordCount)
        return;

    D2UnitStrc* pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);
    if (!pUnitPlayer)
        return;

    int difficulty = GetPlayerDifficulty(pUnitPlayer);
    if (difficulty < 0 || difficulty > 2)
        return;

    D2MonStatsInitStrc monStatsInit = {};

    // Build the file path ONCE (outside the lambda)
    if (gZonesFilePath.empty())
    {
        gZonesFilePath = GetExecutableDir();
        gZonesFilePath += "/Mods/";
        gZonesFilePath += GetModName();
        gZonesFilePath += "/";
        gZonesFilePath += GetModName();
        gZonesFilePath += ".mpq/data/hd/global/excel/desecratedzones.json";
    }

    std::call_once(gZonesLoadedFlag, []() {
        if (LoadDesecratedZones(gZonesFilePath))
            gZonesLoaded = true;
        });

    ApplyGhettoSunder(pGame, pRoom, pUnit, pMonRegData, &monStatsInit);

    if (!GetBaalQuest(pUnitPlayer, pGame))
        return;

    if (gZonesLoaded)
        ApplyGhettoTerrorZone(pGame, pRoom, pUnit, pMonRegData, &monStatsInit);

    ApplyMonsterDifficultyScalingNonTZ(pUnit, difficulty, playerLevel, playerCountGlobal, pGame);

    time_t currentUtc = std::time(nullptr);

    for (const auto& zone : gDesecratedZones)
    {
        const DifficultySettings* difficultySettings = nullptr;
        switch (difficulty)
        {
        case 0: difficultySettings = &zone.default_normal; break;
        case 1: difficultySettings = &zone.default_nightmare; break;
        case 2: difficultySettings = &zone.default_hell; break;
        }
        if (!difficultySettings)
            continue;

        //ApplyStatAdjustments(pGame, pRoom, pUnit, pMonRegData, &monStatsInit, *difficultySettings);
    }
}

uint32_t __fastcall Hooked_ITEMS_CalculateGambleCost(D2UnitStrc* pItem, int nPlayerLevel)
{
    if (!pItem || !pItem->pItemData || !sgptDataTables || !sgptDataTables->pItemsTxt)
        return oGambleForce(pItem, nPlayerLevel);

    if (cachedSettings.gambleForce)
    {
        D2ItemsTxt* itemTxt = &sgptDataTables->pItemsTxt[pItem->dwClassId];

        if (itemTxt->dwGambleCost == -1)
            return oGambleForce(pItem, nPlayerLevel);
        else
            return itemTxt->dwGambleCost;
    }
    else
        return oGambleForce(pItem, nPlayerLevel);
}

int64_t Hooked_HUDWarnings__PopulateHUDWarnings(void* pWidget) {
    D2GameStrc* pGame = nullptr;
    D2Client* pGameClient = GetClientPtr();
    D2UnitStrc* pUnitPlayer = nullptr;

    if (pGameClient != nullptr) {
        pGame = (D2GameStrc*)pGameClient->pGame;
        pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);
    }

    auto result = oHUDWarnings__PopulateHUDWarnings(pWidget);

    void* tzInfoTextWidget = WidgetFindChild(pWidget, "TerrorZoneInfoText");
    void* tzStatAdjustmentsWidget = WidgetFindChild(pWidget, "TerrorZoneStatAdjustments");

    if (!tzInfoTextWidget && !tzStatAdjustmentsWidget) {
        return result;
    }

    if (!GetBaalQuest(pUnitPlayer, pGame)) {
        return result;
    }

    // TerrorZoneInfoText
    if (tzInfoTextWidget) {
        char** pOriginal = (char**)((int64_t)tzInfoTextWidget + 0x88);
        int64_t* nLength = (int64_t*)((int64_t)tzInfoTextWidget + 0x90);

        std::string finalText = BuildTerrorZoneInfoText();
        if (!finalText.empty()) {
            strncpy(gTZInfoText, finalText.c_str(), sizeof(gTZInfoText) - 1);
            gTZInfoText[sizeof(gTZInfoText) - 1] = '\0';

            *pOriginal = gTZInfoText;
            *nLength = strlen(gTZInfoText) + 1;
        }
    }

    // TerrorZoneStatAdjustments
    if (tzStatAdjustmentsWidget) {
        char** pOriginal = (char**)((int64_t)tzStatAdjustmentsWidget + 0x88);
        int64_t* nLength = (int64_t*)((int64_t)tzStatAdjustmentsWidget + 0x90);

        std::string finalText = BuildTerrorZoneStatAdjustmentsText();

        if (finalText.empty()) {
            gTZStatAdjText[0] = '\0';
            *pOriginal = gTZStatAdjText;
            *nLength = 0;
        }
        else {
            strncpy(gTZStatAdjText, finalText.c_str(), sizeof(gTZStatAdjText) - 1);
            gTZStatAdjText[sizeof(gTZStatAdjText) - 1] = '\0';
            *pOriginal = gTZStatAdjText;
            *nLength = strlen(gTZStatAdjText) + 1;
        }
    }

    return result;
}

void Hooked__Widget__OnClose(void* pWidget) {
    oWidget__OnClose(pWidget);
    char* pName = *(reinterpret_cast<char**>(reinterpret_cast<char*>(pWidget) + 0x8));
    if (strcmp(pName, "AutoMap") == 0) {
        gTZInfoText[0] = '\0';
        gTZStatAdjText[0] = '\0';
    }
}

void __fastcall HookedDropTCTest(D2GameStrc* pGame, D2UnitStrc* pMonster, D2UnitStrc* pPlayer, int32_t nTCId, int32_t nQuality, int32_t nItemLevel, int32_t a7, D2UnitStrc** ppItems, int32_t* pnItemsDropped, int32_t nMaxItems)
{
    if (isTerrorized == false)
    {
        oDropTCTest(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);
        return;
    }
    else
        ForceTCDrops(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);
}

#pragma endregion

#pragma region Draw Loop for Detours and Stats Display
void D2RHUD::OnDraw() {
    D2GameStrc* pGame = nullptr;
    D2Client* pGameClient = GetClientPtr();

    if (pGameClient != nullptr)
        pGame = (D2GameStrc*)pGameClient->pGame;

    if (!menuClickHookInstalled)
    {
        mainMenuClickHandlerOrig = reinterpret_cast<GameMenuOnClickHandler>(Pattern::Address(mainMenuClickHandlerOffset));
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)mainMenuClickHandlerOrig, GameMenuOnClickHandlerHook);
        DetourTransactionCommit();
        CCMD_DEBUGCHEAT_Handler_Orig = reinterpret_cast<CCMD_HANDLER_Fptr>(Pattern::Address(CCMD_DEBUGCHEAT_HandlerOffset));
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)CCMD_DEBUGCHEAT_Handler_Orig, CCMD_DEBUGCHEAT_Hook);
        DetourTransactionCommit();
        Process_SCMD_CHATSTART_Orig = reinterpret_cast<Process_SCMD_CHATSTART_Fptr>(Pattern::Address(Process_SCMD_CHATSTARTOffset));
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)Process_SCMD_CHATSTART_Orig, Process_SCMD_CHATSTART_Hook);
        DetourTransactionCommit();
        menuClickHookInstalled = true;

        g_ItemFilterStatusMessage = std::format("D2RHUD {} Loaded Successfully!", Version);
        g_ShouldShowItemFilterMessage = true;
        g_ItemFilterMessageStartTime = std::chrono::steady_clock::now();
    }

    if (g_ShouldShowItemFilterMessage)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_ItemFilterMessageStartTime);
        int timeoutSeconds = 3; //default time
        ImU32 color = IM_COL32(199, 179, 119, 255); //default gold
        int fontIndex = 4; //default font size

        if (g_ItemFilterStatusMessage.find("D2RHUD") != std::string::npos)
        {
            color = IM_COL32(3, 110, 32, 255); // green
            timeoutSeconds = 5;
            fontIndex = 3;
        }

        if (elapsed.count() < timeoutSeconds)
        {
            ImGuiIO& io = ImGui::GetIO();
            ImFont* chosenFont = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size)
                ? io.Fonts->Fonts[fontIndex]
                : nullptr;

                if (chosenFont)
                    ImGui::PushFont(chosenFont);

                auto drawList = ImGui::GetBackgroundDrawList();
                ImVec2 screenSize = io.DisplaySize;
                ImVec2 textSize = ImGui::CalcTextSize(g_ItemFilterStatusMessage.c_str());
                ImVec2 textPos = ImVec2((screenSize.x - textSize.x) * 0.5f,
                    (screenSize.y - textSize.y) * 0.1f);

                drawList->AddText(textPos, color, g_ItemFilterStatusMessage.c_str());

                if (chosenFont)
                    ImGui::PopFont();
        }
        else
            g_ShouldShowItemFilterMessage = false;
    }

    if (!oBankPanelDraw) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oBankPanelDraw = reinterpret_cast<BankPanelDraw_t>(Pattern::Address(bankPanelDraw));
        DetourAttach(&(PVOID&)oBankPanelDraw, HookedBankPanelDraw);
        DetourTransactionCommit();
        UpdateStashFileName(gSelectedPage);
        DWORD oldProtect = 0;
        VirtualProtect(&gpCCMDHandlerTable[CCMD_CUSTOM_OP_CODE], sizeof(D2CCMDStrc), PAGE_EXECUTE_READWRITE, &oldProtect);
        gpCCMDHandlerTable[CCMD_CUSTOM_OP_CODE].pfHandler = (CCMDHANDLER*)&CCMDHANDLER_Custom;
        VirtualProtect(&gpCCMDHandlerTable[CCMD_CUSTOM_OP_CODE], sizeof(D2CCMDStrc), oldProtect, &oldProtect);

        VirtualProtect(&gpSCMDHandlerTable[SCMD_CUSTOM_OP_CODE], sizeof(D2SCMDStrc), PAGE_EXECUTE_READWRITE, &oldProtect);
        gpSCMDHandlerTable[SCMD_CUSTOM_OP_CODE].pfHandler = (SCMDHANDLER*)&SCMDHANDLER_Custom;
        VirtualProtect(&gpSCMDHandlerTable[SCMD_CUSTOM_OP_CODE], sizeof(D2SCMDStrc), oldProtect, &oldProtect);

        // Patch default "generate new shared stash" code
        size_t nSize = 0x10e5e6 - 0x10e45a;
        auto SharedStashGenerate = (uint8_t*)Pattern::Address(0x10e45a);
        VirtualProtect(SharedStashGenerate, nSize, PAGE_EXECUTE_READWRITE, &oldProtect);
        memset(SharedStashGenerate, 0x90, nSize);   //noop it.
        uint8_t* p = SharedStashGenerate;
        *p++ = 0x48; *p++ = 0xB8;                          // mov rax, imm64
        *(uint64_t*)p = (uint64_t)&GenerateSharedStash;    // imm64
        p += 8;
        *p++ = 0xFF; *p++ = 0xD0;                          // call rax
        VirtualProtect(SharedStashGenerate, nSize, oldProtect, &oldProtect);
    }

    if (!oCCMD_ProcessClientSystemMessage) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oCCMD_ProcessClientSystemMessage = reinterpret_cast<CCMD_ProcessClientSystemMessage_t>(Pattern::Address(ccmdProcessClientSystemMessage));
        DetourAttach(&(PVOID&)oCCMD_ProcessClientSystemMessage, CCMD_ProcessClientSystemMessageHook);
        DetourTransactionCommit();
    }

    if (cachedSettings.HPRollover && !oSUNITDMG_ApplyResistancesAndAbsorb) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oSUNITDMG_ApplyResistancesAndAbsorb = reinterpret_cast<SUNITDMG_ApplyResistancesAndAbsorb_t>(Pattern::Address(0x3253d0));
        DetourAttach(&(PVOID&)oSUNITDMG_ApplyResistancesAndAbsorb, HookedSUNITDMG_ApplyResistancesAndAbsorb);
        DetourTransactionCommit();
    }

    if (showMainMenu)
        ShowMainMenu();

    /*
    if (!oD2GAME_UModInit)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oD2GAME_UModInit = reinterpret_cast<D2GAME_UModInit_t>(Pattern::Address(0x2FDDB0));
        DetourAttach(&(PVOID&)oD2GAME_UModInit, Hooked_D2GAME_UModInit);
        DetourTransactionCommit();
    }

    if (!oD2GAME_SpawnChampUnique_1402fddd0)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oD2GAME_SpawnChampUnique_1402fddd0 = reinterpret_cast<D2GAME_SpawnChampUnique_t>(Pattern::Address(0x2FDDD0));
        DetourAttach(&(PVOID&)oD2GAME_SpawnChampUnique_1402fddd0, Hooked_D2GAME_SpawnChampUnique_1402fddd0);
        DetourTransactionCommit();
    }
    */

    
    if (!oD2GAME_UMOD8Array_1402fc530)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oD2GAME_UMOD8Array_1402fc530 = reinterpret_cast<D2GAME_UMOD8Array_t>(Pattern::Address(0x2FC530));
        DetourAttach(&(PVOID&)oD2GAME_UMOD8Array_1402fc530, Hooked_D2GAME_UMOD8Array_1402fc530);
        DetourTransactionCommit();
    }

    /*
    if (!oD2GAME_SpawnMonsters_140301b5f)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oD2GAME_SpawnMonsters_140301b5f = reinterpret_cast<D2GAME_SpawnMonsters_t>(Pattern::Address(0x301B5F));
        DetourAttach(&(PVOID&)oD2GAME_SpawnMonsters_140301b5f, Hooked_D2GAME_SpawnMonsters_140301b5f);
        DetourTransactionCommit();
    }
    */

    if (cachedSettings.HPRollover && !oMONSTER_GetPlayerCountBonus) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oMONSTER_GetPlayerCountBonus = reinterpret_cast<MONSTER_GetPlayerCountBonus_t>(Pattern::Address(0x341120));
        DetourAttach(&(PVOID&)oMONSTER_GetPlayerCountBonus, HookedMONSTER_GetPlayerCountBonus);
        DetourTransactionCommit();

        // Patch max player count for testing
        /*
        DWORD oldProtect = 0;
        {
            for (uint32_t patches : {0x135EF8, 0x135F16, 0x1E31CCC, 0x1E31D64}) {
                auto PlayerCount = (uint32_t*)Pattern::Address(patches);
                VirtualProtect(PlayerCount, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
                *PlayerCount = 65535;
                VirtualProtect(PlayerCount, 4, oldProtect, &oldProtect);
            }
        }
        {
            size_t nSize = 17;
            auto PlayerCount = (uint8_t*)Pattern::Address(0x11FE12);
            VirtualProtect(PlayerCount, nSize, PAGE_EXECUTE_READWRITE, &oldProtect);
            memset(PlayerCount, 0x90, nSize);
            uint8_t* p = PlayerCount;
            *p++ = 0x41; *p++ = 0x89; *p++ = 0xC4;
            VirtualProtect(PlayerCount, nSize, oldProtect, &oldProtect);
        }
        {
            size_t nSize = 3;
            auto PlayerCount = (uint8_t*)Pattern::Address(0x136910);
            VirtualProtect(PlayerCount, nSize, PAGE_EXECUTE_READWRITE, &oldProtect);
            memset(PlayerCount, 0x90, nSize);
            uint8_t* p = PlayerCount;
            *p++ = 0x89; *p++ = 0xC8;
            VirtualProtect(PlayerCount, nSize, oldProtect, &oldProtect);
        }
        */

    }

    if (!oMONSTER_InitializeStatsAndSkills) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oMONSTER_InitializeStatsAndSkills = reinterpret_cast<MONSTER_InitializeStatsAndSkills_t>(Pattern::Address(0x33f380));
        DetourAttach(&(PVOID&)oMONSTER_InitializeStatsAndSkills, HookedMONSTER_InitializeStatsAndSkills);
        DetourTransactionCommit();
    }

    if (!itemFilter->bInstalled) {
        itemFilter->Install(cachedSettings);
    }

    if (!oHUDWarnings__PopulateHUDWarnings && GetClientStatus() == 1) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oHUDWarnings__PopulateHUDWarnings = reinterpret_cast<HUDWarnings__PopulateHUDWarnings_t>(Pattern::Address(0xbb2a90));
        DetourAttach(&(PVOID&)oHUDWarnings__PopulateHUDWarnings, Hooked_HUDWarnings__PopulateHUDWarnings);
        oWidget__OnClose = reinterpret_cast<Widget__OnClose_t>(Pattern::Address(0x5766f0));
        DetourAttach(&(PVOID&)oWidget__OnClose, Hooked__Widget__OnClose);
        DetourTransactionCommit();
        CheckToggleForward();

        if (cachedSettings.CombatLog == true)
            ExecuteDebugCheatFunc("attackinfo 1");
    }

    if (!oDropTCTest) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oDropTCTest = reinterpret_cast<DropTCTest_t>(Pattern::Address(0x2f9d50));
        DetourAttach(&(PVOID&)oDropTCTest, HookedDropTCTest);
        DetourTransactionCommit();
    }

    if (!oGambleForce) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oGambleForce = reinterpret_cast<GambleForce_t>(Pattern::Address(0x1FC0B0));
        DetourAttach(&(PVOID&)oGambleForce, Hooked_ITEMS_CalculateGambleCost);
        DetourTransactionCommit();
    }

    auto drawList = ImGui::GetBackgroundDrawList();
    auto min = drawList->GetClipRectMin();
    auto max = drawList->GetClipRectMax();
    auto width = max.x - min.x;
    auto center = width / 2.f;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 display_size = io.DisplaySize;
    float ypercent1 = display_size.y * 0.0745f;
    float ypercent2 = display_size.y * 0.043f;

    ImFont* selectedFont = nullptr;
    bool fontPushed = false;

    if (display_size.y <= 720)
        selectedFont = io.Fonts->Fonts[0];
    else if (display_size.y <= 900)
        selectedFont = io.Fonts->Fonts[1];
    else if (display_size.y <= 1080)
        selectedFont = io.Fonts->Fonts[2];
    else if (display_size.y <= 1440)
        selectedFont = io.Fonts->Fonts[3];
    else if (display_size.y <= 2160)
        selectedFont = io.Fonts->Fonts[4];

    if (selectedFont)
    {
        ImGui::PushFont(selectedFont);
        fontPushed = true;
    }

    do
    {
        /*
        if (!settings.monsterStatsDisplay)
        {
            if (fontPushed)
                ImGui::PopFont();
            return;
        }
        */

        if (!gMouseHover->IsHovered) break;
        if (gMouseHover->HoveredUnitType > UNIT_MONSTER) break;

        D2UnitStrc* pUnit, * pUnitServer, * pUnitPlayer;
        if (pGame != nullptr)
        {
            pUnit = UNITS_GetServerUnitByTypeAndId(pGame, gMouseHover->HoveredUnitType, gMouseHover->HoveredUnitId);
            pUnitServer = UNITS_GetServerUnitByTypeAndId(pGame, gMouseHover->HoveredUnitType, gMouseHover->HoveredUnitId);
            pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);
        }
        else
        {
            pUnit = GetClientUnitPtrFunc(Pattern::Address(unitDataOffset + 0x400 * gMouseHover->HoveredUnitType), gMouseHover->HoveredUnitId & 0x7F, gMouseHover->HoveredUnitId, gMouseHover->HoveredUnitType);
            pUnitServer = GetClientUnitPtrFunc(Pattern::Address(unitDataOffset + 0x400 * gMouseHover->HoveredUnitType), gMouseHover->HoveredUnitId & 0x7F, gMouseHover->HoveredUnitId, gMouseHover->HoveredUnitType);
        }

        if (!pUnit || !pUnitServer) break;
        if (STATLIST_GetUnitStatSigned(pUnitServer, STAT_HITPOINTS, 0) == 0) break;

        if (pUnitServer && cachedSettings.monsterStatsDisplay == true)
        {
            float totalWidth = 0.f;
            float spaceWidth = ImGui::CalcTextSize(Seperator).x;
            std::string resistances[6];
            float widths[6];

            for (int i = 0; i < 6; i++)
            {
                int resistanceValue = STATLIST_GetUnitStatSigned(pUnitServer, ResistanceStats[i], 0);
                resistances[i] = std::format("{}", resistanceValue);
            }

            for (int i = 0; i < 6; i++)
            {
                if (i > 0) totalWidth += spaceWidth;
                widths[i] = ImGui::CalcTextSize(resistances[i].c_str()).x;
                totalWidth += widths[i];
            }

            float startX = center - (totalWidth / 2.f);
            for (int i = 0; i < 6; i++)
            {
                if (i > 0) startX += spaceWidth;
                drawList->AddText({ startX, ypercent1 }, ResistanceColors[i], resistances[i].c_str());
                startX += widths[i];
            }

            if (pUnitServer)
            {
                if (pGame == nullptr)
                {
                    auto clienthp = std::format("{}%", ((STATLIST_GetUnitStatSigned(pUnitServer, STAT_HITPOINTS, 0) >> 8) * 100) / (STATLIST_GetUnitStatSigned(pUnitServer, STAT_MAXHP, 0) >> 8));
                    auto width = ImGui::CalcTextSize(clienthp.c_str()).x;
                    drawList->AddText({ center - (width / 2.0f) + 1, ypercent2 }, IM_COL32(255, 255, 255, 255), clienthp.c_str());
                }
                else
                {
                    auto hp = std::format("{} / {}", STATLIST_GetUnitStatSigned(pUnitServer, STAT_HITPOINTS, 0) >> 8, STATLIST_GetUnitStatSigned(pUnitServer, STAT_MAXHP, 0) >> 8);
                    auto width = ImGui::CalcTextSize(hp.c_str()).x;
                    drawList->AddText({ center - (width / 2.0f) + 1, ypercent2 }, IM_COL32(255, 255, 255, 255), hp.c_str());
                }

                
                
                //std::string ac = std::format("Pierce IDX: {}", STATLIST_GetUnitStatSigned(pUnitServer, STAT_ARMORCLASS, 0));
                //drawList->AddText({ 20, 10 }, IM_COL32(170, 50, 50, 255), ac.c_str());
                
            }
        }

    } while (false);

    if (fontPushed)
        ImGui::PopFont();
}

#pragma endregion

#pragma region Hotkey Handler


bool D2RHUD::OnKeyPressed(short key)
{
    struct BindingMatch { bool matched; int modifierCount; };
    struct PendingAction { int modifierCount; std::function<void()> action; };
    std::vector<PendingAction> matches;

    std::unordered_map<std::string, const char*> commands = {
        { "Transmute: ", "unused" },
        { "Identify Items: ", "idall 1" },
        { "Force Save: ", "save 1" },
        { "Reset Stats: ", "resetstats 1" },
        { "Reset Skills: ", "resetskills 1" },
        { "Remove Ground Items: ", "itemgroundclear 1" },
        { "Custom Command 1: ", nullptr },
        { "Custom Command 2: ", nullptr },
        { "Custom Command 3: ", nullptr },
        { "Custom Command 4: ", nullptr },
        { "Custom Command 5: ", nullptr },
        { "Custom Command 6: ", nullptr },
        { "Open Cube Panel: ", nullptr },
        { "Open HUDCC Menu: ", nullptr },
    };

    auto GetVirtualKeyFromName = [&](const std::string& token) -> short {
        if (token == "VK_CTRL" || token == "VK_CONTROL") return VK_CONTROL;
        if (token == "VK_SHIFT") return VK_SHIFT;
        if (token == "VK_MENU" || token == "VK_ALT") return VK_MENU;
        if (token == "VK_LBUTTON") return VK_LBUTTON;
        if (token == "VK_RBUTTON") return VK_RBUTTON;
        if (token == "VK_MBUTTON") return VK_MBUTTON;
        if (token == "VK_XBUTTON1") return VK_XBUTTON1;
        if (token == "VK_XBUTTON2") return VK_XBUTTON2;
        auto it = keyMap.find(token);
        return it != keyMap.end() ? it->second : 0;
        };

    auto GetCurrentlyPressedKeys = [&]() -> std::unordered_set<short> {
        std::unordered_set<short> pressed;
        for (auto& [name, vk] : keyMap) {
            if (GetAsyncKeyState(vk) & 0x8000) pressed.insert(vk);
        }
        return pressed;
        };

    auto IsBindingPressed = [&](const std::string& binding, const std::unordered_set<short>& pressed) -> BindingMatch {
        BindingMatch result{ false, 0 };
        std::vector<short> keys;
        size_t start = 0, end;
        while (start < binding.size()) {
            end = binding.find('+', start);
            std::string token = binding.substr(start, end - start);
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);
            short vk = GetVirtualKeyFromName(token);
            if (!vk) return result;
            keys.push_back(vk);
            if (end == std::string::npos) break;
            start = end + 1;
        }
        for (short vk : keys) if (pressed.find(vk) == pressed.end()) return result;
        result.matched = true;
        result.modifierCount = static_cast<int>(keys.size() - 1);
        return result;
        };

    auto pressedKeys = GetCurrentlyPressedKeys();

    auto CheckAndAddMatch = [&](const std::string& binding, int modifierExtra, std::function<void()> action, bool ignoreClientStatus = false) {
        if (binding.empty()) return;
        auto match = IsBindingPressed(binding, pressedKeys);
        if (match.matched && (ignoreClientStatus || GetClientStatus() == 1)) {
            matches.push_back({ match.modifierCount + modifierExtra, action });
        }
        };


    // --- Standard + Custom commands ---
    for (const auto& [name, debugCommand] : commands) {
        if (debugCommand) {
            CheckAndAddMatch(ReadCommandFromFile(filename, name), 0, [=]() {
                if (name == "Transmute: ") D2CLIENT_Transmute();
                else ExecuteDebugCheatFunc(debugCommand);
                });
        }
        else {
            std::string key, value;
            ReadCommandWithValuesFromFile(filename, name, key, value);
            CheckAndAddMatch(key, 0, [=]() {
                if (value.find("/") != std::string::npos)
                    CLIENT_playerCommand(value, value);
                else
                    ExecuteDebugCheatFunc(value.c_str());
                });
        }
    }

    // --- Open Cube Panel ---
    CheckAndAddMatch(ReadCommandFromFile(filename, "Open Cube Panel: "), 0, [=]() {
        if (!gpClientList) return;
        auto pClient = *gpClientList;
        if (!pClient || !pClient->pGame) return;
        reinterpret_cast<int32_t(__fastcall*)(D2GameStrc*, D2UnitStrc*)>(Pattern::Address(0x34F5A0))(pClient->pGame, pClient->pPlayer);
        });

    // --- Open HUDCC Panel ---
    CheckAndAddMatch(ReadCommandFromFile(filename, "Open HUDCC Menu: "), 0, [=]() mutable {
        bool wasOpen = showGrailMenu && showMainMenu;

        showMainMenu = !showMainMenu;

        // If Grail menu was open and now closing, save
        if (wasOpen && !showMainMenu)
            SaveGrailProgress("Grail_Settings_" + GetModName() + ".json", false);
        }, true);



        

    // --- Cycle Terror Zones ---
    for (const auto& tz : { "Cycle TZ Forward: ", "Cycle TZ Backward: " }) {
        CheckAndAddMatch(ReadCommandFromFile(filename, tz), 0, [=]() { if (tz == "Cycle TZ Forward: ") CheckToggleForward(); else CheckToggleBackward(); });
    }

    // --- Handle reload binding from config ---
    std::string reloadBinding = ReadCommandFromFile(filename, "Reload Game or Filter: ");
    CheckAndAddMatch(reloadBinding, 0, [=]() { if (itemFilter) itemFilter->ReloadGameFilter(); }, true);

    // --- Handle filter level binding from config ---
    std::string cyclefilterBinding = ReadCommandFromFile(filename, "Cycle Filter Level: ");
    CheckAndAddMatch(cyclefilterBinding, 0, [=]() { if (itemFilter) itemFilter->CycleFilter(); });

    // --- Toggle Stat Adjustments Display ---
    {
        std::string raw = ReadCommandFromFile(filename, "Toggle Stat Adjustments Display: ");
        if (!raw.empty()) {
            std::string boolPart, keyPart;
            auto commaPos = raw.find(',');
            if (commaPos != std::string::npos) {
                boolPart = raw.substr(0, commaPos);
                keyPart = raw.substr(commaPos + 1);
                keyPart.erase(0, keyPart.find_first_not_of(" \t"));
            }
            else keyPart = raw;

            if (!boolPart.empty()) {
                boolPart.erase(0, boolPart.find_first_not_of(" \t"));
                boolPart.erase(boolPart.find_last_not_of(" \t") + 1);
                showStatAdjusts = (boolPart == "true" || boolPart == "1");
            }

            CheckAndAddMatch(keyPart, 0, [=]() {
                showStatAdjusts = !showStatAdjusts;

                std::ifstream inFile(filename);
                std::ostringstream buffer;
                std::string line;
                std::string prefix = "Toggle Stat Adjustments Display: ";
                std::ostringstream updated;
                updated << prefix << (showStatAdjusts ? "true" : "false") << ", " << keyPart;

                while (std::getline(inFile, line)) {
                    buffer << (line.find(prefix) == 0 ? updated.str() : line) << "\n";
                }
                inFile.close();
                std::ofstream outFile(filename, std::ios::trunc);
                outFile << buffer.str();
                });
        }
    }

    // --- Version display (Ctrl + Alt + V) ---
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
        (GetAsyncKeyState(VK_MENU) & 0x8000) &&
        (GetAsyncKeyState('V') & 0x8000))
    {
        matches.push_back({ 2, [=]() { ShowVersionMessage(); OnStashPageChanged(gSelectedPage + 1); } });
    }

    // --- Execute the match with the most keys ---
    if (!matches.empty()) {
        auto best = std::max_element(matches.begin(), matches.end(),
            [](const PendingAction& a, const PendingAction& b) { return a.modifierCount < b.modifierCount; });
        best->action();
        return true;
    }

    return itemFilter->OnKeyPressed(key);
}


//Show D2RHUD Version Info as a MessageBox Popup
void D2RHUD::ShowVersionMessage()
{
    std::string parsedVersion = "Unknown";

    try
    {
        std::ifstream file(lootFile);
        if (file.is_open())
        {
            std::string line;
            std::regex versionRegex(R"(local\s+version\s*=\s*\"([^\"]+)\")");
            std::smatch match;

            while (std::getline(file, line))
            {
                if (std::regex_search(line, match, versionRegex))
                {
                    parsedVersion = match[1];
                    break;
                }
            }
        }
    }
    catch (const std::exception& ex)
    {
        parsedVersion = std::string("Error: ") + ex.what();
    }

    std::string message = "D2RHUD Version: " + Version + "\n"
        "LootFilter Version: " + parsedVersion;

    MessageBoxA(nullptr, message.c_str(), "Debug Display", MB_OK | MB_ICONINFORMATION);
}


#pragma endregion
