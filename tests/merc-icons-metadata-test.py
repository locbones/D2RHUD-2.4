"""HireableIconCel survives BIN-only publication in the existing metadata file."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("metadata", root / "tools/export-table-metadata.py")
metadata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metadata)

with tempfile.TemporaryDirectory(prefix="d2rhud-merc-icons-metadata-") as temporary:
    data = Path(temporary)
    excel = data / "global/excel"
    excel.mkdir(parents=True)
    text = "skilldesc\tIconCel\tHireableIconCel\nalpha\t8\t0\nbeta\t9\t32\nblank\t7\t\n"
    (excel / "skilldesc.txt").write_text(text, encoding="utf-8")
    binary = struct.pack("<I", 3) + bytes(3 * 0x128)
    (excel / "skilldesc.bin").write_bytes(binary)
    metadata.export(data)
    result = json.loads((data / "D2RHUD/table-metadata.json").read_text(encoding="utf-8"))
    entry = result["tables"]["skilldesc"]
    assert entry["overlay"] == "skilldesc\tHireableIconCel\nalpha\t0\nbeta\t32\nblank\t\n"
    assert entry["names"] == ["alpha", "beta", "blank"]
    assert entry["bin_hash"] == metadata.digest(binary)
    assert (excel / "skilldesc.bin").read_bytes() == binary
print("Python BIN export preserves hireling icon frames, zero and blanks without changing native records.")
