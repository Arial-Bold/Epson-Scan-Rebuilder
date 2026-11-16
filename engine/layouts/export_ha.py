"""Histogram Adjustment dialog (Image Control.rsrc DLOG/DITL 1001) -> histadjust.json."""
import json, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import paths  # noqa: E402
import icrs
d = icrs.dlog(1001)
L, T, R, B = d['rect']
it = {i['n']: i for i in icrs.ditl(1001)}
def rc(n): return list(it[n]['rect'])
items = []
add = items.append
# top: dialog switcher bevels
for n, ic, a in ((43, 4003, 'histogram'), (41, 4005, 'tonecurve'), (5, 4004, 'imageadjust'), (45, 4013, 'palette')):
    add(dict(kind='bevel', rect=rc(n), icon=ic, action='dlg:' + a, on=ic == 4003))
for n in (40, 26, 34, 39):
    add(dict(kind='separator', rect=rc(n)))
# channel
add(dict(kind='label', rect=rc(6), text='Channel', bold=True))
add(dict(kind='separator', rect=rc(46)))
for k, (rn, pn, pict) in enumerate(((11, 7, 3007), (12, 8, 3008), (13, 9, 3009), (14, 10, 3010))):
    add(dict(kind='radio', rect=rc(rn), text='', key=f'ch{k}', action=f'ha:ch{k}', on=k == 0))
    add(dict(kind='image', rect=rc(pn), image=f'pict_{pict}.png'))
for n, t in ((47, 'R'), (48, 'G'), (49, 'B')):
    add(dict(kind='label', rect=rc(n), text=t))
# histogram + input/output indicators (runtime sizes: 237 x 108 view, 235-wide arrow bars)
add(dict(kind='histogram', rect=[112, 52, 349, 160], key='histogram'))
add(dict(kind='levelarrows', rect=[107, 160, 352, 172], key='inArrows', count=3))
add(dict(kind='label', rect=rc(27), text='Input:'))
add(dict(kind='edit', rect=rc(19), key='inSD', text='0'))
add(dict(kind='edit', rect=rc(20), key='inGamma', text='1.00'))
add(dict(kind='edit', rect=rc(21), key='inHL', text='255'))
add(dict(kind='bevel', rect=rc(22), icon=4000, action='ha:pipShadow', tip=paths.STR('imagecontrol', 5002, 22)))
add(dict(kind='bevel', rect=rc(44), icon=4002, action='ha:pipGamma', tip=paths.STR('imagecontrol', 5002, 44)))
add(dict(kind='bevel', rect=rc(23), icon=4001, action='ha:pipHighlight', tip=paths.STR('imagecontrol', 5002, 23)))
add(dict(kind='image', rect=[112, 216, 347, 224], image='pict_3014.png', key='gradient'))
add(dict(kind='levelarrows', rect=[107, 224, 352, 236], key='outArrows', count=2))
add(dict(kind='label', rect=rc(28), text='Output:'))
add(dict(kind='edit', rect=rc(24), key='outSD', text='0'))
add(dict(kind='edit', rect=rc(25), key='outHL', text='255'))
# tone curve viewer
add(dict(kind='label', rect=rc(29), text=paths.STR('imagecontrol', 5006, 9), bold=True))
add(dict(kind='curveview', rect=rc(30), key='curve'))
add(dict(kind='curvebutton', rect=rc(31), key='sdCurve', icon=258, menu=['Boost', 'Normal', 'Soft'],
         tip=paths.STR('imagecontrol', 5002, 31)))
add(dict(kind='curvebutton', rect=rc(32), key='hlCurve', icon=261, menu=['Boost', 'Normal', 'Soft'],
         tip=paths.STR('imagecontrol', 5002, 32)))
add(dict(kind='button', rect=rc(33), text='Show Output', key='showOutput', hold=True,
         tip=paths.STR('imagecontrol', 5002, 33)))
# gray balance
add(dict(kind='label', rect=rc(35), text=paths.STR('imagecontrol', 5006, 6), bold=True))
add(dict(kind='image', rect=[100, 476, 232, 484], image='pict_3023.png'))
add(dict(kind='slider', rect=rc(36), key='grayBalance', min=0, max=100, value=100))
add(dict(kind='edit', rect=rc(38), key='grayBalanceText', text='100'))
add(dict(kind='bevel', rect=rc(37), icon=4002, action='ha:pipGray',
         tip=paths.STR('imagecontrol', 5002, 37)))
# buttons
add(dict(kind='button', rect=rc(1), text='Close', action='ha:close'))
add(dict(kind='button', rect=rc(2), text='Reset', action='ha:reset'))
add(dict(kind='button', rect=rc(3), text='Help', action='ha:help'))
json.dump(dict(window=dict(width=R - L, height=B - T, title='Histogram Adjustment'), items=items),
          open(paths.OUT + '/histadjust.json', 'w'), indent=1)
print(len(items), 'items', R - L, 'x', B - T)
