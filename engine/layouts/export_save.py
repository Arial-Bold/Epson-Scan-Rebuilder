"""File Save Settings (CFileSaveDialog, DLOG/DITL 10900) -> filesave.json;
EPSON TIFF Plug-in Settings (TIFF.rsrc DLOG 10001) -> tiffopts.json;
EPSON JPEG Plug-in Settings (JPEG.rsrc DLOG 10000) -> jpegopts.json.
Texts from InstallUserItemDrawProcs: STR# 10001 (buttons), 10002 #33-38/#65/#71, 10003 #1-4;
plug-in texts from their STR# 2000."""
import json, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import paths  # noqa: E402
import rs
OUT = paths.OUT + '/'

def dump(name, d, items, title):
    L, T, R, B = d['rect']
    json.dump(dict(window=dict(width=R - L, height=B - T, title=title), items=items),
              open(OUT + name, 'w'), indent=1)
    print(name, len(items), 'items', R - L, 'x', B - T)

# ---- File Save Settings
it = {i['n']: i for i in rs.ditl(10900)}
def rc(n): return list(it[n]['rect'])
items = [
    dict(kind='label', rect=rc(4), text='Location', bold=True),
    dict(kind='separator', rect=rc(26)[:2] + [490, rc(26)[3]]),
    dict(kind='radio', rect=rc(23), text='Documents', key='fsDocuments', action='fs:documents'),
    dict(kind='radio', rect=rc(24), text='Pictures', key='fsPictures', action='fs:pictures'),
    dict(kind='radio', rect=rc(25), text='Other:', key='fsOther', action='fs:other'),
    dict(kind='path', rect=rc(5), key='fsPath'),
    dict(kind='button', rect=[rc(7)[0] + 2, 73, rc(7)[2], 93], text='Choose...', key='fsChoose', action='fs:choose'),
    dict(kind='label', rect=rc(8), text=paths.STR('ui', 10002, 34), bold=True),
    dict(kind='separator', rect=rc(27)[:2] + [490, rc(27)[3]]),
    dict(kind='label', rect=rc(9), text='Prefix:'),
    dict(kind='edit', rect=rc(10), key='fsPrefix', text='img'),
    dict(kind='label', rect=rc(11), text='Start Number:'),
    dict(kind='edit', rect=rc(12), key='fsStart', text='001'),
    dict(kind='stepper', rect=rc(13), key='fsArrows', action='fs:step'),
    dict(kind='label', rect=rc(14), text='Image Format', bold=True),
    dict(kind='separator', rect=rc(28)[:2] + [490, rc(28)[3]]),
    dict(kind='label', rect=rc(15), text='Type:'),
    dict(kind='popup', rect=rc(16), key='fsType', text='JPEG (*.jpg)', menu=['EPSON EXIF&PRINT Image Matching II (JPEG) (*.jpg)', 'EPSON EXIF&PRINT Image Matching II (TIFF) (*.tif)', 'JPEG (*.jpg)', 'Multi-TIFF (*.tif)', 'PDF (*.pdf)', 'PICT(*.pct)', 'TIFF (*.tif)'],
         action='fs:type'),
    dict(kind='button', rect=rc(17), text='Options...', key='fsOptions', action='fs:options'),
    dict(kind='label', rect=rc(18), text='Details:'),
    dict(kind='details', rect=rc(29), inner=rc(19), key='fsDetails'),
    dict(kind='check', rect=rc(20), text=paths.STR('ui', 10002, 36), key='fsOverwrite'),
    dict(kind='check', rect=rc(21), text=paths.STR('ui', 10002, 37), key='fsShow'),
    dict(kind='check', rect=rc(22), text=paths.STR('ui', 10002, 38), key='fsOpen'),
    dict(kind='check', rect=rc(30), text=paths.STR('ui', 10002, 71), key='fsAddPage'),
    dict(kind='button', rect=rc(3), text='Help', action='fs:help'),
    dict(kind='button', rect=rc(2), text='Cancel', action='fs:cancel'),
    dict(kind='button', rect=rc(1), text='OK', action='fs:ok', default=True),
]
dump('filesave.json', rs.dlog(10900), items, paths.STR('ui', 10000, 8))

