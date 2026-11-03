"""The real Windows helper child must have no console, even under pythonw."""
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('custody_io', repo / 'tools/marketplace/custody-workspace.py')
io = importlib.util.module_from_spec(spec)
spec.loader.exec_module(io)
with tempfile.TemporaryDirectory(prefix='market-background-') as directory:
    root = Path(directory)
    script = root / 'helper.py'
    script.write_text('import ctypes,json; print(json.dumps({"console":ctypes.windll.kernel32.GetConsoleWindow()}))', encoding='utf-8')
    assert io.run_helper(sys.executable, script, '-I', root / 'unused')['console'] == 0
    with patch.object(io.subprocess, 'run', wraps=subprocess.run) as run:
        io.run_helper(sys.executable, script, '-I', root / 'unused')
        assert run.call_args.kwargs['creationflags'] == subprocess.CREATE_NO_WINDOW
    # Pythonw gives Task Scheduler a GUI executable. Its child also has no console.
    entry = repo / 'tools/marketplace/background-marketplace.py'
    probe = root / 'probe.py'
    target = root / 'result.json'
    probe.write_text('import ctypes,json,runpy,sys\nfrom unittest.mock import patch\n'
        'from pathlib import Path\nm=runpy.run_path(sys.argv[1])\n'
        'with patch("subprocess.run") as run:\n'
        ' run.return_value.returncode=0\n m["main"]()\n'
        ' Path(sys.argv[2]).write_text(json.dumps({"console":ctypes.windll.kernel32.GetConsoleWindow(),"flags":run.call_args.kwargs["creationflags"]}))\n', encoding='utf-8')
    subprocess.run([str(Path(sys.executable).with_name('pythonw.exe')), str(probe), str(entry), str(target)], check=True)
    result = json.loads(target.read_text())
    assert result == {'console': 0, 'flags': subprocess.CREATE_NO_WINDOW}
task = (repo / 'tools/marketplace/start-marketplace.ps1').read_text(encoding='utf-8')
assert "'pythonw.exe'" in task and 'New-ScheduledTaskAction -Execute $windowless' in task
print('Background launch: real helper and pythonw have no console; scheduled entry uses CREATE_NO_WINDOW and retains startup logs.')
