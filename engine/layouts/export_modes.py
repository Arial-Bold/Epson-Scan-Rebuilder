"""
Exports Home Mode (CSimpleDialog, DITL 10100 + Brightness DITL 10110), Office Mode
(CDocumentDialog, DITL 10600) and Full Auto Mode (CFullAutoDialog, DITLs 10000/10003/10001 and
the Customize dialog CFADisclosDialog, DITL 10010) to the app's JSON layouts. Rects come
straight from the DITLs; the texts are the ones Epson's code puts in at run time
(STR# 10001-10003, Localized.rsrc).
"""
import json
import re
import shutil
import struct
import sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import paths  # noqa: E402
import rs
from rs import ditl

OUT = paths.OUT
MODES = ['Full Auto Mode', 'Home Mode', 'Office Mode', 'Professional Mode']
RESOLUTIONS = ["50", "72", "96", "150", "200", "240", "266", "300", "350", "360", "400", "600", "720", "800",
               "1200", "2400", "3200", "4800", "6400", "9600", "12800"]


def R(it):
    return list(it['rect'])


def I(kind, rect, **kw):
    return dict(kind=kind, rect=list(rect), **kw)


def top(d, mode):
    """the banner, Mode: and the Mode list (items 1-3 of every mode's dialog)"""
    return [I('image', R(d[0]), image='pict_10000.png'),
            I('label', R(d[1]), text='Mode:'),
            I('popup', R(d[2]), text=mode, key='mode', action='mode', menu=MODES)]


def header(lab, sep, text):
    """a group title and its blue line; the line runs from the title to the window's edge"""
    s = list(sep['rect'])
    s[2] = 370
    return [I('label', R(lab), text=text, bold=True), I('separator', s)]


def doc_sizes():
    """DMSZ 10000: Office Mode's document sizes (name, width mm, width in., height mm, height in.)"""
    d = rs.L[b'DMSZ'][10000].data
    n = struct.unpack('>h', d[:2])[0]
    sz = (len(d) - 2) // n
    out = []
    for k in range(n):
        e = d[2 + k * sz:2 + (k + 1) * sz]
        f = [s.decode('mac_roman') for s in re.findall(rb'[\x01-\x20]([ -~]{1,31})', e)]
        out.append(dict(name=f[0], wmm=float(f[1]), hmm=float(f[3])))
    return out


def home():
    d = ditl(10100)
    it = top(d, 'Home Mode')
    it += header(d[3], d[35], 'Document Type')
    # MIWI 15024: name and icon of each document type (icons 257-264 of UI.rsrc)
    it.append(I('iconpopup', R(d[4]), key='docType', action='home:doctype',
                menu=['Photograph', 'Positive Film', paths.STR('ui', 10005, 7), 'B&W Negative Film',
                      'Illustration', 'Magazine', 'Newspaper', 'Text/Line Art'],
                images=['ui_257.png', 'ui_259.png', 'ui_260.png', 'ui_261.png',
                        'ui_258.png', 'ui_262.png', 'ui_263.png', 'ui_264.png']))
    it += header(d[5], d[32], 'Image Type')
    for n, (key, text) in zip((6, 7, 8), (('itColor', 'Color'), ('itGray', 'Grayscale'), ('itBW', 'Black&White'))):
        it.append(I('radio', R(d[n]), key=key, text=text, action='home:imagetype'))
    it += header(d[9], d[33], 'Destination')
    for n, (key, text) in zip((10, 11, 12), (('dScreen', 'Screen/Web'), ('dPrinter', 'Printer'), ('dOther', 'Other'))):
        it.append(I('radio', R(d[n]), key=key, text=text, action='home:dest'))
    it += [I('label', R(d[13]), text='Resolution:', key='resLabel'),
           I('edit', R(d[14]), key='resolution', text='300'),
           I('popup', R(d[15]), text='', fills='resolution', key='resArrows', action='home:res', menu=RESOLUTIONS),
           I('label', R(d[16]), text='dpi', key='dpiLabel'),
           I('label', R(d[17]), text='Target Size:'),
           I('popup', R(d[18]), key='target', text='Original', action='home:target', menu=['Original']),
           I('bevel', R(d[19]), icon=10021, key='targetRotate', action='home:rotate', tip=paths.STR('ui', 10002, 83))]
    it += header(d[20], d[34], 'Image Adjustments')
    it += [I('check', R(d[21]), key='descreen', text='Descreening', action='home:adjust'),
           I('check', R(d[31]), key='backlight', text='Backlight Correction', action='home:adjust'),
           I('check', R(d[22]), key='restoration', text='Color Restoration', action='home:adjust'),
           I('check', R(d[23]), key='dustIce', text=paths.STR('ui', 10002, 59), action='home:adjust'),
           I('check', R(d[36]), key='imageOption', text='Image Option', action='home:imgopt'),
           I('radio', R(d[37]), key='ioTET', text='Text Enhancement', action='home:imgoptradio'),
           I('radio', R(d[38]), key='ioAAS', text=paths.STR('ui', 10002, 76), action='home:imgoptradio'),
           I('button', R(d[24]), text='Brightness...', action='home:brightness', key='brightnessButton'),
           I('button', R(d[25]), text='Preview', icon=10008, key='previewButton', action='preview'),
           I('button', R(d[26]), text='Scan', icon=10004, action='scan', key='scanButton'),
           I('bevel', R(d[30]), icon=10037, action='filesave', tip=paths.STR('ui', 10000, 8)),
           I('button', R(d[27]), text='Help', action='home:help'),
           I('button', R(d[28]), text='Configuration...', action='configuration'),
           I('button', R(d[29]), text='Close', action='close')]
    return dict(window=dict(width=380, height=565, title='EPSON Scan'), items=it)


