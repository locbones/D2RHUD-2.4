"""Stable HUD compatibility, bounded PE data reads and independent packaging."""
import argparse
import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import zipfile

repo = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


a = load('protocol_agent', repo / 'tools/marketplace/custody-agent.py')
io = a.io
contract = dict(schema=1, game_mode='2.4', live_custody=2, features=dict(io.HUD_FEATURES))


def pe(declaration=contract):
    raw = bytearray(0x1200)
    def put(fmt, at, value): struct.pack_into(fmt, raw, at, value)
    raw[:2] = b'MZ'; put('<I', 0x3c, 0x80); raw[0x80:0x84] = b'PE\0\0'
    put('<H', 0x84, 0x8664); put('<H', 0x86, 1); put('<H', 0x94, 0xf0); put('<H', 0x96, 0x2000)
    put('<H', 0x98, 0x20b); put('<I', 0x98+108, 16); put('<I', 0x98+112, 0x1000); put('<I', 0x98+116, 0x100)
    section = 0x188
    put('<I', section+12, 0x1000); put('<I', section+16, 0x1000); put('<I', section+20, 0x200)
    directory = 0x200
    for offset, value in ((20, 1), (24, 1), (28, 0x1040), (32, 0x1044), (36, 0x1048)):
        put('<I', directory+offset, value)
    put('<I', 0x240, 0x1200); put('<I', 0x244, 0x1060); put('<H', 0x248, 0)
    name = b'D2RHUD_MarketplaceProtocol\0'; raw[0x260:0x260+len(name)] = name
    encoded = (declaration if isinstance(declaration, str) else json.dumps(declaration)).encode()+b'\0'
    assert len(encoded) < 0xe00
    raw[0x400:0x400+len(encoded)] = encoded
    return bytes(raw)


