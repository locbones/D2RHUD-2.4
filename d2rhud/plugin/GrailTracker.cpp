#include "GrailTracker.h"
#include "../d2/json.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <ctime>
#include <unordered_set>

using json = nlohmann::json;

extern std::string modName;
extern std::string configFilePath;

static std::unordered_map<int, SetItemEntry*> g_SetById;
static std::unordered_map<std::string, SetItemEntry*> g_SetByCode;
static std::unordered_map<int, UniqueItemEntry*> g_UniqueById;
static std::unordered_map<std::string, UniqueItemEntry*> g_UniqueByCode;
static bool g_ValidBaseItemCodesLoaded = false;

static void RebuildGrailStashLookups()
{
    g_SetById.clear();
    g_SetByCode.clear();
    g_UniqueById.clear();
    g_UniqueByCode.clear();

    g_SetById.reserve(g_SetItems.size());
    g_SetByCode.reserve(g_SetItems.size());
    for (auto& s : g_SetItems)
    {
        g_SetById.emplace(s.id, &s);
        if (!s.code.empty())
            g_SetByCode[s.code] = &s;
    }

    g_UniqueById.reserve(g_UniqueItems.size());
    g_UniqueByCode.reserve(g_UniqueItems.size());
    for (auto& u : g_UniqueItems)
    {
        g_UniqueById.emplace(u.id, &u);
        if (!u.code.empty())
            g_UniqueByCode[u.code] = &u;
    }
}

std::vector<UniqueItemEntry> g_UniqueItems;
std::vector<SetItemEntry>    g_SetItems;
std::unordered_set<std::string> g_ExcludedGrailItems;
bool autoBackups = false;
bool backupWithTimestamps = false;
bool overwriteOldBackup = true;
int backupIntervalMinutes = 10;
bool triggerBackupNow = false;
std::mutex backupMutex;
char backupPath[260] = "C:\\MyGrailBackup";

