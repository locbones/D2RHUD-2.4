"""Exercise the companion's actual queued-message predicate, including old caches."""
import ast
from pathlib import Path

source=Path(__file__).resolve().parents[1]/'tools/marketplace/sync-marketplace.py'
tree=ast.parse(source.read_text(encoding='utf-8-sig'))
predicate=next(n for n in ast.walk(tree) if isinstance(n,ast.FunctionDef) and n.name=='deliver')
scope={'muted':set(),'preferences':{'game_direct':True,'game_trade':True,'game_search':False}}
exec(compile(ast.Module(body=[predicate],type_ignores=[]),str(source),'exec'),scope)
def message(kind='trade',**extra):return {'kind':kind,'sender':'seller','body':'Offer accepted',**extra}
assert scope['deliver'](message())
assert scope['deliver'](message('direct'))
assert not scope['deliver'](message(category='search',body='Saved search “Anything”: 1 new matching trade.'))
legacy=message(body='Saved search “Anything”: 1 new matching trade.')
assert not scope['deliver'](legacy) and legacy['category']=='search'
scope['preferences']['game_trade']=False
scope['preferences']['game_search']=True
assert scope['deliver'](message(body='Saved search “Ring”: 1 new matching trade.'))
assert not scope['deliver'](message())
scope['preferences']={'game_direct':True,'game_trade':True}
assert not scope['deliver'](message(body='Saved search “Legacy”: 1 new matching trade.'))
scope['muted']={'seller'}
assert not scope['deliver'](message('direct')) and not scope['deliver'](message())
print('Companion alerts: personal trades, opt-in saved searches, legacy pending notices, independent switches, missing preference defaults and muted senders passed.')
