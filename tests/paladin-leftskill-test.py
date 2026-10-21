"""Exercise the optional colleague table utility without touching installed mods."""
from pathlib import Path
import importlib.util
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

SCRIPT = Path(__file__).resolve().parents[1] / "tools/paladin_leftskill.py"
SPEC = importlib.util.spec_from_file_location("paladin_leftskill", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class TableTests(unittest.TestCase):
    def test_preserves_table_bytes_except_eligible_cells(self):
        header = b"\xef\xbb\xbfskill\tcharclass\taura\tleftskill\trightskill\textra\r\n"
        rows = [b"Holy Fire\tpal\t1\t0\t1\tkeep\r\n", b"Prayer\tpal\t1\t1\t1\t\n",
                b"Other\tsor\t1\t0\t1\tkeep\r\n", b"Attack\tpal\t0\t0\t1\tkeep\r\n", b"short\r\n",
                b"Salvation\tpal\t1\t\t1\tend"]
        raw = header + b"".join(rows)
        output, changed = MODULE.enable_left_auras(raw)
        self.assertEqual(changed, ["Holy Fire", "Salvation"])
        self.assertEqual(output, header + rows[0].replace(b"\t0\t1", b"\t1\t1") + b"".join(rows[1:5]) + rows[5].replace(b"\t\t1", b"\t1\t1"))
        self.assertEqual(MODULE.enable_left_auras(output), (output, []))

    def test_rejects_invalid_tables(self):
        for raw in (b"", b"skill\tcharclass\n", b"skill\tcharclass\taura\tleftskill\trightskill\nBad\tpal\t1\t0\t0\n"):
            with self.assertRaises(ValueError):
                MODULE.enable_left_auras(raw)

    def test_cli_never_overwrites_input_or_existing_output_even_with_optimization(self):
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch) / "skills.txt"
            raw = b"skill\tcharclass\taura\tleftskill\trightskill\nPrayer\tpal\t1\t0\t1\n"
            source.write_bytes(raw)
            target = Path(scratch) / "output.txt"
            for output in (source, target):
                output.write_bytes(raw)
                result = subprocess.run([sys.executable, "-O", str(SCRIPT), "--skills", str(source), "--out", str(output)], capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(output.read_bytes(), raw)
            target.unlink()
            result = subprocess.run([sys.executable, str(SCRIPT), "--skills", str(source), "--out", str(target)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(source.read_bytes(), raw)
            self.assertIn(b"Prayer\tpal\t1\t1\t1", target.read_bytes())


if __name__ == "__main__":
    unittest.main()