def home_adjust():
    """CImageAdjustmentDialog (DITL 10110): Brightness and Contrast -100..100 (the slider's value
    minus 100), Threshold 0..255 in Brightness's place for Black & White"""
    d = ditl(10110)
    it = [I('label', R(d[0]), text='Brightness:', key='bLabel'),
          I('slider', R(d[1]), key='brightness', min=-100, max=100, value=0),
          I('edit', R(d[2]), key='brightnessText', text='0'),
          I('label', R(d[3]), text='Contrast:', key='cLabel'),
          I('slider', R(d[4]), key='contrast', min=-100, max=100, value=0),
          I('edit', R(d[5]), key='contrastText', text='0'),
          I('label', R(d[6]), text='Threshold:', key='tLabel'),
          I('slider', R(d[7]), key='threshold', min=0, max=255, value=110),
          I('edit', R(d[8]), key='thresholdText', text='110'),
          I('button', R(d[9]), text='Reset', action='ha:reset'),
          I('button', R(d[10]), text='Close', action='ha:close', default=True)]
    return dict(window=dict(width=380, height=145, title='Brightness'), items=it)


def office():
    d = ditl(10600)
    it = top(d, 'Office Mode')
    it += header(d[3], d[4], 'Image Type')
    for n, (key, text) in zip((5, 6, 7), (('itColor', 'Color'), ('itGray', 'Grayscale'), ('itBW', 'Black&White'))):
        it.append(I('radio', R(d[n]), key=key, text=text, action='office:imagetype'))
    it += header(d[8], d[9], 'Document Settings')
    sizes = doc_sizes()
    it += [I('label', R(d[10]), text='Document Source:'),
           I('popup', R(d[11]), key='docSource', text='Scanner Glass', menu=['Scanner Glass']),
           I('label', R(d[12]), text='Size:'),
           I('popup', R(d[13]), key='size', text='A4', action='office:size', menu=[s['name'] for s in sizes],
             sizes=sizes),
           I('label', R(d[14]), text='Orientation:'),
           I('radio', R(d[15]), key='orPortrait', text='', action='office:orientation'),
           I('icon', R(d[16]), icon=10029),
           I('radio', R(d[18]), key='orLandscape', text='', action='office:orientation'),
           I('icon', R(d[19]), icon=10031),
           I('label', R(d[21]), text='Resolution:'),
           I('edit', R(d[22]), key='resolution', text='300'),
           I('popup', R(d[23]), text='', fills='resolution', action='office:res', menu=RESOLUTIONS),
           I('label', R(d[24]), text='dpi')]
    it += header(d[25], d[26], 'Image Adjustments')
    it += [I('check', R(d[33]), key='unsharp', text='Unsharp Mask', action='office:adjust'),
           I('check', R(d[34]), key='descreen', text='Descreening', action='office:adjust'),
           I('check', R(d[44]), key='imageOption', text='Image Option', action='office:imgopt'),
           I('radio', R(d[47]), key='ioTET', text='Text Enhancement', action='office:imgoptradio'),
           I('radio', R(d[48]), key='ioAAS', text=paths.STR('ui', 10002, 76), action='office:imgoptradio'),
           I('radio', R(d[49]), key='ioDropout', text='Dropout:', action='office:imgoptradio'),
           I('popup', R(d[51]), key='dropoutColor', text='Red', menu=['Red', 'Green', 'Blue'], action='office:imgopt'),
           I('radio', R(d[50]), key='ioEnhance', text='Color Enhance:', action='office:imgoptradio'),
           I('popup', R(d[52]), key='enhanceColor', text='Red', menu=['Red', 'Green', 'Blue'], action='office:imgopt'),
           I('label', R(d[27]), text='Brightness:', key='bLabel'),
           I('slider', R(d[28]), key='brightness', min=-100, max=100, value=0),
           I('edit', R(d[29]), key='brightnessText', text='0'),
           I('label', R(d[30]), text='Contrast:', key='cLabel'),
           I('slider', R(d[31]), key='contrast', min=-100, max=100, value=0),
           I('edit', R(d[32]), key='contrastText', text='0'),
           I('label', R(d[35]), text='Threshold:', key='tLabel'),
           I('slider', R(d[36]), key='threshold', min=0, max=255, value=110),
           I('edit', R(d[37]), key='thresholdText', text='110'),
           I('button', R(d[38]), text='Preview', icon=10008, key='previewButton', action='preview'),
           I('button', R(d[39]), text='Scan', icon=10004, action='scan', key='scanButton'),
           I('bevel', R(d[43]), icon=10037, action='filesave', tip=paths.STR('ui', 10000, 8)),
           I('button', R(d[40]), text='Help', action='office:help'),
           I('button', R(d[41]), text='Configuration...', action='configuration'),
           I('button', R(d[42]), text='Close', action='close')]
    return dict(window=dict(width=380, height=565, title='EPSON Scan'), items=it)