static UniqueItemEntry g_StaticUniqueItemsRMD[] = {
{ 0, 0, "Amulet of the Viper", "vip", "Amulet", false }, { 1, 1, "Staff of Kings", "msf", "Staff", false }, { 2, 2, "Horadric Staff", "hst", "Staff", false }, { 3, 3, "Hell Forge Hammer", "hfh", "Hammer", false }, { 4, 4, "KhalimFlail", "qf1", "Flail", false },
{ 5, 5, "SuperKhalimFlail", "qf2", "Flail", false }, { 6, 6, "The Gnasher", "hax", "Hand Axe", false }, { 7, 7, "Deathspade", "axe", "Axe", false }, { 8, 8, "Bladebone", "2ax", "Double Axe", false }, { 9, 9, "Skull Splitter", "mpi", "Sickle (necro)", false },
{ 10, 10, "Rakescar", "wax", "War Axe", false }, { 11, 11, "Fechmars Axe", "lax", "Large Axe", false }, { 12, 12, "Goreshovel", "bax", "Broad Axe", false }, { 13, 13, "The Chieftan", "btx", "Battle Axe", false }, { 14, 14, "Brainhew", "gax", "Great Axe (barb)", false },
{ 15, 15, "The Humongous", "gix", "Giant Axe(Barb)", false }, { 16, 16, "Iros Torch", "wnd", "Wand", false }, { 17, 17, "Maelstromwrath", "ywn", "Yew Wand", false }, { 18, 18, "Gravenspine", "bwn", "Bone Wand", false }, { 19, 19, "Umes Lament", "gwn", "Grim Wand", false },
{ 20, 20, "Felloak", "clb", "Branch (Druid)", false }, { 21, 21, "Knell Striker", "scp", "Scepter", false }, { 22, 22, "Rusthandle", "gsc", "Grand Scepter", false }, { 23, 23, "Stormeye", "wsp", "War Scepter", false }, { 24, 24, "Stoutnail", "spc", "Spiked Club", false },
{ 25, 25, "Crushflange", "mac", "Mace", false }, { 26, 26, "Bloodrise", "mst", "Morning Star", false }, { 27, 27, "The Generals Tan Do Li Ga", "fla", "Flail", false }, { 28, 28, "Ironstone", "whm", "War Hammer", false }, { 29, 29, "Bonesnap", "mau", "Maul (Barb)", false },
{ 30, 30, "Steeldriver", "gma", "Great Maul (Barb)", false }, { 31, 31, "Rixots Keen", "ssd", "Short Sword", false }, { 32, 32, "Blood Crescent", "scm", "Scimitar", false }, { 33, 33, "Krintizs Skewer", "sbr", "Saber", false }, { 34, 34, "Gleamscythe", "flc", "Falchion", false },
{ 35, 35, "Light's Beacon", "crs", "Crystal Sword (Sorc)", false }, { 36, 36, "Griswold's Edge", "bsd", "Broad Sword", false }, { 37, 37, "Hellplague", "lsd", "Long Sword", false }, { 38, 38, "Culwens Point", "wsd", "War Sword", false }, { 39, 39, "Shadowfang", "2hs", "Katana (Assassin)", false },
{ 40, 40, "Soulflay", "clm", "Claymore", false }, { 41, 41, "Kinemils Awl", "gis", "Giant Sword", false }, { 42, 42, "Blacktongue", "bsw", "Bastard Sword", false }, { 43, 43, "Ripsaw", "flb", "Flamberge", false }, { 44, 44, "The Patriarch", "gsd", "Great Sword", false },
{ 45, 45, "Gull", "dgr", "Dagger", false }, { 46, 46, "The Diggler", "dir", "Dirk", false }, { 47, 47, "The Jade Tan Do", "kri", "Kris (Necro)", false }, { 48, 48, "Irices Shard", "bld", "Blade (Necro)", false }, { 49, 49, "Shadow Strike", "tkf", "Throwing Knife", false },
{ 50, 50, "Madawc's First", "tax", "Throwing Axe", false }, { 51, 51, "Carefully", "bkf", "Balanced Knife", false }, { 52, 52, "Ancient's Assualt", "bal", "Balanced Axe", false }, { 53, 53, "Harpoonist's Training", "jav", "Javelin", false }, { 54, 54, "Glorious Point", "pil", "Pilum", false },
{ 55, 55, "Not So", "ssp", "Short Spear", false }, { 56, 56, "Double Trouble", "glv", "Glaive", false }, { 57, 57, "Straight Shot", "tsp", "Throwing Spear", false }, { 58, 58, "The Dragon Chang", "spr", "Spear (Pole Now)", false }, { 59, 59, "Razortine", "tri", "Trident (Pole Now)", false },
{ 60, 60, "Bloodthief", "brn", "Brandistock(Pole Now)", false }, { 61, 61, "Lance of Yaggai", "spt", "Spetum(Pole Now)", false }, { 62, 62, "The Tannr Gorerod", "pik", "Pike(Pole Now)", false }, { 63, 63, "Dimoaks Hew", "bar", "Bardiche", false }, { 64, 64, "Steelgoad", "vou", "Voulge", false },
{ 65, 65, "Soul Harvest", "scy", "Scythe (Necro)", false }, { 66, 66, "The Battlebranch", "pax", "Poleaxe", false }, { 67, 67, "Woestave", "hal", "Halberd", false }, { 68, 68, "The Grim Reaper", "wsc", "Thresher (Necro)", false }, { 69, 69, "Bane Ash", "sst", "Short Staff", false },
{ 70, 70, "Serpent Lord", "lst", "Long Staff", false }, { 71, 71, "Lazarus Spire", "cst", "Gnarled Staff", false }, { 72, 72, "The Salamander", "bst", "Battle Staff", false }, { 73, 73, "The Iron Jang Bong", "wst", "War Staff", false }, { 74, 74, "Pluckeye", "sbw", "Short Bow", false },
{ 75, 75, "Witherstring", "hbw", "Hunter's Bow", false }, { 76, 76, "Rimeraven", "lbw", "Long Bow", false }, { 77, 77, "Piercerib", "cbw", "Composite Bow", false }, { 78, 78, "Pullspite", "sbb", "Short Battle Bow", false }, { 79, 79, "Wizendraw", "lbb", "Long Battle Bow", false },
{ 80, 80, "Hellclap", "swb", "Short War Bow", false }, { 81, 81, "Blastbark", "lwb", "Long War Bow", false }, { 82, 82, "Leadcrow", "lxb", "Light Crossbow", false }, { 83, 83, "Ichorsting", "mxb", "Crossbow", false }, { 84, 84, "Hellcast", "hxb", "Heavy Crossbow", false },
{ 85, 85, "Doomspittle", "rxb", "Repeating Crossbow", false }, { 86, 86, "Coldkill", "9ha", "Hatchet", false }, { 87, 87, "Butcher's Pupil", "9ax", "Cleaver", false }, { 88, 88, "Islestrike", "92a", "Twin Axe", false }, { 89, 89, "Pompeii's Wrath", "9mp", "Battle Sickle (Necro)", false },
{ 90, 90, "Guardian Naga", "9wa", "Naga", false }, { 91, 91, "Warlord's Trust", "9la", "Military Axe", false }, { 92, 92, "Spellsteel", "9ba", "Bearded Axe", false }, { 93, 93, "Stormrider", "9bt", "Tabar", false }, { 94, 94, "Boneslayer Blade", "9ga", "Gothic Axe (Barb)", false },
{ 95, 95, "The Minataur", "9gi", "Ancient Axe (Barb)", false }, { 96, 96, "Suicide Branch", "9wn", "Burnt Wand", false }, { 97, 97, "Carin Shard", "9yw", "Petrified Wand", false }, { 98, 98, "Arm of King Leoric", "9bw", "Tomb Wand", false }, { 99, 99, "Blackhand Key", "9gw", "Grave Wand", false },
{ 100, 100, "Dark Clan Crusher", "9sp", "Limb(Druid)", false }, { 101, 101, "Zakarum's Hand", "9sc", "Rune Scepter", false }, { 102, 102, "The Fetid Sprinkler", "9qs", "Holy Water Sprinkler", false }, { 103, 103, "Hand of Blessed Light", "9ws", "Divine Scepter", false }, { 104, 104, "Fleshrender", "9cl", "Barbed Club", false },
{ 105, 105, "Sureshrill Frost", "9ma", "Flanged Mace", false }, { 106, 106, "Moonfall", "9mt", "Jagged Star", false }, { 107, 107, "Baezil's Vortex", "9fl", "Knout", false }, { 108, 108, "Earthshaker", "9wh", "Battle Hammer", false }, { 109, 109, "Bloodtree Stump", "9m9", "War Club (Barb)", false },
{ 110, 110, "The Gavel of Pain", "9gm", "Martel de Fer (Barb)", false }, { 111, 111, "Bloodletter", "9ss", "Gladius", false }, { 112, 112, "Coldsteel Eye", "9sm", "Cutlass", false }, { 113, 113, "Hexfire", "9sb", "Shamshir", false }, { 114, 114, "Blade of Ali Baba", "9fc", "Tulwar", false },
{ 115, 115, "Ginther's Rift", "9cr", "Dimensional Blade (sorc)", false }, { 116, 116, "Headstriker", "9bs", "Battle Sword", false }, { 117, 117, "Plague Bearer", "9ls", "Rune Sword", false }, { 118, 118, "The Atlantian", "9wd", "Ancient Sword", false }, { 119, 119, "Crainte Vomir", "92h", "Katana (Assassin)", false },
{ 120, 120, "Bing Sz Wang", "9cm", "Dacian Falx", false }, { 121, 121, "The Vile Husk", "9gs", "Tusk Sword", false }, { 122, 122, "Cloudcrack", "9b9", "Gothic Sword", false }, { 123, 123, "Todesfaelle Flamme", "9fb", "Zweihander", false }, { 124, 124, "Swordguard", "9gd", "Executioner Sword", false },
{ 125, 125, "Spineripper", "9dg", "Poignard", false }, { 126, 126, "Heart Carver", "9di", "Rondel", false }, { 127, 127, "Blackbog's Sharp", "9kr", "Cinquedeas (Necro)", false }, { 128, 128, "Stormspike", "9bl", "Stilleto (Necro)", false }, { 129, 129, "Deathbit", "9tk", "battle dart (Assassin)", false },
{ 130, 130, "The Scalper", "9ta", "Francisca (Barb)", false }, { 131, 131, "Constantly Waging", "9bk", "War Dart", false }, { 132, 132, "Realm Crusher", "9b8", "Hurlbat", false }, { 133, 133, "Quickening Strikes", "9ja", "War Javelin (Barb)", false }, { 134, 134, "Shrapnel Impact", "9pi", "Great Pilum (Barb)", false },
{ 135, 135, "Tempest Flash", "9s9", "Simbilan (Barb)", false }, { 136, 136, "Untethered", "9gl", "Spiculum (Barb)", false }, { 137, 137, "Unrelenting Will", "9ts", "Harpoon (Barb)", false }, { 138, 138, "The Impaler", "9sr", "War Spear (Now Pole)", false }, { 139, 139, "Kelpie Snare", "9tr", "Fuscina (Now Pole)", false },
{ 140, 140, "Soulfeast Tine", "9br", "War Fork (Now Pole)", false }, { 141, 141, "Hone Sundan", "9st", "Yari (Now Pole)", false }, { 142, 142, "Spire of Honor", "9p9", "Lance (Now Pole)", false }, { 143, 143, "The Meat Scraper", "9b7", "Lochaber Axe", false }, { 144, 144, "Blackleach Blade", "9vo", "Bill", false },
{ 145, 145, "Athena's Wrath", "9s8", "Battle Scythe (Necro)", false }, { 146, 146, "Pierre Tombale Couant", "9pa", "Partizan", false }, { 147, 147, "Husoldal Evo", "9h9", "Bec-de-Corbin", false }, { 148, 148, "Grim's Burning Dead", "9wc", "Grim Scythe (Necro)", false }, { 149, 149, "Razorswitch", "8ss", "Jo Stalf", false },
{ 150, 150, "Ribcracker", "8ls", "Quarterstaff", false }, { 151, 151, "Chromatic Ire", "8cs", "Cedar Staff", false }, { 152, 152, "Warpspear", "8bs", "Gothic Staff", false }, { 153, 153, "Skullcollector", "8ws", "Rune Staff", false }, { 154, 154, "Skystrike", "8sb", "Edge Bow", false },
{ 155, 155, "Riphook", "8hb", "Razor Bow", false }, { 156, 156, "Kuko Shakaku", "8lb", "CedarBow", false }, { 157, 157, "Endlesshail", "8cb", "Double Bow", false }, { 158, 158, "Whichwild String", "8s8", "Short Siege Bow", false }, { 159, 159, "Cliffkiller", "8l8", "Long Siege Bow", false },
{ 160, 160, "Magewrath", "8sw", "Rune Bow", false }, { 161, 161, "Godstrike Arch", "8lw", "Gothic Bow", false }, { 162, 162, "Langer Briser", "8lx", "Arbalest", false }, { 163, 163, "Pus Spiter", "8mx", "Siege Crossbow", false }, { 164, 164, "Buriza-Do Kyanon", "8hx", "Balista", false },
{ 165, 165, "Demon Machine", "8rx", "Chu-Ko-Nu", false }, { 166, 166, "Untrained Eye", "ktr", "Katar", false }, { 167, 167, "Redemption", "wrb", "Wrist Blade", false }, { 168, 168, "Ancient Hand", "axf", "Hatchet Hands", false }, { 169, 169, "Willbreaker", "ces", "Cestus", false },
{ 170, 170, "Skyfall Grip", "clw", "Claws", false }, { 171, 171, "Oathbinder", "btl", "Blade Talons", false }, { 172, 172, "Pride's Fan", "skr", "Scissors Katar", false }, { 173, 173, "Burning Sun", "9ar", "Quhab", false }, { 174, 174, "Severance", "9wb", "Wrist Spike", false },
{ 175, 175, "Hand of Madness", "9xf", "Fascia", false }, { 176, 176, "Vanquisher", "9cs", "Hand Scythe", false }, { 177, 177, "Wind-Forged Blade", "9lw", "Greater Claws", false }, { 178, 178, "Bartuc's Cut-Throat", "9tw", "Greater Talons", false }, { 179, 179, "Void Ripper", "9qr", "Scissors Quhab", false },
{ 180, 180, "Soul-Forged Grip", "7ar", "Suwayyah", false }, { 181, 181, "Jadetalon", "7wb", "wrist sword", false }, { 182, 182, "Malignant Touch", "7xf", "War Fist", false }, { 183, 183, "Shadowkiller", "7cs", "battle cestus", false }, { 184, 184, "Firelizard's Talons", "7lw", "feral claws", false },
{ 185, 185, "Viz-Jaq'taar Order", "7tw", "Runic Talons", false }, { 186, 186, "Mage Crusher", "7qr", "Scissors Suwayyah", false }, { 187, 187, "Razoredge", "7ha", "tomahawk", false }, { 188, 188, "Glittering Crescent", "7ax", "Small Crescent", false }, { 189, 189, "Runemaster", "72a", "ettin axe", false },
{ 190, 190, "Cranebeak", "7mp", "Reaper Sickle (Necro)", false }, { 191, 191, "Deathcleaver", "7wa", "berserker axe", false }, { 192, 192, "Blessed Beheader", "7la", "Feral Axe", false }, { 193, 193, "Ethereal Edge", "7ba", "silver-edged axe", false }, { 194, 194, "Hellslayer", "7bt", "Decapitator", false },
{ 195, 195, "Messerschmidt's Reaver", "7ga", "Champion Axe (Barb)", false }, { 196, 196, "Executioner's Justice", "7gi", "glorious axe (Barb)", false }, { 197, 197, "Bane Glow", "7wn", "Polished Wand", false }, { 198, 198, "Malthael Touch", "7yw", "Ghost Wand", false }, { 199, 199, "Boneshade", "7bw", "lich wand", false },
{ 200, 200, "Deaths's Web", "7gw", "unearthed wand", false }, { 201, 201, "Nord's Tenderizer", "7cl", "Bough (Druid)", false }, { 202, 202, "Heaven's Light", "7sc", "mighty scepter", false }, { 203, 203, "The Redeemer", "7qs", "Seraph Rod", false }, { 204, 204, "Ironward", "7ws", "caduceus", false },
{ 205, 205, "Demonlimb", "7sp", "tyrant club", false }, { 206, 206, "Stormlash", "7ma", "Reinforced Mace", false }, { 207, 207, "Baranar's Star", "7mt", "Devil Star", false }, { 208, 208, "Horizon's Tornado", "7fl", "scourge", false }, { 209, 209, "Schaefer's Hammer", "7wh", "Legendary Mallet", false },
{ 210, 210, "Windhammer", "7m7", "ogre maul (Barb)", false }, { 211, 211, "The Cranium Basher", "7gm", "Thunder Maul (Barb)", false }, { 212, 212, "Vows of Promise", "7ss", "Falcata", false }, { 213, 213, "Djinnslayer", "7sm", "ataghan", false }, { 214, 214, "Bloodmoon", "7sb", "elegant blade", false },
{ 215, 215, "Starward Fencer", "7fc", "Hydra Edge", false }, { 216, 216, "Lightsabre", "7cr", "Phase Blade (Sorc)", false }, { 217, 217, "Azurewrath", "7bs", "Conquest Sword", false }, { 218, 218, "Frostwind", "7ls", "Cryptic Sword", false }, { 219, 219, "Last Legend", "7wd", "Mythical Sword", false },
{ 220, 220, "Oashi", "72h", "Shinogi (Assassin)", false }, { 221, 221, "Gleam Rod", "7cm", "Highland Blade", false }, { 222, 222, "Flamebellow", "7gs", "balrog blade", false }, { 223, 223, "Doombringer", "7b7", "Champion Sword", false }, { 224, 224, "Burning Bane", "7fb", "Colossal Sword", false },
{ 225, 225, "The Grandfather", "7gd", "Colossus Blade", false }, { 226, 226, "Wizardspike", "7dg", "Bone Knife", false }, { 227, 227, "Rapid Strike", "7di", "Mithral Point", false }, { 228, 228, "Fleshripper", "7kr", "fanged knife (Necro)", false }, { 229, 229, "Ghostflame", "7bl", "legend spike (necro)", false },
{ 230, 230, "Sentinels Call", "7tk", "Flying Knife", false }, { 231, 231, "Gimmershred", "7ta", "flying axe (Barb)", false }, { 232, 232, "Warshrike", "7bk", "winged knife (Assassin)", false }, { 233, 233, "Lacerator", "7b8", "winged axe (Barb)", false }, { 234, 234, "Contemplation", "7ja", "Hyperion Javelin", false },
{ 235, 235, "Main Hand", "7pi", "Stygian Pilum", false }, { 236, 236, "Demon's Arch", "7s7", "balrog spear", false }, { 237, 237, "Wraithflight", "7gl", "ghost glaive", false }, { 238, 238, "Gargoyle's Bite", "7ts", "winged harpoon", false }, { 239, 239, "Arioc's Needle", "7sr", "Hyperion Spear (Now Pole)", false },
{ 240, 240, "Rock Piercer", "7tr", "Stygian Pike", false }, { 241, 241, "Viperfork", "7br", "Mancatcher", false }, { 242, 242, "Flash Forward", "7st", "Ghost Spear (Now Pole)", false }, { 243, 243, "Steelpillar", "7p7", "war pike (Now Pole)", false }, { 244, 244, "Bonehew", "7o7", "ogre axe", false },
{ 245, 245, "Tundra Tamer", "7vo", "Colossus Voulge", false }, { 246, 246, "The Reaper's Toll", "7s8", "Reaper Scythe", false }, { 247, 247, "Tomb Reaver", "7pa", "cryptic axe", false }, { 248, 248, "Wind Shatter", "l17", "Glorious Axe - LB", false }, { 249, 249, "Bonespire", "7wc", "Reaper Thresher (Necro)", false },
{ 250, 250, "Natures Intention", "6bs", "Walking Stick", false }, { 251, 251, "Thermite Quicksand", "6ls", "Stalagmite", false }, { 252, 252, "Ondal's Wisdom", "6cs", "elder staff", false }, { 253, 253, "Stone Crusher", "6bs", "Shillelagh", false }, { 254, 254, "Mang Song's Lesson", "6ws", "archon staff", false },
{ 255, 255, "Cold Crow's Caw", "6sb", "Spider Bow", false }, { 256, 256, "Trembling Vortex", "6hb", "Blade Bow", false }, { 257, 257, "Corrupted String", "6lb", "Shadow Bow", false }, { 258, 258, "Gyro Blaster", "6cb", "Great Bow", false }, { 259, 259, "Underground", "6s7", "Diamond Bow", false },
{ 260, 260, "Eaglehorn", "6l7", "Crusader Bow", false }, { 261, 261, "Widowmaker", "6sw", "ward bow", false }, { 262, 262, "Windforce", "6lw", "Hydra Bow", false }, { 263, 263, "Shadow Hunter", "6lx", "Pellet Bow", false }, { 264, 264, "Amnestys Glare", "6mx", "Gorgon Crossbow", false },
{ 265, 265, "Hellrack", "6hx", "colossus crossbow", false }, { 266, 266, "Gutsiphon", "6rx", "demon crossbow", false }, { 267, 267, "Enlightener", "ob1", "Eagle Orb", false }, { 268, 268, "Endothermic Stone", "ob2", "Sacred Globe", false }, { 269, 269, "Sensor", "ob3", "Smoked Sphere", false },
{ 270, 270, "Lightning Rod", "ob4", "Clasped Orb", false }, { 271, 271, "Energizer", "ob5", "Jared's Stone", false }, { 272, 272, "The Artemis String", "am1", "Stag Bow", false }, { 273, 273, "Pinaka", "am2", "Reflex Bow", false }, { 274, 274, "The Pain Producer", "am3", "Maiden Spear", false },
{ 275, 275, "The Poking Pike", "am4", "Maiden Pike", false }, { 276, 276, "Skovos Striker", "am5", "Maiden Javelin", false }, { 277, 277, "Risen Phoenix", "ob6", "Glowing Orb", false }, { 278, 278, "Glacial Oasis", "ob7", "Crystalline Globe", false }, { 279, 279, "Thunderous", "ob8", "Cloudy Sphere", false },
{ 280, 280, "Magic", "ob9", "Sparkling Ball", false }, { 281, 281, "The Oculus", "oba", "Swirling Crystal", false }, { 282, 282, "Windraven", "am6", "Ashwood Bow", false }, { 283, 285, "Lycander's Aim", "am7", "Ceremonial Bow", false }, { 284, 286, "Titan's Revenge", "ama", "Ceremonial Javelin", false },
{ 285, 287, "Lycander's Flank", "am9", "Ceremonial Pike", false }, { 286, 288, "Above All", "obb", "Heavenly Stone", false }, { 287, 289, "Eschuta's Temper", "obc", "eldritch orb", false }, { 288, 290, "Belphegor's Beating", "obd", "Demon Heart", false }, { 289, 291, "Tempest Firey", "obe", "Vortex Orb", false },
{ 290, 292, "Death's Fathom", "obf", "dimensional shard", false }, { 291, 293, "Bloodraven's Charge", "amb", "matriarchal bow", false }, { 292, 294, "Shredwind Hell", "amc", "Grand Matron Bow", false }, { 293, 295, "Thunderstroke", "amf", "matriarchal javelin", false }, { 294, 296, "Stoneraven", "amd", "matriarchal spear", false },
{ 295, 297, "Biggin's Bonnet", "cap", "Cap", false }, { 296, 298, "Tarnhelm", "skp", "Skull Cap", false }, { 297, 299, "Coif of Glory", "hlm", "Helm", false }, { 298, 300, "Duskdeep", "fhl", "Full Helm", false }, { 299, 301, "Howltusk", "ghm", "Great Helm", false },
{ 300, 302, "Undead Crown", "crn", "Crown (Paladin)", false }, { 301, 303, "The Face of Horror", "msk", "Mask", false }, { 302, 304, "Greyform", "qui", "Quilted Armor", false }, { 303, 305, "Blinkbats Form", "lea", "Leather Armor", false }, { 304, 306, "The Centurion", "hla", "Hard Leather", false },
{ 305, 307, "Twitchthroe", "stu", "Studded Leather", false }, { 306, 308, "Darkglow", "rng", "Ring Mail", false }, { 307, 309, "Hawkmail", "scl", "Scale Mail", false }, { 308, 310, "Sparking Mail", "chn", "Chain Mail", false }, { 309, 311, "Venomsward", "brs", "Breast Plate", false },
{ 310, 312, "Iceblink", "spl", "Splint Mail", false }, { 311, 313, "Boneflesh", "plt", "Plate Mail", false }, { 312, 314, "Rockfleece", "fld", "Field Plate", false }, { 313, 315, "Rattlecage", "gth", "Gothic Plate", false }, { 314, 316, "Goldskin", "ful", "Full Plate Mail", false },
{ 315, 317, "Victors Silk", "aar", "AncientArmor", false }, { 316, 318, "Heavenly Garb", "ltp", "Light Plate", false }, { 317, 319, "Pelta Lunata", "buc", "Buckler", false }, { 318, 320, "Umbral Disk", "sml", "Small Shield", false }, { 319, 321, "Stormguild", "lrg", "Large Shield (Zon)", false },
{ 320, 322, "Steelclash", "kit", "Kite Shield", false }, { 321, 323, "Bverrit Keep", "tow", "Tower Shield", false }, { 322, 324, "The Ward", "gts", "Gothic Shield", false }, { 323, 325, "The Hand of Broc", "lgl", "Gloves", false }, { 324, 326, "Bloodfist", "vgl", "Heavy Gloves", false },
{ 325, 327, "Chance Guards", "mgl", "Bracers", false }, { 326, 328, "Magefist", "tgl", "Light Gauntlets", false }, { 327, 329, "Frostburn", "hgl", "Gauntlets", false }, { 328, 330, "Hotspur", "lbt", "Leather Boots", false }, { 329, 331, "Gorefoot", "vbt", "Heavy Boots", false },
{ 330, 332, "Treads of Cthon", "mbt", "Chain Boots", false }, { 331, 333, "Goblin Toe", "tbt", "Light Plate Boots", false }, { 332, 334, "Tearhaunch", "hbt", "Plate Boots", false }, { 333, 335, "Lenyms Cord", "lbl", "Sash", false }, { 334, 336, "Snakecord", "vbl", "Light Belt", false },
{ 335, 337, "Nightsmoke", "mbl", "Belt", false }, { 336, 338, "Goldwrap", "tbl", "Heavy Belt", false }, { 337, 339, "Bladebuckle", "hbl", "Girdle", false }, { 338, 340, "Wormskull", "bhm", "Bone Helm (Necro)", false }, { 339, 341, "Wall of the Eyeless", "bsh", "Bone Shield (Necro)", false },
{ 340, 342, "Swordback Hold", "spk", "Spiked Shield", false }, { 341, 343, "Peasent Crown", "xap", "War Hat", false }, { 342, 344, "Rockstopper", "xkp", "Sallet", false }, { 343, 345, "Stealskull", "xlm", "Casque", false }, { 344, 346, "Darksight Helm", "xhl", "Basinet", false },
{ 345, 347, "Valkyrie Wing", "xhm", "Winged Helm", false }, { 346, 348, "Crown of Thieves", "xrn", "Grand Crown (Paladin)", false }, { 347, 349, "Blackhorn's Face", "xsk", "Death Mask", false }, { 348, 350, "The Spirit Shroud", "xui", "Ghost Armor", false }, { 349, 351, "Skin of the Vipermagi", "xea", "Serpentskin Armor", false },
{ 350, 352, "Skin of the Flayerd One", "xla", "Demonhide Armor", false }, { 351, 353, "Ironpelt", "xtu", "Tresllised Armor", false }, { 352, 354, "Spiritforge", "xng", "Linked Mail", false }, { 353, 355, "Crow Caw", "xcl", "Tigulated Mail", false }, { 354, 356, "Shaftstop", "xhn", "Mesh Armor", false },
{ 355, 357, "Duriel's Shell", "xrs", "Cuirass", false }, { 356, 358, "Skullder's Ire", "xpl", "Russet Armor", false }, { 357, 359, "Guardian Angel", "xlt", "Templar Coat", false }, { 358, 360, "Toothrow", "xld", "Sharktooth Armor", false }, { 359, 361, "Atma's Wail", "xth", "Embossed Plate", false },
{ 360, 362, "Black Hades", "xul", "Chaos Armor", false }, { 361, 363, "Corpsemourn", "xar", "Ornate Armor", false }, { 362, 364, "Que-Hegan's Wisdon", "xtp", "Mage Plate", false }, { 363, 365, "Visceratuant", "xuc", "Defender", false }, { 364, 366, "Mosers Blessed Circle", "xml", "Round Shield", false },
{ 365, 367, "Stormchaser", "xrg", "Scutum (Zon)", false }, { 366, 368, "Tiamat's Rebuke", "xit", "Dragon Shield", false }, { 367, 369, "Kerke's Sanctuary", "xow", "Pavise", false }, { 368, 370, "Radimant's Sphere", "xts", "Ancient Shield", false }, { 369, 371, "Venom Grip", "xlg", "Demonhide Gloves", false },
{ 370, 372, "Gravepalm", "xvg", "Sharkskin Gloves", false }, { 371, 373, "Ghoulhide", "xmg", "Heavy Bracers", false }, { 372, 374, "Lavagout", "xtg", "Battle Guantlets", false }, { 373, 375, "Hellmouth", "xhg", "War Gauntlets", false }, { 374, 376, "Infernostride", "xlb", "Demonhide Boots", false },
{ 375, 377, "Waterwalk", "xvb", "Sharkskin Boots", false }, { 376, 378, "Silkweave", "xmb", "Mesh Boots", false }, { 377, 379, "Wartraveler", "xtb", "Battle Boots", false }, { 378, 380, "Gorerider", "xhb", "War Boots", false }, { 379, 381, "String of Ears", "zlb", "Demonhide Sash", false },
{ 380, 382, "Razortail", "zvb", "Sharkskin Belt", false }, { 381, 383, "Gloomstrap", "zmb", "Mesh Belt", false }, { 382, 384, "Snowclash", "ztb", "Battle Belt", false }, { 383, 385, "Thudergod's Vigor", "zhb", "War Belt", false }, { 384, 386, "Vampiregaze", "xh9", "Grim Helm (Necro)", false },
{ 385, 387, "Lidless Wall", "xsh", "Grim Shield (Necro)", false }, { 386, 388, "Lance Guard", "xpk", "Barbed Shield", false }, { 387, 389, "Primal Power", "dr1", "Wolf Head", false }, { 388, 390, "Murder of Crows", "dr2", "Hawk Helm", false }, { 389, 391, "Cheetah Stance", "dr3", "Antlers", false },
{ 390, 392, "Uproar", "dr4", "Falcon Mask", false }, { 391, 393, "Flame Spirit", "dr5", "Spirit Mask", false }, { 392, 394, "Toothless Maw", "ba1", "Jawbone Cap", false }, { 393, 395, "Darkfear", "ba2", "Fanged Helm", false }, { 394, 396, "Thermal Shock", "ba3", "Horned Helm", false },
{ 395, 397, "Nature's Protector", "ba4", "Assault Helmet", false }, { 396, 398, "Reckless Fury", "ba5", "Avenger Guard", false }, { 397, 399, "Sigurd's Staunch", "pa1", "Targe", false }, { 398, 400, "Caster's Courage", "pa2", "Rondache", false }, { 399, 401, "Briar Patch", "pa3", "Heraldic Shield", false },
{ 400, 402, "Ricochet", "pa4", "Aerin Shield", false }, { 401, 403, "Favored Path", "pa5", "Crown Shield", false }, { 402, 404, "Old Friend", "ne1", "Preserved Head", false }, { 403, 405, "Decomposed Leader", "ne2", "Zombie Head", false }, { 404, 406, "Tangled Fellow", "ne3", "Unraveller Head", false },
{ 405, 407, "Stubborn Stone", "ne4", "Gargoyle Head", false }, { 406, 408, "Spiked Dreamcatcher", "ne5", "Demon Head", false }, { 407, 409, "Journeyman's Band", "ci0", "Circlet", false }, { 408, 410, "Hygieia's Purity", "ci1", "Coronet", false }, { 409, 411, "Kira's Guardian", "ci2", "tiara", false },
{ 410, 412, "Griffon's Eye", "ci3", "diadem", false }, { 411, 413, "Harlequin Crest", "uap", "Shako", false }, { 412, 414, "Tarnhelm's Revenge", "ukp", "Hydraskull", false }, { 413, 415, "Steelshade", "ulm", "armet", false }, { 414, 416, "Veil of Steel", "uhl", "Giant Conch", false },
{ 415, 417, "Nightwing's Veil", "uhm", "spired helm", false }, { 416, 418, "Crown of Ages", "urn", "corona (Pali)", false }, { 417, 419, "Andariel's Visage", "usk", "demonhead", false }, { 418, 420, "Ormus' Robes", "uui", "dusk shroud", false }, { 419, 421, "Arcane Protector", "uea", "Wyrmhide", false },
{ 420, 422, "Spell Splitter", "ula", "Scarab Husk", false }, { 421, 423, "The Gladiator's Bane", "utu", "Wire Fleece", false }, { 422, 424, "Balled Lightning", "ung", "Diamond Mail", false }, { 423, 425, "Giant Crusher", "ucl", "Loricated Mail", false }, { 424, 426, "Chained Lightning", "uhn", "Boneweave", false },
{ 425, 427, "Savitr's Garb", "urs", "Great Hauberk", false }, { 426, 428, "Arkaine's Valor", "upl", "Balrog Skin", false }, { 427, 429, "Strength Unleashed", "ult", "Hellforge Plate", false }, { 428, 430, "Leviathan", "uld", "kraken shell", false }, { 429, 431, "Duality", "uth", "Lacquered Plate", false },
{ 430, 432, "Steel Carapice", "uul", "shadow plate", false }, { 431, 433, "Tyrael's Might", "uar", "sacred armor", false }, { 432, 434, "Spiritual Protector", "utp", "Archon Plate", false }, { 433, 435, "Cleansing Ward", "uuc", "Heater", false }, { 434, 436, "Blackoak Shield", "uml", "Luna", false },
{ 435, 437, "Astrogha's Web", "urg", "Hyperion", false }, { 436, 438, "Stormshield", "uit", "Monarch", false }, { 437, 439, "Medusa's Gaze", "uow", "aegis", false }, { 438, 440, "Spirit Ward", "uts", "ward", false }, { 439, 441, "Indra's Mark", "ulg", "Bramble Mitts", false },
{ 440, 442, "Dracul's Grasp", "uvg", "vampirebone gloves", false }, { 441, 443, "Souldrain", "umg", "vambraces", false }, { 442, 444, "Carthas's Presence", "utg", "Crusader Gauntlets", false }, { 443, 445, "Steelrend", "uhg", "ogre gauntlets", false }, { 444, 446, "Mana Wyrm", "ulb", "Wyrmhide Boots", false },
{ 445, 447, "Sandstorm Trek", "uvb", "scarabshell boots", false }, { 446, 448, "Marrowwalk", "umb", "boneweave boots", false }, { 447, 449, "Crimson Shift", "utb", "Mirrored Boots", false }, { 448, 450, "Lelantus's Frenzy", "uhb", "Myrmidon Greaves", false }, { 449, 451, "Arachnid Mesh", "ulc", "spiderweb sash", false },
{ 450, 452, "Nosferatu's Coil", "uvc", "vampirefang belt", false }, { 451, 453, "Verdugo's Hearty Cord", "umc", "mithril coil", false }, { 452, 454, "Magni's Warband", "utc", "Troll Belt", false }, { 453, 455, "Arcanist's Safeguard", "uhc", "Colossus Girdle", false }, { 454, 456, "Giantskull", "uh9", "bone visage (NECRO)", false },
{ 455, 457, "Headhunter's Glory", "ush", "troll nest (Necro)", false }, { 456, 458, "Spike Thorn", "upk", "blade barrier", false }, { 457, 459, "Flame of Combat", "dr6", "Alpha Helm", false }, { 458, 460, "Mystic Command", "dr7", "Griffon Headress", false }, { 459, 461, "Rama's Protector", "dr8", "Hunter's Guise", false },
{ 460, 462, "Snow Spirit", "dr9", "Sacred Feathers", false }, { 461, 463, "Efreeti's Fury", "dra", "Totemic Mask", false }, { 462, 464, "Combat Visor", "ba6", "Jawbone Visor", false }, { 463, 465, "Strength of Pride", "ba7", "Lion Helm", false }, { 464, 466, "Fighter's Stance", "ba8", "Rage Mask", false },
{ 465, 467, "Piercing Cold", "ba9", "Savage Helmet", false }, { 466, 468, "Arreat's Face", "baa", "Slayer Guard", false }, { 467, 469, "Fara's Defender", "pa6", "Akaran Targe", false }, { 468, 470, "Rakkis's Guard", "pa7", "Akaran Rondache", false }, { 469, 471, "Assaulter's Armament", "pa8", "Protector Shield", false },
{ 470, 472, "Herald of Zakarum", "pa9", "Gilded Shield", false }, { 471, 473, "Blackheart's Barrage", "paa", "Royal Shield", false }, { 472, 474, "Mehtan's Carrion", "ne6", "Mummified Trophy", false }, { 473, 475, "Venom Storm", "ne7", "Fetish Trophy", false }, { 474, 476, "Bone Zone", "ne8", "Sexton Trophy", false },
{ 475, 477, "Contagion", "ne9", "Cantor Trophy", false }, { 476, 478, "Homunculus", "nea", "Heirophant Trophy", false }, { 477, 479, "Cerebus", "drb", "blood spirit", false }, { 478, 480, "Pack Mentality", "drc", "Sun Spirit", false }, { 479, 481, "Spiritkeeper", "drd", "earth spirit", false },
{ 480, 482, "Cavern Dweller", "dre", "sky spirit", false }, { 481, 483, "Jalal's Mane", "dra", "Dream Spirit", false }, { 482, 484, "Berserker's Stance", "bab", "Carnage Helm", false }, { 483, 485, "Wolfhowl", "bac", "fury visor", false }, { 484, 486, "Demonhorn's Edge", "bad", "destroyer helm", false },
{ 485, 487, "Halaberd's Reign", "bae", "conqueror crown", false }, { 486, 488, "Warrior's Resolve", "baf", "Guardian Crown", false }, { 487, 489, "Primordial Punisher", "pab", "Sacred Targe", false }, { 488, 490, "Alma Negra", "pac", "sacred rondache", false }, { 489, 491, "Faithful Guardian", "pad", "Ancient Shield", false },
{ 490, 492, "Dragonscale", "pae", "zakarum shield", false }, { 491, 493, "Shield of Forsaken Light", "paf", "Vortex Shield", false }, { 492, 494, "Onikuma", "neb", "Minion Skull", false }, { 493, 495, "Bone Parade", "neg", "Hellspawn Skull", false }, { 494, 496, "Elanuzuru", "ned", "Overseer Skull", false },
{ 495, 497, "Boneflame", "nee", "succubae skull", false }, { 496, 498, "Darkforce Spawn", "nef", "bloodlord skull", false }, { 497, 504, "Earthshifter", "Wp3", "Fenris Fur", false }, { 498, 510, "Shadowdancer", "Ab3", "Bladed Boots", false }, { 499, 513, "Templar's Might", "Bp3", "Enlightened Plate", false },
{ 500, 516, "Nature's Nurture", "Oa3", "Oaken Armor", false }, { 501, 519, "Firebelr", "Vg3", "Vizjerei Vestige", false }, { 502, 520, "Flightless", "aqv", "Arrows", false }, { 503, 521, "Pinpoint", "aqv", "Arrows", false }, { 504, 522, "Nokozan Relic", "amu", "Amulet", false },
{ 505, 523, "The Eye of Etlich", "amu", "Amulet", false }, { 506, 524, "The Mahim-Oak Curio", "amu", "Amulet", false }, { 507, 525, "Nagelring", "rin", "Ring", false }, { 508, 526, "Manald Heal", "rin", "Ring", false }, { 509, 527, "The Stone of Jordan", "rin", "Ring", false },
{ 510, 528, "Bul Katho's Wedding Band", "rin", "Ring", false }, { 511, 529, "The Cat's Eye", "amu", "Amulet", false }, { 512, 530, "The Rising Sun", "amu", "Amulet", false }, { 513, 531, "Crescent Moon", "amu", "Amulet", false }, { 514, 532, "Mara's Kaleidoscope", "amu", "Amulet", false },
{ 515, 533, "Atma's Scarab", "amu", "Amulet", false }, { 516, 534, "Dwarf Star", "rin", "Ring", false }, { 517, 535, "Raven Frost", "rin", "Ring", false }, { 518, 536, "Highlord's Wrath", "amu", "Amulet", false }, { 519, 537, "Saracen's Chance", "amu", "Amulet", false },
{ 520, 538, "Nature's Peace", "rin", "ring", false }, { 521, 539, "Seraph's Hymn", "amu", "amulet", false }, { 522, 540, "Wisp Projector", "rin", "ring", false }, { 523, 541, "Constricting Ring", "rin", "Ring", false }, { 524, 542, "Gheed's Fortune", "cm3", "charm", false },
{ 525, 543, "Annihilus", "cm1", "charm", false }, { 526, 544, "Carrion Wind", "rin", "ring", false }, { 527, 545, "Metalgrid", "amu", "amulet", false }, { 528, 550, "Rainbow Facet1", "jew", "jewel", false }, { 529, 551, "Rainbow Facet2", "jew", "jewel", false },
{ 530, 552, "Rainbow Facet3", "jew", "jewel", false }, { 531, 553, "Rainbow Facet4", "jew", "jewel", false }, { 532, 554, "Rainbow Facet5", "jew", "jewel", false }, { 533, 555, "Rainbow Facet6", "jew", "jewel", false }, { 534, 556, "Hellfire Torch", "cm2", "charm", false },
{ 535, 557, "Beacon of Hope", "BoH", "Beacon", false }, { 536, 558, "MythosLog", "y08", "KillTracker", false }, { 537, 559, "Storage Bag", "Z01", "bag", false }, { 538, 560, "Magefist", "tgl", "Light Gauntlets", false }, { 539, 561, "Magefist", "tgl", "Light Gauntlets", false },
{ 540, 562, "Magefist", "tgl", "Light Gauntlets", false }, { 541, 563, "Magefist", "tgl", "Light Gauntlets", false }, { 542, 564, "IceClone Armor", "St1", "", false }, { 543, 565, "IceClone Armor2", "St2", "", false }, { 544, 566, "Hydra Master", "6ls", "Stalagmite", false },
{ 545, 567, "Spiritual Savior", "utp", "Archon Plate", false }, { 546, 568, "IceClone Armor3", "St3", "", false }, { 547, 569, "Fletcher's Fury", "Ag1", "Trainee Gloves", false }, { 548, 570, "Indra's Guidance", "Ag2", "Adept Gloves", false }, { 549, 572, "Robbin's Temple", "ci1", "Coronet", false },
{ 550, 573, "Trials Charm c1", "a59", "Large Charm", false }, { 551, 574, "Trials Charm c2", "a60", "Large Charm", false }, { 552, 575, "Trials Charm c3", "a61", "Large Charm", false }, { 553, 576, "Trials Charm c4", "a62", "Large Charm", false }, { 554, 577, "Trials Charm c5", "a63", "Large Charm", false },
{ 555, 578, "Trials Charm c6", "a64", "Large Charm", false }, { 556, 579, "Trials Charm c7", "a65", "Large Charm", false }, { 557, 580, "MegaCharm", "a66", "Large Charm", false }, { 558, 581, "Spirit Striker", "aqv", "Arrows", false }, { 559, 582, "Aim of Indra", "aqv", "Arrows", false },
{ 560, 583, "Enchanted Flame", "aqv", "Arrows", false }, { 561, 584, "Mageflight", "aqv", "Arrows", false }, { 562, 585, "Energy Manipulator", "amu", "amulet", false }, { 563, 586, "Trinity", "amu", "amulet", false }, { 564, 587, "Quintessence", "amu", "amulet", false },
{ 565, 588, "Life Everlasting", "rin", "ring", false }, { 566, 589, "Hunter's Mark", "rin", "ring", false }, { 567, 590, "Unholy Commander", "cm3", "charm", false }, { 568, 591, "Tommy's Enlightener", "7qs", "Seraph Rod", false }, { 569, 592, "Curtis's Fortifier", "uhc", "Colossus Girdle", false },
{ 570, 593, "Kurec's Pride", "drd", "earth spirit", false }, { 571, 594, "Spiritual Guardian", "utp", "Archon Plate", false }, { 572, 595, "Blackmaw's Brutality", "xld", "Sharktooth Armor", false }, { 573, 596, "Spencer's Dispenser", "oba", "Swirling Crystal", false }, { 574, 597, "Fletching of Frostbite", "aqv", "Arrows", false },
{ 575, 598, "Healthy Breakfast", "cm1", "charm", false }, { 576, 599, "MythosLogAmazon", "y01", "KillTracker", false }, { 577, 600, "MythosLogAssassin", "y02", "KillTracker", false }, { 578, 601, "MythosLogBarbarian", "y03", "KillTracker", false }, { 579, 602, "MythosLogDruid", "y04", "KillTracker", false },
{ 580, 603, "MythosLogNecromancer", "y05", "KillTracker", false }, { 581, 604, "MythosLogPaladin", "y06", "KillTracker", false }, { 582, 605, "MythosLogSorceress", "y07", "KillTracker", false }, { 583, 606, "Cola Cube", "cm1", "charm", false }, { 584, 607, "Soul Stompers", "umb", "boneweave boots", false },
{ 585, 608, "MapReceipt01", "m27", "map", false }, { 586, 609, "Kingdom's Heart", "uar", "Sacred Armor", false }, { 587, 610, "Prismatic Facet", "j00", "Sacred Jewel", false }, { 588, 611, "Null Charm", "cm3", "charm", false }, { 589, 612, "SS Full Plate", "St4", "shadow plate", false },
{ 590, 613, "SS Full Plate", "St5", "shadow plate", false }, { 591, 614, "SS Full Plate", "St6", "shadow plate", false }, { 592, 615, "SS Full Plate", "St7", "shadow plate", false }, { 593, 616, "SS Full Plate", "St8", "shadow plate", false }, { 594, 617, "SS Full Plate", "St9", "shadow plate", false },
{ 595, 618, "SS Full Plate", "St0", "shadow plate", false }, { 596, 619, "Messerschmidt's Reaver SS", "Ss1", "Champion Axe (Barb)", false }, { 597, 620, "Lightsabre SS", "Ss2", "Phase Blade (Sorc)", false }, { 598, 621, "Crainte Vomir", "Ss3", "Katana (Assassin)", false }, { 599, 622, "Crainte Vomir", "Ss4", "Katana (Assassin)", false },
{ 600, 623, "Spiritual Sentinel", "utp", "Archon Plate", false }, { 601, 624, "Spiritual Warden", "utp", "Archon Plate", false }, { 602, 626, "Harlequin Crest Legacy", "uap", "Shako", false }, { 603, 627, "The Cat's Eye Legacy", "amu", "Amulet", false }, { 604, 628, "Arkaine's Valor Bugged", "upl", "Balrog Skin", false },
{ 605, 629, "String of Ears Bugged", "zlb", "Demonhide Sash", false }, { 606, 630, "Wizardspike Fused", "tgl", "Light Gauntlets", false }, { 607, 631, "Exsanguinate", "vgl", "Heavy Gloves", false }, { 608, 632, "Monar's Gale", "xts", "Ancient Shield", false }, { 609, 633, "MythosLogAmazonA", "y34", "KillTracker", false },
{ 610, 634, "MythosLogAssassinA", "y35", "KillTracker", false }, { 611, 635, "MythosLogBarbarianA", "y36", "KillTracker", false }, { 612, 636, "MythosLogDruidA", "y37", "KillTracker", false }, { 613, 637, "MythosLogNecromancerA", "y38", "KillTracker", false }, { 614, 638, "MythosLogPaladinA", "y39", "KillTracker", false },
{ 615, 639, "MythosLogSorceressA", "y40", "KillTracker", false }, { 616, 640, "MythosLogAmazonB", "y34", "KillTracker", false }, { 617, 641, "MythosLogAssassinB", "y35", "KillTracker", false }, { 618, 642, "MythosLogBarbarianB", "y36", "KillTracker", false }, { 619, 643, "MythosLogDruidB", "y37", "KillTracker", false },
{ 620, 644, "MythosLogNecromancerB", "y38", "KillTracker", false }, { 621, 645, "MythosLogPaladinB", "y39", "KillTracker", false }, { 622, 646, "MythosLogSorceressB", "y40", "KillTracker", false }, { 623, 647, "MythosLogAmazonC", "y34", "KillTracker", false }, { 624, 648, "MythosLogAssassinC", "y35", "KillTracker", false },
{ 625, 649, "MythosLogBarbarianC", "y36", "KillTracker", false }, { 626, 650, "MythosLogDruidC", "y37", "KillTracker", false }, { 627, 651, "MythosLogNecromancerC", "y38", "KillTracker", false }, { 628, 652, "MythosLogPaladinC", "y39", "KillTracker", false }, { 629, 653, "MythosLogSorceressC", "y40", "KillTracker", false },
{ 630, 654, "Black Cats Secret", "cm3", "charm", false }, { 631, 655, "Dustdevil", "l18", "Runic Talons - LB", false }, { 632, 656, "Improvise", "6sw", "ward bow", false }, { 633, 657, "Ken'Juk's Blighted Visage", "usk", "demonhead", false }, { 634, 658, "Philios Prophecy", "amc", "Pellet Bow", false },
{ 635, 659, "Whisper", "cqv", "Bolts", false }, { 636, 660, "Dragon's Cinder", "cqv", "Bolts", false }, { 637, 661, "Serpent's Fangs", "cqv", "Bolts", false }, { 638, 662, "Valkyrie Wing Legacy", "xhm", "Winged Helm", false }, { 639, 663, "War Traveler Bugged", "xtb", "Battle Boots", false },
{ 640, 664, "Undead Crown Fused", "rin", "Ring", false }, { 641, 665, "Soul of Edyrem", "m37", "Charm", false }, { 642, 668, "Black Suede", "lbt", "Leather Boots", false }, { 643, 669, "Allebasi", "amu", "Amulet", false }, { 644, 674, "Bigfoot", "xhb", "War Boots", false },
{ 645, 675, "Static Calling", "7mp", "Reaper Sickle", false }, { 646, 676, "Magefist Prism", "tgl", "Light Gauntlets", false }, { 647, 677, "Wisp Projector Fused", "rin", "Ring", false }, { 648, 678, "Akara's Blessing", "amu", "Amulet", false }, { 649, 679, "Piercing Ray Vestment", "Bp3", "Enlightened Plate", false },
{ 650, 680, "Spartan's Ire", "uhl", "Giant Conch", false },
};

