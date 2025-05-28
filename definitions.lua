---@meta

---@param ... any
_G.print = function(...) end
---@param ... string
---@return string[]
_G.SSet = function(...) end
---@param ... integer
---@return integer[]
_G.ISet = function(...) end

---@class D2ItemsTxt
---@field Code string
---@field UberCode string
---@field UltraCode string

---@class D2ItemFilterResultStrc
---@field Hide boolean
---@field Name string -- update the name of the item. '%s' get's replaced with the item name.

---@class D2ItemDataStrc
---@field Quality integer -- 0: None, 1: Inferior, 2: Normal, 3: Superior, 4: Magic, 5: Set, 6: Rare, 7: Unique, 8: Crafted
---@field Owner integer -- ID of the player that owns the item
---@field Flags integer
---@field IsEthereal boolean
---@field IsIdentified boolean
---@field BodyLoc integer
---@field Page integer
---@field FileIndex integer -- index of the item in the sets.txt/uniques.txt file
---@field RarePrefix integer
---@field RareSuffix integer
---@field MagicPrefixes integer[] -- size 3
---@field MagicSuffixes integer[] -- size 3
---@field Gfx integer


---@class D2UnitStrc
---@field Address integer
---@field ID integer -- unique identifier for the unit for the current game session
---@field Mode integer -- animation mode of the player or item. can tell if on ground, equipped, etc.
D2UnitStrc = {}

---@param statId integer
---@param layer? integer
---@return integer
---Get the current value of a stat for the unit.
function D2UnitStrc:Stat(statId, layer) end

---@class D2ItemUnitStrc : D2UnitStrc
---@field Name string
---@field Txt D2ItemsTxt
---@field Data D2ItemDataStrc
---@field IsOnGround boolean
---@field IsEquipped boolean
---@field Rarity integer
D2ItemUnitStrc = {}

---@param typeId integer
---@return boolean
---Check if the item is of a specific type.
function D2ItemUnitStrc:IsType(typeId) end

---@class D2PlayerUnitStrc : D2UnitStrc
---@field Class integer
---@field Name string