def refuses(callback):
    try: callback()
    except (ValueError, BlockingIOError): return
    raise AssertionError('An unsupported or invalid compatibility declaration was accepted')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--hud', type=Path)
    parser.add_argument('--payload', type=Path)
    parser.add_argument('--helper', type=Path)
    parser.add_argument('--cache', type=Path)
    args = parser.parse_args()
    assert io.parse_hud_contract(pe()) == contract
    for patch in ({'schema': 2}, {'live_custody': 3}, {'game_mode': '3.x'}, {'schema': True}, {'features': []},
                  {'features': {'gift_resources': True}}, {'features': {'BAD NAME': 1}}):
        refuses(lambda patch=patch: io.parse_hud_contract(pe(dict(contract, **patch))))
    for invalid in ('null', '[]', '{"schema":1,"schema":1}', '{broken'):
        refuses(lambda invalid=invalid: io.parse_hud_contract(pe(invalid)))
    for at, fmt, value in ((0x3c, '<I', 0xfffffff0), (0x94, '<H', 4), (0x86, '<H', 97),
                           (0x200+24, '<I', 65537), (0x244, '<I', 0xffffff00), (0x248, '<H', 1),
                           (0x240, '<I', 0x1020), (0x240, '<I', 0xfffffff0)):
        bad = bytearray(pe()); struct.pack_into(fmt, bad, at, value)
        refuses(lambda bad=bad: io.parse_hud_contract(bytes(bad)))
    refuses(lambda: io.parse_hud_contract(pe()[:0x420]))
    legacy = bytearray(pe()); legacy[0x260:0x260+25] = b'OtherExportWithoutProtocol'
    assert io.parse_hud_contract(bytes(legacy)) is None
    with tempfile.TemporaryDirectory(prefix='market-hud-protocol-') as temporary:
        root = Path(temporary); game = root/'game'; saves = root/'saves'; state = root/'state'
        data = game/'Mods/RMD-MP/RMD-MP.mpq/data'
        for folder in (saves, state, data/'global/excel'): folder.mkdir(parents=True)
        for name in ('weapons', 'armor', 'misc', 'itemstatcost', 'itemtypes'):
            (data/f'global/excel/{name}.bin').write_bytes(b'fixture')
        hud = game/'d2rhud.dll'; hud.write_bytes(pe())
        helper = root/'custody-save.exe'; helper.write_bytes(b'never executed fixture helper')
        launcher = root/'Pandemonium.exe'; launcher.write_bytes(b'never executed fixture launcher')
        cfg = dict(schema=1, enrolled=True, owner='a'*32, state_directory=str(state), save_directory=str(saves),
            mod_data=str(data), game_directory=str(game), catalog=a.catalog(data), helper=str(helper),
            helper_sha256=io.digest(helper.read_bytes()), launcher_path=str(launcher),
            launcher_sha256=io.digest(launcher.read_bytes()), hud_sha256='0'*64)
        expected = cfg.copy()
        sentinel = saves/'Stash_SC_Page11.d2i'; sentinel.write_bytes(b'untouched player save')
        pending = state/'pending.json'; pending.write_bytes(b'untouched recovery journal')
        agent = a.Agent(cfg)
        assert io.hud_features(cfg, {}) == list(io.HUD_FEATURES)
        # An unrelated HUD rebuild needs neither an enrolled hash change nor a
        # new companion manifest, even with an unfinished transfer preserved.
        hud.write_bytes(pe()+b'unrelated map-event code changes')
        agent.verify(); assert io.hud_features(cfg, {}) == list(io.HUD_FEATURES)
        assert cfg == expected and sentinel.read_bytes() == b'untouched player save' and pending.read_bytes() == b'untouched recovery journal'
        future = copy.deepcopy(contract); future['features']['gift_resources'] = 2; future['features']['future_feature'] = 1
        hud.write_bytes(pe(future)); agent.verify()
        assert 'gift_resources' not in io.hud_features(cfg, {}) and 'torch_class' in io.hud_features(cfg, {})
        hud.write_bytes(pe(dict(contract, live_custody=3))); refuses(agent.verify)
        hud.write_bytes(pe()); helper.write_bytes(b'changed parser'); refuses(agent.verify)
        helper.write_bytes(b'never executed fixture helper'); launcher.write_bytes(b'changed launcher'); refuses(agent.verify)
        launcher.write_bytes(b'never executed fixture launcher')
        # Legacy HUDs retain exact enrollment pins and capabilities during rollout.
        hud.write_bytes(b'legacy fixture HUD'); refuses(agent.verify)
        cfg['hud_sha256'] = io.digest(hud.read_bytes()); agent.verify()
        assert io.hud_features(cfg, {'torch_class_hud_sha256': [cfg['hud_sha256']]}) == ['torch_class']
        reads = 0; original_read = io.read
        def counted(*args, **kwargs):
            nonlocal reads
            reads += 1
            return original_read(*args, **kwargs)
        io.read = counted
        try:
            hud.write_bytes(pe()); io.hud_contract(hud); io.hud_contract(hud); assert reads == 1
            replacement = root/'replacement.dll'; replacement.write_bytes(pe(future))
            os.utime(replacement, ns=(hud.stat().st_atime_ns, hud.stat().st_mtime_ns)); os.replace(replacement, hud)
            assert io.hud_contract(hud)['features']['gift_resources'] == 2 and reads == 2
        finally: io.read = original_read
        if args.hud:
            actual = io.hud_contract(args.hud)
            assert actual == contract, actual
            hud.write_bytes(args.hud.read_bytes()+b'future unrelated HUD rebuild')
            cfg['hud_sha256'] = '0'*64; agent.verify()
            assert io.hud_features(cfg, {}) == list(io.HUD_FEATURES)
        if args.payload:
            assert args.helper and args.cache
            builder = load('protocol_builder', repo/'tools/marketplace/build-companion.py')
            output = root/'companion.zip'; shutil.copyfile(args.payload, output)
            builder.build(args.helper, output, args.cache, '0.1.14', hud)
            before = output.read_bytes()
            hud.write_bytes(hud.read_bytes()+b'another unrelated change'); os.utime(hud, (2200000000, 2200000000))
            builder.build(args.helper, output, args.cache, '0.1.14', hud)
            assert output.read_bytes() == before, 'A HUD-only rebuild changed the companion package'
            with zipfile.ZipFile(output) as archive:
                manifest = json.loads(archive.read('manifest.json'))
                assert manifest['hud_protocol'] == 1
                if args.hud:
                    assert all(io.digest(args.hud.read_bytes()) not in hashes for hashes in manifest['features'].values())
    print('HUD protocol: compatible rebuilds without re-enrollment, exact feature versions, legacy rollout, malformed/unknown export refusal, cached reads, helper/launcher integrity and unchanged saves/journal passed.')
    if args.payload: print('HUD-only rebuilds produce a byte-identical companion package; no launcher repack is needed.')


if __name__ == '__main__': main()