static SetItemEntry g_StaticSetItemsRMD[] = {
    { "Civerb's Ward", 0, "Civerb's Vestments", "Large Shield", "lrg", false },
    { "Civerb's Icon", 1, "Civerb's Vestments", "Amulet", "amu", false },
    { "Civerb's Cudgel", 2, "Civerb's Vestments", "Grand Scepter", "gsc", false },
    { "Hsarus' Iron Heel", 3, "Hsarus' Defense", "Chain Boots", "mbt", false },
    { "Hsarus' Iron Fist", 4, "Hsarus' Defense", "Buckler", "buc", false },
    { "Hsarus' Iron Stay", 5, "Hsarus' Defense", "Belt", "mbl", false },
    { "Cleglaw's Tooth", 6, "Cleglaw's Brace", "Long Sword", "lsd", false },
    { "Cleglaw's Claw", 7, "Cleglaw's Brace", "Small Shield", "sml", false },
    { "Cleglaw's Pincers", 8, "Cleglaw's Brace", "Chain Gloves", "mgl", false },
    { "Iratha's Collar", 9, "Iratha's Finery", "Amulet", "amu", false },
    { "Iratha's Cuff", 10, "Iratha's Finery", "Light Gauntlets", "tgl", false },
    { "Iratha's Coil", 11, "Iratha's Finery", "Crown", "crn", false },
    { "Iratha's Cord", 12, "Iratha's Finery", "Heavy Belt", "tbl", false },
    { "Isenhart's Lightbrand", 13, "Isenhart's Armory", "Broad Sword", "bsd", false },
    { "Isenhart's Parry", 14, "Isenhart's Armory", "Gothic Shield", "gts", false },
    { "Isenhart's Case", 15, "Isenhart's Armory", "Breast Plate", "brs", false },
    { "Isenhart's Horns", 16, "Isenhart's Armory", "Full Helm", "fhl", false },
    { "Vidala's Barb", 17, "Vidala's Rig", "Long Battle Bow", "lbb", false },
    { "Vidala's Fetlock", 18, "Vidala's Rig", "Light Plated Boots", "tbt", false },
    { "Vidala's Ambush", 19, "Vidala's Rig", "Leather Armor", "lea", false },
    { "Vidala's Snare", 20, "Vidala's Rig", "Amulet", "amu", false },
    { "Milabrega's Orb", 21, "Milabrega's Regalia", "Kite Shield", "kit", false },
    { "Milabrega's Rod", 22, "Milabrega's Regalia", "War Scepter", "wsp", false },
    { "Milabrega's Diadem", 23, "Milabrega's Regalia", "Crown", "crn", false },
    { "Milabrega's Robe", 24, "Milabrega's Regalia", "Ancient Armor", "aar", false },
    { "Cathan's Rule", 25, "Cathan's Traps", "Battle Staff", "bst", false },
    { "Cathan's Mesh", 26, "Cathan's Traps", "Chain Mail", "chn", false },
    { "Cathan's Visage", 27, "Cathan's Traps", "Mask", "msk", false },
    { "Cathan's Sigil", 28, "Cathan's Traps", "Amulet", "amu", false },
    { "Cathan's Seal", 29, "Cathan's Traps", "Ring", "rin", false },
    { "Tancred's Crowbill", 30, "Tancred's Battlegear", "Sickle", "mpi", false },
    { "Tancred's Spine", 31, "Tancred's Battlegear", "Full Plate Mail", "ful", false },
    { "Tancred's Hobnails", 32, "Tancred's Battlegear", "Boots", "lbt", false },
    { "Tancred's Weird", 33, "Tancred's Battlegear", "Amulet", "amu", false },
    { "Tancred's Skull", 34, "Tancred's Battlegear", "Bone Helm", "bhm", false },
    { "Sigon's Gage", 35, "Sigon's Complete Steel", "Gauntlets", "hgl", false },
    { "Sigon's Visor", 36, "Sigon's Complete Steel", "Great Helm", "ghm", false },
    { "Sigon's Shelter", 37, "Sigon's Complete Steel", "Gothic Plate", "gth", false },
    { "Sigon's Sabot", 38, "Sigon's Complete Steel", "Greaves", "hbt", false },
    { "Sigon's Wrap", 39, "Sigon's Complete Steel", "Plated Belt", "hbl", false },
    { "Sigon's Guard", 40, "Sigon's Complete Steel", "Tower Shield", "tow", false },
    { "Infernal Cranium", 41, "Infernal Tools", "Cap", "cap", false },
    { "Infernal Torch", 42, "Infernal Tools", "Grim Wand", "gwn", false },
    { "Infernal Sign", 43, "Infernal Tools", "Heavy Belt", "tbl", false },
    { "Berserker's Headgear", 44, "Berserker's Garb", "Helm", "hlm", false },
    { "Berserker's Hauberk", 45, "Berserker's Garb", "Splint Mail", "spl", false },
    { "Berserker's Hatchet", 46, "Berserker's Garb", "Double Axe", "2ax", false },
    { "Death's Hand", 47, "Death's Disguise", "Leather Gloves", "lgl", false },
    { "Death's Guard", 48, "Death's Disguise", "Sash", "lbl", false },
    { "Death's Touch", 49, "Death's Disguise", "War Sword", "wsd", false },
    { "Angelic Sickle", 50, "Angelical Raiment", "Sabre", "sbr", false },
    { "Angelic Mantle", 51, "Angelical Raiment", "Ring Mail", "rng", false },
    { "Angelic Halo", 52, "Angelical Raiment", "Ring", "rin", false },
    { "Angelic Wings", 53, "Angelical Raiment", "Amulet", "amu", false },
    { "Arctic Horn", 54, "Arctic Gear", "Short War Bow", "swb", false },
    { "Arctic Furs", 55, "Arctic Gear", "Quilted Armor", "qui", false },
    { "Arctic Binding", 56, "Arctic Gear", "Light Belt", "vbl", false },
    { "Arctic Mitts", 57, "Arctic Gear", "Light Gauntlets", "tgl", false },
    { "Arcanna's Sign", 58, "Arcanna's Tricks", "Amulet", "amu", false },
    { "Arcanna's Deathwand", 59, "Arcanna's Tricks", "War Staff", "wst", false },
    { "Arcanna's Head", 60, "Arcanna's Tricks", "Skull Cap", "skp", false },
    { "Arcanna's Flesh", 61, "Arcanna's Tricks", "Light Plate", "ltp", false },
    { "Natalya's Totem", 62, "Natalya's Odium", "Casque", "xlm", false },
    { "Natalya's Mark", 63, "Natalya's Odium", "Scissors Suwayyah", "7qr", false },
    { "Natalya's Shadow", 64, "Natalya's Odium", "Mantle", "Ca2", false },
    { "Natalya's Soul", 65, "Natalya's Odium", "Mesh Boots", "xmb", false },
    { "Aldur's Stony Gaze", 66, "Aldur's Watchtower", "Hunter's Guise", "dr8", false },
    { "Aldur's Deception", 67, "Aldur's Watchtower", "Shadow Plate", "uul", false },
    { "Aldur's Gauntlet", 68, "Aldur's Watchtower", "Jagged Star", "9mt", false },
    { "Aldur's Advance", 69, "Aldur's Watchtower", "Battle Boots", "xtb", false },
    { "Immortal King's Will", 70, "Immortal King", "Avenger Guard", "ba5", false },
    { "Immortal King's Soul Cage", 71, "Immortal King", "Sacred Armor", "uar", false },
    { "Immortal King's Detail", 72, "Immortal King", "War Belt", "zhb", false },
    { "Immortal King's Forge", 73, "Immortal King", "War Gauntlets", "xhg", false },
    { "Immortal King's Pillar", 74, "Immortal King", "War Boots", "xhb", false },
    { "Immortal King's Stone Crusher", 75, "Immortal King", "Ogre Maul", "7m7", false },
    { "Tal Rasha's Fire-Spun Cloth", 76, "Tal Rasha's Wrappings", "Mesh Belt", "zmb", false },
    { "Tal Rasha's Adjudication", 77, "Tal Rasha's Wrappings", "Amulet", "amu", false },
    { "Tal Rasha's Lidless Eye", 78, "Tal Rasha's Wrappings", "Swirling Crystal", "oba", false },
    { "Tal Rasha's Howling Wind", 79, "Tal Rasha's Wrappings", "Lacquered Plate", "uth", false },
    { "Tal Rasha's Horadric Crest", 80, "Tal Rasha's Wrappings", "Death Mask", "xsk", false },
    { "Griswold's Valor", 81, "Griswold's Legacy", "Treasured Headdress", "Pc3", false },
    { "Griswold's Heart", 82, "Griswold's Legacy", "Ornate Plate", "xar", false },
    { "Griswolds's Redemption", 83, "Griswold's Legacy", "Caduceus", "7ws", false },
    { "Griswold's Honor", 84, "Griswold's Legacy", "Vortex Shield", "paf", false },
    { "Trang-Oul's Guise", 85, "Trang-Oul's Avatar", "Bone Visage", "uh9", false },
    { "Trang-Oul's Scales", 86, "Trang-Oul's Avatar", "Chaos Armor", "xul", false },
    { "Trang-Oul's Wing", 87, "Trang-Oul's Avatar", "Cantor Trophy", "ne9", false },
    { "Trang-Oul's Claws", 88, "Trang-Oul's Avatar", "Heavy Bracers", "xmg", false },
    { "Trang-Oul's Girth", 89, "Trang-Oul's Avatar", "Troll Belt", "utc", false },
    { "M'avina's True Sight", 90, "M'avina's Battle Hymn", "Diadem", "ci3", false },
    { "M'avina's Embrace", 91, "M'avina's Battle Hymn", "Kraken Shell", "uld", false },
    { "M'avina's Icy Clutch", 92, "M'avina's Battle Hymn", "Battle Gauntlets", "xtg", false },
    { "M'avina's Tenet", 93, "M'avina's Battle Hymn", "Sharkskin Belt", "zvb", false },
    { "M'avina's Caster", 94, "M'avina's Battle Hymn", "Grand Matron Bow", "amc", false },
    { "Telling of Beads", 95, "The Disciple", "Amulet", "amu", false },
    { "Laying of Hands", 96, "The Disciple", "Bramble Mitts", "ulg", false },
    { "Rite of Passage", 97, "The Disciple", "Demonhide Boots", "xlb", false },
    { "Spiritual Custodian", 98, "The Disciple", "Dusk Shroud", "uui", false },
    { "Credendum", 99, "The Disciple", "Mithril Coil", "umc", false },
    { "Dangoon's Teaching", 100, "Heaven's Brethren", "Reinforced Mace", "7ma", false },
    { "Heaven's Taebaek", 101, "Heaven's Brethren", "Ward", "uts", false },
    { "Haemosu's Adament", 102, "Heaven's Brethren", "Cuirass", "xrs", false },
    { "Ondal's Almighty", 103, "Heaven's Brethren", "Spired Helm", "uhm", false },
    { "Guillaume's Face", 104, "Orphan's Call", "Winged Helm", "xhm", false },
    { "Wilhelm's Pride", 105, "Orphan's Call", "Battle Belt", "ztb", false },
    { "Magnus' Skin", 106, "Orphan's Call", "Sharkskin Gloves", "xvg", false },
    { "Wihtstan's Guard", 107, "Orphan's Call", "Round Shield", "xml", false },
    { "Hwanin's Splendor", 108, "Hwanin's Majesty", "Grand Crown", "xrn", false },
    { "Hwanin's Refuge", 109, "Hwanin's Majesty", "Tigulated Mail", "xcl", false },
    { "Hwanin's Seal", 110, "Hwanin's Majesty", "Belt", "mbl", false },
    { "Hwanin's Justice", 111, "Hwanin's Majesty", "Bill", "9vo", false },
    { "Sazabi's Cobalt Redeemer", 112, "Sazabi's Grand Tribute", "Cryptic Sword", "7ls", false },
    { "Sazabi's Ghost Liberator", 113, "Sazabi's Grand Tribute", "Balrog Skin", "upl", false },
    { "Sazabi's Mental Sheath", 114, "Sazabi's Grand Tribute", "Basinet", "xhl", false },
    { "Bul-Kathos' Sacred Charge", 115, "Bul-Kathos' Children", "Colossus Blade", "7gd", false },
    { "Bul-Kathos' Tribal Guardian", 116, "Bul-Kathos' Children", "Mythical Sword", "7wd", false },
    { "Cow King's Horns", 117, "Cow King's Leathers", "War Hat", "xap", false },
    { "Cow King's Hide", 118, "Cow King's Leathers", "Studded Leather", "stu", false },
    { "Cow King's Hoofs", 119, "Cow King's Leathers", "Heavy Boots", "vbt", false },
    { "Naj's Puzzler", 120, "Naj's Ancient Set", "Elder Staff", "6cs", false },
    { "Naj's Light Plate", 121, "Naj's Ancient Set", "Hellforge Plate", "ult", false },
    { "Naj's Circlet", 122, "Naj's Ancient Set", "Circlet", "ci0", false },
    { "McAuley's Paragon", 123, "McAuley's Folly", "Cap", "cap", false },
    { "McAuley's Riprap", 124, "McAuley's Folly", "Heavy Boots", "vbt", false },
    { "McAuley's Taboo", 125, "McAuley's Folly", "Heavy Gloves", "vgl", false },
    { "McAuley's Superstition", 126, "McAuley's Folly", "Bone Wand", "bwn", false },
    { "Vessel's Atonment", 127, "Holy Vessel", "Blessed Plate", "Bp1", false },
    { "Vessel's Fufillment", 128, "Holy Vessel", "Heraldic Shield", "pa3", false },
    { "Vessel's Anointment", 129, "Holy Vessel", "Palisade Crown", "Pc1", false },
    { "Vessel's Armament", 130, "Holy Vessel", "Scepter", "scp", false },
    { "Pointed Justice", 131, "Majestic Lancer", "Maiden Javelins", "am5", false },
    { "True Parry", 132, "Majestic Lancer", "Large Shield", "lrg", false },
    { "Solidarity", 133, "Majestic Lancer", "Skovos Circle", "Zc1", false },
    { "Island Shore", 134, "Skovos Storm", "Stag Bow", "am1", false },
    { "Raging Seas", 135, "Skovos Storm", "Heavy Gloves", "vgl", false },
    { "Eye of the Storm", 136, "Skovos Storm", "Arrows", "aqv", false },
    { "Sturdy Garment", 137, "Wonder Wear", "Mesh Belt", "zmb", false },
    { "True Deflector", 138, "Wonder Wear", "Sallet", "xkp", false },
    { "Encased Corset", 139, "Wonder Wear", "Trellised Armor", "xtu", false },
    { "Silver Bracers", 140, "Wonder Wear", "Battle Gauntlets", "xtg", false },
    { "Outreach", 141, "Vizjerei Vocation", "Arcanic Touch", "Vg1", false },
    { "Masterful Teachings", 142, "Vizjerei Vocation", "Sacred Globe", "ob2", false },
    { "Inner Focus", 143, "Vizjerei Vocation", "Circlet", "ci0", false },
    { "Disruptor", 144, "Beyond Battlemage", "Dimensional Blade", "9cr", false },
    { "Bursting Desire", 145, "Beyond Battlemage", "Dragon Shield", "xit", false },
    { "Underestimated", 146, "Beyond Battlemage", "Serpentskin Armor", "xea", false },
    { "Tundra Storm", 147, "Glacial Plains", "Death Mast", "xsk", false },
    { "Enduring Onslaught", 148, "Glacial Plains", "Demonhide Sash", "zlb", false },
    { "Frozen Goliath", 149, "Glacial Plains", "Barbed Shield", "xpk", false },
    { "Rathma's Reaper", 150, "Rathma's Calling", "Battle Sickle", "9mp", false },
    { "Rathma's Shelter", 151, "Rathma's Calling", "Troll Nest", "ush", false },
    { "Rathma's Vestage", 152, "Rathma's Calling", "Bone Visage", "uh9", false },
    { "Rathma's Fortress", 153, "Rathma's Calling", "Wyrmhide", "uea", false },
    { "Stacato's Sigil", 154, "Stacatomamba's Guidance", "Ring", "rin", false },
    { "Mamba's Circle", 155, "Stacatomamba's Guidance", "Ring", "rin", false },
    { "Kreigur's Will", 156, "Kreigur's Mastery", "Shinogi", "72h", false },
    { "Kreigur's Judgement", 157, "Kreigur's Mastery", "Shinogi", "72h", false },
    { "Kami", 158, "Scarlet Sukami", "Colossal Sword", "7fb", false },
    { "Su", 159, "Scarlet Sukami", "Champion Sword", "7b7", false },
    { "Ysenob's Blood", 160, "Mirrored Flames", "Myrmidon Greaves", "uhb", false },
    { "Noertap's Pride", 161, "Mirrored Flames", "Colossus Girdle", "uhc", false },
    { "Olbaid's Deceipt", 162, "Mirrored Flames", "Ogre Gauntlets", "uhg", false },
    { "Gale Strength", 163, "Unstoppable Force", "Winged Harpoon", "7ts", false },
    { "Assault Prowess", 164, "Unstoppable Force", "Winged Harpoon", "7ts", false },
    { "Thirst for Blood", 165, "Underworld's Unrest", "Boneweave Boots", "umb", false },
    { "Rotting Reaper", 166, "Underworld's Unrest", "Reaper Sickle", "mpi", false },
    { "Siphon String", 167, "Underworld's Unrest", "Vampirefang Belt", "uvc", false },
    { "Crown of Cold", 168, "Elemental Blueprints", "Diadem", "ci3", false },
    { "Blazing Band", 169, "Elemental Blueprints", "Ring", "rin", false },
    { "Lightning Locket", 170, "Elemental Blueprints", "Amulet", "amu", false },
    { "Brewing Storm", 171, "Raijin's Rebellion", "Ward", "uts", false },
    { "Charged Chaos", 172, "Raijin's Rebellion", "Kraken Shell", "uld", false },
    { "Electron Emitter", 173, "Raijin's Rebellion", "Corona", "urn", false },
    { "Achyls' Armament", 174, "Mikael's Toxicity", "Boneweave", "uhn", false },
    { "Pendant of Pestilence", 175, "Mikael's Toxicity", "Amulet", "amu", false },
    { "Plague Protector", 176, "Mikael's Toxicity", "Troll Nest", "ush", false },
    { "Meat Masher", 177, "Warrior's Wrath", "Thunder Maul", "7gm", false },
    { "Supreme Strength", 178, "Warrior's Wrath", "Crusader Gauntlets", "utg", false },
    { "Repeating Reaper", 179, "Blessings of Artemis", "Hydra Bow", "6lw", false },
    { "Fletcher's Friend", 180, "Blessings of Artemis", "Huntress Gloves", "Ag3", false },
    { "Band of Brothers", 181, "Artio's Calling", "Ring", "rin", false },
    { "Grizzlepaw's Hide", 182, "Artio's Calling", "Grizzly Gear", "Gg3", false },
    { "Animal Instinct", 183, "Artio's Calling", "Mithril Coil", "umc", false },
    { "Justitia's Anger", 184, "Justitia's Divinity", "Cadaceus", "7ws", false },
    { "Justitia's Embrace", 185, "Justitia's Divinity", "Vortex Shield", "paf", false },
    { "Hand of Efreeti", 186, "Pulsing Presence", "Bramble Mitts", "ulg", false },
    { "Morning Frost", 187, "Pulsing Presence", "Scarabshell Boots", "uvb", false },
    { "Thunderlord's Vision", 188, "Pulsing Presence", "Demonhead", "usk", false },
    { "Coil of Heaven", 189, "Celestial Caress", "Ring", "rin", false },
    { "Band of Divinity", 190, "Celestial Caress", "Ring", "rin", false },
    { "Godly Locket", 191, "Celestial Caress", "Amulet", "amu", false },
    { "Chains of Bondage", 192, "Breaker of Chains", "Diadem", "ci3", false },
    { "Chains of Force", 193, "Breaker of Chains", "Archon Plate", "utp", false },
    { "Night's Disguise", 194, "Silhouette of Silence", "Cloak", "Ca3", false },
    { "Silent Stalkers", 195, "Silhouette of Silence", "Bladed Boots", "Ab3", false },
    { "Toxic Grasp", 196, "Silhouette of Silence", "Vampirebone Gloves", "uvg", false },
    { "Blade Binding", 197, "Mangala's Teachings", "Vampirefang Belt", "uvc", false },
    { "Murderous Intent", 198, "Mangala's Teachings", "Shako", "uap", false },
    { "Band of Suffering", 199, "Sacrificial Trinity", "Ring", "rin", false },
    { "Loop of Regret", 200, "Sacrificial Trinity", "Ring", "rin", false },
    { "Locket of Burden", 201, "Sacrificial Trinity", "Amulet", "amu", false },
    { "Bulwark of Defiance", 202, "Plates of Protection", "Aegis", "uow", false },
    { "Marauder's Mark", 203, "Plates of Protection", "Berserker Axe", "7wa", false },
    { "Girdle of Resilience", 204, "Plates of Protection", "Colossus Girdle", "uhc", false },
    { "Crippling Conch", 205, "Black Tempest", "Giant Conch", "uhl", false },
    { "Sub-Zero Sash", 206, "Black Tempest", "Spiderweb Sash", "ulc", false },
    { "Band of Permafrost", 207, "Black Tempest", "Ring", "rin", false },
    { "Morality", 208, "Memento Mori", "Cryptic Axe", "7pa", false },
    { "Remembrance", 209, "Memento Mori", "Bone Visage", "uh9", false },
    { "Harbinger", 210, "Memento Mori", "Boneweave", "uhn", false },
    { "Promethium", 211, "Cascading Caldera", "Amulet", "amu", false },
    { "Searing Step", 212, "Cascading Caldera", "Scarabshell Boots", "uvb", false },
    { "Flameward", 213, "Cascading Caldera", "Gothic Shield", "gts", false },
    { "Vortex1", 214, "Path of the Vortex", "Scissors Suwayyah", "7qr", false },
    { "Maelstrom1", 215, "Path of the Vortex", "Scissors Suwayyah", "7qr", false },
    { "Vortex2", 216, "Path of the Vortex2", "Glorious Axe", "7gi", false },
    { "Maelstrom2", 217, "Path of the Vortex2", "Glorious Axe", "7gi", false },
    { "Great Warrior", 218, "Blacklight", "Guardian Crown", "baf", false },
    { "Great Defender", 219, "Blacklight", "Blade Barrier", "upk", false },
    { "Great Warrior2", 220, "Blacklight2", "Conqueror Crown", "urn", false },
    { "Great Defender2", 221, "Blacklight2", "Zakarum Shield", "pae", false },
    { "Frozen Retribution", 222, "Winter Warrior", "Blade Barrier", "upk", false },
    { "Chilling Clasp", 223, "Winter Warrior", "Amulet", "amu", false },
};

