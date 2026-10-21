"""Preserve HUD-only source metadata next to compiled D2R 2.4 tables.

Run after the mod's BINs have been generated, before omitting TXT from a release.
Never compiles or modifies native tables. Each entry is bound to its exact BIN.
"""
from pathlib import Path
import argparse
import json
import struct

def digest(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"{value:016x}"

def table(path):
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    return [line.split("\t") for line in lines]

def rows(cells):
    return [row for row in cells[1:] if row and row[0] and row[0].lower() != "expansion"]

def export(data):
    excel = data / "global/excel"
    columns = {"itemstatcost":"stat", "properties":"code", "itemtypes":"code",
               "weapons":"code", "armor":"code", "misc":"code", "gems":"code",
               "skills":"skill", "skilldesc":"skilldesc", "states":"state",
               "monstats":"id", "superuniques":"superunique", "treasureclassex":"treasure class",
               "charstats":"class", "playerclass":"class", "plrtype":"name", "levels":"name"}
    output = {"version":1, "tables":{}}
    for source in sorted(excel.glob("*.txt")):
        name = source.stem.lower()
        binary = source.with_suffix(".bin")
        if not binary.exists():
            binary = excel / (name + ".bin")
        if not binary.exists():
            continue
        raw = binary.read_bytes()
        if len(raw) < 4:
            raise ValueError(f"Truncated BIN: {binary}")
        count = struct.unpack_from("<I", raw)[0]
        if count <= 0 or (len(raw)-4) % count:
            raise ValueError(f"Unsupported BIN layout: {binary}")
        cells = table(source)
        if not cells:
            continue
        header = [value.lower() for value in cells[0]]
        records = rows(cells)
        key = columns.get(name, "index")
        entry = {"bin_hash":digest(raw), "bin_size":len(raw)}
        if key in header:
            index = header.index(key)
            if len(records) < count:
                raise ValueError(f"Not enough source rows for {name}: {len(records)} < {count}")
            entry["names"] = [row[index] if index < len(row) else "" for row in records[:count]]
        if name=="itemtypes" and "itemtype" in header:
            index=header.index("itemtype")
            entry["labels"]=[row[index] if index<len(row) else "" for row in records[:count]]
        if name == "cubemain":
            # Exact original rows keep quest-history definition digests stable.
            if len(cells)-1 != count:
                raise ValueError("cubemain TXT/BIN row counts differ; compile before exporting")
            entry["source"] = source.read_bytes().decode("utf-8")
        if name in ("monstats", "superuniques"):
            wanted = [i for i, key in enumerate(header) if key in ("id", "hcidx", "superunique") or "treasureclass" in key or key.startswith("tc")]
            lines = ["\t".join(cells[0][i] for i in wanted)]
            lines += ["\t".join(row[i] if i < len(row) else "" for i in wanted) for row in records[:count]]
            entry["overlay"] = "\n".join(lines) + "\n"
        if len(entry) > 2:
            output["tables"][name] = entry

    # The engine prepends generated item-type treasure classes. Derive this
    # native ID offset from compiled monster references, never TXT row offsets.
    tcfile, monfile = excel/"treasureclassex.bin", excel/"monstats.bin"
    monsource = excel/"monstats.txt"
    if tcfile.exists() and monfile.exists() and monsource.exists() and "treasureclassex" in output["tables"]:
        tcraw, monraw = tcfile.read_bytes(), monfile.read_bytes()
        tccount, moncount = struct.unpack_from("<I",tcraw)[0], struct.unpack_from("<I",monraw)[0]
        if len(tcraw)==4+tccount*0x2f0 and len(monraw)==4+moncount*0x1dc:
            tc = {tcraw[4+i*0x2f0:4+i*0x2f0+32].split(b"\0")[0].decode("utf-8"):i for i in range(tccount)}
            cells = table(monsource); header = cells[0]; offsets=set()
            for index,row in enumerate(rows(cells)[:moncount]):
                for difficulty,suffix in enumerate(("", "(N)", "(H)")):
                    for variant in range(3):
                        col=header.index(f"TreasureClass{variant+1}{suffix}")
                        name=row[col] if col<len(row) else ""
                        native=struct.unpack_from("<H",monraw,4+index*0x1dc+0xba+difficulty*8+variant*2)[0]
                        if name in tc and native:
                            offsets.add(native-tc[name])
            if len(offsets)!=1:
                raise ValueError(f"Monster/treasure BINs do not agree on native class IDs: {offsets}")
            base=offsets.pop()
            if not 0<=base<=65535:
                raise ValueError("Invalid native treasure class ID base")
            output["tables"]["treasureclassex"]["id_base"]=base

    target=data/"D2RHUD/table-metadata.json"
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(json.dumps(output,ensure_ascii=False,separators=(",",":")),encoding="utf-8")
    auxiliary=excel/"transmog_table.txt"
    if auxiliary.exists():
        (target.parent/"transmog_table.json").write_text(json.dumps({"version":1,"source":auxiliary.read_bytes().decode("utf-8")},ensure_ascii=False,separators=(",",":")),encoding="utf-8")
    print(f"Exported {len(output['tables'])} checksum-bound HUD table metadata entries: {target}")

if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data",type=Path,help="Mod's data directory containing global/excel")
    export(parser.parse_args().data.resolve())
