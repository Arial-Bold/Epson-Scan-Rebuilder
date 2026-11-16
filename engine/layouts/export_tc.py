"""Tone Correction dialog (Image Control.rsrc DLOG/DITL 1002) -> tonecurve.json."""
import json, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import paths  # noqa: E402
import icrs
d = icrs.dlog(1002)
L, T, R, B = d['rect']
it = {i['n']: i for i in icrs.ditl(1002)}
def rc(n): return list(it[n]['rect'])
items = []; add = items.append
for n, ic, a in ((6, 4003, 'histogram'), (29, 4005, 'tonecurve'), (5, 4004, 'imageadjust'), (30, 4013, 'palette')):
    add(dict(kind='bevel', rect=rc(n), icon=ic, action='dlg:' + a, on=ic == 4005))
add(dict(kind='separator', rect=[8, 44, 340, 45]))
add(dict(kind='separator', rect=[8, 366, 340, 367]))
add(dict(kind='label', rect=rc(7), text=paths.STR('imagecontrol', 5007, 2), bold=True))
add(dict(kind='separator', rect=rc(31)))
add(dict(kind='edit', rect=rc(28), key='toneName', text=''))
add(dict(kind='popup', rect=rc(8), text='', menu=['Linear'], key='tonePopup', action='tc:pick'))
add(dict(kind='button', rect=rc(9), text='Save', key='toneSave', action='tc:save'))
add(dict(kind='button', rect=rc(10), text='Delete', key='toneDelete', action='tc:delete'))
add(dict(kind='label', rect=rc(15), text='Channel', bold=True))
add(dict(kind='separator', rect=rc(32)))
for k, (rn, pn, pict) in enumerate(((11, 22, 3007), (12, 23, 3008), (13, 24, 3009), (14, 25, 3010))):
    add(dict(kind='radio', rect=rc(rn), text='', key=f'ch{k}', action=f'tc:ch{k}', on=k == 0))
    add(dict(kind='image', rect=rc(pn), image=f'pict_{pict}.png'))
for n, t in ((33, 'R'), (34, 'G'), (35, 'B')):
    add(dict(kind='label', rect=rc(n), text=t))
add(dict(kind='toneview', rect=rc(16), key='toneView'))
add(dict(kind='image', rect=rc(26), image='pict_3019.png', key='hgrad'))
add(dict(kind='image', rect=rc(27), image='pict_3015.png', key='vgrad'))
add(dict(kind='label', rect=rc(17), text='Input:'))
add(dict(kind='edit', rect=rc(19), key='tcIn', text=''))
add(dict(kind='label', rect=rc(18), text='Output:'))
add(dict(kind='edit', rect=rc(20), key='tcOut', text=''))
add(dict(kind='button', rect=rc(1), text='Close', action='tc:close'))
add(dict(kind='button', rect=rc(2), text='Reset', action='tc:reset'))
add(dict(kind='button', rect=rc(3), text='Help', action='tc:help'))
json.dump(dict(window=dict(width=R - L, height=B - T, title='Tone Correction'), items=items),
          open(paths.OUT + '/tonecurve.json', 'w'), indent=1)
print(len(items), 'items', R - L, 'x', B - T)