base = paths.FILE_FORMAT + '/'
def plug(n):
    rs.L = paths.rsrc(base + n + '.bundle/Contents/Resources/English.lproj/Localized.rsrc')
    rs.U = paths.rsrc(base + n + '.bundle/Contents/Resources/' + n + '.rsrc')
    return {i['n']: i for i in rs.ditl({'TIFF': 10001, 'JPEG': 10000}[n])}

# ---- TIFF options: the radio's text is drawn by the static item beside it
it = plug('TIFF')
items = [
    dict(kind='label', rect=rc(8), text='Byte Order', bold=True),
    dict(kind='separator', rect=rc(12)),
    dict(kind='radio', rect=rc(5), text='Macintosh', key='tMac', action='t:mac'),
    dict(kind='radio', rect=rc(7), text='Windows', key='tWin', action='t:win'),
    dict(kind='label', rect=rc(9), text='Compression', bold=True),
    dict(kind='separator', rect=rc(13)),
    dict(kind='label', rect=rc(10), text='B&W:'),
    dict(kind='popup', rect=rc(11), key='tBW', text='None', menu=['None', 'CCITT Group4']),
    dict(kind='check', rect=rc(14), text=paths.STR('jpeg', 2000, 13), key='tICC'),
    dict(kind='button', rect=rc(1), text='OK', action='t:ok', default=True),
    dict(kind='button', rect=rc(2), text='Cancel', action='t:cancel'),
    dict(kind='button', rect=rc(3), text='Help', action='t:help'),
]
dump('tiffopts.json', rs.dlog(10001), items, paths.STR('tiff', 2000, 1))

# ---- JPEG options
it = plug('JPEG')
items = [
    dict(kind='label', rect=rc(14), text=paths.STR('jpeg', 2000, 11), bold=True),
    dict(kind='separator', rect=rc(16)),
    dict(kind='label', rect=rc(4), text='Compression Level:'),
    dict(kind='edit', rect=rc(5), key='jLevel', text='16'),
    dict(kind='label', rect=rc(6), text='(1-100)'),
    dict(kind='slider', rect=rc(7), key='jSlider', min=1, max=100, value=16),
    dict(kind='label', rect=rc(8), text='High Quality'),
    dict(kind='label', rect=rc(9), text='Low Quality', align='right'),
    dict(kind='label', rect=rc(15), text='Encoding', bold=True),
    dict(kind='separator', rect=rc(17)),
    dict(kind='radio', rect=rc(11), text='Standard', key='jStd', action='j:std'),
    dict(kind='radio', rect=rc(13), text='Progressive', key='jProg', action='j:prog'),
    dict(kind='check', rect=rc(18), text=paths.STR('jpeg', 2000, 13), key='jICC'),
    dict(kind='button', rect=rc(1), text='OK', action='j:ok', default=True),
    dict(kind='button', rect=rc(2), text='Cancel', action='j:cancel'),
    dict(kind='button', rect=rc(3), text='Help', action='j:help'),
]
dump('jpegopts.json', rs.dlog(10000), items, paths.STR('jpeg', 2000, 1))

# ---- CMsgScnContinueDlg (DITL 11007): Edit Page / Add Page / Save File, STR#10008 #20 and #60
rs.L = paths.rsrc(paths.UI_LOCALIZED); rs.U = paths.rsrc(paths.UI_RSRC)
it = {i['n']: i for i in rs.ditl(11007)}
items = [
    dict(kind='noteicon', rect=rc(4)),
    dict(kind='text', rect=rc(5), key='cText1', bold=True,
         text=paths.STR('ui', 10008, 20)),
    dict(kind='text', rect=rc(6), key='cText2',
         text=paths.STR('ui', 10008, 60)),
    dict(kind='button', rect=rc(2), text='Add Page', action='mp:add'),
    dict(kind='button', rect=rc(1), text='Edit Page', key='mpEdit', action='mp:edit', default=True),
    dict(kind='button', rect=rc(3), text='Save File', action='mp:save'),
]
dump('continue.json', rs.dlog(11007), items, 'EPSON Scan')

