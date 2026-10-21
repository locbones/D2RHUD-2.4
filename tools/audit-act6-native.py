"""Read-only Act VI audit of the supported executable; never applies patches.

Requires pefile and capstone. Output records native bytes and containing unwind
fragments, so follow-up work cannot mistake a nearby immediate for an act bound.
"""
import argparse
import bisect
import hashlib
import json
from pathlib import Path

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

SUPPORTED = "e4e13570e60c0a65cb0469d6659be8a6008f9433ac50db1c4f94501d7d9016f8"
SITES = {
    "quest_flag_reader": (0x243880, 0x3f),
    "quest_buffer_allocator": (0x243a20, 0x9b),
    "quest_buffer_loader": (0x243ae0, 0xda),
    "quest_normalization_count": (0x243b31, 4),
    "quest_save_section": (0x318d9f, 0x64),
    "quest_load_section": (0x31d5fd, 0x66),
    "waypoint_load_section": (0x31d67e, 0xa4),
    "act_vector_init": (0x27b360, 0x25),
    "saved_act_validation": (0x31aa3e, 0x15),
    "quest_panel_factory": (0xbb6c10, 0x92),
    "quest_grid_write": (0xbb71c0, 0x31),
    "quest_tab_unlock": (0xbb738e, 0x76),
    "quest_tab_visibility_bound": (0xbb7556, 0xb),
    "quest_record_lookup": (0x147ac5, 0x36),
    "waypoint_panel_factory": (0x1531a0, 0x11d),
    "waypoint_tab_unlock": (0x154ba0, 0x9b),
    "controller_waypoint_labels_bound": (0x1547b2, 0x4d),
}


def audit(path):
    raw = path.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != SUPPORTED:
        raise ValueError(f"Unsupported executable SHA256: {digest}")
    pe = pefile.PE(data=raw)
    image = pe.get_memory_mapped_image()
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    fragments = sorted((e.struct.BeginAddress, e.struct.EndAddress)
                       for e in pe.DIRECTORY_ENTRY_EXCEPTION)
    starts = [a for a, _ in fragments]
    sites = {}
    for name, (start, size) in SITES.items():
        parts = []
        for a, b in fragments[max(0, bisect.bisect_right(starts, start) - 1):]:
            if a >= start + size:
                break
            if b <= start:
                continue
            # Decode from a verified instruction boundary, not the selected RVA.
            decoded = [dict(rva=i.address, bytes=i.bytes.hex(),
                            instruction=f"{i.mnemonic} {i.op_str}")
                       for i in md.disasm(image[a:b], a)
                       if start <= i.address < start + size]
            parts.append(dict(start=a, end=b, instructions=decoded))
        sites[name] = dict(rva=start, size=size,
                           sha256=hashlib.sha256(image[start:start+size]).hexdigest(),
                           fragments=parts)
    records = []
    for i in range(41):
        row = image[0x14b9200+i*24:0x14b9200+(i+1)*24]
        records.append(dict(flag_record=i, visible=bool(row[0]), button=row[1],
                            display_order=row[2], act=row[3],
                            text_rva=int.from_bytes(row[8:16], "little") - pe.OPTIONAL_HEADER.ImageBase
                            if any(row[8:16]) else None,
                            script_id=int.from_bytes(row[16:20], "little"),
                            stored_id=int.from_bytes(row[20:24], "little")))
    return dict(executable=str(path.resolve()), sha256=digest,
                image_base=pe.OPTIONAL_HEADER.ImageBase,
                scope="Static audit only; no patches installed and no runtime acceptance implied.",
                sites=sites, quest_records=records,
                globals={"act_info": 0x1c9ea80, "act_count": 0x1c9ea88,
                         "quest_presentation": 0x14b9200,
                         "quest_status_previous": 0x1d60cd8,
                         "quest_status_current": 0x1d60d10})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = audit(args.exe)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Recorded {len(result['sites'])} native contracts; executable unchanged.")
