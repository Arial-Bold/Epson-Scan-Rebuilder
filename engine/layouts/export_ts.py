"""Target Size dialog (CTargetSizeDialog, UI Localized DLOG/DITL 10500) -> targetsize.json.
Labels from CTargetSizeDialog's setup: STR# 10003 #50, #79, #51, #52, #66; buttons STR# 10001."""
import json, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import paths  # noqa: E402
from rs import dlog, ditl
d = dlog(10500)
L, T, R, B = d['rect']
it = {i['n']: i for i in ditl(10500)}
def rc(n): return list(it[n]['rect'])
items = [
    dict(kind='label', rect=rc(1), text='Target Size:'),
    dict(kind='list', rect=rc(2), key='list'),
    dict(kind='button', rect=rc(3), text='Save', key='tsSave', action='ts:save'),
    dict(kind='button', rect=rc(4), text='Delete', key='tsDelete', action='ts:delete'),
    dict(kind='label', rect=rc(5), text=paths.STR('ui', 10003, 79)),
    dict(kind='edit', rect=rc(6), key='tsName', text=''),
    dict(kind='label', rect=rc(7), text='W'),
    dict(kind='edit', rect=rc(8), key='tsW', text=''),
    dict(kind='label', rect=rc(9), text='H'),
    dict(kind='edit', rect=rc(10), key='tsH', text=''),
    dict(kind='label', rect=rc(11), text='Units:'),
    dict(kind='popup', rect=rc(12), key='tsUnits', text='in.', menu=['pixels', 'mm', 'in.'], action='ts:units'),
    dict(kind='button', rect=rc(13), text='Cancel', action='ts:cancel'),
    dict(kind='button', rect=rc(14), text='OK', key='tsOK', action='ts:ok', default=True),
]
json.dump(dict(window=dict(width=R - L, height=B - T, title='Target Size'), items=items),
          open(paths.OUT + '/targetsize.json', 'w'), indent=1)
print(len(items), 'items', R - L, 'x', B - T)
