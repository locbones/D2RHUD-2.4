-- D2RLAN / Pandemonium loot filter engine. Preferred syntax and aliases: see the guide.
--#region Not/OR Exclusion Variable

-- Define NOT variable as a non-global entity
local function NOT(value)
    return setmetatable(type(value) == "table" and value or { value }, { __not = true })
end

-- Define OR variable as a non-global entity
local function OR(value)
    return setmetatable(type(value) == "table" and value or { value }, { __or = true })
end

local configEnv = {
    NOT = NOT,
    OR = OR,
}

setmetatable(configEnv, { __index = _G })

-- Define LUA Environment
local function loadConfig(filename)
    local chunk, err
    if _VERSION == "Lua 5.1" then
        chunk, err = loadfile(filename)
        if not chunk then error(err) end
        setfenv(chunk, configEnv)
    else
        chunk, err = loadfile(filename, "t", configEnv)
        if not chunk then error(err) end
    end
    return chunk()
end


-- Supported TTS pipes
local pipeNames = {
    "\\\\.\\pipe\\D2RLAN_TTS",
    "\\\\.\\pipe\\Pandemonium_TTS"
}

local pipe
local activePipeName
local nextPipeAttempt = 0
local closePipe

-- Open the first available pipe
local function openPipe()
    if pipe then return end
    if os.time() < nextPipeAttempt then return end
    nextPipeAttempt = os.time() + 5

    for _, pipeName in ipairs(pipeNames) do
        local p = io.open(pipeName, "w")

        if p then
            pipe = p
            nextPipeAttempt = 0
            activePipeName = pipeName
            return
        end
    end

    -- Missing audio service is optional; retry at most once every five seconds.
end

-- Write a line to the pipe
local function speak(text)
    if not text or text == "" then return end

    openPipe()

    if pipe then
        local ok = pcall(function()
            assert(pipe:write(text .. "\n"))
            assert(pipe:flush())
        end)

        if not ok then
            closePipe()
            nextPipeAttempt = os.time() + 5
        end
    end
end

-- Reload TTS voice
local function reloadTTSVoice()
    openPipe()

    if pipe then
        local ok = pcall(function()
            assert(pipe:write("VOICE_RELOAD\n"))
            assert(pipe:flush())
        end)

        if not ok then
            closePipe()
            nextPipeAttempt = os.time() + 5
        end
    end
end

-- Close pipe on shutdown or connection failure
closePipe = function()
    if pipe then
        pcall(function() pipe:close() end)
        pipe = nil
        activePipeName = nil
    end
end



-- Voice refresh is requested only when audio playback is enabled.

--#endregion Not Exclusion Variable

-- Clear cache and load config
package.loaded["lootfilter_config"] = nil
local config = loadConfig("lootfilter_config.lua")
assert(type(config) == "table", "lootfilter_config.lua must return a table")
local version = "1.5.2"
local mod = "RMD"
for _, key in ipairs({"allowOverrides", "audioPlayback", "hide_mismatches", "strict"}) do
    assert(config[key] == nil or type(config[key]) == "boolean", key .. " must be boolean")
end
assert(config.debug == nil or type(config.debug) == "boolean" or type(config.debug) == "table", "debug must be boolean or a table")
assert(config.Debug == nil or type(config.Debug) == "boolean" or type(config.Debug) == "table", "Debug must be boolean or a table")
assert(config.language == nil or type(config.language) == "string", "language must be text")
assert(config.filter_level == nil or tonumber(config.filter_level), "filter_level must be numeric")
local userLanguage = config.language or "enUS"
if config.audioPlayback == true then reloadTTSVoice(); closePipe() end


--#region Mapping Tables

-- Mapping tables for user-convenience
local qualityMap = { [1] = "Inferior", [2] = "Normal", [3] = "Superior", [4] = "Magic", [5] = "Set", [6] = "Rare", [7] = "Unique", [8] = "Crafted", [9] = "Tempered" }
local rarityMap = { [0] = "Normal", [1] = "Exceptional", [2] = "Elite" }
local difficultyMap = { [0] = "Normal", [1] = "Nightmare", [2] = "Hell" }
local locationMap = { onplayer = 0, equipped = 1 , onground = 3, dropping = 5, atvendor = "atvendor" }
local colorMap = { white = "ÿc0", red = "ÿc1", green = "ÿc2", blue = "ÿc3", gold = "ÿc4", grey = "ÿc5", gray = "ÿc5", black = "ÿc6", tan = "ÿc7", orange = "ÿc8", yellow = "ÿc9", purple = "ÿc;", ["dark green"] = "ÿcA", turquoise = "ÿcN", pink = "ÿcO", lilac = "ÿcP" }
local colorMapLink = { white = "0", red = "1", green = "2", blue = "3", gold = "4", grey = "5", gray = "5", black = "6", tan = "7", orange = "8", yellow = "9", purple = ";", ["dark green"] = "A", turquoise = "N", pink = "O" }
local ColorsOfTheRainbow = { "ÿc1", "ÿc2", "ÿc3", "ÿc4", "ÿc5", "ÿc7", "ÿc8", "ÿc9", "ÿc;", "ÿcA", "ÿcN", "ÿcO" }
local FlameColors = { "ÿc1", "ÿc8", "ÿc5", "ÿcU" }
local CandyColors = { "ÿcN", "ÿc3", "ÿc;", "ÿcO" }
local OceanColors = { "ÿcN", "ÿc3", "ÿcT", "ÿcP" }
local ChristmasColors = { "ÿc1", "ÿc2", "ÿcA", "ÿc7" }
local ToxicColors = { "ÿc2", "ÿcA", "ÿc0" }

