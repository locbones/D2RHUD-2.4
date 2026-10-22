"""Export readable HUD descriptions from the mod's existing quest cube recipes.

Writes event objectives and the original lesson text. Only actual crafting
activities and the three charm upgrades retain cube operations.
"""
import argparse
import csv
import json
import re
from pathlib import Path


def rows(path):
    with path.open(encoding="utf-8-sig", errors="replace", newline="") as source:
        return list(csv.DictReader(source, delimiter="\t"))


def clean(text):
    return re.sub(r"(?:ÿ|\ufffd)c[\w;]", "", text).replace("\n", " ").strip()


def export(excel, output, filter_path):
    strings = {}
    for path in (excel.parent.parent / "local/lng/strings").glob("*.json"):
        try:
            for entry in json.loads(path.read_text(encoding="utf-8-sig")):
                if isinstance(entry, dict) and "Key" in entry:
                    strings[entry["Key"]] = clean(entry.get("enUS", entry["Key"]))
        except (ValueError, UnicodeError):
            continue
    names = {}
    for table in ("weapons.txt", "armor.txt", "misc.txt"):
        for item in rows(excel / table):
            code = item.get("code", "")
            if code:
                label = strings.get(item.get("namestr"), item.get("name") or code)
                names[code] = label if re.search(r"[A-Za-z]{2}", label) else item.get("name") or code
    names.update({"weap": "any weapon", "armo": "any armor", "rin": "a ring",
                  "amu": "an amulet", "char": "a charm", "belt": "a belt",
                  "gemz": "any gem", "ingr": "a crafting ingredient"})
    names.update({f"Bt{i:02d}": f"Quest {i} Parchment of Proof" for i in range(1, 100)})
    names.update({f"Barrier{i}": "Evil Barrier quest charm" for i in range(1, 5)})

    def item(text):
        tokens = text.split(",")
        base, qualifiers = tokens[0], tokens[1:]
        name = names.get(base, strings.get(base, base))
        for qualifier, adjective in (("uni", "unique"), ("rar", "rare"), ("crf", "crafted"), ("set", "set")):
            if qualifier in qualifiers:
                name = adjective + " " + name
        quantity = next((token[4:] for token in qualifiers if token.startswith("qty=")), None)
        return (quantity + " x " if quantity else "") + name

    def recipe(row):
        ingredients = " + ".join(item(row.get(f"input {i}", "")) for i in range(1, 8) if row.get(f"input {i}"))
        results = []
        for field in ("output", "output b", "output c"):
            text = row.get(field, "")
            if not text:
                continue
            if text.split(",")[0] == "useitem":
                results.append("the original item, with its contents retained")
            elif text.startswith("Barrier"):
                results.append("upgraded Evil Barrier quest charm")
            else:
                results.append(item(text))
        return ingredients + " -> " + " + ".join(results)

    text = filter_path.read_text(encoding="utf-8-sig")
    lessons = {}
    for m in re.finditer(r"\{\s*-- Evil Barrier (\d+)\s*\n([\s\S]*?)(?=\{\s*-- Evil Barrier |\Z)", text):
        desc = re.search(r'suffix_desc\s*=\s*("(?:\\.|[^"\\])*")', m[2])
        if not desc:
            continue
        raw = json.loads(desc[1])
        plain = lambda value: re.sub(r"\{[^}]+\}", "", value).replace("%%", "%").strip()
        task = plain(raw.split("{gold}Task:")[0].split("{gold}Special Notes:")[-1])
        tips = plain(raw.split("{turquoise}Tips:")[0])
        reward = plain(raw.split("{gold}Reward:")[0].split("----------------------------------------")[-1])
        lessons[int(m[1])] = (task, tips, reward)
    if set(lessons) != set(range(1, 46)):
        raise ValueError("Expected all 45 original filter lessons")
    titles = {1:"Store a skull",2:"Withdraw a skull",3:"Your equipped weapon",4:"The Mythos Statue",5:"One thousand monsters",6:"The Gnasher",7:"Countess and Thul",8:"Withdraw an Amn",9:"Daylight Tier 2",10:"Recipes and the Rift Shop",11:"A beginner's scribblings",12:"The Gidbinn",13:"Socket an amulet",14:"Quill of Quality",15:"Beacon of Hope",16:"Hexfire",17:"Mercenary information",18:"Enhance a belt",19:"Craft a Safety ring",20:"Daylight Tier 5",21:"Blood Contract",22:"Dye an item",23:"Skin of the Vipermagi",24:"The Mod Info menu",25:"Arm of King Leoric",26:"Gem Vein",27:"Soul Crystal",28:"Mod credits",29:"Unrelenting Will",30:"Socket Remover",31:"The Grail Tracker",32:"Upconvert magic jewels",33:"The Loot Filter",34:"The Guide Compendium",35:"Griswold's set",36:"Rainbow Facet",37:"Essence of Hatred",38:"Gold Bar",39:"Ramaladni's Gift",40:"Milk Bottle",41:"A Fallen's gold pile",42:"Body parts",43:"Complete a Codex",44:"Limit Break I",45:"A Limit Break item"}
    craft = {9,13,18,19,20,21,22,32,43,44}
    acknowledge = set()
    confirmations = {10:"a Thawing Potion with an Antidote Potion",17:"a Thawing Potion with an Identify Scroll",24:"an Antidote Potion with a Town Portal Scroll",28:"a Thawing Potion with a Town Portal Scroll",31:"two Town Portal Scrolls",33:"two Identify Scrolls",34:"an Identify Scroll with a Town Portal Scroll"}
    all_rows = rows(excel / "cubemain.txt")
    sets = {r["index"] for r in rows(excel / "setitems.txt")}
    uniques = {r["index"] for r in rows(excel / "uniqueitems.txt")}
    types = {r["Code"] for r in rows(excel / "itemtypes.txt")}
    def predicate(row):
        tokens = row["input 1"].split(",")
        base = tokens[0]
        result = {"unique" if base in uniques else "set" if base in sets else "code" if base in names and base not in types else "type": base}
        for token, quality in (("uni",7),("set",5),("crf",8),("rar",6)):
            if token in tokens: result["quality"] = quality
        if row["op"] in {"15", "18"} and row["param"] != "398":
            result.update(stat=int(row["param"]), min=int(row["value"]))
            if row["op"] == "18": result["max"] = int(row["value"])
        return result
    quests = {}
    for row in all_rows:
        match = re.fullmatch(r"Quest (\d+)([ab])", row.get("description", ""))
        if not match or row.get("enabled") != "1":
            continue
        number, part = int(match[1]), match[2]
        quest = quests.setdefault(number, {"id": number, "title": titles.get(number, f"Mod lesson {number}"),
            "objective": lessons[number][0], "tips": lessons[number][1], "reward": lessons[number][2],
            "trigger": "craft" if number in craft else "acknowledge" if number in acknowledge else "item",
            "items": [], "recipes": [], "milestone": number in {15,30,45}, "bonus": number in {5,10,20,25,35,40},
            "turnIn": "Upgrade your quest charm in the cube with a Quill of Quality and your choice of weapon, armor, ring, amulet or charm. No proof parchment is required." if number in {15,30,45} else "Progress is recorded automatically. Stat rewards can be claimed here while carrying your Evil Barrier charm.",
            "objectiveRecipes": [], "rewardRecipes": []})
        if part == "a" and quest["trigger"] in {"item", "craft"}:
            pred = predicate(row)
            if pred not in quest["items"]: quest["items"].append(pred)
        line = recipe(row)
        if part == "b":
            line = re.sub(r" \+ Quest \d+ Parchment of Proof", "", line)
        target = quest["objectiveRecipes" if part == "a" else "rewardRecipes"]
        if line not in target:
            target.append(line)
    if not quests or sorted(quests) != list(range(1, len(quests) + 1)):
        raise ValueError("Expected a consecutive quest chain")
    for quest in quests.values():
        if not quest["objectiveRecipes"] or not quest["rewardRecipes"]:
            raise ValueError(f"Incomplete quest {quest['id']}")
    for q in quests.values():
        n = q["id"]
        if n in {1,2,7,8}:
            q.update(trigger="depositSkull" if n==1 else "countessThul" if n==7 else "recipe",items=[])
            for i,r in enumerate(all_rows):
                desc=r.get("description","")
                if r.get("enabled")=="1" and ((n==1 and re.fullmatch(r"Add Gem(?: x\d+)?",desc)) or (n==2 and desc=="Remove Gem - Skull") or (n==7 and re.fullmatch(r"Add Rune - Thul(?: x\d+)?",desc)) or (n==8 and desc=="Remove Rune - Amn")):
                    q["recipes"].append(i)
            if not q["recipes"]: raise ValueError(f"No real crafting operation for quest {n}")
        elif n==3: q.update(trigger="dropWeapon",items=[])
        elif n in confirmations:
            q.update(trigger="questRecipe",items=[],recipes=[],objective=("Cube "+confirmations[n]+".") if n==10 else lessons[n][0].rstrip(". ")+". Confirm by cubing "+confirmations[n]+".")
        elif n==4: q.update(trigger="statueCube")
        elif n==5: q.update(trigger="kills",target=1000)
        elif n in {19,32}:
            q["recipes"]=[i for i,r in enumerate(all_rows) if r.get("enabled")=="1" and (r.get("description","").startswith("Safety Ring") if n==19 else r.get("description")=="Upconvert Magic Jewels")]
        if n==7: q["boss"]=6
        if n in {1,2,3,4,5,7,8,10,17,24,28,31,33,34}: q["objectiveRecipes"]=[]
        else: q["objectiveRecipes"]=[] # The lesson watches the activity, not a proof recipe.
        if not q["milestone"]: q["rewardRecipes"]=[]
        if q["milestone"]:
            match=re.search(r'code\s*=\s*"Bt'+str(n)+r'"[\s\S]*?prefix_desc\s*=\s*("(?:\\.|[^"\\])*")',text)
            if not match: raise ValueError(f"Missing upgrade choices for quest {n}")
            original=plain(json.loads(match[1])).split("Cube this")[0]
            choices=[]
            for choice in re.finditer(r"([\s\S]*?)Reward ([A-E]):\s*\(([^)]+)\)",original):
                choices.append((choice[2],choice[3]+" + Quill of Quality + Evil Barrier charm\n"+choice[1].strip()))
            if len(choices)!=5: raise ValueError(f"Expected five upgrade choices for quest {n}")
            q["rewardRecipes"]=[value for _,value in sorted(choices)]
            q["reward"]="Choose one of the five charm upgrades below. The Quill and chosen ingredient are consumed."
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({"version": 2, "quests": list(quests.values())}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {len(quests)} quests to {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--excel-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--filter-path", required=True, type=Path)
    args = parser.parse_args()
    export(args.excel_root, args.output, args.filter_path)