static UniqueItemEntry g_StaticUniqueItems[] = {
{ 0, 0, "The Gnasher", "hax", "Hand Axe", false }, { 1, 1, "Deathspade", "axe", "Axe", false }, { 2, 2, "Bladebone", "2ax", "Double Axe", false }, { 3, 3, "Mindrend", "mpi", "Military Pick", false }, { 4, 4, "Rakescar", "wax", "War Axe", false },
{ 5, 5, "Fechmars Axe", "lax", "Large Axe", false }, { 6, 6, "Goreshovel", "bax", "Broad Axe", false }, { 7, 7, "The Chieftan", "btx", "Battle Axe", false }, { 8, 8, "Brainhew", "gax", "Great Axe", false }, { 9, 9, "The Humongous", "gix", "Giant Axe", false },
{ 10, 10, "Iros Torch", "wnd", "Wand", false }, { 11, 11, "Maelstromwrath", "ywn", "Yew Wand", false }, { 12, 12, "Gravenspine", "bwn", "Bone Wand", false }, { 13, 13, "Umes Lament", "gwn", "Grim Wand", false }, { 14, 14, "Felloak", "clb", "Club", false },
{ 15, 15, "Knell Striker", "scp", "Scepter", false }, { 16, 16, "Rusthandle", "gsc", "Grand Scepter", false }, { 17, 17, "Stormeye", "wsp", "War Scepter", false }, { 18, 18, "Stoutnail", "spc", "Spiked Club", false }, { 19, 19, "Crushflange", "mac", "Mace", false },
{ 20, 20, "Bloodrise", "mst", "Morning Star", false }, { 21, 21, "The Generals Tan Do Li Ga", "fla", "Flail", false }, { 22, 22, "Ironstone", "whm", "War Hammer", false }, { 23, 23, "Bonesob", "mau", "Maul", false }, { 24, 24, "Steeldriver", "gma", "Great Maul", false },
{ 25, 25, "Rixots Keen", "ssd", "Short Sword", false }, { 26, 26, "Blood Crescent", "scm", "Scimitar", false }, { 27, 27, "Krintizs Skewer", "sbr", "Saber", false }, { 28, 28, "Gleamscythe", "flc", "Falchion", false }, { 29, 30, "Griswolds Edge", "bsd", "Broad Sword", false },
{ 30, 31, "Hellplague", "lsd", "Long Sword", false }, { 31, 32, "Culwens Point", "wsd", "War Sword", false }, { 32, 33, "Shadowfang", "2hs", "2-Handed Sword", false }, { 33, 34, "Soulflay", "clm", "Claymore", false }, { 34, 35, "Kinemils Awl", "gis", "Giant Sword", false },
{ 35, 36, "Blacktongue", "bsw", "Bastard Sword", false }, { 36, 37, "Ripsaw", "flb", "Flamberge", false }, { 37, 38, "The Patriarch", "gsd", "Great Sword", false }, { 38, 39, "Gull", "dgr", "Dagger", false }, { 39, 40, "The Diggler", "dir", "Dirk", false },
{ 40, 41, "The Jade Tan Do", "kri", "Kris", false }, { 41, 42, "Irices Shard", "bld", "Blade", false }, { 42, 43, "The Dragon Chang", "spr", "Spear", false }, { 43, 44, "Razortine", "tri", "Trident", false }, { 44, 45, "Bloodthief", "brn", "Brandistock", false },
{ 45, 46, "Lance of Yaggai", "spt", "Spetum", false }, { 46, 47, "The Tannr Gorerod", "pik", "Pike", false }, { 47, 48, "Dimoaks Hew", "bar", "Bardiche", false }, { 48, 49, "Steelgoad", "vou", "Voulge", false }, { 49, 50, "Soul Harvest", "scy", "Scythe", false },
{ 50, 51, "The Battlebranch", "pax", "Poleaxe", false }, { 51, 52, "Woestave", "hal", "Halberd", false }, { 52, 53, "The Grim Reaper", "wsc", "War Scythe", false }, { 53, 54, "Bane Ash", "sst", "Short Staff", false }, { 54, 55, "Serpent Lord", "lst", "Long Staff", false },
{ 55, 56, "Lazarus Spire", "cst", "Gnarled Staff", false }, { 56, 57, "The Salamander", "bst", "Battle Staff", false }, { 57, 58, "The Iron Jang Bong", "wst", "War Staff", false }, { 58, 59, "Pluckeye", "sbw", "Short Bow", false }, { 59, 60, "Witherstring", "hbw", "Hunter's Bow", false },
{ 60, 61, "Rimeraven", "lbw", "Long Bow", false }, { 61, 62, "Piercerib", "cbw", "Composite Bow", false }, { 62, 63, "Pullspite", "sbb", "Short Battle Bow", false }, { 63, 64, "Wizendraw", "lbb", "Long Battle Bow", false }, { 64, 65, "Hellclap", "swb", "Short War Bow", false },
{ 65, 66, "Blastbark", "lwb", "Long War Bow", false }, { 66, 67, "Leadcrow", "lxb", "Light Crossbow", false }, { 67, 68, "Ichorsting", "mxb", "Crossbow", false }, { 68, 69, "Hellcast", "hxb", "Heavy Crossbow", false }, { 69, 70, "Doomspittle", "rxb", "Repeating Crossbow", false },
{ 70, 71, "War Bonnet", "cap", "Cap", false }, { 71, 72, "Tarnhelm", "skp", "Skull Cap", false }, { 72, 73, "Coif of Glory", "hlm", "Helm", false }, { 73, 74, "Duskdeep", "fhl", "Full Helm", false }, { 74, 75, "Wormskull", "bhm", "Bone Helm", false },
{ 75, 76, "Howltusk", "ghm", "Great Helm", false }, { 76, 77, "Undead Crown", "crn", "Crown", false }, { 77, 78, "The Face of Horror", "msk", "Mask", false }, { 78, 79, "Greyform", "qui", "Quilted Armor", false }, { 79, 80, "Blinkbats Form", "lea", "Leather Armor", false },
{ 80, 81, "The Centurion", "hla", "Hard Leather", false }, { 81, 82, "Twitchthroe", "stu", "Studded Leather", false }, { 82, 83, "Darkglow", "rng", "Ring Mail", false }, { 83, 84, "Hawkmail", "scl", "Scale Mail", false }, { 84, 85, "Sparking Mail", "chn", "Chain Mail", false },
{ 85, 86, "Venomsward", "brs", "Breast Plate", false }, { 86, 87, "Iceblink", "spl", "Splint Mail", false }, { 87, 88, "Boneflesh", "plt", "Plate Mail", false }, { 88, 89, "Rockfleece", "fld", "Field Plate", false }, { 89, 90, "Rattlecage", "gth", "Gothic Plate", false },
{ 90, 91, "Goldskin", "ful", "Full Plate Mail", false }, { 91, 92, "Victors Silk", "aar", "AncientArmor", false }, { 92, 93, "Heavenly Garb", "ltp", "Light Plate", false }, { 93, 94, "Pelta Lunata", "buc", "Buckler", false }, { 94, 95, "Umbral Disk", "sml", "Small Shield", false },
{ 95, 96, "Stormguild", "lrg", "Large Shield", false }, { 96, 97, "Wall of the Eyeless", "bsh", "Bone Shield", false }, { 97, 98, "Swordback Hold", "spk", "Spiked Shield", false }, { 98, 99, "Steelclash", "kit", "Kite Shield", false }, { 99, 100, "Bverrit Keep", "tow", "Tower Shield", false },
{ 100, 101, "The Ward", "gts", "Gothic Shield", false }, { 101, 102, "The Hand of Broc", "lgl", "Gloves", false }, { 102, 103, "Bloodfist", "vgl", "Heavy Gloves", false }, { 103, 104, "Chance Guards", "mgl", "Bracers", false }, { 104, 105, "Magefist", "tgl", "Light Gauntlets", false },
{ 105, 106, "Frostburn", "hgl", "Gauntlets", false }, { 106, 107, "Hotspur", "lbt", "Leather Boots", false }, { 107, 108, "Gorefoot", "vbt", "Heavy Boots", false }, { 108, 109, "Treads of Cthon", "mbt", "Chain Boots", false }, { 109, 110, "Goblin Toe", "tbt", "Light Plate Boots", false },
{ 110, 111, "Tearhaunch", "hbt", "Plate Boots", false }, { 111, 112, "Lenyms Cord", "lbl", "Sash", false }, { 112, 113, "Snakecord", "vbl", "Light Belt", false }, { 113, 114, "Nightsmoke", "mbl", "Belt", false }, { 114, 115, "Goldwrap", "tbl", "Heavy Belt", false },
{ 115, 116, "Bladebuckle", "hbl", "Girdle", false }, { 116, 117, "Nokozan Relic", "amu", "Amulet", false }, { 117, 118, "The Eye of Etlich", "amu", "Amulet", false }, { 118, 119, "The Mahim-Oak Curio", "amu", "Amulet", false }, { 119, 120, "Nagelring", "rin", "Ring", false },
{ 120, 121, "Manald Heal", "rin", "Ring", false }, { 121, 122, "The Stone of Jordan", "rin", "Ring", false }, { 122, 123, "Amulet of the Viper", "vip", "Amulet", false }, { 123, 124, "Staff of Kings", "msf", "Staff", false }, { 124, 125, "Horadric Staff", "hst", "Staff", false },
{ 125, 126, "Hell Forge Hammer", "hfh", "Hammer", false }, { 126, 127, "KhalimFlail", "qf1", "Flail", false }, { 127, 128, "SuperKhalimFlail", "qf2", "Flail", false }, { 128, 129, "Coldkill", "9ha", "Hatchet", false }, { 129, 130, "Butcher's Pupil", "9ax", "Cleaver", false },
{ 130, 131, "Islestrike", "92a", "Twin Axe", false }, { 131, 132, "Pompe's Wrath", "9mp", "Crowbill", false }, { 132, 133, "Guardian Naga", "9wa", "Naga", false }, { 133, 134, "Warlord's Trust", "9la", "Military Axe", false }, { 134, 135, "Spellsteel", "9ba", "Bearded Axe", false },
{ 135, 136, "Stormrider", "9bt", "Tabar", false }, { 136, 137, "Boneslayer Blade", "9ga", "Gothic Axe", false }, { 137, 138, "The Minataur", "9gi", "Ancient Axe", false }, { 138, 139, "Suicide Branch", "9wn", "Burnt Wand", false }, { 139, 140, "Carin Shard", "9yw", "Petrified Wand", false },
{ 140, 141, "Arm of King Leoric", "9bw", "Tomb Wand", false }, { 141, 142, "Blackhand Key", "9gw", "Grave Wand", false }, { 142, 143, "Dark Clan Crusher", "9cl", "Cudgel", false }, { 143, 144, "Zakarum's Hand", "9sc", "Rune Scepter", false }, { 144, 145, "The Fetid Sprinkler", "9qs", "Holy Water Sprinkler", false },
{ 145, 146, "Hand of Blessed Light", "9ws", "Divine Scepter", false }, { 146, 147, "Fleshrender", "9sp", "Barbed Club", false }, { 147, 148, "Sureshrill Frost", "9ma", "Flanged Mace", false }, { 148, 149, "Moonfall", "9mt", "Jagged Star", false }, { 149, 150, "Baezil's Vortex", "9fl", "Knout", false },
{ 150, 151, "Earthshaker", "9wh", "Battle Hammer", false }, { 151, 152, "Bloodtree Stump", "9m9", "War Club", false }, { 152, 153, "The Gavel of Pain", "9gm", "Martel de Fer", false }, { 153, 154, "Bloodletter", "9ss", "Gladius", false }, { 154, 155, "Coldsteel Eye", "9sm", "Cutlass", false },
{ 155, 156, "Hexfire", "9sb", "Shamshir", false }, { 156, 157, "Blade of Ali Baba", "9fc", "Tulwar", false }, { 157, 158, "Ginther's Rift", "9cr", "Dimensional Blade", false }, { 158, 159, "Headstriker", "9bs", "Battle Sword", false }, { 159, 160, "Plague Bearer", "9ls", "Rune Sword", false },
{ 160, 161, "The Atlantian", "9wd", "Ancient Sword", false }, { 161, 162, "Crainte Vomir", "92h", "Espadon", false }, { 162, 163, "Bing Sz Wang", "9cm", "Dacian Falx", false }, { 163, 164, "The Vile Husk", "9gs", "Tusk Sword", false }, { 164, 165, "Cloudcrack", "9b9", "Gothic Sword", false },
{ 165, 166, "Todesfaelle Flamme", "9fb", "Zweihander", false }, { 166, 167, "Swordguard", "9gd", "Executioner Sword", false }, { 167, 168, "Spineripper", "9dg", "Poignard", false }, { 168, 169, "Heart Carver", "9di", "Rondel", false }, { 169, 170, "Blackbog's Sharp", "9kr", "Cinquedeas", false },
{ 170, 171, "Stormspike", "9bl", "Stilleto", false }, { 171, 172, "The Impaler", "9sr", "War Spear", false }, { 172, 173, "Kelpie Snare", "9tr", "Fuscina", false }, { 173, 174, "Soulfeast Tine", "9br", "War Fork", false }, { 174, 175, "Hone Sundan", "9st", "Yari", false },
{ 175, 176, "Spire of Honor", "9p9", "Lance", false }, { 176, 177, "The Meat Scraper", "9b7", "Lochaber Axe", false }, { 177, 178, "Blackleach Blade", "9vo", "Bill", false }, { 178, 179, "Athena's Wrath", "9s8", "Battle Scythe", false }, { 179, 180, "Pierre Tombale Couant", "9pa", "Partizan", false },
{ 180, 181, "Husoldal Evo", "9h9", "Bec-de-Corbin", false }, { 181, 182, "Grim's Burning Dead", "9wc", "Grim Scythe", false }, { 182, 183, "Razorswitch", "8ss", "Jo Stalf", false }, { 183, 184, "Ribcracker", "8ls", "Quarterstaff", false }, { 184, 185, "Chromatic Ire", "8cs", "Cedar Staff", false },
{ 185, 186, "Warpspear", "8bs", "Gothic Staff", false }, { 186, 187, "Skullcollector", "8ws", "Rune Staff", false }, { 187, 188, "Skystrike", "8sb", "Edge Bow", false }, { 188, 189, "Riphook", "8hb", "Razor Bow", false }, { 189, 190, "Kuko Shakaku", "8lb", "CedarBow", false },
{ 190, 191, "Endlesshail", "8cb", "Double Bow", false }, { 191, 192, "Whichwild String", "8s8", "Short Siege Bow", false }, { 192, 193, "Cliffkiller", "8l8", "Long Siege Bow", false }, { 193, 194, "Magewrath", "8sw", "Rune Bow", false }, { 194, 195, "Godstrike Arch", "8lw", "Gothic Bow", false },
{ 195, 196, "Langer Briser", "8lx", "Arbalest", false }, { 196, 197, "Pus Spiter", "8mx", "Siege Crossbow", false }, { 197, 198, "Buriza-Do Kyanon", "8hx", "Balista", false }, { 198, 199, "Demon Machine", "8rx", "Chu-Ko-Nu", false }, { 199, 201, "Peasent Crown", "xap", "War Hat", false },
{ 200, 202, "Rockstopper", "xkp", "Sallet", false }, { 201, 203, "Stealskull", "xlm", "Casque", false }, { 202, 204, "Darksight Helm", "xhl", "Basinet", false }, { 203, 205, "Valkiry Wing", "xhm", "Winged Helm", false }, { 204, 206, "Crown of Thieves", "xrn", "Grand Crown", false },
{ 205, 207, "Blackhorn's Face", "xsk", "Death Mask", false }, { 206, 208, "Vampiregaze", "xh9", "Grim Helm", false }, { 207, 209, "The Spirit Shroud", "xui", "Ghost Armor", false }, { 208, 210, "Skin of the Vipermagi", "xea", "SerpentSkin Armor", false }, { 209, 211, "Skin of the Flayerd One", "xla", "Demonhide Armor", false },
{ 210, 212, "Ironpelt", "xtu", "Tresllised Armor", false }, { 211, 213, "Spiritforge", "xng", "Linked Mail", false }, { 212, 214, "Crow Caw", "xcl", "Tigulated Mail", false }, { 213, 215, "Shaftstop", "xhn", "Mesh Armor", false }, { 214, 216, "Duriel's Shell", "xrs", "Cuirass", false },
{ 215, 217, "Skullder's Ire", "xpl", "Russet Armor", false }, { 216, 218, "Guardian Angel", "xlt", "Templar Coat", false }, { 217, 219, "Toothrow", "xld", "Sharktooth Armor", false }, { 218, 220, "Atma's Wail", "xth", "Embossed Plate", false }, { 219, 221, "Black Hades", "xul", "Chaos Armor", false },
{ 220, 222, "Corpsemourn", "xar", "Ornate Armor", false }, { 221, 223, "Que-Hegan's Wisdon", "xtp", "Mage Plate", false }, { 222, 224, "Visceratuant", "xuc", "Defender", false }, { 223, 225, "Mosers Blessed Circle", "xml", "Round Shield", false }, { 224, 226, "Stormchaser", "xrg", "Scutum", false },
{ 225, 227, "Tiamat's Rebuke", "xit", "Dragon Shield", false }, { 226, 228, "Kerke's Sanctuary", "xow", "Pavise", false }, { 227, 229, "Radimant's Sphere", "xts", "Ancient Shield", false }, { 228, 230, "Lidless Wall", "xsh", "Grim Shield", false }, { 229, 231, "Lance Guard", "xpk", "Barbed Shield", false },
{ 230, 232, "Venom Grip", "xlg", "Demonhide Gloves", false }, { 231, 233, "Gravepalm", "xvg", "Sharkskin Gloves", false }, { 232, 234, "Ghoulhide", "xmg", "Heavy Bracers", false }, { 233, 235, "Lavagout", "xtg", "Battle Guantlets", false }, { 234, 236, "Hellmouth", "xhg", "War Gauntlets", false },
{ 235, 237, "Infernostride", "xlb", "Demonhide Boots", false }, { 236, 238, "Waterwalk", "xvb", "Sharkskin Boots", false }, { 237, 239, "Silkweave", "xmb", "Mesh Boots", false }, { 238, 240, "Wartraveler", "xtb", "Battle Boots", false }, { 239, 241, "Gorerider", "xhb", "War Boots", false },
{ 240, 242, "String of Ears", "zlb", "Demonhide Sash", false }, { 241, 243, "Razortail", "zvb", "Sharkskin Belt", false }, { 242, 244, "Gloomstrap", "zmb", "Mesh Belt", false }, { 243, 245, "Snowclash", "ztb", "Battle Belt", false }, { 244, 246, "Thudergod's Vigor", "zhb", "War Belt", false },
{ 245, 248, "Harlequin Crest", "uap", "Shako", false }, { 246, 249, "Veil of Steel", "uhm", "Spired Helm", false }, { 247, 250, "The Gladiator's Bane", "utu", "Wire Fleece", false }, { 248, 251, "Arkaine's Valor", "upl", "Balrog Skin", false }, { 249, 252, "Blackoak Shield", "uml", "Luna", false },
{ 250, 253, "Stormshield", "uit", "Monarch", false }, { 251, 254, "Hellslayer", "7bt", "Decapitator", false }, { 252, 255, "Messerschmidt's Reaver", "7ga", "Champion Axe", false }, { 253, 256, "Baranar's Star", "7mt", "Devil Star", false }, { 254, 257, "Schaefer's Hammer", "7wh", "Legendary Mallet", false },
{ 255, 258, "The Cranium Basher", "7gm", "Thunder Maul", false }, { 256, 259, "Lightsabre", "7cr", "Phase Blade", false }, { 257, 260, "Doombringer", "7b7", "Champion Sword", false }, { 258, 261, "The Grandfather", "7gd", "Colossus Blade", false }, { 259, 262, "Wizardspike", "7dg", "Bone Knife", false },
{ 260, 264, "Stormspire", "7wc", "Giant Thresher", false }, { 261, 265, "Eaglehorn", "6l7", "Crusader Bow", false }, { 262, 266, "Windforce", "6lw", "Hydra Bow", false }, { 263, 268, "Bul Katho's Wedding Band", "rin", "Ring", false }, { 264, 269, "The Cat's Eye", "amu", "Amulet", false },
{ 265, 270, "The Rising Sun", "amu", "Amulet", false }, { 266, 271, "Crescent Moon", "amu", "Amulet", false }, { 267, 272, "Mara's Kaleidoscope", "amu", "Amulet", false }, { 268, 273, "Atma's Scarab", "amu", "Amulet", false }, { 269, 274, "Dwarf Star", "rin", "Ring", false },
{ 270, 275, "Raven Frost", "rin", "Ring", false }, { 271, 276, "Highlord's Wrath", "amu", "Amulet", false }, { 272, 277, "Saracen's Chance", "amu", "Amulet", false }, { 273, 279, "Arreat's Face", "baa", "Slayer Guard", false }, { 274, 280, "Homunculus", "nea", "Heirophant Trophy", false },
{ 275, 281, "Titan's Revenge", "ama", "Ceremonial Javelin", false }, { 276, 282, "Lycander's Aim", "am7", "Ceremonial Bow", false }, { 277, 283, "Lycander's Flank", "am9", "Ceremonial Pike", false }, { 278, 284, "The Oculus", "oba", "Swirling Crystal", false }, { 279, 285, "Herald of Zakarum", "pa9", "Aerin Shield", false },
{ 280, 286, "Cutthroat1", "9tw", "Runic Talons", false }, { 281, 287, "Jalal's Mane", "dra", "Dream Spirit", false }, { 282, 288, "The Scalper", "9ta", "Francisca", false }, { 283, 289, "Bloodmoon", "7sb", "elegant blade", false }, { 284, 290, "Djinnslayer", "7sm", "ataghan", false },
{ 285, 291, "Deathbit", "9tk", "battle dart", false }, { 286, 292, "Warshrike", "7bk", "winged knife", false }, { 287, 293, "Gutsiphon", "6rx", "demon crossbow", false }, { 288, 294, "Razoredge", "7ha", "tomahawk", false }, { 289, 296, "Demonlimb", "7sp", "tyrant club", false },
{ 290, 297, "Steelshade", "ulm", "armet", false }, { 291, 298, "Tomb Reaver", "7pa", "cryptic axe", false }, { 292, 299, "Deaths's Web", "7gw", "unearthed wand", false }, { 293, 300, "Nature's Peace", "rin", "ring", false }, { 294, 301, "Azurewrath", "7cr", "phase blade", false },
{ 295, 302, "Seraph's Hymn", "amu", "amulet", false }, { 296, 304, "Fleshripper", "7kr", "fanged knife", false }, { 297, 306, "Horizon's Tornado", "7fl", "scourge", false }, { 298, 307, "Stone Crusher", "7wh", "legendary mallet", false }, { 299, 308, "Jadetalon", "7wb", "wrist sword", false },
{ 300, 309, "Shadowdancer", "uhb", "myrmidon greaves", false }, { 301, 310, "Cerebus", "drb", "blood spirit", false }, { 302, 311, "Tyrael's Might", "uar", "sacred armor", false }, { 303, 312, "Souldrain", "umg", "vambraces", false }, { 304, 313, "Runemaster", "72a", "ettin axe", false },
{ 305, 314, "Deathcleaver", "7wa", "berserker axe", false }, { 306, 315, "Executioner's Justice", "7gi", "glorious axe", false }, { 307, 316, "Stoneraven", "amd", "matriarchal spear", false }, { 308, 317, "Leviathan", "uld", "kraken shell", false }, { 309, 319, "Wisp", "rin", "ring", false },
{ 310, 320, "Gargoyle's Bite", "7ts", "winged harpoon", false }, { 311, 321, "Lacerator", "7b8", "winged axe", false }, { 312, 322, "Mang Song's Lesson", "6ws", "archon staff", false }, { 313, 323, "Viperfork", "7br", "war fork", false }, { 314, 324, "Ethereal Edge", "7ba", "silver-edged axe", false },
{ 315, 325, "Demonhorn's Edge", "bad", "destroyer helm", false }, { 316, 326, "The Reaper's Toll", "7s8", "thresher", false }, { 317, 327, "Spiritkeeper", "drd", "earth spirit", false }, { 318, 328, "Hellrack", "6hx", "colossus crossbow", false }, { 319, 329, "Alma Negra", "pac", "sacred rondache", false },
{ 320, 330, "Darkforge Spawn", "nef", "bloodlord skull", false }, { 321, 331, "Widowmaker", "6sw", "ward bow", false }, { 322, 332, "Bloodraven's Charge", "amb", "matriarchal bow", false }, { 323, 333, "Ghostflame", "7bl", "legend spike", false }, { 324, 334, "Shadowkiller", "7cs", "battle cestus", false },
{ 325, 335, "Gimmershred", "7ta", "flying axe", false }, { 326, 336, "Griffon's Eye", "ci3", "diadem", false }, { 327, 337, "Windhammer", "7m7", "ogre maul", false }, { 328, 338, "Thunderstroke", "amf", "matriarchal javelin", false }, { 329, 340, "Demon's Arch", "7s7", "balrog spear", false },
{ 330, 341, "Boneflame", "nee", "succubae skull", false }, { 331, 342, "Steelpillar", "7p7", "war pike", false }, { 332, 343, "Nightwing's Veil", "uhm", "spired helm", false }, { 333, 344, "Crown of Ages", "urn", "corona", false }, { 334, 345, "Andariel's Visage", "usk", "demonhead", false },
{ 335, 347, "Dragonscale", "pae", "zakarum shield", false }, { 336, 348, "Steel Carapice", "uul", "shadow plate", false }, { 337, 349, "Medusa's Gaze", "uow", "aegis", false }, { 338, 350, "Ravenlore", "dre", "sky spirit", false }, { 339, 351, "Boneshade", "7bw", "lich wand", false },
{ 340, 353, "Flamebellow", "7gs", "balrog blade", false }, { 341, 354, "Fathom", "obf", "dimensional shard", false }, { 342, 355, "Wolfhowl", "bac", "fury visor", false }, { 343, 356, "Spirit Ward", "uts", "ward", false }, { 344, 357, "Kira's Guardian", "ci2", "tiara", false },
{ 345, 358, "Ormus' Robes", "uui", "dusk shroud", false }, { 346, 359, "Gheed's Fortune", "cm3", "charm", false }, { 347, 360, "Stormlash", "7fl", "scourge", false }, { 348, 361, "Halaberd's Reign", "bae", "conqueror crown", false }, { 349, 363, "Spike Thorn", "upk", "blade barrier", false },
{ 350, 364, "Dracul's Grasp", "uvg", "vampirebone gloves", false }, { 351, 365, "Frostwind", "7ls", "cryptic sword", false }, { 352, 366, "Templar's Might", "uar", "sacred armor", false }, { 353, 367, "Eschuta's temper", "obc", "eldritch orb", false }, { 354, 368, "Firelizard's Talons", "7lw", "feral claws", false },
{ 355, 369, "Sandstorm Trek", "uvb", "scarabshell boots", false }, { 356, 370, "Marrowwalk", "umb", "boneweave boots", false }, { 357, 371, "Heaven's Light", "7sc", "mighty scepter", false }, { 358, 373, "Arachnid Mesh", "ulc", "spiderweb sash", false }, { 359, 374, "Nosferatu's Coil", "uvc", "vampirefang belt", false },
{ 360, 375, "Metalgrid", "amu", "amulet", false }, { 361, 376, "Verdugo's Hearty Cord", "umc", "mithril coil", false }, { 362, 378, "Carrion Wind", "rin", "ring", false }, { 363, 379, "Giantskull", "uh9", "bone visage", false }, { 364, 380, "Ironward", "7ws", "caduceus", false },
{ 365, 381, "Annihilus", "cm1", "charm", false }, { 366, 382, "Arioc's Needle", "7sr", "hyperion spear", false }, { 367, 383, "Cranebeak", "7mp", "war spike", false }, { 368, 384, "Nord's Tenderizer", "7cl", "truncheon", false }, { 369, 385, "Earthshifter", "7gm", "thunder maul", false },
{ 370, 386, "Wraithflight", "7gl", "ghost glaive", false }, { 371, 387, "Bonehew", "7o7", "ogre axe", false }, { 372, 388, "Ondal's Wisdom", "6cs", "elder staff", false }, { 373, 389, "The Reedeemer", "7sc", "mighty scepter", false }, { 374, 390, "Headhunter's Glory", "ush", "troll nest", false },
{ 375, 391, "Steelrend", "uhg", "ogre gauntlets", false }, { 376, 392, "Rainbow Facet", "jew", "jewel", false }, { 377, 393, "Rainbow Facet", "jew", "jewel", false }, { 378, 394, "Rainbow Facet", "jew", "jewel", false }, { 379, 395, "Rainbow Facet", "jew", "jewel", false },
{ 380, 396, "Rainbow Facet", "jew", "jewel", false }, { 381, 397, "Rainbow Facet", "jew", "jewel", false }, { 382, 398, "Rainbow Facet", "jew", "jewel", false }, { 383, 399, "Rainbow Facet", "jew", "jewel", false }, { 384, 400, "Hellfire Torch", "cm2", "charm", false },
{ 385, 401, "Cold Rupture", "cm3", "charm", false }, { 386, 402, "Flame Rift", "cm3", "charm", false }, { 387, 403, "Crack of the Heavens", "cm3", "charm", false }, { 388, 404, "Rotting Fissure", "cm3", "charm", false }, { 389, 405, "Bone Break", "cm3", "charm", false },
{ 390, 406, "Black Cleft", "cm3", "charm", false },
};

