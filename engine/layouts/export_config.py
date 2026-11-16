"""Configuration (CConfigDialog, DLOG/DITL 10400 + tab DITLs appended in overlay mode:
10401 Preview = items 5-19, 10402 Color = 20-35, 10404 Other = 36-49; 10403 ADF and 10405
Film Size are not shown for the V550 (no duplex ADF; Medium Format Film Size pref is -1 in ESPR).
Texts from CConfigDialog::InstallUserItemDrawProcs. Other tab: no Vertical banding / Show Texture
on the V550, so LocateOtherTabItem moves items 36-43 up 36."""
import json, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import paths  # noqa: E402
import rs
items = []
base = {i['n']: i for i in rs.ditl(10400)}
tabs = [rs.ditl(10401), rs.ditl(10402), rs.ditl(10404)]
num = 5
G = {}
for t, lst in enumerate(tabs):
    for i in lst:
        G[num] = (t, list(i['rect'])); num += 1
def R(n, dy=0):
    r = G[n][1]; return [r[0], r[1] + dy, r[2], r[3] + dy]
def add(n, kind, dy=0, **kw):
    d = dict(kind=kind, rect=R(n, dy), tab=G[n][0], key=kw.pop('key', 'i%d' % n)); d.update(kw); items.append(d)

items.append(dict(kind='tabs', rect=list(base[4]['rect']), key='tabs', tabs=['Preview', 'Color', 'Other'], value=0,
                  action='cfg:tab'))
# Preview (5-19)
add(5, 'check', text=paths.STR('ui', 10002, 40), key='rotation')
add(6, 'label', text='Units:')
add(7, 'popup', key='units', text='in.', menu=['mm', 'in.'])
add(8, 'check', text='Quality Preview', key='quality')
add(9, 'label', text=paths.STR('ui', 10003, 65))
add(10, 'popup', key='denseArea', text='1 x 1 pixel', menu=['1 x 1 pixel', '3 x 3 pixels', '5 x 5 pixels'])
add(11, 'label', text=paths.STR('ui', 10003, 64))
add(12, 'popup', key='sampArea', text='1 x 1 pixel', menu=['1 x 1 pixel', '3 x 3 pixels', '5 x 5 pixels'])
add(13, 'separator')
add(14, 'check', text=paths.STR('ui', 10002, 42), key='autoRotate')
add(15, 'label', text=paths.STR('ui', 10003, 67))
add(16, 'slider', key='dtrMargin', min=1, max=3, value=1)
add(17, 'label', text='Small')
add(19, 'label', text='Large', align='right')
# Color (20-35)
add(20, 'radio', text='Color Control', key='ccColor', action='cfg:cc')
add(21, 'check', text=paths.STR('ui', 10002, 47), key='contAE')
add(22, 'label', text='Display Gamma:')
add(23, 'popup', key='gamma', text='2.2', menu=['1.8', '2.2'])
add(24, 'radio', text='ColorSync', key='ccSync', action='cfg:cs')
add(25, 'label', text='Source (Scanner):')
add(26, 'popup', key='source', text='EPSON Standard', menu=['EPSON Standard'])
add(27, 'label', text='Target:')
add(28, 'popup', key='target', text='sRGB IEC61966-2.1', menu=['sRGB IEC61966-2.1'])
add(29, 'check', text=paths.STR('ui', 10002, 48), key='useMonitor')
add(30, 'radio', text=paths.STR('ui', 10002, 46), key='ccNone', action='cfg:none')
add(31, 'slider', key='expLevel', min=1, max=7, value=4)
add(32, 'label', text=paths.STR('ui', 10003, 89))
add(33, 'label', text='Low')
add(34, 'label', text='High', align='right')
add(35, 'button', text='Recommended Value', key='recommend', action='cfg:recommend')
# Other (36-49): 36-43 moved up 36
add(44, 'check', text='', key='skew')
add(45, 'label', text=paths.STR('ui', 10002, 70))
items[-1]['rect'][3] = items[-1]['rect'][1] + 16
add(43, 'separator', dy=-36)
add(36, 'check', text='Save settings', key='saveSettings', dy=-36)
add(37, 'label', text='Work Area:', dy=-36)
add(38, 'popup', key='workArea', text='', menu=[''], action='cfg:vol', dy=-36)
add(39, 'label', text='Free Space:', dy=-36)
add(40, 'label', text='', key='freeSpace', dy=-36)
add(41, 'label', text=paths.STR('ui', 10003, 76), dy=-36)
add(42, 'button', text='Reset All', key='resetAll', action='cfg:reset', dy=-36)
for n, text, act, key in ((3, 'Help', 'cfg:help', None), (2, 'Cancel', 'cfg:cancel', None), (1, 'OK', 'cfg:ok', None)):
    d = dict(kind='button', rect=list(base[n]['rect']), text=text, action=act)
    if n == 1: d['default'] = True
    items.append(d)
d = rs.dlog(10400); L, T, Rr, B = d['rect']
json.dump(dict(window=dict(width=Rr - L, height=B - T, title='Configuration'), items=items),
          open(paths.OUT + '/config.json', 'w'), indent=1)
print(len(items), 'items', Rr - L, 'x', B - T)