def full_auto():
    t = ditl(10000)
    idle = ditl(10003)
    work = ditl(10001)
    it = top(t, 'Full Auto Mode')
    # idle (DITL 10003): STR# 10003 #15-#17 are the notes CFullAutoDialog draws
    it += [I('image', R(idle[0]), image='pict_10002.png', key='idlePicture', group='idle'),
           I('button', R(idle[1]), text='Scan', icon=10004, action='fa:scan', key='scanButton', group='idle',
             default=True),
           I('wrap', R(idle[3]), key='note1', group='idle',
             text=paths.STR('ui', 10003, 16)),
           I('wrap', R(idle[4]), key='note2', group='idle',
             text=paths.STR('ui', 10003, 17)),
           I('button', R(idle[5]), text='Customize...', action='fa:customize', group='idle'),
           I('icon', R(idle[7]), image='ui_265.png', group='idle'),
           I('wrap', R(idle[8]), key='note3', group='idle',
             text=paths.STR('ui', 10003, 15)),
           I('button', R(idle[9]), text='Help', action='fa:help', group='idle'),
           I('button', R(idle[2]), text='Close', action='close', group='idle')]
    # working (DITL 10001): the document-type picture goes in the bubble (item 6)
    it += [I('image', R(work[1]), image='pict_10001.png', key='workPicture', group='work'),
           I('icon', R(work[5]), key='docPicture', group='work'),
           I('wrap', R(work[3]), key='status', group='work', text='', align='center'),
           I('progress', R(work[2]), key='progress', group='work'),
           I('label', R(work[9]), key='percent', text='', group='work'),
           I('wrap', R(work[6]), key='docTypeText', group='work', text=''),
           I('button', R(work[0]), text='Cancel', action='fa:cancel', group='work')]
    return dict(window=dict(width=380, height=498, title='EPSON Scan'), items=it)


