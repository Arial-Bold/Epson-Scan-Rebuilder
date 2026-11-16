import json, shutil, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import paths  # noqa: E402
from rs import ditl
OUT = paths.OUT
R = lambda it: list(it['rect'])
def item(kind, rect, **kw): return dict(kind=kind, rect=rect, **kw)
m = ditl(10300); t = ditl(10301); th = ditl(10302)
items = [
    item('tabs', R(m[0]), tabs=['Full Preview', 'Zoom'], value=0, action='pvTab', key='pvTabs'),
    # left tool column (Full Preview tab)
    item('bevel', R(t[0]), icon=10015, text='Zoom', action='pvZoom', key='pvZoom', tip=paths.STR('ui', 10017, 17)),
    item('label', R(t[1]), text='Marquee', bold=True, key='pvL1'), item('separator', R(t[2]), key='pvS1'),
    item('bevel', R(t[3]), icon=10020, action='pvDelete', key='pvDelete', tip=paths.STR('ui', 10017, 20)),
    item('bevel', R(t[4]), icon=10019, action='pvCopy', key='pvCopy', tip=paths.STR('ui', 10017, 21)),
    item('bevel', R(t[5]), icon=10018, action='pvLocate', key='pvLocate', tip=paths.STR('ui', 10017, 22)),
    item('button', R(t[7]), text='All', action='pvAll', key='pvAll', tip=paths.STR('ui', 10017, 24)),
    item('label', R(t[8]), text='Rotation', bold=True, key='pvL2'), item('separator', R(t[9]), key='pvS2'),
    item('bevel', R(t[10]), symbol='rotate.left', action='pvRotL', key='pvRotL', tip=paths.STR('ui', 10017, 27)),
    item('bevel', R(t[11]), symbol='rotate.right', action='pvRotR', key='pvRotR', tip=paths.STR('ui', 10017, 28)),
    # left tool column (Thumbnail tab, DITL 10302): frame tools, then the thumbnail size
    item('label', R(th[0]), text='Frame', bold=True, key='tbL1'), item('separator', R(th[1]), key='tbS1'),
    item('bevel', R(th[2]), icon=10017, action='tbRotate', key='tbRotate', tip=paths.STR('ui', 10017, 31)),
    item('bevel', R(th[3]), icon=10016, action='tbMirror', key='tbMirror', tip=paths.STR('ui', 10017, 32)),
    item('bevel', R(th[4]), icon=10020, action='tbDelete', key='tbDelete', tip=paths.STR('ui', 10017, 20)),
    item('button', R(th[5]), text='All', action='tbAll', key='tbAll', tip=paths.STR('ui', 10017, 34)),
    item('label', R(th[6]), text='Thumbnail', bold=True, key='tbL2'), item('separator', R(th[7]), key='tbS2'),
    item('bevel', R(th[8]), icon=10041, action='tbLarge', key='tbLarge', tip=paths.STR('ui', 10017, 37)),
    item('bevel', R(th[9]), icon=10040, action='tbSmall', key='tbSmall', tip=paths.STR('ui', 10017, 38)),
    item('bevel', R(th[10]), icon=10042, action='tbFull', key='tbFull', tip=paths.STR('ui', 10017, 39)),
    item('bevel', [24, 268, 54, 298], icon=10028, action='tbPrev', key='tbPrev'),
    item('bevel', [58, 268, 88, 298], icon=10027, action='tbNext', key='tbNext'),
    item('label', [24, 302, 88, 316], text='', key='tbFrameText'),
    # preview image area (inside the tab panel, right of the tools)
    item('canvas', [110, 46, 500, 505]),
    # bottom tool row
    item('bevel', R(m[1]), icon=10043, action='pvHelp', tip=paths.STR('ui', 10017, 2)),
    item('bevel', R(m[2]), icon=10025, action='pvDensi', key='pvDensi', tip=paths.STR('ui', 10000, 4)),
    item('bevel', R(m[3]), icon=10000, key='pvAF', tip=paths.STR('ui', 10017, 4)),
    item('bevel', R(m[4]), icon=10001, key='pvMF', tip=paths.STR('ui', 10017, 5)),
    item('label', R(m[6]), text='Focus Position: 0.0', key='pvFocusText'),
    # status bar
    item('placard', R(m[7])), item('placard', R(m[12])), item('placard', R(m[14])),
    item('icon', R(m[8]), icon=10024), item('label', R(m[9]), text='1.42 in', tag='statusW'),
    item('icon', R(m[10]), icon=10023), item('label', R(m[11]), text='0.94 in', tag='statusH'),
    item('label', R(m[13]), text='4544 x 3008 pixels  78.2 MB', tag='statusSize'),
    item('label', R(m[15]), text='100%', tag='statusZoom'),
]
# resize behaviour: tabs + preview area grow; bottom toolbar and status bar stay at the bottom
for it in items:
    k, t = it['kind'], it.get('tag')
    if k in ('tabs', 'canvas'):
        it['grow'] = 'both'
    elif it['rect'][1] >= 520:
        it['pin'] = 'bottom'
for it in items:
    if it['kind'] == 'placard' and it['rect'][0] == 196: it['grow'] = 'width'
    if it['kind'] == 'placard' and it['rect'][0] == 421: it['right'] = True
    if it.get('tag') == 'statusZoom': it['right'] = True
# the focus button positions overlap in the resource (they swap at runtime); use the first two
layout = dict(window=dict(width=520, height=600, title='Preview', resizable=True), items=items)
json.dump(layout, open(f'{OUT}/preview.json', 'w'), indent=1)
print('preview items', len(items))
