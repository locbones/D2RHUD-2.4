"""D-G source columns survive BIN release export without widening native rows."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("metadata", root / "tools/export-table-metadata.py")
metadata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metadata)

with tempfile.TemporaryDirectory(prefix="d2rhud-seven-output-metadata-") as temporary:
    data = Path(temporary)
    excel = data / "global/excel"
    excel.mkdir(parents=True)
    header = ["enabled", "op", "param", "value", "numinputs", "output", "output b", "output c"]
    row = ["1", "0", "0", "0", "7", "useitem1", "useitem2", "useitem3"]
    for slot in "defg":
        header += [f"output {slot}", f"{slot} lvl", f"{slot} plvl", f"{slot} ilvl"]
        row += [f"useitem{ord(slot) - ord('a') + 1},qty=2", "80", "25", "10"]
        for mod in range(1, 6):
            header += [f"{slot} mod {mod}{suffix}" for suffix in ("", " chance", " param", " min", " max")]
            row += ["test-property", "35", "7", "-10", "20"]
    source = "\t".join(header) + "\n" + "\t".join(row) + "\n"
    (excel / "cubemain.txt").write_bytes(source.encode("utf-8"))
    record = bytearray(0x148)
    record[0], record[0x10] = 1, 7
    for slot in range(3):
        record[0x4c + slot * 0x54 + 8] = 254
    binary = struct.pack("<I", 1) + record
    (excel / "cubemain.bin").write_bytes(binary)
    for name, text in (("properties", "code\ntest-property\n"), ("itemtypes", "code\nweap\n")):
        (excel / f"{name}.txt").write_text(text, encoding="utf-8")
        (excel / f"{name}.bin").write_bytes(struct.pack("<I", 1) + bytes(16))
    metadata.export(data)
    result = json.loads((data / "D2RHUD/table-metadata.json").read_text(encoding="utf-8"))["tables"]
    assert result["cubemain"]["source"] == source
    assert result["properties"]["names"] == ["test-property"]
    assert result["itemtypes"]["names"] == ["weap"]
    assert (excel / "cubemain.bin").read_bytes() == binary
print("BIN export preserves D-G, level fields, all five modifier fields and table symbols; native records remain unchanged.")
