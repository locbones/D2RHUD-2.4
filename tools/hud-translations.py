"""Extract, validate and optionally machine-seed editable HUD language packs.

Translation is an explicit authoring step (--translate), never a game feature.
Only public interface strings are sent to Google Translate when requested.
"""
import argparse
import ast
import concurrent.futures
import json
from pathlib import Path
import re
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
PACKS = ROOT / 'translations'
LANGUAGES = {
    'enUS': ('English', 'en'), 'deDE': ('Deutsch', 'de'),
    'esES': ('Español (España)', 'es'), 'esMX': ('Español (Latinoamérica)', 'es'),
    'frFR': ('Français', 'fr'), 'itIT': ('Italiano', 'it'),
    'jaJP': ('日本語', 'ja'), 'koKR': ('한국어', 'ko'),
    'plPL': ('Polski', 'pl'), 'ptBR': ('Português (Brasil)', 'pt'),
    'ruRU': ('Русский', 'ru'), 'zhCN': ('简体中文', 'zh-CN'),
    'zhTW': ('繁體中文', 'zh-TW'),
}
FORMAT = re.compile(r'%(?:[-+ #0]*)(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|I64|I32|[hljztL])?[diuoxXfFeEgGaAcsp%]')
TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|(?:u8)?"(?:\\.|[^"\\])*"')

def sources():
    plugin = ROOT / 'd2rhud/plugin'
    files = set((plugin / 'D2RHUD').glob('*'))
    files.update(plugin.glob('*UI*'))
    files.update(plugin.glob('ItemFilter/*UI*'))
    for name in ['SettingsUI.h', 'JournalSkin.h', 'GrailTracker.cpp', 'FloatingDamage.cpp',
                 'BuffIcons.cpp', 'StatRanges.cpp', 'QuestJournal.cpp', 'QuestJournalEvents.inl', 'HUDControls.cpp',
                 'CubeMapData.h', 'CubeMap.cpp', 'CubeMapRewardsData.h', 'CandyCornSaveUI.cpp',
                 'ItemFilter/LootFilterFields.h', 'ItemFilter/LootFilterPreview.h']:
        files.add(plugin / name)
    return sorted(p for p in files if p.suffix in ['.cpp', '.h', '.inl'] and p.exists())

def extract(quests):
    strings = set()
    def add(text):
        for part in text.split('\0'):
            part = part.split('##', 1)[0]
            if not part or not re.search(r'[A-Za-z]{2}', part): continue
            if re.match(r'^[a-z_0-9.]+$', part): continue
            if re.match(r'^[A-Za-z_]+[A-Z][a-zA-Z_0-9]*$', part) and ' ' not in part: continue
            if re.search(r'\.(?:png|dds|bin|txt|json|lua|dll|exe|otf|ttf|csv|wav|mp3)$', part, re.I): continue
            if '\\' in part or '://' in part or len(part) > 12000: continue
            strings.add(part)
    for path in sources():
        text = path.read_text(encoding='utf-8-sig')
        tokens = list(TOKEN.finditer(text)); i = 0
        while i < len(tokens):
            token = tokens[i]
            if not token.group().startswith(('"', 'u8"')): i += 1; continue
            # Ignore include paths; join C++ adjacent string literals.
            if text[text.rfind('\n', 0, token.start())+1:token.start()].lstrip().startswith('#include'):
                i += 1; continue
            value = ''; end = token.start()
            while i < len(tokens) and tokens[i].group().startswith(('"', 'u8"')):
                if value and text[end:tokens[i].start()].strip(): break
                literal = tokens[i].group().removeprefix('u8')
                try: value += ast.literal_eval(literal)
                except (SyntaxError, ValueError): pass
                end = tokens[i].end(); i += 1
            add(value)
    if quests:
        document = json.loads(Path(quests).read_text(encoding='utf-8-sig'))
        for quest in document['quests']:
            for field in ['title', 'objective', 'tips', 'reward', 'turnIn', 'objectiveRecipes', 'rewardRecipes']:
                value = quest.get(field, '')
                for entry in value if isinstance(value, list) else [value]: add(entry)
    # Dynamic headings use complete translatable templates.
    for text in ['HUD language', 'Follow game language', 'Language and translations', 'Reload translations',
                 'Open translation folder', 'Open translation editor', 'Language changes apply after restarting the game.',
                 'Edit a language JSON file, then reload translations. Missing text falls back to English.',
                 'Machine-translated starter; native-speaker review is welcome.', 'Quest %d', 'Open HUD Menu',
                 '%s  /  %d of %d quests completed', '%u / %d monsters']:
        add(text)
    PACKS.mkdir(exist_ok=True)
    save('enUS', {key: key for key in sorted(strings)})
    print(f'Extracted {len(strings)} English entries.', flush=True)