RMDAreaIDs = {
    -- Act 1
    ["Rogue Encampment"] = 1, ["Blood Moor"] = 2, ["Cold Plains"] = 3, ["Stony Field"] = 4, ["Dark Wood"] = 5, ["Black Marsh"] = 6, ["Tamoe Highland"] = 7, ["Den Of Evil"] = 8, ["Cave Level 1"] = 9, ["Underground Passage Level 1"] = 10,
    ["Hole Level 1"] = 11, ["Pit Level 1"] = 12, ["Cave Level 2"] = 13, ["Underground Passage Level 2"] = 14, ["Hole Level 2"] = 15, ["Pit Level 2"] = 16, ["Burial Grounds"] = 17, ["Crypt"] = 18, ["Mausoleum"] = 19, ["Forgotten Tower"] = 20,
    ["Tower Cellar Level 1"] = 21, ["Tower Cellar Level 5"] = 25, ["Monastery Gate"] = 26, ["Outer Cloister"] = 27, ["Barracks"] = 28, ["Jail Level 3"] = 31, ["Inner Cloister"] = 32, ["Cathedral"] = 33, ["Catacombs Level 2"] = 35,
    ["Catacombs Level 4"] = 37, ["Tristram"] = 38, ["Moo Moo Farm"] = 39,
    -- Act 2
    ["Lut Gholein"] = 40, ["Rocky Waste"] = 41, ["Dry Hills"] = 42, ["Far Oasis"] = 43, ["Lost City"] = 44, ["Valley Of Snakes"] = 45, ["Canyon Of The Magi"] = 46, ["A2 Sewers Level 1"] = 47, ["A2 Sewers Level 2"] = 48, ["A2 Sewers Level 3"] = 49,
    ["Palace Cellar Level 3"] = 54, ["Stony Tomb Level 1"] = 55, ["Halls Of The Dead Level 1"] = 56, ["Halls Of The Dead Level 2"] = 57, ["Claw Viper Temple Level 1"] = 58, ["Stony Tomb Level 2"] = 59, ["Halls Of The Dead Level 3"] = 60,
    ["Claw Viper Temple Level 2"] = 61, ["Maggot Lair Level 3"] = 64, ["Ancient Tunnels"] = 65, ["Tal Rashas Tomb level 1"] = 66, ["Tal Rashas Tomb level 2"] = 67, ["Tal Rashas Tomb level 3"] = 68, ["Tal Rashas Tomb level 4"] = 69,
    ["Tal Rashas Tomb level 5"] = 70, ["Tal Rashas Tomb level 6"] = 71, ["Tal Rashas Tomb level 7"] = 72, ["Duriels Lair"] = 73, ["Arcane Sanctuary"] = 74,
    -- Act 3
    ["Kurast Docktown"] = 75, ["Spider Forest"] = 76, ["Great Marsh"] = 77, ["Flayer Jungle"] = 78, ["Lower Kurast"] = 79, ["Kurast Bazaar"] = 80, ["Upper Kurast"] = 81, ["Kurast Causeway"] = 82, ["Travincal"] = 83, ["Spider Cave"] = 84,
    ["Spider Cavern"] = 85, ["Swampy Pit Level 1"] = 86, ["Swampy Pit Level 2"] = 87, ["Flayer Dungeon Level 1"] = 88, ["Flayer Dungeon Level 2"] = 89, ["Swampy Pit Level 3"] = 90, ["Flayer Dungeon Level 3"] = 91, ["A3 Sewers Level 1"] = 92,
    ["A3 Sewers Level 2"] = 93, ["Ruined Temple"] = 94, ["Disused Fane"] = 95, ["Forgotten Reliquary"] = 96, ["Forgotten Temple"] = 97, ["Ruined Fane"] = 98, ["Disused Reliquary"] = 99, ["Durance Of Hate Level 1"] = 100, ["Durance Of Hate Level 2"] = 101, ["Durance Of Hate Level 3"] = 102,
    -- Act 4
    ["The Pandemonium Fortress"] = 103, ["Outer Steppes"] = 104, ["Plains Of Despair"] = 105, ["City Of The Damned"] = 106, ["River Of Flame"] = 107, ["Chaos Sanctuary"] = 108,
    -- Act 5
    ["Harrogath"] = 109, ["Bloody Foothills"] = 110, ["Frigid Highlands"] = 111, ["Arreat Plateau"] = 112, ["Crystalized Passage"] = 113, ["Frozen River"] = 114, ["Glacial Trail"] = 115, ["Drifter Cavern"] = 116, ["Frozen Tundra"] = 117,
    ["Ancients Way"] = 118, ["Icy Cellar"] = 119, ["Arreat Summit"] = 120, ["Nihlathaks Temple"] = 121, ["Halls Of Anguish"] = 122, ["Halls Of Pain"] = 123, ["Halls Of Vaught"] = 124, ["Abaddon"] = 125, ["Pit Of Acheron"] = 126, ["Infernal Pit"] = 127,
    ["The Worldstone Keep Level 1"] = 128, ["The Worldstone Keep Level 2"] = 129, ["The Worldstone Keep Level 3"] = 130, ["Throne Of Destruction"] = 131, ["The Worldstone Chamber"] = 132,
    -- Act X
    ["Matrons Den"] = 133, ["Fogotten Sands"] = 134, ["Furnace of Pain"] = 135, ["Tristram2"] = 136
}

AreaIDs = {
    -- Act 1
    ["Rogue Encampment"] = 1, ["Blood Moor"] = 2, ["Cold Plains"] = 3, ["Stony Field"] = 4, ["Dark Wood"] = 5, ["Black Marsh"] = 6, ["Tamoe Highland"] = 7, ["Den Of Evil"] = 8, ["Cave Level 1"] = 9, ["Underground Passage Level 1"] = 10,
    ["Hole Level 1"] = 11, ["Pit Level 1"] = 12, ["Cave Level 2"] = 13, ["Underground Passage Level 2"] = 14, ["Hole Level 2"] = 15, ["Pit Level 2"] = 16, ["Burial Grounds"] = 17, ["Crypt"] = 18, ["Mausoleum"] = 19, ["Forgotten Tower"] = 20,
    ["Tower Cellar Level 1"] = 21, ["Tower Cellar Level 2"] = 22, ["Tower Cellar Level 3"] = 23, ["Tower Cellar Level 4"] = 24, ["Tower Cellar Level 5"] = 25, ["Monastery Gate"] = 26, ["Outer Cloister"] = 27, ["Barracks"] = 28, ["Jail Level 1"] = 29,
    ["Jail Level 2"] = 30, ["Jail Level 3"] = 31, ["Inner Cloister"] = 32, ["Cathedral"] = 33, ["Catacombs Level 1"] = 34, ["Catacombs Level 2"] = 35, ["Catacombs Level 3"] = 36, ["Catacombs Level 4"] = 37, ["Tristram"] = 38, ["Moo Moo Farm"] = 39,
    -- Act 2
    ["Lut Gholein"] = 40, ["Rocky Waste"] = 41, ["Dry Hills"] = 42, ["Far Oasis"] = 43, ["Lost City"] = 44, ["Valley Of Snakes"] = 45, ["Canyon Of The Magi"] = 46, ["A2 Sewers Level 1"] = 47, ["A2 Sewers Level 2"] = 48, ["A2 Sewers Level 3"] = 49,
    ["Harem Level 1"] = 50, ["Harem Level 2"] = 51, ["Palace Cellar Level 1"] = 52, ["Palace Cellar Level 2"] = 53, ["Palace Cellar Level 3"] = 54, ["Stony Tomb Level 1"] = 55, ["Halls Of The Dead Level 1"] = 56, ["Halls Of The Dead Level 2"] = 57,
    ["Claw Viper Temple Level 1"] = 58, ["Stony Tomb Level 2"] = 59, ["Halls Of The Dead Level 3"] = 60, ["Claw Viper Temple Level 2"] = 61, ["Maggot Lair Level 1"] = 62, ["Maggot Lair Level 2"] = 63, ["Maggot Lair Level 3"] = 64, ["Ancient Tunnels"] = 65,
    ["Tal Rashas Tomb level 1"] = 66, ["Tal Rashas Tomb level 2"] = 67, ["Tal Rashas Tomb level 3"] = 68, ["Tal Rashas Tomb level 4"] = 69, ["Tal Rashas Tomb level 5"] = 70, ["Tal Rashas Tomb level 6"] = 71, ["Tal Rashas Tomb level 7"] = 72, ["Duriels Lair"] = 73,
    -- Act 3
    ["Arcane Sanctuary"] = 74, ["Kurast Docktown"] = 75, ["Spider Forest"] = 76, ["Great Marsh"] = 77, ["Flayer Jungle"] = 78, ["Lower Kurast"] = 79, ["Kurast Bazaar"] = 80, ["Upper Kurast"] = 81, ["Kurast Causeway"] = 82, ["Travincal"] = 83,
    ["Spider Cave"] = 84, ["Spider Cavern"] = 85, ["Swampy Pit Level 1"] = 86, ["Swampy Pit Level 2"] = 87, ["Flayer Dungeon Level 1"] = 88, ["Flayer Dungeon Level 2"] = 89, ["Swampy Pit Level 3"] = 90, ["Flayer Dungeon Level 3"] = 91,
    ["A3 Sewers Level 1"] = 92, ["A3 Sewers Level 2"] = 93, ["Ruined Temple"] = 94, ["Disused Fane"] = 95, ["Forgotten Reliquary"] = 96, ["Forgotten Temple"] = 97, ["Ruined Fane"] = 98, ["Disused Reliquary"] = 99, ["Durance Of Hate Level 1"] = 100,
    ["Durance Of Hate Level 2"] = 101, ["Durance Of Hate Level 3"] = 102,
    -- Act 4
    ["The Pandemonium Fortress"] = 103, ["Outer Steppes"] = 104, ["Plains Of Despair"] = 105, ["City Of The Damned"] = 106, ["River Of Flame"] = 107, ["Chaos Sanctuary"] = 108,
    -- Act 5
    ["Harrogath"] = 109, ["Bloody Foothills"] = 110, ["Frigid Highlands"] = 111, ["Arreat Plateau"] = 112, ["Crystalized Passage"] = 113, ["Frozen River"] = 114, ["Glacial Trail"] = 115, ["Drifter Cavern"] = 116, ["Frozen Tundra"] = 117, ["Ancients Way"] = 118,
    ["Icy Cellar"] = 119, ["Arreat Summit"] = 120, ["Nihlathaks Temple"] = 121, ["Halls Of Anguish"] = 122, ["Halls Of Pain"] = 123, ["Halls Of Vaught"] = 124, ["Abaddon"] = 125, ["Pit Of Acheron"] = 126, ["Infernal Pit"] = 127,
    ["The Worldstone Keep Level 1"] = 128, ["The Worldstone Keep Level 2"] = 129, ["The Worldstone Keep Level 3"] = 130, ["Throne Of Destruction"] = 131, ["The Worldstone Chamber"] = 132,
    -- Act X
    ["Matrons Den"] = 133, ["Fogotten Sands"] = 134, ["Furnace of Pain"] = 135, ["Tristram2"] = 136
}

