"""Image Adjustment dialog (Image Control.rsrc DLOG/DITL 1000) -> imageadjust.json."""
import json, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import paths  # noqa: E402
import icrs
d = icrs.dlog(1000)
L, T, R, B = d['rect']
items = []
def rect(r): return list(r)
names = {14: 'Brightness', 15: 'Contrast', 19: 'Saturation', 24: 'Cyan', 23: 'Red', 29: 'Magenta', 28: 'Green',
         34: 'Yellow', 33: 'Blue', 35: 'Color Balance'}
sliders = {9: 'brightness', 12: 'contrast', 17: 'saturation', 21: 'balCR', 26: 'balMG', 31: 'balYB'}
edits = {8: 'brightness', 11: 'contrast', 16: 'saturation', 20: 'balCR', 25: 'balMG', 30: 'balYB'}
icons = {4003: 'histogram', 4005: 'tonecurve', 4004: 'imageadjust', 4013: 'palette'}
for it in icrs.ditl(1000):
    n, r, c = it['n'], it['rect'], it.get('cntl')
    if it['type'] == 8:
        lab = dict(kind='label', rect=rect(r), text=names.get(n, it['text'].strip()), bold=n in (14, 15, 19, 35))
        if lab['text'] in ('High', 'Red', 'Green', 'Blue'):
            lab['align'] = 'right'                # right end of the slider above
            lab['rect'][2] = min(lab['rect'][2], 159)
        items.append(lab)
    elif c and c['proc'] == 376:
        txt = {'OK': 'Close'}.get(c['title'], c['title'])
        items.append(dict(kind='button', rect=rect(r), text=txt, action='ia:' + txt.lower()))
    elif c and c['proc'] == 144:
        items.append(dict(kind='separator', rect=rect(r)))
    elif c and c['proc'] in (32, 33):
        ic = c['max']
        items.append(dict(kind='bevel', rect=rect(r), icon=ic, action='dlg:' + icons.get(ic, str(ic)),
                          on=ic == 4004))
    elif c and c['proc'] == 53:
        items.append(dict(kind='slider', rect=rect(r), key=sliders[n], min=c['min'], max=c['max'], value=0))
    elif c and c['proc'] == 280:
        items.append(dict(kind='edit', rect=rect(r), key=edits[n] + 'Text', text='0'))
    elif c and c['proc'] == 256:
        items.append(dict(kind='ticks', rect=rect(r), count=11))
json.dump(dict(window=dict(width=R - L, height=B - T, title='Image Adjustment'), items=items),
          open(paths.OUT + '/imageadjust.json', 'w'), indent=1)
print(len(items), 'items', R - L, 'x', B - T)
