"""Concurrent supervisor logging stays in one labeled, bounded stream."""
import importlib.util
import io
from pathlib import Path
import tempfile
import threading

spec = importlib.util.spec_from_file_location('host', Path(__file__).resolve().parents[1] / 'tools/marketplace/companion-host.py')
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    log = root / 'diagnostics.log'
    threads = [threading.Thread(target=host.drain, args=(io.StringIO('worker line\n' * 100), log, source)) for source in ('RMD-MP/custody', 'RMD-MP/SC', 'Other/HC')]
    for thread in threads: thread.start()
    for thread in threads: thread.join()
    lines = log.read_text(encoding='utf-8').splitlines()
    assert len(lines) == 300
    for source in ('RMD-MP/custody', 'RMD-MP/SC', 'Other/HC'):
        assert sum(f'[{source}] worker line' in line for line in lines) == 100
    assert list(root.iterdir()) == [log]
    log.write_text('old' * 400000, encoding='utf-8')
    host.log_line(log, 'new supervisor entry')
    assert log.stat().st_size < 1024
    assert log.with_suffix('.previous.log').stat().st_size == 1200000
    assert '[supervisor] new supervisor entry' in log.read_text()
    host.log_line(root / 'missing' / 'diagnostics.log', 'failure is nonfatal')
print('Combined worker logging, concurrent labels, bounded rotation and nonfatal I/O failures passed.')