-- The list of item types
local itemTypes = {
    "Shield", "Armor", "Gold", "Bow Quiver", "Crossbow Quiver", "Player Body Part", "Herb", "Potion", "Ring", "Elixir", "Amulet", "Charm", "Not Used", "Boots", "Gloves", "Not Used", "Book", "Belt", "Gem", "Torch", "Scroll", "Not Used", 
    "Scepter", "Wand", "Staff", "Bow", "Axe", "Club", "Sword", "Hammer", "Knife", "Spear", "Polearm", "Crossbow", "Mace", "Helm", "Missile Potion", "Quest", "Body Part", "Key", "Throwing Knife", "Throwing Axe", "Javelin", "Weapon", 
    "Melee Weapon", "Missile Weapon", "Thrown Weapon", "Combo Weapon", "Any Armor", "Any Shield", "Miscellaneous", "Socket Filler", "Second Hand", "Staves And Rods", "Missile", "Blunt", "Jewel", "Class Specific", "Amazon Item", "Barbarian Item", 
    "Necromancer Item", "Paladin Item", "Sorceress Item", "Assassin Item", "Druid Item", "Hand to Hand", "Orb", "Voodoo Heads", "Auric Shields", "Primal Helm", "Pelt", "Cloak", "Rune", "Circlet", "Healing Potion", "Mana Potion", "Rejuv Potion", 
    "Stamina Potion", "Antidote Potion", "Thawing Potion", "Small Charm", "Medium Charm", "Large Charm", "Amazon Bow", "Amazon Spear", "Amazon Javelin", "Hand to Hand 2", "Magic Bow Quiv", "Magic Xbow Quiv", "Chipped Gem", "Flawed Gem", "Standard Gem", 
    "Flawless Gem", "Perfect Gem", "Amethyst", "Diamond", "Emerald", "Ruby", "Sapphire", "Topaz", "Skull", "Swords and Knives", "Spears and Polearms"
}

-- Create the mapping starting from integer 2
local itemTypeMap = {}
for i, name in ipairs(itemTypes) do
    itemTypeMap[i + 1] = name  -- start from 2
end

--#endregion Mapping Tables

--#region Style Controls
local function HSVtoRGB(h, s, v)
    local r, g, b

    local i = math.floor(h * 6)
    local f = h * 6 - i
    local p = v * (1 - s)
    local q = v * (1 - f * s)
    local t = v * (1 - (1 - f) * s)

    i = i % 6

    if i == 0 then
        r, g, b = v, t, p
    elseif i == 1 then
        r, g, b = q, v, p
    elseif i == 2 then
        r, g, b = p, v, t
    elseif i == 3 then
        r, g, b = p, q, v
    elseif i == 4 then
        r, g, b = t, p, v
    elseif i == 5 then
        r, g, b = v, p, q
    end

    return r, g, b
end