def save(locale, strings, reviewed=None):
    document = {'_meta': {'locale': locale, 'name': LANGUAGES[locale][0], 'version': 1,
                          'origin': 'English source' if locale == 'enUS' else 'machine-translated starter; community review needed'},
                'strings': dict(sorted(strings.items())), 'reviewed': reviewed or []}
    (PACKS / f'{locale}.json').write_text(json.dumps(document, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')

def seed(locale):
    english = json.loads((PACKS / 'enUS.json').read_text(encoding='utf-8'))['strings']
    path = PACKS / f'{locale}.json'
    old = json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}
    strings = {key: value for key, value in old.get('strings', {}).items() if key in english}
    pending = [key for key in english if key not in strings]
    batches = []; batch = []; size = 0
    for key in pending:
        cost = len(urllib.parse.quote(key))
        if batch and (size+cost > 7500 or len(batch) >= 45): batches.append(batch); batch = []; size = 0
        batch.append(key); size += cost+5
    if batch: batches.append(batch)
    for index, batch in enumerate(batches):
        # Preserve format arguments exactly, including their ordering and types.
        protected = []; placeholders = []
        for key in batch:
            tokens = []
            def mask(match):
                tokens.append(match.group()); return f'ZXQPH{len(tokens)-1}QXZ'
            protected.append(FORMAT.sub(mask, key)); placeholders.append(tokens)
        query = [('client', 'gtx'), ('sl', 'en'), ('tl', LANGUAGES[locale][1])] + [('q', text) for text in protected]
        url = 'https://translate.googleapis.com/translate_a/t?' + urllib.parse.urlencode(query)
        for attempt in range(5):
            try:
                request = urllib.request.Request(url, headers={'User-Agent': 'D2RHUD-translation-authoring/1.0'})
                with urllib.request.urlopen(request, timeout=45) as response: values = json.loads(response.read())
                assert isinstance(values, list) and len(values) == len(batch)
                break
            except Exception:
                if attempt == 4: raise
                time.sleep(2**attempt)
        for key, value, tokens in zip(batch, values, placeholders):
            if not isinstance(value, str): raise ValueError(f'Unexpected translation for {locale}')
            for n, token in enumerate(tokens): value = value.replace(f'ZXQPH{n}QXZ', token)
            if not value or '\0' in value or '##' in value or key.count('%')!=value.count('%') or FORMAT.findall(value) != FORMAT.findall(key) or re.search(r'ZXQPH\d', value):
                # Keep a usable English fallback, visibly unreviewed in the editor.
                value = key
            strings[key] = value
        save(locale, strings, old.get('reviewed'))
        print(f'{locale}: {index+1}/{len(batches)} batches', flush=True)
        time.sleep(.12)
    save(locale, strings, old.get('reviewed'))

def validate():
    english = json.loads((PACKS / 'enUS.json').read_text(encoding='utf-8'))['strings']
    for locale in LANGUAGES:
        document = json.loads((PACKS / f'{locale}.json').read_text(encoding='utf-8'))
        assert document['_meta']['locale'] == locale
        assert document['strings'].keys() == english.keys(), f'{locale}: missing or obsolete entries'
        for key, value in document['strings'].items():
            assert isinstance(value, str) and value and '\0' not in value
            assert FORMAT.findall(key) == FORMAT.findall(value) and key.count('%')==value.count('%'), f'{locale}: unsafe placeholders: {key}'
            assert '##' not in value and not re.search(r'ZXQPH\d', value)
        print(f'{locale}: {len(english)} valid entries', flush=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extract', action='store_true'); parser.add_argument('--quests')
    parser.add_argument('--translate', action='store_true'); parser.add_argument('--validate', action='store_true')
    args = parser.parse_args()
    if args.extract: extract(args.quests)
    if args.translate:
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            list(pool.map(seed, [locale for locale in LANGUAGES if locale != 'enUS']))
    if args.validate: validate()