def full_auto_custom():
    d = ditl(10010)
    it = [I('check', R(d[0]), key='specDoc', text=paths.STR('ui', 10002, 1), action='fac:state'),
          I('radio', R(d[1]), key='docPhoto', text='Photograph/Document', action='fac:doc'),
          I('radio', R(d[2]), key='docFilm', text='Film', action='fac:doc'),
          I('check', R(d[3]), key='specRes', text=paths.STR('ui', 10002, 4), action='fac:state'),
          I('label', R(d[4]), text='Photograph/Document:', key='resReflLabel'),
          I('edit', R(d[5]), key='resReflective', text='300'),
          I('popup', R(d[6]), text='', fills='resReflective', key='resReflArrows', menu=RESOLUTIONS),
          I('label', R(d[7]), text='dpi', key='resReflDpi'),
          I('label', R(d[8]), text='Film:', key='resFilmLabel'),
          I('edit', R(d[9]), key='resFilm', text='300'),
          I('popup', R(d[10]), text='', fills='resFilm', key='resFilmArrows', menu=RESOLUTIONS),
          I('label', R(d[11]), text='dpi', key='resFilmDpi'),
          I('check', R(d[12]), key='dust', text='Dust Removal'),
          I('check', R(d[13]), key='restoration', text='Color Restoration'),
          I('separator', R(d[14])),
          I('check', R(d[15]), key='autoRotate', text=paths.STR('ui', 10002, 42)),
          I('separator', R(d[16])),
          I('button', R(d[17]), text=paths.STR('ui', 10001, 31), action='filesave'),
          I('button', R(d[20]), text='Help', action='fac:help'),
          I('button', R(d[19]), text='Cancel', action='fac:cancel'),
          I('button', R(d[18]), text='OK', action='fac:ok', default=True)]
    return dict(window=dict(width=460, height=334, title='Customize'), items=it)


def home_defaults():
    """SPPR 10000 (CSPPrefs): Home Mode's settings for each document type, 0xa2 bytes a type from
    +0x66, and FAPR 10000 (CFAPrefs): Full Auto's"""
    d = rs.L[b'SPPR'][10000].data
    F = [('imageType', 200, 'l'), ('destination', 0x6c, 'l'), ('filmType', 0x78, 'l'), ('aeType', 0x7c, 'l'),
         ('resolution', 0x80, 'l'), ('usm', 0xd8, 'h'), ('descreen', 0xde, 'h'), ('descreenLevel', 0xe0, 'l'),
         ('previewMode', 0xe6, 'l'), ('restoration', 0xea, 'h'), ('dust', 0xec, 'h'), ('dustLevel', 0xee, 'l'),
         ('ice', 0xf8, 'h'), ('iceLevel', 0xfa, 'l'), ('backlight', 0xfe, 'h'), ('backlightLevel', 0x100, 'l'),
         ('imageOption', 0x104, 'h'), ('imageOptionRadio', 0x106, 'h')]
    types = []
    for i in range(8):
        rec = {}
        for n, o, t in F:
            o += i * 0xa2
            rec[n] = struct.unpack('>i', d[o:o + 4])[0] if t == 'l' else struct.unpack('>h', d[o:o + 2])[0]
        types.append(rec)
    f = rs.L[b'FAPR'][10000].data
    h = lambda o: struct.unpack('>h', f[o:o + 2])[0]
    fa = dict(specifyDocument=h(0x12), documentType=h(0x14), specifyResolution=h(0x16),
              reflectiveResolution=h(0x18), filmResolution=h(0x1a), dust=h(0x1c),
              dustLevel=struct.unpack('>i', f[0x1e:0x22])[0], restoration=h(0x22), manualStart=h(0x24),
              autoRotate=h(0x26))
    # CDTR::CalcFilmRes: STR# 10007 #1 and #2, the print size (inches) Full Auto scans film for
    return dict(home=dict(current=struct.unpack('>i', d[0x20:0x24])[0], types=types), fullAuto=fa,
                filmPrintLong=6.0, filmPrintShort=4.0)


def main():
    for name, L in (('home', home()), ('homeadjust', home_adjust()), ('office', office()),
                    ('fullauto', full_auto()), ('fullautocustom', full_auto_custom())):
        json.dump(L, open(f'{OUT}/{name}.json', 'w'), indent=1)
        print(name, len(L['items']), 'items')
    json.dump(home_defaults(), open(f'{OUT}/modedefaults.json', 'w'), indent=1)


if __name__ == '__main__':
    main()