-- Preset Background Styles
local backgroundStyles = {
    Rainbow = function(Item, Result, Tick)
        local speed = 2000.0
        local id = ((Item.ID * 2654435761) % 2 ^ 32)
        local offset = (id % speed) / speed
        local t = ((Tick % speed) / speed + offset) % 1.0
        local hue = t
        local r, g, b = HSVtoRGB(hue, 1.0, 1.0)
        local pulse = (math.sin(t * 2 * math.pi) + 1) / 2
        local a = 0.6 + 0.3 * pulse
        local width = 2 + 4 * pulse

        Result.Border[1], Result.Border[2], Result.Border[3], Result.Border[4], Result.Border[5] = r, g, b, a, width
        Result.Background[1], Result.Background[2], Result.Background[3], Result.Background[4] = r, g, b, a * 0.4
    end,
    OceanDrift = function(Item, Result, Tick)
        local t = (Tick % 2000) / 2000
        local pulse = (math.sin(t * 2 * math.pi) + 1) / 2
        local r = 0.0
        local g = 0.1 + 0.15 * pulse
        local b = 0.4 + 0.4 * pulse
        local a = 0.6 + 0.3 * pulse
        local width = 2 + 4 * pulse

        Result.Border[1], Result.Border[2], Result.Border[3], Result.Border[4], Result.Border[5] = r, g, b, a, width
        Result.Background[1], Result.Background[2], Result.Background[3], Result.Background[4] = r, g, b, a * 0.4
    end,
    FadingEmber = function(Item, Result, Tick)
        local t = (Tick % 2000) / 2000
        local pulse = (math.sin(t * 2 * math.pi) + 1) / 2
        local r = 0.4 + 0.2 * pulse
        local g = 0.05 + 0.1 * pulse
        local b = 0.0
        local a = 0.6 + 0.3 * pulse
        local width = 2 + 4 * pulse

        Result.Border[1], Result.Border[2], Result.Border[3], Result.Border[4], Result.Border[5] = r, g, b, a, width
        Result.Background[1], Result.Background[2], Result.Background[3], Result.Background[4] = r, g, b, a * 0.4
    end,
    MidnightMemory = function(Item, Result, Tick)
        local t = (Tick % 2000) / 2000
        local pulse = (math.sin(t * 2 * math.pi) + 1) / 2
        local r = 0.2 + 0.2 * pulse
        local g = 0.1 + 0.1 * (1 - pulse)
        local b = 0.4 + 0.3 * pulse
        local a = 0.6 + 0.3 * pulse
        local width = 2 + 4 * pulse

        Result.Border[1], Result.Border[2], Result.Border[3], Result.Border[4], Result.Border[5] = r, g, b, a, width
        Result.Background[1], Result.Background[2], Result.Background[3], Result.Background[4] = r, g, b, a * 0.4
    end,
    VerdantBloom = function(Item, Result, Tick)
        local t = (Tick % 2000) / 2000
        local pulse = (math.sin(t * 2 * math.pi) + 1) / 2
        local r = 0.05 + 0.1 * pulse
        local g = 0.2 + 0.3 * pulse
        local b = 0.05 + 0.1 * (1 - pulse)
        local a = 0.6 + 0.3 * pulse
        local width = 2 + 4 * pulse

        Result.Border[1], Result.Border[2], Result.Border[3], Result.Border[4], Result.Border[5] = r, g, b, a, width
        Result.Background[1], Result.Background[2], Result.Background[3], Result.Background[4] = r, g, b, a * 0.4
    end,
    SapphireSonnet = function(Item, Result, Tick)
        local t = (Tick % 2500) / 2500
        local pulse = (math.sin(t * 2 * math.pi) + 1) / 1
        local r = 0.2
        local g = 0.1 + 0.1 * pulse
        local b = 0.4 + 0.2 * pulse
        local a = 1
        local width = 1 + 2 * pulse

        Result.Border[1], Result.Border[2], Result.Border[3], Result.Border[4], Result.Border[5] = r, g, b, a * 0.8, width
        Result.Background[1], Result.Background[2], Result.Background[3], Result.Background[4] = r, g, b, a * 0.1
    end,
    TamarilloGlow = function(Item, Result, Tick)
        local t = (Tick % 2300) / 2300
        local pulse = (math.sin(t * 2 * math.pi) + 1) / 1
        local r = 0.4 + 0.2 * pulse
        local g = 0.1
        local b = 0.1
        local a = 1
        local width = 1

        Result.Border[1], Result.Border[2], Result.Border[3], Result.Border[4], Result.Border[5] = 0, 0, 0, 1, width
        Result.Background[1], Result.Background[2], Result.Background[3], Result.Background[4] = r, g, b, a * 0.7
    end
}

-- Helper to remove color codes and split into itemType and itemName
local function SplitItemName(Name)
    local cleanName = Name:gsub("ÿc.", "")
    local newlineIndex = cleanName:find("\n", 1, true)
    if newlineIndex then
        return cleanName:sub(1, newlineIndex), cleanName:sub(newlineIndex + 1)
    else
        return "", cleanName
    end
end

-- Preset Name Styles
local function utf8_chars(str)
    local chars = {}
    local i = 1
    local len = #str
    while i <= len do
        local byte = string.byte(str, i)
        local char_len = 1
        if byte >= 0xF0 then
            char_len = 4
        elseif byte >= 0xE0 then
            char_len = 3
        elseif byte >= 0xC0 then
            char_len = 2
        end
        table.insert(chars, string.sub(str, i, i + char_len - 1))
        i = i + char_len
    end
    return chars
end