# ---- EPSON PDF Plug-in Settings (PDF.rsrc DLOG 10000 + "General" tab DITL 10101 in overlay mode;
# only the General tab: the Text tab needs the OCR module, Security needs espdfenc.bundle,
# neither of which the V550 package installs). Texts: _PdfControlInfo -> STR# 2000.
pbase = paths.FILE_FORMAT + '/PDF.bundle/Contents/Resources/'
rs.L = paths.rsrc(pbase + 'English.lproj/Localized.rsrc')
rs.U = paths.rsrc(pbase + 'PDF.rsrc')
b = {i['n']: i for i in rs.ditl(10000)}
g = {i['n'] + 4: i for i in rs.ditl(10101)}
def G(n): return list(g[n]['rect'])
def header(n, text, sep):
    r = G(n); s = G(sep); mid = (r[1] + r[3]) // 2
    return [dict(kind='label', rect=r, text=text, bold=True),
            dict(kind='separator', rect=[s[0], mid, max(s[2], 480), mid + 1])]
items = [dict(kind='tabs', rect=list(b[4]['rect']), tabs=['General'], value=0)]
items += [dict(kind='label', rect=G(5), text='Paper Size:'),
          dict(kind='popup', rect=G(6), key='pPaper', text='Actual Image Size', action='p:paper',
               menu=['A5 (148 x 210 mm)', 'B5 (182 x 257 mm)', 'Letter (8 1/2 x 11 in.)', 'A4 (210 x 297 mm)',
                     'Legal (8 1/2 x 14 in.)', 'US B (11 x 17 in.)', 'B4 (257 x 364 mm)', 'A3 (297 x 420 mm)',
                     'Actual Image Size'])]
items += header(9, 'Orientation', 19)
items += [dict(kind='radio', rect=G(7), text='Portrait', key='pPortrait', action='p:portrait'),
          dict(kind='radio', rect=G(8), text='Landscape', key='pLandscape', action='p:landscape')]
items += header(15, 'Margin', 20)
items += [dict(kind='label', rect=G(10), text='Top:', key='pTopL'),
          dict(kind='edit', rect=G(11), key='pTop', text='0.0'),
          dict(kind='label', rect=G(12), text='Left:', key='pLeftL'),
          dict(kind='edit', rect=G(13), key='pLeft', text='0.0'),
          dict(kind='popup', rect=G(14), key='pUnits', text='mm', menu=['mm', 'in.'], action='p:units')]
items += header(22, 'Page Number', 23)
items += [dict(kind='radio', rect=G(24), text=paths.STR('pdf', 2000, 15), key='pAll', action='p:all'),
          dict(kind='radio', rect=G(25), text=paths.STR('pdf', 2000, 16), key='pSel', action='p:sel'),
          dict(kind='edit', rect=G(26), key='pNum', text='1'),
          dict(kind='stepper', rect=[340, 350, 353, 373], key='pArrows', action='p:step')]   # item 27's CNTL is 36 x 56
items += header(18, 'Compression Level', 21)
items += [dict(kind='label', rect=G(16), text='Color/Grayscale:'),
          dict(kind='popup', rect=G(17), key='pComp', text='Standard Compression',
               menu=['Standard Compression', 'High Compression'])]
items += [dict(kind='button', rect=list(b[3]['rect']), text='Help', action='p:help'),
          dict(kind='button', rect=list(b[2]['rect']), text='Cancel', action='p:cancel'),
          dict(kind='button', rect=list(b[1]['rect']), text='OK', action='p:ok', default=True)]
dump('pdfopts.json', rs.dlog(10000), items, paths.STR('pdf', 2000, 1))