static SetItemEntry g_StaticSetItems[] = {
    { "Civerb's Ward", 0, "Civerb's Vestments", "Large Shield", "lrg", false },
    { "Civerb's Icon", 1, "Civerb's Vestments", "Amulet", "amu", false },
    { "Civerb's Cudgel", 2, "Civerb's Vestments", "Grand Scepter", "gsc", false },
    { "Hsarus' Iron Heel", 3, "Hsarus' Defense", "Chain Boots", "mbt", false },
    { "Hsarus' Iron Fist", 4, "Hsarus' Defense", "Buckler", "buc", false },
    { "Hsarus' Iron Stay", 5, "Hsarus' Defense", "Belt", "mbl", false },
    { "Cleglaw's Tooth", 6, "Cleglaw's Brace", "Long Sword", "lsd", false },
    { "Cleglaw's Claw", 7, "Cleglaw's Brace", "Small Shield", "sml", false },
    { "Cleglaw's Pincers", 8, "Cleglaw's Brace", "Chain Gloves", "mgl", false },
    { "Iratha's Collar", 9, "Iratha's Finery", "Amulet", "amu", false },
    { "Iratha's Cuff", 10, "Iratha's Finery", "Light Gauntlets", "tgl", false },
    { "Iratha's Coil", 11, "Iratha's Finery", "Crown", "crn", false },
    { "Iratha's Cord", 12, "Iratha's Finery", "Heavy Belt", "tbl", false },
    { "Isenhart's Lightbrand", 13, "Isenhart's Armory", "Broad Sword", "bsd", false },
    { "Isenhart's Parry", 14, "Isenhart's Armory", "Gothic Shield", "gts", false },
    { "Isenhart's Case", 15, "Isenhart's Armory", "Breast Plate", "brs", false },
    { "Isenhart's Horns", 16, "Isenhart's Armory", "Full Helm", "fhl", false },
    { "Vidala's Barb", 17, "Vidala's Rig", "Long Battle Bow", "lbb", false },
    { "Vidala's Fetlock", 18, "Vidala's Rig", "Light Plated Boots", "tbt", false },
    { "Vidala's Ambush", 19, "Vidala's Rig", "Leather Armor", "lea", false },
    { "Vidala's Snare", 20, "Vidala's Rig", "Amulet", "amu", false },
    { "Milabrega's Orb", 21, "Milabrega's Regalia", "Kite Shield", "kit", false },
    { "Milabrega's Rod", 22, "Milabrega's Regalia", "War Scepter", "wsp", false },
    { "Milabrega's Diadem", 23, "Milabrega's Regalia", "Crown", "crn", false },
    { "Milabrega's Robe", 24, "Milabrega's Regalia", "Ancient Armor", "aar", false },
    { "Cathan's Rule", 25, "Cathan's Traps", "Battle Staff", "bst", false },
    { "Cathan's Mesh", 26, "Cathan's Traps", "Chain Mail", "chn", false },
    { "Cathan's Visage", 27, "Cathan's Traps", "Mask", "msk", false },
    { "Cathan's Sigil", 28, "Cathan's Traps", "Amulet", "amu", false },
    { "Cathan's Seal", 29, "Cathan's Traps", "Ring", "rin", false },
    { "Tancred's Crowbill", 30, "Tancred's Battlegear", "Military Pick", "mpi", false },
    { "Tancred's Spine", 31, "Tancred's Battlegear", "Full Plate Mail", "ful", false },
    { "Tancred's Hobnails", 32, "Tancred's Battlegear", "Boots", "lbt", false },
    { "Tancred's Weird", 33, "Tancred's Battlegear", "Amulet", "amu", false },
    { "Tancred's Skull", 34, "Tancred's Battlegear", "Bone Helm", "bhm", false },
    { "Sigon's Gage", 35, "Sigon's Complete Steel", "Gauntlets", "hgl", false },
    { "Sigon's Visor", 36, "Sigon's Complete Steel", "Great Helm", "ghm", false },
    { "Sigon's Shelter", 37, "Sigon's Complete Steel", "Gothic Plate", "gth", false },
    { "Sigon's Sabot", 38, "Sigon's Complete Steel", "Greaves", "hbt", false },
    { "Sigon's Wrap", 39, "Sigon's Complete Steel", "Plated Belt", "hbl", false },
    { "Sigon's Guard", 40, "Sigon's Complete Steel", "Tower Shield", "tow", false },
    { "Infernal Cranium", 41, "Infernal Tools", "Cap", "cap", false },
    { "Infernal Torch", 42, "Infernal Tools", "Grim Wand", "gwn", false },
    { "Infernal Sign", 43, "Infernal Tools", "Heavy Belt", "tbl", false },
    { "Berserker's Headgear", 44, "Berserker's Garb", "Helm", "hlm", false },
    { "Berserker's Hauberk", 45, "Berserker's Garb", "Splint Mail", "spl", false },
    { "Berserker's Hatchet", 46, "Berserker's Garb", "Double Axe", "2ax", false },
    { "Death's Hand", 47, "Death's Disguise", "Leather Gloves", "lgl", false },
    { "Death's Guard", 48, "Death's Disguise", "Sash", "lbl", false },
    { "Death's Touch", 49, "Death's Disguise", "War Sword", "wsd", false },
    { "Angelic Sickle", 50, "Angelical Raiment", "Sabre", "sbr", false },
    { "Angelic Mantle", 51, "Angelical Raiment", "Ring Mail", "rng", false },
    { "Angelic Halo", 52, "Angelical Raiment", "Ring", "rin", false },
    { "Angelic Wings", 53, "Angelical Raiment", "Amulet", "amu", false },
    { "Arctic Horn", 54, "Arctic Gear", "Short War Bow", "swb", false },
    { "Arctic Furs", 55, "Arctic Gear", "Quilted Armor", "qui", false },
    { "Arctic Binding", 56, "Arctic Gear", "Light Belt", "vbl", false },
    { "Arctic Mitts", 57, "Arctic Gear", "Light Gauntlets", "tgl", false },
    { "Arcanna's Sign", 58, "Arcanna's Tricks", "Amulet", "amu", false },
    { "Arcanna's Deathwand", 59, "Arcanna's Tricks", "War Staff", "wst", false },
    { "Arcanna's Head", 60, "Arcanna's Tricks", "Skull Cap", "skp", false },
    { "Arcanna's Flesh", 61, "Arcanna's Tricks", "Light Plate", "ltp", false },
    { "Natalya's Totem", 62, "Natalya's Odium", "Grim Helm", "xh9", false },
    { "Natalya's Mark", 63, "Natalya's Odium", "Scissors Suwayyah", "7qr", false },
    { "Natalya's Shadow", 64, "Natalya's Odium", "Loricated Mail", "ucl", false },
    { "Natalya's Soul", 65, "Natalya's Odium", "Mesh Boots", "xmb", false },
    { "Aldur's Stony Gaze", 66, "Aldur's Watchtower", "Hunter's Guise", "dr8", false },
    { "Aldur's Deception", 67, "Aldur's Watchtower", "Shadow Plate", "uul", false },
    { "Aldur's Gauntlet", 68, "Aldur's Watchtower", "Jagged Star", "9mt", false },
    { "Aldur's Advance", 69, "Aldur's Watchtower", "Battle Boots", "xtb", false },
    { "Immortal King's Will", 70, "Immortal King", "Avenger Guard", "ba5", false },
    { "Immortal King's Soul Cage", 71, "Immortal King", "Sacred Armor", "uar", false },
    { "Immortal King's Detail", 72, "Immortal King", "War Belt", "zhb", false },
    { "Immortal King's Forge", 73, "Immortal King", "War Gauntlets", "xhg", false },
    { "Immortal King's Pillar", 74, "Immortal King", "War Boots", "xhb", false },
    { "Immortal King's Stone Crusher", 75, "Immortal King", "Ogre Maul", "7m7", false },
    { "Tal Rasha's Fire-Spun Cloth", 76, "Tal Rasha's Wrappings", "Mesh Belt", "zmb", false },
    { "Tal Rasha's Adjudication", 77, "Tal Rasha's Wrappings", "Amulet", "amu", false },
    { "Tal Rasha's Lidless Eye", 78, "Tal Rasha's Wrappings", "Swirling Crystal", "oba", false },
    { "Tal Rasha's Howling Wind", 79, "Tal Rasha's Wrappings", "Lacquered Plate", "uth", false },
    { "Tal Rasha's Horadric Crest", 80, "Tal Rasha's Wrappings", "Death Mask", "xsk", false },
    { "Griswold's Valor", 81, "Griswold's Legacy", "Corona", "urn", false },
    { "Griswold's Heart", 82, "Griswold's Legacy", "Ornate Plate", "xar", false },
    { "Griswolds's Redemption", 83, "Griswold's Legacy", "Caduceus", "7ws", false },
    { "Griswold's Honor", 84, "Griswold's Legacy", "Vortex Shield", "paf", false },
    { "Trang-Oul's Guise", 85, "Trang-Oul's Avatar", "Bone Visage", "uh9", false },
    { "Trang-Oul's Scales", 86, "Trang-Oul's Avatar", "Chaos Armor", "xul", false },
    { "Trang-Oul's Wing", 87, "Trang-Oul's Avatar", "Cantor Trophy", "ne9", false },
    { "Trang-Oul's Claws", 88, "Trang-Oul's Avatar", "Heavy Bracers", "xmg", false },
    { "Trang-Oul's Girth", 89, "Trang-Oul's Avatar", "Troll Belt", "utc", false },
    { "M'avina's True Sight", 90, "M'avina's Battle Hymn", "Diadem", "ci3", false },
    { "M'avina's Embrace", 91, "M'avina's Battle Hymn", "Kraken Shell", "uld", false },
    { "M'avina's Icy Clutch", 92, "M'avina's Battle Hymn", "Battle Gauntlets", "xtg", false },
    { "M'avina's Tenet", 93, "M'avina's Battle Hymn", "Sharkskin Belt", "zvb", false },
    { "M'avina's Caster", 94, "M'avina's Battle Hymn", "Grand Matron Bow", "amc", false },
    { "Telling of Beads", 95, "The Disciple", "Amulet", "amu", false },
    { "Laying of Hands", 96, "The Disciple", "Bramble Mitts", "ulg", false },
    { "Rite of Passage", 97, "The Disciple", "Demonhide Boots", "xlb", false },
    { "Spiritual Custodian", 98, "The Disciple", "Dusk Shroud", "uui", false },
    { "Credendum", 99, "The Disciple", "Mithril Coil", "umc", false },
    { "Dangoon's Teaching", 100, "Heaven's Brethren", "Reinforced Mace", "7ma", false },
    { "Heaven's Taebaek", 101, "Heaven's Brethren", "Ward", "uts", false },
    { "Haemosu's Adament", 102, "Heaven's Brethren", "Cuirass", "xrs", false },
    { "Ondal's Almighty", 103, "Heaven's Brethren", "Spired Helm", "uhm", false },
    { "Guillaume's Face", 104, "Orphan's Call", "Winged Helm", "xhm", false },
    { "Wilhelm's Pride", 105, "Orphan's Call", "Battle Belt", "ztb", false },
    { "Magnus' Skin", 106, "Orphan's Call", "Sharkskin Gloves", "xvg", false },
    { "Wihtstan's Guard", 107, "Orphan's Call", "Round Shield", "xml", false },
    { "Hwanin's Splendor", 108, "Hwanin's Majesty", "Grand Crown", "xrn", false },
    { "Hwanin's Refuge", 109, "Hwanin's Majesty", "Tigulated Mail", "xcl", false },
    { "Hwanin's Seal", 110, "Hwanin's Majesty", "Belt", "mbl", false },
    { "Hwanin's Justice", 111, "Hwanin's Majesty", "Bill", "9vo", false },
    { "Sazabi's Cobalt Redeemer", 112, "Sazabi's Grand Tribute", "Cryptic Sword", "7ls", false },
    { "Sazabi's Ghost Liberator", 113, "Sazabi's Grand Tribute", "Balrog Skin", "upl", false },
    { "Sazabi's Mental Sheath", 114, "Sazabi's Grand Tribute", "Basinet", "xhl", false },
    { "Bul-Kathos' Sacred Charge", 115, "Bul-Kathos' Children", "Colossus Blade", "7gd", false },
    { "Bul-Kathos' Tribal Guardian", 116, "Bul-Kathos' Children", "Mythical Sword", "7wd", false },
    { "Cow King's Horns", 117, "Cow King's Leathers", "War Hat", "xap", false },
    { "Cow King's Hide", 118, "Cow King's Leathers", "Studded Leather", "stu", false },
    { "Cow King's Hoofs", 119, "Cow King's Leathers", "Heavy Boots", "vbt", false },
    { "Naj's Puzzler", 120, "Naj's Ancient Set", "Elder Staff", "6cs", false },
    { "Naj's Light Plate", 121, "Naj's Ancient Set", "Hellforge Plate", "ult", false },
    { "Naj's Circlet", 122, "Naj's Ancient Set", "Circlet", "ci0", false },
    { "McAuley's Paragon", 123, "McAuley's Folly", "Cap", "cap", false },
    { "McAuley's Riprap", 124, "McAuley's Folly", "Heavy Boots", "vbt", false },
    { "McAuley's Taboo", 125, "McAuley's Folly", "Heavy Gloves", "vgl", false },
    { "McAuley's Superstition", 126, "McAuley's Folly", "Bone Wand", "bwn", false },

};

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

