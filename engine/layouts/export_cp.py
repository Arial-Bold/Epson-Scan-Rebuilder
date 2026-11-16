"""Color Palette dialog (Image Control.rsrc DLOG/DITL 1003) -> colorpalette.json."""
import json, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import paths  # noqa: E402
import icrs
d = icrs.dlog(1003)
L, T, R, B = d['rect']
it = {i['n']: i for i in icrs.ditl(1003)}
def rc(n): return list(it[n]['rect'])
items = []; add = items.append
for n, ic, a in ((4, 4003, 'histogram'), (5, 4005, 'tonecurve'), (6, 4004, 'imageadjust'), (7, 4013, 'palette')):
    add(dict(kind='bevel', rect=rc(n), icon=ic, action='dlg:' + a, on=ic == 4013))
add(dict(kind='separator', rect=rc(8)))
add(dict(kind='separator', rect=rc(30)))
for lab, ed, ch in ((9, 10, 'R'), (11, 12, 'G'), (13, 14, 'B')):
    add(dict(kind='label', rect=rc(lab), text=ch + ':'))
    add(dict(kind='edit', rect=rc(ed), key='pal' + ch, text='192'))
add(dict(kind='bevel', rect=rc(15), icon=4014, action='cp:set',
         tip=paths.STR('imagecontrol', 5003, 15)))
add(dict(kind='bevel', rect=rc(16), icon=4015, action='cp:get', tip=paths.STR('imagecontrol', 5003, 16)))
add(dict(kind='palette', rect=rc(17), key='palette'))
add(dict(kind='bevel', rect=rc(18), icon=4016, action='cp:load', tip=paths.STR('imagecontrol', 5003, 18)))
add(dict(kind='bevel', rect=rc(19), icon=4017, action='cp:save', tip=paths.STR('imagecontrol', 5003, 19)))
add(dict(kind='colorstore', rect=rc(20), key='store', cells=[rc(n) for n in range(21, 30)]))
add(dict(kind='button', rect=rc(1), text='Close', action='cp:close'))
add(dict(kind='button', rect=rc(31), text='Reset', key='cpReset', action='cp:reset'))
add(dict(kind='button', rect=rc(3), text='Help', action='cp:help'))
json.dump(dict(window=dict(width=R - L, height=B - T, title='Color Palette'), items=items),
          open(paths.OUT + '/colorpalette.json', 'w'), indent=1)
print(len(items), 'items', R - L, 'x', B - T)
