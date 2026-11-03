"""Windowless scheduled entry point; run with pythonw.exe on Windows."""
import os
from pathlib import Path
import subprocess


def main():
    console = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    script = Path(__file__).with_name('start-marketplace.ps1')
    logs = Path.home() / '.d2r-marketplace'
    logs.mkdir(exist_ok=True)
    with (logs / 'startup.log').open('ab') as log:
        try:
            return subprocess.run([str(console), '-NoLogo', '-NoProfile', '-NonInteractive',
                '-WindowStyle', 'Hidden', '-File', str(script)], stdin=subprocess.DEVNULL,
                stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW, check=False).returncode
        except Exception as error:
            log.write(('Marketplace background startup failed: ' + str(error) + '\n').encode('utf-8'))
            return 1


if __name__ == '__main__':
    raise SystemExit(main())