static int MaxInt5(int a, int b, int c, int d, int e)
{
    return MaxInt(a, MaxInt(b, MaxInt(c, MaxInt(d, e))));
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

bool GenerateStaticArrays(const std::string& filepath, int itemsPerLine)
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
    int colUniqueItemName = FindColumn(header, "*ItemName");

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

        if (colUniqueName >= 0 && colUniqueID >= 0 && colUniqueCode >= 0 && colUniqueEnabled >= 0 && colUniqueItemName >= 0)
        {
            int maxCol = MaxInt5(colUniqueName, colUniqueID, colUniqueCode, colUniqueEnabled, colUniqueItemName);
            if (cols.size() <= maxCol) continue;

            int idVal;
            if (!SafeStringToInt(cols[colUniqueID], idVal)) continue;

            std::string enabledStr = cols[colUniqueEnabled];
            bool enabled = (enabledStr == "1" || enabledStr == "true");
            if (!enabled) continue;

            outFile << "{ " << runningIndex << ", "  // index
                << idVal << ", \""                     // id
                << cols[colUniqueName] << "\", \""     // name
                << cols[colUniqueCode] << "\", \""      // code
                << cols[colUniqueItemName] << "\","      // ItemName
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

        if (colSetName >= 0 && colUniqueID >= 0 && colUniqueName >= 0 && colSetItemCode >= 0 && colUniqueItemName >= 0)
        {
            int maxCol = MaxInt5(colUniqueName, colUniqueID, colSetName, colSetItemCode, colUniqueItemName);
            if (cols.size() <= maxCol) continue;

            int idVal;
            if (!SafeStringToInt(cols[colUniqueID], idVal)) continue;

            outFile << "    { \"" << cols[colUniqueName] << "\", "
                << idVal << ", \"" << cols[colSetName] << "\", \""
                << cols[colUniqueItemName] << "\", \""      // ItemName
                << cols[colSetItemCode] << "\", false }, ";
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

        std::string filename = configFilePath;

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

static bool LoadUniqueItemsFromFile(const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    std::string line;
    std::getline(file, line);
    auto header = SplitTab(line);

    int colIndex = FindColumn(header, "index");
    int colID = FindColumn(header, "*ID");
    int colEnabled = FindColumn(header, "enabled");
    int colCode = FindColumn(header, "code");
    int colItemName = FindColumn(header, "*ItemName");

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
        if (!SafeStringToInt(cols[colID], indexVal))
            continue;
        entry.id = indexVal;

        entry.name = cols[colIndex];
        entry.code = cols[colCode];
        if (colItemName >= 0 && static_cast<int>(cols.size()) > colItemName)
            entry.itemName = cols[colItemName];

        std::string enabledStr = cols[colEnabled];
        entry.enabled = (enabledStr == "1" || enabledStr == "true");

        if (!(enabledStr == "1" || enabledStr == "true"))
            continue;

        g_UniqueItems.push_back(entry);
    }

    return !g_UniqueItems.empty();
}

bool LoadUniqueItems(const std::string& filepath)
{
    g_UniqueItems.clear();

    if (modName == "RMD-MP")
    {
        const int arraySize = sizeof(g_StaticUniqueItemsRMD) / sizeof(g_StaticUniqueItemsRMD[0]);
        for (int i = 0; i < arraySize; ++i)
            g_UniqueItems.push_back(g_StaticUniqueItemsRMD[i]);
        return true;
    }
    

    // Use static array for Retail Mods if file doesn't exist
    if (!std::filesystem::exists("Mods/" + modName + "/" + modName + ".mpq/data/global/excel/uniqueitems.txt") && modName != "RMD-MP")
    {
        int arraySize = sizeof(g_StaticUniqueItems) / sizeof(g_StaticUniqueItems[0]);
        for (int i = 0; i < arraySize; ++i)
        {
            g_UniqueItems.push_back(g_StaticUniqueItems[i]);
        }
        return true;
    }

    return LoadUniqueItemsFromFile(filepath);
}

static bool LoadSetItemsFromFile(const std::string& filepath)
{
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
    int colItemName = FindColumn(header, "*ItemName");

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
        if (colItemName >= 0 && static_cast<int>(cols.size()) > colItemName)
            entry.itemName = cols[colItemName];
        g_SetItems.push_back(entry);
    }

    return !g_SetItems.empty();
}

bool LoadSetItems(const std::string& filepath)
{
    g_SetItems.clear();

    if (modName == "RMD-MP")
    {
        const int arraySize = sizeof(g_StaticSetItemsRMD) / sizeof(g_StaticSetItemsRMD[0]);
        for (int i = 0; i < arraySize; ++i)
            g_SetItems.push_back(g_StaticSetItemsRMD[i]);
        return true;
    }

    // Use static array for Retail Mods if file doesn't exist
    if (!std::filesystem::exists(filepath))
    {
        const int arraySize = sizeof(g_StaticSetItems) / sizeof(g_StaticSetItems[0]);
        for (int i = 0; i < arraySize; ++i)
            g_SetItems.push_back(g_StaticSetItems[i]);
        return true;
    }

    return LoadSetItemsFromFile(filepath);
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

// --- Stash / D2I parsing (FindItemOffsets) ---
#include "ItemFilter/ItemFilter.h"
#include <Windows.h>
#include <cctype>
#include <memory>
#include <regex>
#include <stdexcept>
#include <unordered_map>

extern std::wstring GetSavePath();
extern bool isHardcore;

static bool IsHardcoreForGrailScan();

std::unordered_map<uint32_t, std::string> g_SetItemLookup;
std::unordered_map<uint32_t, std::string> g_UniqueItemLookup;

bool showStashParseDebug = false;
bool g_ForceStashRescan = false;
bool g_StashScanInProgress = false;
double g_DeferStashScanUntil = 0.0;
int g_StashScanPageFilter = 0;
std::vector<int> g_AvailableStashPages;
std::vector<StashParsedItemDebug> g_StashDebugEntries;

static std::string GetGrailPluginDirectory()
{
    char buf[MAX_PATH]{};
    HMODULE self = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&GetGrailPluginDirectory),
            &self) && self)
    {
        GetModuleFileNameA(self, buf, MAX_PATH);
        return std::filesystem::path(buf).parent_path().string();
    }
    return {};
}

