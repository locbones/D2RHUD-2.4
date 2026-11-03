"""Package an already-built HUD, guarded launcher and save companion, without credentials."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--hud',type=Path,required=True)
p.add_argument('--launcher',type=Path,required=True)
p.add_argument('--helper',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
root=a.output.resolve()
root.mkdir(parents=True,exist_ok=False)
source=Path(__file__).resolve().parent
files={'game/d2rhud.dll':a.hud,'game/lootfilter.lua':source.parents[1]/'lootfilter.lua','launcher/Pandemonium.exe':a.launcher,'tools/custody-save.exe':a.helper,'install-player.ps1':source/'install-player.ps1'}
for name in ['custody-agent.py','custody-workspace.py','sync-marketplace.py','sync-dreamhost.ps1','start-marketplace-sync.ps1','stop-marketplace-sync.ps1','start-custody-agent.ps1','start-marketplace.ps1','background-marketplace.py','open-marketplace.ps1','install-custody.ps1','install-native-layout.py','revalidate-custody.ps1']:
    files['tools/'+name]=source/name
for name in ['stash-shop-button.json','marketplace-workspace.json']:
    files['tools/layouts/'+name]=source/'layouts'/name
for name in ['README.md','CUSTODY.md','WEB.md']:
    files['documentation/'+name]=source/name
for relative,path in files.items():
    target=root/relative
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(path,target)
(root/'INSTALL.txt').write_text('RMD-MP Marketplace 2.4\n\nWindows and Python 3.10+ are required. Close D2R and Pandemonium for installation.\nObtain your own player and agent pairing JSON files privately from the operator.\nRun install-player.ps1 with -GameDirectory, -LauncherPath, -PlayerPairing and -AgentPairing.\nThe installer validates the release, backs up existing binaries and saves, installs the native\nJSON controls and guarded companion, and creates the Marketplace website desktop shortcut.\nChoose separate stash pages in game Setup. View another page while deposits or deliveries sync.\nLink the website with a code from Pandemonium Settings > Marketplace > Get link code. No password is needed.\nNever share pairing files or recovery journals. See documentation/ for operation and recovery.\n',encoding='utf-8')
manifest={'schema':1,'game_mode':'2.4','mod':'RMD-MP','files':{}}
for path in sorted(root.rglob('*')):
    if path.is_file():
        manifest['files'][path.relative_to(root).as_posix()]=hashlib.sha256(path.read_bytes()).hexdigest()
(root/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
archive=root.parent/(root.name+'.zip')
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED) as z:
    for path in sorted(root.rglob('*')):
        if path.is_file():z.write(path,path.relative_to(root).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    for relative,digest in manifest['files'].items():assert hashlib.sha256(z.read(relative)).hexdigest()==digest
print(json.dumps({'archive':str(archive),'files':len(manifest['files']),'sha256':hashlib.sha256(archive.read_bytes()).hexdigest()}))