local nameStyles = {
    Rainbow = function(Item, Name, Tick)
        local itemType, itemName = SplitItemName(Name)
        local result = { itemType }
        local slowFactor = 300
        local colorCount = #ColorsOfTheRainbow
        local offset = ((Item.ID * 2654435761) % 2^32) % colorCount

        local chars = utf8_chars(itemName)

        for i = 1, #chars do
            local index = (math.floor(Tick / slowFactor) + i - 1 + offset) % colorCount + 1
            table.insert(result, ColorsOfTheRainbow[index] .. itemName:sub(i, i))
        end

        return table.concat(result)
    end,

    RainbowGroup = function(Item, Name, Tick)
        local itemType, itemName = SplitItemName(Name)
        local result = { itemType }
        local slowFactor = 300
        local colorCount = #ColorsOfTheRainbow
        local offset = ((Item.ID * 2654435761) % 2^32) % colorCount
        local index = (math.floor(Tick / slowFactor) + offset) % colorCount + 1
        local color = ColorsOfTheRainbow[index]

        local chars = utf8_chars(itemName)

        for i = 1, #chars do
            table.insert(result, color .. itemName:sub(i, i))
        end

        return table.concat(result)
    end,

    CottonCandy = function(Item, Name, Tick)
        local itemType, itemName = SplitItemName(Name)
        local result = { itemType }
        local speed = 500

        local chars = utf8_chars(itemName)

        for i = 1, #chars do
            local index = (math.floor((Tick / speed) + i) % #CandyColors) + 1
            table.insert(result, CandyColors[index] .. itemName:sub(i, i))
        end

        return table.concat(result)
    end,

    FrostWave = function(Item, Name, Tick)
        local itemType, itemName = SplitItemName(Name)
        local result = { itemType }
        local speed = 500

        local chars = utf8_chars(itemName)

        for i = 1, #chars do
            local index = (math.floor((Tick / speed) + i) % #OceanColors) + 1
            table.insert(result, OceanColors[index] .. itemName:sub(i, i))
        end

        return table.concat(result)
    end,

    ToxicFog = function(Item, Name, Tick)
        local itemType, itemName = SplitItemName(Name)
        local result = { itemType }
        local speed = 500

        local chars = utf8_chars(itemName)

        for i = 1, #chars do
            local index = (math.floor((Tick / speed) + i) % #ToxicColors) + 1
            table.insert(result, ToxicColors[index] .. itemName:sub(i, i))
        end

        return table.concat(result)
    end,

    OpenFlames = function(Item, Name, Tick)
        local itemType, itemName = SplitItemName(Name)
        local result = { itemType }
        local speed = 500

        local chars = utf8_chars(itemName)

        for i = 1, #chars do
            local index = (math.floor((Tick / speed) + i) % #FlameColors) + 1
            table.insert(result, FlameColors[index] .. chars[i])
        end

        return table.concat(result)
    end,

    Christmas = function(Item, Name, Tick)
        local itemType, itemName = SplitItemName(Name)
        local result = { itemType }
        local speed = 500

        local chars = utf8_chars(itemName)

        for i = 1, #chars do
            local index = (math.floor((Tick / speed) + i) % #ChristmasColors) + 1
            table.insert(result, ChristmasColors[index] .. itemName:sub(i, i))
        end

        return table.concat(result)
    end,

    RainbowStatic = function(Item, Name)
        local ColorsOfTheRainbowSize = #ColorsOfTheRainbow
        local cleanName = Name:gsub("ÿc.", "")
        local result = {}
        local offset = ((Item.ID * 2654435761) % 2 ^ 32) % ColorsOfTheRainbowSize

        for i = 1, #cleanName do
            local char = cleanName:sub(i, i)
            local colorIndex = (i - 1 + offset) % ColorsOfTheRainbowSize + 1
            local color = ColorsOfTheRainbow[colorIndex]
            table.insert(result, color .. char)
        end

        return table.concat(result)
    end,

    RainbowGroupStatic = function(Item, Name)
        local cleanName = Name:gsub("ÿc.", "")
        local colorIndex = ((Item.ID * 2654435761) % 2 ^ 32) % #ColorsOfTheRainbow + 1
        local color = ColorsOfTheRainbow[colorIndex]
        return color .. cleanName
    end,

    CottonCandyStatic = function(Item, Name)
        local cleanName = Name:gsub("ÿc.", "")
        local result = {}
        for i = 1, #cleanName do
            local colorIndex = (i % #CandyColors) + 1
            table.insert(result, CandyColors[colorIndex] .. cleanName:sub(i, i))
        end
        return table.concat(result)
    end,

    FrostWaveStatic = function(Item, Name)
        local cleanName = Name:gsub("ÿc.", "")
        local result = {}
        for i = 1, #cleanName do
            local colorIndex = (i % #OceanColors) + 1
            table.insert(result, OceanColors[colorIndex] .. cleanName:sub(i, i))
        end
        return table.concat(result)
    end,

    ToxicFogStatic = function(Item, Name)
        local cleanName = Name:gsub("ÿc.", "")
        local result = {}
        for i = 1, #cleanName do
            local colorIndex = (i % #ToxicColors) + 1
            table.insert(result, ToxicColors[colorIndex] .. cleanName:sub(i, i))
        end
        return table.concat(result)
    end,

    OpenFlamesStatic = function(Item, Name)
        local cleanName = Name:gsub("ÿc.", "")
        local result = {}
        for i = 1, #cleanName do
            local colorIndex = (i % #FlameColors) + 1
            table.insert(result, FlameColors[colorIndex] .. cleanName:sub(i, i))
        end
        return table.concat(result)
    end,
}
--#endregion Style Controls

-- Rule compilation happens once per reload. Item evaluation only runs predicates.
local diagnostics = { version = version, rules = 0, disabled = 0, warnings = {}, errors = {}, evaluated = 0, tested = 0 }
local function report(kind, message)
    local list = diagnostics[kind]
    list[#list + 1] = message
    if #list <= 20 then print("[LootFilter " .. kind .. "] " .. message) end
end

local aliases = {
    code = "codes", qualities = "quality", rarities = "rarity", indices = "index", indexes = "index",
    item_level = "ilvl", item_levels = "ilvl", ilvls = "ilvl", socket = "sockets",
    maxsockets = "maxsock", max_sockets = "maxsock", itypes = "itype", item_type = "itype",
    item_types = "itype", itemtypes = "itype", difficulties = "difficulty", areas = "area",
    locations = "location", stats = "stat", pstats = "pstat", filter_level = "filter_levels", levels = "filter_levels",
}
local conditionNames = { "location", "quality", "ilvl", "sockets", "maxsock", "identified", "grail",
    "rarity", "index", "itype", "difficulty", "area", "ethereal", "runeword", "stat", "pstat" }
local actionNames = { "hide", "hide_mismatches", "notify", "audio", "print", "background", "border",
    "name_override", "prefix", "suffix", "prefix_desc", "suffix_desc", "background_style", "name_style" }
local knownFields = { codes = true, filter_levels = true, style = true, enabled = true, label = true }
for _, key in ipairs(conditionNames) do knownFields[key] = true end
for _, key in ipairs(actionNames) do knownFields[key] = true end

local function trim(value) return value:match("^%s*(.-)%s*$") end
local function canonicalize(raw)
    assert(type(raw) == "table", "rule must be a table")
    local rule = {}
    for key, value in pairs(raw) do
        local canonical = aliases[key] or key
        assert(knownFields[canonical], "unknown field '" .. tostring(key) .. "'")
        assert(rule[canonical] == nil, "conflicting aliases for '" .. canonical .. "'")
        rule[canonical] = value
    end
    return rule
end

local function array(value)
    if type(value) == "table" then return value end
    return { value }
end

-- Lists are alternatives; NOT inverts the complete group. Never coerce invalid input to zero.
local function compileChoices(value, compileOne, allowCSV)
    local mt = type(value) == "table" and getmetatable(value)
    local predicates = {}
    local values = array(value)
    assert(#values > 0, "empty condition list")
    for key in pairs(values) do
        assert(type(key) == "number" and key >= 1 and key <= #values and key % 1 == 0, "condition must be a dense list")
    end
    for _, entry in ipairs(values) do
        if allowCSV and type(entry) == "string" and entry:find(",", 1, true) then
            assert(not entry:match("^%s*,") and not entry:match(",%s*$") and not entry:match(",%s*,"), "empty list entry")
            for part in entry:gmatch("[^,]+") do predicates[#predicates + 1] = compileOne(trim(part)) end
        else
            predicates[#predicates + 1] = compileOne(entry)
        end
    end
    return function(actual)
        local matched = false
        for _, predicate in ipairs(predicates) do if predicate(actual) then matched = true; break end end
        if mt and mt.__not then return not matched end
        return matched
    end
end

local operators = {
    ["=="] = function(a, b) return a == b end, ["!="] = function(a, b) return a ~= b end,
    ["~="] = function(a, b) return a ~= b end, [">"] = function(a, b) return a > b end,
    ["<"] = function(a, b) return a < b end, [">="] = function(a, b) return a >= b end,
    ["<="] = function(a, b) return a <= b end,
}
local function resolveNumber(value, names)
    local numeric = tonumber(value)
    if numeric then return numeric end
    if type(value) == "string" and names then
        for id, name in pairs(names) do if trim(value):lower() == name:lower() then return id end end
    end
    error("unknown numeric/name value '" .. tostring(value) .. "'")
end
local function compileNumber(value, names)
    return compileChoices(value, function(entry)
        assert(type(entry) == "number" or type(entry) == "string", "expected number or comparison string")
        local op, operand = "==", entry
        if type(entry) == "string" then
            entry = trim(entry)
            local prefix, rest = entry:match("^([<>~=!]+)%s*(.-)$")
            if prefix then op, operand = prefix, rest
            elseif entry:sub(-1) == "+" then op, operand = ">=", trim(entry:sub(1, -2))
            elseif entry:sub(-1) == "-" then op, operand = "<=", trim(entry:sub(1, -2))
            else operand = entry end
        end
        local compare = assert(operators[op], "unknown comparison '" .. op .. "'")
        local expected = resolveNumber(operand, names)
        return function(actual) return actual ~= nil and compare(actual, expected) end
    end, true)
end

local function compileCodes(value)
    assert(value ~= nil, "missing codes (use codes = \"allitems\" to match everything)")
    local mt = type(value) == "table" and getmetatable(value)
    local codes, all = {}, false
    for _, code in ipairs(array(value)) do
        assert(type(code) == "string" and code ~= "", "codes must contain strings")
        if code:lower() == "allitems" then all = true else codes[code] = true end
    end
    assert(all or next(codes), "empty codes list")
    return function(code)
        local hit = (all and not (HideAllItems and HideAllItems[code])) or codes[code] == true
        if mt and mt.__not then return not hit end
        return hit
    end
end

local function normalizeLocation(value)
    if value == 3 or value == 5 then return "onground" end
    return value
end
local function compileLocations(value)
    return compileChoices(value or "onground", function(entry)
        local id = type(entry) == "string" and locationMap[trim(entry):lower()] or entry
        assert(id ~= nil and (id == "atvendor" or type(id) == "number"), "unknown location '" .. tostring(entry) .. "'")
        id = normalizeLocation(id)
        return function(actual) return actual == id end
    end, true)
end

-- Common town spelling used by published configs, plus numeric/custom area IDs.
AreaIDs["Kurast Docks"], RMDAreaIDs["Kurast Docks"] = 75, 75
AreaIDs["Forgotten Sands"], RMDAreaIDs["Forgotten Sands"] = 134, 134
local areaLookup = {}
for name, id in pairs(AreaIDs) do areaLookup[name:lower()] = id end
if mod == "RMD" then for name, id in pairs(RMDAreaIDs) do areaLookup[name:lower()] = id end end
local function compileAreas(value)
    return compileChoices(value, function(entry)
        local id = tonumber(entry) or (type(entry) == "string" and areaLookup[trim(entry):lower()])
        assert(id, "unknown area '" .. tostring(entry) .. "'")
        return function(actual) return actual == id end
    end, false)
end

local typeLookup = {}
for id, name in pairs(itemTypeMap) do typeLookup[name:lower()] = id end
local function compileTypes(value)
    return compileChoices(value, function(entry)
        local id = tonumber(entry) or (type(entry) == "string" and typeLookup[trim(entry):lower()])
        assert(id and id >= 0 and id % 1 == 0, "unknown item type '" .. tostring(entry) .. "'")
        return function(ctx) return ctx.isType(id) end
    end, true)
end

local function integer(value, name)
    local n = tonumber(value)
    assert(n and n >= 0 and n % 1 == 0, name .. " must be a nonnegative integer")
    return n
end
local function compileStats(value)
    assert(type(value) == "table", "stat/pstat must be a table")
    local mt = getmetatable(value)
    local any = mt and mt.__or
    local blocks, tests = value.index ~= nil and { value } or value, {}
    -- Published generated filters use stat = {} for items without tracked stats.
    -- Preserve the AND identity (true), and OR identity (false).
    if #blocks == 0 then diagnostics.emptyStats = (diagnostics.emptyStats or 0) + 1 end
    for _, block in ipairs(blocks) do
        assert(type(block) == "table" and block.index ~= nil, "stat requires index")
        for key in pairs(block) do assert(key == "index" or key == "param" or key == "op" or key == "value", "unknown stat field '" .. tostring(key) .. "'") end
        local compare, expected = operators[block.op or "=="], block.value
        if block.op == "between" then
            assert(type(expected) == "table" and #expected == 2, "between requires {minimum, maximum}")
            local lo, hi = tonumber(expected[1]), tonumber(expected[2])
            assert(lo and hi and lo <= hi, "invalid between bounds")
            compare = function(actual) return actual >= lo and actual <= hi end
        else
            assert(compare and tonumber(expected), "invalid stat operator/value")
            expected = tonumber(expected)
        end
        local indices = array(block.index)
        local params = block.param ~= nil and array(block.param) or { false }
        assert(#indices > 0 and #params > 0, "empty stat indices/params")
        for _, index in ipairs(indices) do
            index = integer(index, "stat index")
            for _, param in ipairs(params) do
                if param ~= false then param = integer(param, "stat param") end
                local i, p, cmp, wanted = index, param, compare, expected
                tests[#tests + 1] = function(getStat) return cmp(getStat(i, p ~= false and p or nil), wanted) end
            end
        end
    end
    return function(getStat)
        local hit = not any
        for _, test in ipairs(tests) do
            local passed = test(getStat)
            if any and passed then hit = true; break end
            if not any and not passed then hit = false; break end
        end
        if mt and mt.__not then return not hit end
        return hit
    end
end

local function styleKey(value) return value:gsub("%s+", ""):lower() end
local namePresets, backgroundPresets = {}, {}
for key, callback in pairs(nameStyles) do namePresets[styleKey(key)] = callback end
for key, callback in pairs(backgroundStyles) do backgroundPresets[styleKey(key)] = callback end
local textFields = { "name_override", "prefix", "suffix", "prefix_desc", "suffix_desc", "notify", "audio" }
local function localized(value)
    if type(value) == "table" then return value[userLanguage] or value.enUS end
    return value
end

local function compileRule(raw, source, number)
    local rule = canonicalize(raw)
    assert(rule.label == nil or type(rule.label) == "string", "label must be text")
    if rule.enabled == false then return nil end
    assert(rule.enabled == nil or type(rule.enabled) == "boolean", "enabled must be boolean")
    if rule.style ~= nil then
        assert(type(config.styles) == "table" and type(config.styles[rule.style]) == "table", "unknown style '" .. tostring(rule.style) .. "'")
        local style = config.styles[rule.style]
        local actions = {}
        for _, key in ipairs(actionNames) do actions[key] = true end
        for key in pairs(style) do assert(actions[key], "style accepts actions only: '" .. tostring(key) .. "'") end
        for _, key in ipairs(actionNames) do if rule[key] == nil then rule[key] = style[key] end end
    end
    for _, key in ipairs({ "hide", "hide_mismatches", "identified", "grail", "ethereal", "runeword" }) do
        assert(rule[key] == nil or type(rule[key]) == "boolean", key .. " must be boolean")
    end
    assert(rule.print == nil or type(rule.print) == "boolean" or type(rule.print) == "string", "print must be boolean or text")
    for _, key in ipairs(textFields) do
        local value = rule[key]
        if type(value) == "table" then
            for lang, text in pairs(value) do assert(type(lang) == "string" and type(text) == "string", key .. " translations must be strings") end
        else assert(value == nil or type(value) == "string", key .. " must be text or translations") end
        rule[key] = localized(value)
    end
    for _, key in ipairs({ "background", "border" }) do
        local value = rule[key]
        if value ~= nil then
            assert(type(value) == "table" and #value >= 3 and #value <= (key == "border" and 5 or 4), key .. " requires RGB, optional alpha/thickness")
            for _, channel in ipairs(value) do assert(type(channel) == "number", key .. " channels must be numeric") end
        end
    end
    for key, presets in pairs({ name_style = namePresets, background_style = backgroundPresets }) do
        if rule[key] ~= nil then
            assert(type(rule[key]) == "string", key .. " must be text")
            rule[key] = assert(presets[styleKey(rule[key])], "unknown " .. key .. " '" .. rule[key] .. "'")
        end
    end
    local compiled = { rule = rule, source = source, number = number, tests = {}, codes = compileCodes(rule.codes), locations = compileLocations(rule.location) }
    if rule.filter_levels ~= nil then compiled.levels = compileNumber(rule.filter_levels) end
    local function add(key, predicate) compiled.tests[#compiled.tests + 1] = { key, predicate } end
    for _, key in ipairs(conditionNames) do
        local value = rule[key]
        if value ~= nil and key ~= "location" then
            if key == "stat" or key == "pstat" then
                local predicate = compileStats(value)
                add(key, function(ctx) return (key ~= "pstat" or ctx.Me ~= nil) and predicate(key == "stat" and ctx.stat or ctx.pstat) end)
            elseif key == "itype" then add(key, compileTypes(value))
            elseif key == "area" then
                local predicate = compileAreas(value); add(key, function(ctx) return predicate(ctx.area) end)
            elseif key == "maxsock" and type(value) == "boolean" then
                -- Preserve published filters: true means at least capacity; false
                -- means a different count (relevant to modded over-cap items).
                add(key, function(ctx)
                    if value then return ctx.stat(194) >= ctx.maxSockets() end
                    return ctx.stat(194) ~= ctx.maxSockets()
                end)
            elseif key == "identified" or key == "grail" or key == "ethereal" or key == "runeword" then add(key, function(ctx) return ctx[key] == value end)
            else
                local names = key == "quality" and qualityMap or key == "rarity" and rarityMap or key == "difficulty" and difficultyMap or nil
                local predicate = compileNumber(value, names)
                add(key, function(ctx)
                    local actual
                    if key == "sockets" then actual = ctx.stat(194)
                    elseif key == "maxsock" then actual = ctx.maxSockets()
                    else actual = ctx[key] end
                    return predicate(actual)
                end)
            end
        end
    end
    return compiled
end

local function compileRules(rules, source)
    assert(type(rules) == "table", source .. ": rules must be a table")
    local compiled = {}
    for key in pairs(rules) do
        assert(type(key) == "number" and key >= 1 and key <= #rules and key % 1 == 0, source .. ": rules must be a dense list")
    end
    for number, raw in ipairs(rules) do
        local ok, result = pcall(compileRule, raw, source, number)
        if not ok then
            diagnostics.disabled = diagnostics.disabled + 1
            report("errors", source .. " rule #" .. number .. ": " .. tostring(result))
        elseif result then compiled[#compiled + 1] = result; diagnostics.rules = diagnostics.rules + 1 end
    end
    return compiled
end

-- Lua files share NOT/OR and the same configuration environment.
local baseRules = compileRules(config.rules or {}, "config")
local overrideRules, overrideLevels = {}, {}
if config.allowOverrides then
    local filename = "override_rules.lua"
    local file = io.open(filename, "r")
    if not file then filename = "overrides.lua"; file = io.open(filename, "r") end
    if file then
        file:close()
        local ok, overrides = pcall(loadConfig, filename)
        if not ok or type(overrides) ~= "table" or type(overrides.rules) ~= "table" then
            report("errors", filename .. ": " .. (ok and "must return a table containing rules" or tostring(overrides)))
        elseif overrides.rules[1] ~= nil then overrideRules = compileRules(overrides.rules, filename)
        else
            for level, rules in pairs(overrides.rules) do overrideLevels[tostring(level)] = compileRules(rules, filename .. "/" .. tostring(level)) end
        end
    else report("warnings", "allowOverrides is enabled but override_rules.lua / overrides.lua was not found") end
end
if diagnostics.emptyStats then report("warnings", tostring(diagnostics.emptyStats) .. " empty stat groups retained with legacy AND/OR behavior") end
if config.strict and (#diagnostics.errors > 0) then error("Loot filter validation failed; see diagnostics above") end

local levelCache, levelCacheCount = {}, 0
local candidateCache, candidateCount = {}, 0
local function rulesForLevel(level)
    local key = tostring(level)
    if levelCache[key] then return levelCache[key] end
    local result = {}
    local function append(rules)
        for _, compiled in ipairs(rules) do if not compiled.levels or compiled.levels(tonumber(level)) then result[#result + 1] = compiled end end
    end
    append(baseRules)
    append(overrideLevels[key] or overrideLevels["level_" .. key] or overrideLevels.level_1 or overrideRules)
    if levelCacheCount >= 16 then levelCache, levelCacheCount = {}, 0; candidateCache, candidateCount = {}, 0 end
    levelCache[key], levelCacheCount = result, levelCacheCount + 1
    return result
end

local function candidatesFor(level, code)
    local key = tostring(level) .. ":" .. code
    if candidateCache[key] then return candidateCache[key] end
    local result = {}
    for _, compiled in ipairs(rulesForLevel(level)) do
        if compiled.codes(code) then result[#result + 1] = compiled end
    end
    if candidateCount >= 512 then candidateCache, candidateCount = {}, 0 end
    candidateCache[key], candidateCount = result, candidateCount + 1
    return result
end

local debug = config.debug
if debug == nil then debug = config.Debug end
if debug == true then debug = { mode = "matches" } end
if type(debug) ~= "table" then debug = nil end
if debug then
    local mode = debug.mode or "matches"
    if mode ~= "matches" and mode ~= "failures" and mode ~= "all" then
        report("warnings", "unknown debug.mode; tracing disabled")
        debug = nil
    end
end
local debugCount = 0
local function trace(kind, compiled, ctx, reason)
    if not debug or debugCount >= (tonumber(debug.limit) or 100) then return end
    if debug.code and debug.code ~= ctx.code then return end
    if debug.rule and tonumber(debug.rule) ~= compiled.number then return end
    local mode = debug.mode or "matches"
    if mode ~= "all" and mode ~= kind then return end
    debugCount = debugCount + 1
    print("[LootFilter " .. compiled.source .. " #" .. compiled.number .. "] " .. ctx.code .. " " .. kind .. (reason and (": " .. reason) or ""))
end

local function applyColorTags(text)
    return (text:gsub("{(.-)}", function(color) return colorMap[color:lower()] or ("{" .. color .. "}") end))
end
function GetWelcomeMessage()
    if type(config.reload) == "string" then return applyColorTags(config.reload) end
end
function GetFilterDiagnostics()
    -- Return a snapshot so debugging tools cannot mutate the active evaluator.
    local copy = {}
    for key, value in pairs(diagnostics) do
        if type(value) == "table" then copy[key] = {}; for i, message in ipairs(value) do copy[key][i] = message end
        else copy[key] = value end
    end
    return copy
end
function IsRuneword(flags) return math.floor((flags or 0) / 0x04000000) % 2 == 1 end
function IsIdentified(flags) return math.floor((flags or 0) / 0x10) % 2 == 1 end

local function cachedStats(unit)
    local cache = {}
    return function(index, param)
        if not unit then return 0 end
        local key = tostring(index) .. ":" .. tostring(param)
        if cache[key] == nil then cache[key] = (param ~= nil and unit:Stat(index, param) or unit:Stat(index)) or 0 end
        return cache[key]
    end
end
local function placeholders(template, ctx, rule)
    return (template:gsub("{([^{}]+)}", function(key)
        local values = {
            code = ctx.code, ilvl = ctx.ilvl, rarity = rarityMap[ctx.rarity] or ctx.rarity,
            quality = qualityMap[ctx.quality] or ctx.quality, ethereal = tostring(ctx.ethereal), index = ctx.index,
        }
        if values[key] ~= nil then return tostring(values[key]) end
        if key == "sockets" then return tostring(ctx.stat(194)) end
        if key == "maxsock" then return tostring(ctx.maxSockets()) end
        if key == "name" then return ctx.isType(4) and not ctx.isType(45) and not ctx.isType(50) and tostring(ctx.stat(14)) or ctx.Item.Name end
        if key == "link" then return ": " .. ctx.Item:Link("4") end
        if key == "itype" then
            local matches = {}
            for _, id in ipairs(array(rule.itype)) do
                id = tonumber(id) or (type(id) == "string" and typeLookup[id:lower()])
                if id and ctx.isType(id) then matches[#matches + 1] = id end
            end
            return table.concat(matches, ", ")
        end
        local stat, param = key:match("^stat=%((%d+),?(%d*)%)$")
        if stat then return tostring(ctx.stat(tonumber(stat), tonumber(param))) end
        stat, param = key:match("^pstat=%((%d+),?(%d*)%)$")
        if stat then return tostring(ctx.pstat(tonumber(stat), tonumber(param))) end
        return "{" .. key .. "}"
    end))
end
local function render(template, ctx, rule) return applyColorTags(placeholders(template, ctx, rule)) end
local function channel(value) return math.max(0, math.min(1, value / 255)) end

-- Actions accumulate in author order. A hide action terminates evaluation.
function ApplyFilter(Me, Item, Result, level)
    if not Item or not Result or not Item.Data or not Item.Txt then return end
    diagnostics.evaluated = diagnostics.evaluated + 1
    local data = Item.Data
    local location = normalizeLocation(Item.Mode)
    if Me and Me.ID ~= data.Owner and Item.Mode ~= 3 and Item.Mode ~= 4 and Item.Mode ~= 5 and (data.Owner or 0) >= 450 then location = "atvendor" end
    local identified = IsIdentified(data.Flags)
    -- Vendor items can have incomplete native data; keep their base presentation.
    if location == "atvendor" and not identified then return end
    local maxSockets, types = nil, {}
    local ctx = {
        Item = Item, Me = Me, code = Item.Txt.Code, quality = data.Quality, rarity = Item.Rarity,
        index = data.FileIndex, ilvl = data.ItemLevel, area = Item.Area, difficulty = Me and Me.Difficulty or 0,
        identified = identified, ethereal = data.IsEthereal, runeword = IsRuneword(data.Flags),
        grail = Result.Grail and Result.Grail.isGrail and Result.Grail.collected or false,
        stat = cachedStats(Item), pstat = cachedStats(Me),
        maxSockets = function() if maxSockets == nil then maxSockets = Item:MaxSockets() or 0 end; return maxSockets end,
        isType = function(id) if types[id] == nil then types[id] = Item:IsType(id) end; return types[id] end,
    }
    local matched = false
    for _, compiled in ipairs(candidatesFor(level or config.filter_level or 1, ctx.code)) do
        diagnostics.tested = diagnostics.tested + 1
        local rule = compiled.rule
        if compiled.codes(ctx.code) then
            local inLocation, failed = compiled.locations(location), nil
            if not inLocation then failed = "location" end
            if not failed then
                for _, test in ipairs(compiled.tests) do if not test[2](ctx) then failed = test[1]; break end end
            end
            if not failed then
                matched = true
                trace("matches", compiled, ctx)
                if config.audioPlayback == true and rule.audio then
                    local text = trim(placeholders(rule.audio, ctx, rule))
                    if text ~= "" then
                        -- %a excludes digits, so .mp3 used to fall through to SAY.
                        -- A quoted filename is still a file, not spoken text.
                        local quote = text:sub(1, 1)
                        if (quote == '"' or quote == "'") and text:sub(-1) == quote then
                            text = trim(text:sub(2, -2))
                        end
                        local extension = text:lower():match("%.([%w]+)$")
                        speak((extension == "mp3" or extension == "flac" or extension == "wav") and ("PLAY:" .. text) or ("SAY:" .. text))
                    end
                end
                if rule.notify then
                    local message = rule.notify
                    local color = message:match("{([%a%s]+)}%s*{link}") or message:match("{([%a%s]+)}")
                    local link = ": " .. Item:Link(color and colorMapLink[color:lower()] or "4")
                    message = message:gsub("{link}", function() return link end)
                    print(render(message, ctx, rule))
                end
                if rule.hide then
                    Result.Hide = true
                    if rule.print then print(type(rule.print) == "string" and render(rule.print, ctx, rule) or ("Hiding item: " .. (Item.Name or ctx.code))) end
                    return
                end
                if rule.background then
                    for i = 1, 4 do Result.Background[i] = channel(rule.background[i] or 242.25) end
                end
                if rule.border then
                    for i = 1, 4 do Result.Border[i] = channel(rule.border[i] or 255) end
                    Result.Border[5] = math.max(0, math.floor(rule.border[5] or 1))
                end
                local name, desc = Result.Name or Item.Name or "", Result.Description or ""
                if rule.name_override then name = render(rule.name_override, ctx, rule) end
                if rule.prefix then name = render(rule.prefix, ctx, rule) .. name end
                if rule.suffix then name = name .. render(rule.suffix, ctx, rule) end
                -- Keep the native tooltip's historical (reversed) display order.
                if rule.prefix_desc then desc = desc .. render(rule.prefix_desc, ctx, rule) end
                if rule.suffix_desc then desc = render(rule.suffix_desc, ctx, rule) .. desc end
                Result.Name, Result.Description = name, desc
                if rule.background_style then Result.BackgroundFunction = rule.background_style end
                if rule.name_style then Result.NameFunction = rule.name_style end
            else
                trace("failures", compiled, ctx, failed)
                -- A mismatch may hide only at explicitly selected locations; inventory/equipment
                -- remain safe when location was omitted. Never hide just for a location mismatch.
                if rule.hide_mismatches and inLocation then Result.Hide = true; return end
            end
        end
    end
    -- Decide only after every applicable rule had a chance to match.
    if not matched and config.hide_mismatches and location == "onground" then Result.Hide = true end
end