void BuildItemNameLookups()
{
    g_SetItemLookup.clear();
    g_UniqueItemLookup.clear();

    for (auto& s : g_SetItems)
        g_SetItemLookup[s.id] = s.setName.empty() ? "Unknown Set Item" : s.setName;

    for (auto& u : g_UniqueItems)
        g_UniqueItemLookup[u.id] = u.name.empty() ? "Unknown Unique" : u.name;
}

class BitReader {
public:
    BitReader(const std::vector<uint8_t>& buffer)
        : bufPtr(buffer.data()), bufSize(buffer.size()), bitPos(0) {
    }

    BitReader(const uint8_t* data, size_t size)
        : bufPtr(data), bufSize(size), bitPos(0) {
    }

    uint32_t ReadBits(size_t bits) {
        if (bits > 32)
            throw std::runtime_error("Cannot read more than 32 bits at once");

        uint32_t result = 0;
        for (size_t i = 0; i < bits; ++i) {
            size_t byteIdx = bitPos >> 3;
            size_t bitIdx = bitPos & 7;

            if (byteIdx >= bufSize)
                throw std::runtime_error("Buffer overflow");

            if ((bufPtr[byteIdx] >> bitIdx) & 1)
                result |= (1u << i);

            ++bitPos;
        }
        return result;
    }

    uint8_t ReadUInt8(size_t bits) { return static_cast<uint8_t>(ReadBits(bits)); }
    uint16_t ReadUInt16(size_t bits) { return static_cast<uint16_t>(ReadBits(bits)); }
    uint32_t ReadUInt32(size_t bits) { return ReadBits(bits); }
    bool ReadBit() { return ReadBits(1) != 0; }

    void SkipBits(size_t bits) { bitPos += bits; }
    void SetBitPos(size_t pos) { bitPos = pos; }
    size_t GetBitPos() const { return bitPos; }
    size_t GetBytePos() const { return bitPos >> 3; }

    void AlignToByte() {
        if (bitPos & 7)
            bitPos = ((bitPos >> 3) + 1) << 3;
    }

    bool HasBits(size_t n) const {
        return bitPos + n <= bufSize * 8;
    }

private:
    const uint8_t* bufPtr;
    size_t bufSize;
    size_t bitPos;
};


struct EarAttributes {
    uint8_t clazz = 0;
    uint8_t level = 0;
    std::string name;
};

struct Item {
    // Base header flags
    bool identified = false;
    bool socketed = false;
    bool new_flag = false;
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

    // Item core data
    uint32_t id = 0;
    uint8_t level = 0;
    std::string type;

    uint8_t nr_of_items_in_sockets = 0;

    // Picture, class-specific
    bool multiple_pictures = false;
    uint8_t picture_id = 0;
    bool class_specific = false;
    uint16_t auto_affix_id = 0;

    // Quality
    uint8_t quality = 0;

    uint8_t low_quality_id = 0;
    uint8_t file_index = 0;

    uint16_t magic_prefix = 0;
    uint16_t magic_suffix = 0;

    uint16_t set_id = 0;
    uint16_t unique_id = 0;

    uint8_t rare_name_id = 0;
    uint8_t rare_name_id2 = 0;
    uint16_t magical_name_ids[6] = { 0 };

    EarAttributes ear_attributes;

    uint16_t personalized_id = 0;
    uint32_t runeword_id = 0;

    std::vector<Item> socketed_items;
};

#pragma endregion

#pragma region Huffman Tree

struct HuffmanNode {
    char value = 0;
    HuffmanNode* left = nullptr;
    HuffmanNode* right = nullptr;
    ~HuffmanNode() { delete left; delete right; }
};

static const std::vector<std::pair<char, std::string>> HUFFMAN_CODES = {
    {' ', "10"},
    {'0', "11111011"},
    {'1', "1111100"},
    {'2', "001100"},
    {'3', "1101101"},
    {'4', "11111010"},
    {'5', "00010110"},
    {'6', "1101111"},
    {'7', "01111"},
    {'8', "000100"},
    {'9', "01110"},
    {'a', "11110"},
    {'b', "0101"},
    {'c', "01000"},
    {'d', "110001"},
    {'e', "110000"},
    {'f', "010011"},
    {'g', "11010"},
    {'h', "00011"},
    {'i', "1111110"},
    {'j', "000101110"},
    {'k', "010010"},
    {'l', "11101"},
    {'m', "01101"},
    {'n', "001101"},
    {'o', "1111111"},
    {'p', "11001"},
    {'q', "11011001"},
    {'r', "11100"},
    {'s', "0010"},
    {'t', "01100"},
    {'u', "00001"},
    {'v', "1101110"},
    {'w', "00000"},
    {'x', "00111"},
    {'y', "0001010"},
    {'z', "11011000"},

    // --- Capitals ---
    {'A', "00010111101011100"},
    {'B', "00010111111001110"},
    {'C', "00010111111011001"},
    {'D', "00010111110011111"},
    {'E', "00010111101111000"},
    {'F', "00010111110011100"},
    {'G', "00010111110101011"},
    {'H', "00010111110111001"},
    {'I', "00010111110111100"},
    {'J', "0001011110000111"},
    {'K', "00010111110100100"},
    {'L', "00010111110010010"},
    {'M', "00010111111011011"},
    {'N', "0001011110000010"},
    {'O', "00010111111101011"},
    {'P', "00010111110100111"},
    {'Q', "00010111101100100"},
    {'R', "00010111111100111"},
    {'S', "00010111101101010"},
    {'T', "00010111110001010"},
    {'U', "00010111110001001"},
    {'V', "00010111110010001"},
    {'W', "00010111111101110"},
    {'X', "00010111111111010"},
    {'Y', "00010111111111101"},
    {'Z', "00010111110000100"},
};

HuffmanNode* BuildHuffmanTreeFromTable() {
    auto root = new HuffmanNode{};

    for (auto& [ch, bits] : HUFFMAN_CODES) {
        HuffmanNode* node = root;

        for (char b : bits) {
            if (b == '0') {
                if (!node->left) node->left = new HuffmanNode{};
                node = node->left;
            }
            else {
                if (!node->right) node->right = new HuffmanNode{};
                node = node->right;
            }
        }

        node->value = ch; // leaf
    }

    return root;
}

char DecodeHuffmanChar(BitReader& reader, HuffmanNode* root) {
    HuffmanNode* node = root;
    int depth = 0;
    while (node && node->value == 0 && depth++ < 30) {
        bool bit = reader.ReadBit();
        node = bit ? node->right : node->left;
    }
    if (!node) throw std::runtime_error("Invalid Huffman tree traversal");
    return node->value;
}

std::string DecodeHuffmanString(BitReader& reader, HuffmanNode* root) {
    std::string s;
    for (int i = 0; i < 4; ++i) {
        char c = DecodeHuffmanChar(reader, root);
        if (c == ' ' || c == 0) break;
        s += c;
    }
    return s;
}

static std::string TrimItemCode(std::string code)
{
    while (!code.empty() && (code.back() == ' ' || code.back() == '\0'))
        code.pop_back();
    return code;
}

static uint8_t NormalizeItemQuality(uint8_t rawQuality);

