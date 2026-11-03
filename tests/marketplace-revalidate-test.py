"""HUD-only pin updates must not admit changed launchers/helpers/catalogs."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('agent', repo / 'tools/marketplace/custody-agent.py')
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)


def main():
    assert os.name == 'nt'
    with tempfile.TemporaryDirectory(prefix='market-revalidate-') as temporary:
        root = Path(temporary)
        state, saves, game = [root / name for name in ('state', 'saves', 'game')]
        data = game / 'Mods/RMD-MP/RMD-MP.mpq/data'
        for path in (state, saves, game, data / 'global/excel'):
            path.mkdir(parents=True)
        for name in ('weapons', 'armor', 'misc', 'itemstatcost', 'itemtypes'):
            (data / f'global/excel/{name}.bin').write_bytes(b'fixture')
        helper, launcher, hud, verified = [root / name for name in ('helper.exe', 'launcher.exe', 'game/d2rhud.dll', 'verified.dll')]
        for path in (helper, launcher):
            path.write_bytes(path.name.encode())
        hud.write_bytes(b'updated verified HUD')
        verified.write_bytes(hud.read_bytes())
        (state / 'guard.lock').touch()
        save = saves / 'Stash_SC_Page11.d2i'
        save.write_bytes(b'untouched player save')
        enrollment = root / 'enrollment.json'
        cfg = dict(schema=1, enrolled=True, state_directory=str(state), save_directory=str(saves),
                   mod_data=str(data), helper=str(helper), helper_sha256=a.io.digest(helper.read_bytes()),
                   catalog=a.catalog(data), game_directory=str(game), launcher_path=str(launcher),
                   launcher_sha256=a.io.digest(launcher.read_bytes()), hud_sha256='0' * 64,
                   python=sys.executable, agent_script=str(repo / 'tools/marketplace/custody-agent.py'))
        a.io.save(enrollment, cfg)
        original = enrollment.read_bytes()
        command = ['powershell.exe', '-NoProfile', '-File', str(repo / 'tools/marketplace/revalidate-custody.ps1'),
                   '-VerifiedHudPath', str(verified), '-EnrollmentPath', str(enrollment)]
        def run(success):
            before_enrollment = enrollment.read_bytes()
            result = subprocess.run(command, capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
            assert (result.returncode == 0) == success, result.stdout + result.stderr
            if not success:
                assert enrollment.read_bytes() == before_enrollment
            assert save.read_bytes() == b'untouched player save'
        for path in (launcher, helper, data / 'global/excel/misc.bin', verified):
            before = path.read_bytes()
            path.write_bytes(b'unexpected replacement')
            run(False)
            path.write_bytes(before)
        (state / 'pending.json').write_text('{}', encoding='utf-8')
        run(False)
        (state / 'pending.json').unlink()
        with a.lease(state / 'guard.lock'):
            run(False)
        run(True)
        updated = a.io.document(enrollment)
        assert updated == dict(cfg, hud_sha256=a.io.digest(hud.read_bytes()))
        backups = list((state / 'backups').glob('hud-revalidation-*/custody-enrollment.json'))
        assert len(backups) == 1 and backups[0].read_bytes() == original
        # A launcher update needs its own explicit, matching build artifact.
        launcher.write_bytes(b'updated verified launcher')
        run(False)
        verified_launcher = root / 'verified-launcher.exe'
        verified_launcher.write_bytes(b'wrong launcher artifact')
        command.extend(['-VerifiedLauncherPath', str(verified_launcher)])
        run(False)
        verified_launcher.write_bytes(launcher.read_bytes())
        run(True)
        assert a.io.document(enrollment) == dict(updated, launcher_sha256=a.io.digest(launcher.read_bytes()))
        run(True)
    print('HUD revalidation: artifact mismatch, launcher/helper/catalog drift, pending transfer, active writer, atomic pin update, backup and save immutability passed.')


if __name__ == '__main__':
    main()