Item ParseItem(const uint8_t* data, size_t size, HuffmanNode* huffmanRoot, uint32_t fileVersion)
{
    Item item;
    BitReader reader(data, size);

    reader.SkipBits(4);
    item.identified = reader.ReadBit();
    reader.SkipBits(1);
    item.socketed = reader.ReadBit();
    reader.SkipBits(2);
    item.new_flag = reader.ReadBit();
    reader.SkipBits(1);
    item.is_ear = reader.ReadBit();
    item.starter_item = reader.ReadBit();
    reader.SkipBits(8);
    item.simple_item = reader.ReadBit();
    item.ethereal = reader.ReadBit();
    reader.SkipBits(1);
    item.personalized = reader.ReadBit();
    reader.SkipBits(1);
    item.given_runeword = reader.ReadBit();
    reader.SkipBits(5);

    if (fileVersion >= 0x61)
        item.version = reader.ReadUInt16(3);
    else
        item.version = reader.ReadUInt16(10);

    item.location_id = reader.ReadUInt8(3);
    item.equipped_id = reader.ReadUInt8(4);
    item.position_x = reader.ReadUInt8(4);
    item.position_y = reader.ReadUInt8(4);
    item.alt_position_id = reader.ReadUInt8(3);

    if (item.is_ear && item.simple_item)
    {
        item.ear_attributes.clazz = reader.ReadUInt8(3);
        item.ear_attributes.level = reader.ReadUInt8(7);
        for (int i = 0; i < 15; i++)
        {
            const uint8_t ch = reader.ReadUInt8(7);
            if (ch == 0)
                break;
            item.ear_attributes.name.push_back(static_cast<char>(ch));
        }
        return item;
    }

    if (fileVersion >= 0x61)
        item.type = DecodeHuffmanString(reader, huffmanRoot);
    else
    {
        for (int i = 0; i < 4; ++i)
        {
            const char c = static_cast<char>(reader.ReadUInt8(8));
            if (c && c != ' ')
                item.type += c;
        }
    }
    item.type = TrimItemCode(item.type);

    item.nr_of_items_in_sockets = reader.ReadUInt8(item.simple_item ? 1 : 3);

    if (item.simple_item)
        return item;

    item.id = reader.ReadUInt32(32);
    item.level = reader.ReadUInt8(7);
    item.quality = NormalizeItemQuality(reader.ReadUInt8(4));

    item.multiple_pictures = reader.ReadBit();
    if (item.multiple_pictures)
        item.picture_id = reader.ReadUInt8(3);

    item.class_specific = reader.ReadBit();
    if (item.class_specific)
        item.auto_affix_id = reader.ReadUInt16(11);

    switch (item.quality)
    {
    case 1:
        item.low_quality_id = reader.ReadUInt8(3);
        break;
    case 3:
        item.file_index = reader.ReadUInt8(3);
        break;
    case 4:
        item.magic_prefix = reader.ReadUInt16(11);
        item.magic_suffix = reader.ReadUInt16(11);
        break;
    case 5:
        item.set_id = reader.ReadUInt16(12);
        break;
    case 6:
    case 8:
        item.rare_name_id = reader.ReadUInt8(8);
        item.rare_name_id2 = reader.ReadUInt8(8);
        for (int i = 0; i < 3; ++i)
        {
            if (reader.ReadBit())
                item.magical_name_ids[i] = reader.ReadUInt16(11);
            if (reader.ReadBit())
                item.magical_name_ids[i + 3] = reader.ReadUInt16(11);
        }
        break;
    case 7:
        item.unique_id = reader.ReadUInt16(12);
        break;
    default:
        break;
    }

    return item;
}

std::vector<size_t> FindItemOffsets(const std::vector<uint8_t>& buf, size_t start, size_t end) {
    std::vector<size_t> offsets;
    for (size_t i = start; i + 4 < end; i++) {
        // D2R item flags have multiple patterns depending on item properties
        // Common patterns: 10 00 80 00, 10 20 a0 00, 10 08 80 00, etc.
        // Byte 0: lower nibble is typically 0 (0x10, 0x00)
        // Byte 2: has bit 7 set (0x80, 0xa0, 0xc0)
        // Byte 3: is 0x00
        bool byte0_valid = (buf[i] & 0x0F) == 0;      // lower nibble is 0
        bool byte2_valid = (buf[i + 2] & 0x80) != 0;    // bit 7 set
        bool byte3_valid = buf[i + 3] == 0x00;          // must be 0

        if (byte0_valid && byte2_valid && byte3_valid) {
            offsets.push_back(i);
        }
    }
    return offsets;
}

#pragma endregion

static std::unordered_set<std::string> g_ValidBaseItemCodes;

static void LoadBaseItemCodesFromExcel(const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
        return;

    std::string line;
    std::getline(file, line);
    auto header = SplitTab(line);
    int colCode = FindColumn(header, "code");
    if (colCode < 0)
        colCode = 0;

    while (std::getline(file, line))
    {
        auto cols = SplitTab(line);
        if (cols.size() <= static_cast<size_t>(colCode))
            continue;
        std::string code = TrimItemCode(cols[colCode]);
        if (code.size() >= 3 && code.size() <= 4)
            g_ValidBaseItemCodes.insert(code);
    }
}

static void EnsureValidBaseItemCodesLoaded()
{
    if (g_ValidBaseItemCodesLoaded)
        return;
    g_ValidBaseItemCodesLoaded = true;

    const std::string dllDir = GetGrailPluginDirectory();
    if (!dllDir.empty())
    {
        LoadBaseItemCodesFromExcel(dllDir + "/items.txt");
        LoadBaseItemCodesFromExcel(dllDir + "/armor.txt");
        LoadBaseItemCodesFromExcel(dllDir + "/weapons.txt");
    }

    const std::string excelBase = "Mods/" + modName + "/" + modName + ".mpq/data/global/excel/";
    LoadBaseItemCodesFromExcel(excelBase + "items.txt");
    LoadBaseItemCodesFromExcel(excelBase + "armor.txt");
    LoadBaseItemCodesFromExcel(excelBase + "weapons.txt");
}

static bool IsValidItemTypeCode(const std::string& code)
{
    if (code.size() < 3 || code.size() > 4)
        return false;
    for (char c : code)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)))
            return false;
    }
    if (g_ValidBaseItemCodes.empty())
        return true;
    return g_ValidBaseItemCodes.count(code) != 0;
}

static bool IsSharedStashPanelItem(const Item& item)
{
    return item.location_id == 0 && item.alt_position_id == 5;
}

static uint8_t NormalizeItemQuality(uint8_t rawQuality)
{
    if (rawQuality <= 9)
        return rawQuality;
    const uint8_t flipped = static_cast<uint8_t>((~rawQuality) & 0x0F);
    if (flipped <= 9)
        return flipped;
    return rawQuality;
}

const char* GetQualityName(uint32_t q)
{
    const char* names[] = { "", "Inferior", "Normal", "Superior", "Magic", "Set", "Rare", "Unique", "Crafted", "Tempered" };
    return q < 10 ? names[q] : "Unknown";
}

static bool IsStashOffsetCandidate(const Item& item)
{
    if (item.is_ear && item.simple_item)
        return false;
    if (!IsSharedStashPanelItem(item))
        return false;
    if (item.position_x > 15 || item.position_y > 12)
        return false;
    return IsValidItemTypeCode(TrimItemCode(item.type));
}

static bool IsGrailEligibleStashItem(const Item& item)
{
    if (!IsStashOffsetCandidate(item))
        return false;
    return item.quality <= 9;
}

static SetItemEntry* ResolveSetItemForStash(const Item& item)
{
    if (item.quality != 5)
        return nullptr;

    const std::string code = TrimItemCode(item.type);

    const auto idIt = g_SetById.find(static_cast<int>(item.set_id));
    if (idIt != g_SetById.end())
        return idIt->second;

    const auto codeIt = g_SetByCode.find(code);
    if (codeIt != g_SetByCode.end())
        return codeIt->second;

    return nullptr;
}

static UniqueItemEntry* ResolveUniqueItemForStash(const Item& item)
{
    if (item.quality != 7)
        return nullptr;

    const std::string code = TrimItemCode(item.type);

    const auto idIt = g_UniqueById.find(static_cast<int>(item.unique_id));
    if (idIt != g_UniqueById.end())
        return idIt->second;

    const auto codeIt = g_UniqueByCode.find(code);
    if (codeIt != g_UniqueByCode.end())
        return codeIt->second;

    return nullptr;
}

static bool TryParseStashItemAtOffset(
    const std::vector<uint8_t>& buf,
    size_t offset,
    size_t tabEnd,
    HuffmanNode* huffmanRoot,
    uint32_t fileVersion,
    Item& outItem)
{
    if (offset + 16 >= tabEnd)
        return false;

    const size_t maxSlice = (std::min)(tabEnd - offset, static_cast<size_t>(512));
    try
    {
        outItem = ParseItem(buf.data() + offset, maxSlice, huffmanRoot, fileVersion);
        return IsStashOffsetCandidate(outItem);
    }
    catch (...)
    {
        return false;
    }
}

static std::vector<size_t> FilterItemOffsets(
    const std::vector<uint8_t>& buf,
    const std::vector<size_t>& rawOffsets,
    size_t tabEnd,
    HuffmanNode* huffmanRoot,
    uint32_t fileVersion)
{
    std::vector<size_t> filtered;
    filtered.reserve(rawOffsets.size());

    for (size_t offset : rawOffsets)
    {
        Item item;
        if (!TryParseStashItemAtOffset(buf, offset, tabEnd, huffmanRoot, fileVersion, item))
            continue;

        if (!filtered.empty() && offset - filtered.back() < 12)
        {
            Item prev;
            if (TryParseStashItemAtOffset(buf, filtered.back(), tabEnd, huffmanRoot, fileVersion, prev))
            {
                const int prevScore = (prev.quality == 5 || prev.quality == 7) ? 2 : 1;
                const int curScore = (item.quality == 5 || item.quality == 7) ? 2 : 1;
                if (curScore <= prevScore)
                    continue;
            }
            filtered.pop_back();
        }

        filtered.push_back(offset);
    }

    return filtered;
}

void ClearStashDebugEntries()
{
    g_StashDebugEntries.clear();
}

const char* GetStashItemQualityName(uint8_t quality)
{
    return GetQualityName(quality);
}

static void RecordStashParseFailure(const Item& item, int page, int tab, const char* note)
{
    if (!showStashParseDebug)
        return;

    StashParsedItemDebug entry;
    entry.page = page;
    entry.tab = tab;
    entry.x = item.position_x + 1;
    entry.y = item.position_y + 1;
    entry.code = item.type;
    entry.quality = item.quality;
    entry.setId = item.set_id;
    entry.uniqueId = item.unique_id;
    entry.identified = item.identified;
    entry.note = note;
    g_StashDebugEntries.push_back(std::move(entry));
}

static void RecordStashDebugEntry(const Item& item, int page, int tab)
{
    if (!showStashParseDebug)
        return;

    StashParsedItemDebug entry;
    entry.page = page;
    entry.tab = tab;
    entry.x = item.position_x + 1;
    entry.y = item.position_y + 1;
    entry.code = item.type;
    entry.quality = item.quality;
    entry.setId = item.set_id;
    entry.uniqueId = item.unique_id;
    entry.identified = item.identified;

    if (SetItemEntry* setEntry = ResolveSetItemForStash(item))
    {
        entry.grailMatched = true;
        entry.grailName = setEntry->name;
        if (!g_SetById.count(static_cast<int>(item.set_id)) && g_SetByCode.count(TrimItemCode(item.type)))
            entry.note = "Set ID not in grail list (matched by item code)";
    }
    else if (UniqueItemEntry* uniqueEntry = ResolveUniqueItemForStash(item))
    {
        entry.grailMatched = true;
        entry.grailName = uniqueEntry->name;
        if (!g_UniqueById.count(static_cast<int>(item.unique_id)) && g_UniqueByCode.count(TrimItemCode(item.type)))
            entry.note = "Unique ID not in grail list (matched by item code)";
    }
    else if (item.quality == 5)
        entry.note = "Set ID/code not in grail list";
    else if (item.quality == 7)
        entry.note = "Unique ID/code not in grail list";
    else
        entry.note = GetQualityName(item.quality);

    g_StashDebugEntries.push_back(std::move(entry));
}


static int ParseSharedStash(const std::string& filePath, int pageNum)
{
    EnsureValidBaseItemCodesLoaded();

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file)
        return 1;

    const size_t fileSize = static_cast<size_t>(file.tellg());
    if (fileSize < 16)
        return 1;

    file.seekg(0);

    std::vector<uint8_t> buf;
    buf.resize(fileSize);
    file.read(reinterpret_cast<char*>(buf.data()), fileSize);

    if (buf[0] != 0x55 || buf[1] != 0xAA || buf[2] != 0x55 || buf[3] != 0xAA)
        return 1;

    const uint32_t version = buf[8];
    const int page = pageNum;

    std::unique_ptr<HuffmanNode> huffman(BuildHuffmanTreeFromTable());

    int totalItems = 0;
    int uniqueCount = 0;
    int setCount = 0;

    RebuildGrailStashLookups();

    std::vector<size_t> tabOffsets;
    tabOffsets.reserve(8);

    for (size_t i = 0, end = fileSize - 3; i < end; ++i) {
        if (buf[i] == 0x55 && buf[i + 1] == 0xAA && buf[i + 2] == 0x55 && buf[i + 3] == 0xAA)
            tabOffsets.push_back(i);
    }

    for (size_t tabIdx = 0; tabIdx < tabOffsets.size(); ++tabIdx) {
        const size_t tabStart = tabOffsets[tabIdx];
        const size_t tabEnd = (tabIdx + 1 < tabOffsets.size()) ? tabOffsets[tabIdx + 1] : fileSize;

        size_t jmOffset = 0;
        for (size_t i = tabStart; i + 1 < tabEnd; ++i) {
            if (buf[i] == 'J' && buf[i + 1] == 'M') {
                jmOffset = i;
                break;
            }
        }

        if (!jmOffset)
            continue;

        if (jmOffset + 3 < fileSize && buf[jmOffset + 2] == 0 && buf[jmOffset + 3] == 0)
            continue;

        auto itemOffsets = FindItemOffsets(buf, jmOffset + 4, tabEnd);
        if (itemOffsets.empty())
            continue;

        itemOffsets = FilterItemOffsets(buf, itemOffsets, tabEnd, huffman.get(), version);
        if (itemOffsets.empty())
            continue;

        const int tab = static_cast<int>(tabIdx) + 1;

        for (size_t i = 0; i < itemOffsets.size(); ++i) {
            const size_t offset = itemOffsets[i];
            const size_t nextOffset = (i + 1 < itemOffsets.size()) ? itemOffsets[i + 1] : tabEnd;

            try {
                Item item = ParseItem(
                    buf.data() + offset,
                    nextOffset - offset,
                    huffman.get(),
                    version);

                if (!IsStashOffsetCandidate(item))
                    continue;

                if (showStashParseDebug)
                    RecordStashDebugEntry(item, page, tab);

                if (!IsGrailEligibleStashItem(item))
                    continue;

                ++totalItems;

                if (SetItemEntry* setEntry = ResolveSetItemForStash(item)) {
                    setEntry->collected = true;
                    setEntry->locations.push_back({
                        page, tab, item.position_x + 1, item.position_y + 1 });
                    ++setCount;
                }
                else if (UniqueItemEntry* uniqueEntry = ResolveUniqueItemForStash(item)) {
                    uniqueEntry->collected = true;
                    uniqueEntry->locations.push_back({
                        page, tab, item.position_x + 1, item.position_y + 1 });
                    ++uniqueCount;
                }
            }
            catch (...) {
            }
        }
    }

    g_GrailRevision++;
    return 0;
}
void ScanStashPages()
{
    if (!IsPlayerInGame())
        return;

    if (g_StashScanInProgress)
        return;

    g_StashScanInProgress = true;
    struct StashScanScopeGuard {
        ~StashScanScopeGuard() { g_StashScanInProgress = false; }
    } stashScanGuard;

    g_ForceStashRescan = false;
    const bool hardcore = IsHardcoreForGrailScan();
    isHardcore = hardcore;

    // Reset Collected State
    for (auto& s : g_SetItems) {
        s.collected = false;
        s.locations.clear();
    }
    for (auto& u : g_UniqueItems) {
        u.collected = false;
        u.locations.clear();
    }

    if (showStashParseDebug)
        g_StashDebugEntries.clear();

    namespace fs = std::filesystem;
    const std::wstring stashFolder = GetSavePath() + L"\\Diablo II Resurrected\\Mods\\" + std::wstring(modName.begin(), modName.end()) + L"\\";

    if (!fs::exists(stashFolder))
        return;

    const std::string prefix = hardcore ? "Stash_HC_Page" : "Stash_SC_Page";
    const std::string suffix = ".d2i";

    std::vector<std::pair<int, std::string>> pages;
    pages.reserve(64);

    for (const auto& entry : fs::directory_iterator(stashFolder))
    {
        if (!entry.is_regular_file())
            continue;

        const std::string filename = entry.path().filename().string();

        // Page String Checks
        if (filename.rfind(prefix, 0) != 0)
            continue;
        if (filename.size() <= prefix.size() + suffix.size())
            continue;
        if (filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;

        // Extract Page Number
        const std::string numStr = filename.substr(prefix.size(), filename.size() - prefix.size() - suffix.size());

        int pageNum = std::atoi(numStr.c_str());
        if (pageNum >= 1 && pageNum <= 64)
            pages.emplace_back(pageNum, entry.path().string());
    }

    // Only sort if needed
    if (pages.size() > 1)
    {
        std::sort(pages.begin(), pages.end(),
            [](const auto& a, const auto& b) {
                return a.first < b.first;
            });
    }

    g_AvailableStashPages.clear();
    g_AvailableStashPages.reserve(pages.size());
    for (const auto& [pageNum, path] : pages)
        g_AvailableStashPages.push_back(pageNum);

    const int pageFilter = showStashParseDebug ? g_StashScanPageFilter : 0;

    for (const auto& [pageNum, path] : pages)
    {
        if (pageFilter != 0 && pageNum != pageFilter)
            continue;
        ParseSharedStash(path, pageNum);
    }

    ReloadGameFilterForGrail();
}

static bool IsHardcoreForGrailScan()
{
    constexpr uint32_t sharedStashFlagOffset = 0x1BF0883;
    const uint64_t addr = Pattern::Address(sharedStashFlagOffset);
    if (!addr)
        return false;
    const uint8_t value = *reinterpret_cast<const uint8_t*>(addr);
    return (value & (1 << 2)) != 0;
}

void LoadAllItemData()
{
    g_UniqueItems.clear();
    g_SetItems.clear();

    const std::string dllDir = GetGrailPluginDirectory();
    const std::string modExcel = "Mods/" + modName + "/" + modName + ".mpq/data/global/excel/";

    if (!dllDir.empty())
    {
        LoadUniqueItems(dllDir + "/uniqueitems.txt");
        LoadSetItems(dllDir + "/setitems.txt");
    }
    if (g_UniqueItems.empty())
        LoadUniqueItems(modExcel + "uniqueitems.txt");
    if (g_SetItems.empty())
        LoadSetItems(modExcel + "setitems.txt");

    SortItemLists();
    g_ValidBaseItemCodesLoaded = false;
    RebuildGrailStashLookups();
    BuildItemNameLookups();
    LoadGrailProgress(configFilePath);
}

GrailStatus GetGrailStatus(uint32_t id, bool isSetItem)
{
    GrailStatus g;
    g.isGrail = false;

    if (isSetItem)
    {
        for (auto& s : g_SetItems)
        {
            if (s.id == id)
            {
                g.isGrail = true;
                if (s.collected) g.collected = true;
                g.located += static_cast<int>(s.locations.size());
                break;
            }
        }
    }
    else
    {
        for (auto& u : g_UniqueItems)
        {
            if (u.id == id)
            {
                g.isGrail = true;
                if (u.collected) g.collected = true;
                g.located += static_cast<int>(u.locations.size());
                break;
            }
        }
    }

    return g;
}
