"""
Exports Professional Mode as sections made of rows, so the app can open and
close the triangle-controlled rows the way Epson Scan did. Row tops and item
positions come straight from the original DITL layouts; a row's height is
the distance to the next row in the original, so spacing matches exactly.
"""
import json
import shutil
import sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import paths  # noqa: E402
from rs import ditl

OUT = paths.OUT


def R(it):
    return list(it['rect'])


def item(kind, rect, **kw):
    return dict(kind=kind, rect=rect, **kw)


def build_section(title, d, rows, end):
    """rows: list of (top, group, [items with absolute DITL rects]) or (top, group, items, show).
    `show` names the condition under which Epson shows the row ('film' / 'reflective'); rows that
    share a top are alternatives that occupy the same place (only one is ever shown)."""
    out = []
    tops = sorted(set(r[0] for r in rows)) + [end]
    for row in rows:
        top, group, items = row[:3]
        show = row[3] if len(row) > 3 else None
        rel = []
        for it in items:
            r = it['rect']
            rel.append(dict(it, rect=[r[0], r[1] - top, r[2], r[3] - top]))
        nxt = tops[tops.index(top) + 1]
        o = dict(height=nxt - top, group=group, items=rel)
        if show: o['show'] = show
        out.append(o)
    return dict(title=title, rows=out)


def header(d, title):
    return [item('label', R(d[0]), text=title, bold=True), item('separator', R(d[1]))]


LEVELS = ['Low', 'Medium', 'High']


def original():
    d = ditl(10202)
    rows = [
        (0, None, header(d, 'Original')),
        (25, None, [item('label', R(d[2]), text='Document Type:'),
                    item('popup', R(d[3]), key='documentType', text='Film (with Film Area Guide)',
                         menu=['Reflective', 'Film (with Film Area Guide)', 'Film'], action='state')]),
        (51, None, [item('label', R(d[6]), text='Film Type:'),
                    item('popup', R(d[5]), key='filmType', text=paths.STR('ui', 10005, 7),
                         menu=['Positive Film', paths.STR('ui', 10005, 7), 'B&W Negative Film'])], 'film'),
        # Reflective (ShowOriginalItem): Document Source (items 5-6, STR# 10003 #40; the V550's only
        # source is the glass) and Auto Exposure Type below it (items 9-10, #42, MINF 15011)
        (51, None, [item('label', R(d[4]), text='Document Source:'),
                    item('popup', R(d[5]), key='docSource', text='Scanner Glass', menu=['Scanner Glass'])],
         'reflective'),
        (77, None, [item('label', R(d[8]), text=paths.STR('ui', 10003, 42)),
                    item('popup', R(d[9]), key='aeType', text='Photo', menu=['Photo', 'Document'],
                         action='state')], 'reflective'),
    ]
    return build_section('Original', d, rows, 103)


def destination():
    d = ditl(10203)
    rows = [
        (0, None, header(d, 'Destination')),
        (24, None, [item('disclosure', R(d[2]), on=False, toggles='imageOption', key='imageTypeDisclosure'),
                    item('label', R(d[3]), text='Image Type:'),
                    item('popup', R(d[4]), key='imageType', text='48-bit Color',
                         menu=['48-bit Color', '24-bit Color', 'Color Smoothing', '16-bit Grayscale',
                               '8-bit Grayscale', 'Black & White'], action='state')]),
        # Image Option (STR# 10003 #97; MINF 15035 kept by the V550's capabilities) for 8-bit
        # Grayscale and Black & White. Item 10 under it is "Speed priority scanning", which only
        # shows on scanners with a scan-speed choice (GetScanModeNum), so not on the V550.
        (50, 'imageOption', [item('label', R(d[5]), text='Image Option:'),
                             item('popup', R(d[6]), key='imageOption', text='None',
                                  menu=['None', 'Dropout: Red', 'Dropout: Green', 'Dropout: Blue',
                                        'Color Enhance: Red', 'Color Enhance: Green', 'Color Enhance: Blue',
                                        paths.STR('twainprotocolmanager', 10000, 9), paths.STR('ui', 10002, 76)])],
         'imageOption'),
        (111, None, [item('label', R(d[10]), text='Resolution:'), item('edit', R(d[11]), key='resolution', text='3200'),
                     # CScanInf::InitResolution for Professional Mode: MINF 15005, plus the optical
                     # resolution, 1.5x and 2x it (6400, 9600, 12800 on the V550)
                     item('popup', R(d[12]), text='', fills='resolution',
                          menu=['50', '72', '96', '150', '200', '240', '266', '300', '350', '360', '400', '600',
                                '720', '800', '1200', '2400', '3200', '4800', '6400', '9600', '12800']),
                     item('label', R(d[13]), text='dpi')]),
        (143, None, [item('label', R(d[14]), text='Document Size:')]),
        # Document Size: the marquee, in mm or in. (MIWD 15008)
        (160, None, [item('label', R(d[15]), text='W'), item('edit', R(d[16]), text='8.50', key='srcW'),
                     item('label', R(d[17]), text='H'), item('edit', R(d[18]), text='11.70', key='srcH'),
                     item('popup', R(d[19]), text='in.', menu=['mm', 'in.'], key='srcUnits', action='size-units')]),
        # Target Size: TSIZ 10000 + "Customize..."; the button beside it turns the target round
        # (cicn 10021 portrait / 10022 landscape)
        (186, None, [item('disclosure', R(d[20]), on=True, toggles='targetSize'),
                     item('label', R(d[21]), text='Target Size:'),
                     item('popup', R(d[22]), text='Original', key='target', action='target',
                          menu=['Original', 'Thumbnail (80 x 120)', 'Screen (240 x 320)', 'Screen (480 x 640)',
                                'Screen (600 x 800)', 'Screen (768 x 1024)', '10 x 15 cm (4 x 6 in.)',
                                '5 x 7 in.', 'Letter (8 1/2 x 11 in.)', 'A4 (210 x 297 mm)',
                                'Legal (8 1/2 x 14 in.)', '8 x 10 in.', '10 x 13 in.', '11 x 14 in.',
                                'US B (11 x 17 in.)', 'A3 (297 x 420 mm)', 'Customize...']),
                     item('bevel', R(d[23]), icon=10021, key='targetRotate', action='target-rotate')]),
        # target in pixels / mm / in. (MIWD 15010); the lock (cicn 10009 open / 10010 shut)
        (217, 'targetSize', [item('label', R(d[24]), text='W'), item('edit', R(d[25]), text='8.50', key='tgtW'),
                             item('label', R(d[26]), text='H'), item('edit', R(d[27]), text='11.70', key='tgtH'),
                             item('popup', R(d[28]), text='in.', menu=['pixels', 'mm', 'in.'], key='tgtUnits',
                                  action='size-units'),
                             item('bevel', R(d[29]), icon=10009, key='targetLock', action='target-lock')]),
        (248, 'targetSize', [item('label', R(d[30]), text='Scale:'), item('edit', R(d[31]), text='100', key='scale'),
                             item('stepper', R(d[32]), key='scaleStepper', action='scale-step'), item('label', R(d[33]), text='%')]),
        (283, 'targetSize', [item('label', R(d[34]), text='Trimming:')]),
        (306, 'targetSize', [item('radio', R(d[35]), text='Standard', on=True, key='trimStd', action='trim-std'),
                             item('radio', R(d[36]), text='High', on=False, key='trimHigh', action='trim-high')]),
    ]
    sec = build_section('Destination', d, rows, 330)
    # without the speed checkbox the Image Option row keeps its own 26 px plus the 9 px of space
    # the original leaves before Resolution (76 -> 111 is 35 for a 26 px row)
    for r in sec['rows']:
        if r.get('show') == 'imageOption': r['height'] = 35
    return sec


def adjustments():
    d = ditl(10204)
    acts = ('dlg:histogram', 'dlg:tonecurve', 'dlg:imageadjust', 'dlg:palette', 'autoexposure-button')
    tips = ('Histogram Adjustment', 'Tone Correction', 'Image Adjustment', 'Color Palette', 'Auto Exposure')
    tools = [item('bevel', R(d[k]), icon=ic, action=a, tip=t)
             for k, ic, a, t in zip(range(2, 7), (10011, 10012, 10013, 10014, 10036), acts, tips)]
    tools.append(item('button', R(d[7]), text='Reset', action='reset-adjust'))

    def feature(tri, cb, lab, pop, text, group, on, level_label='Level:', menu=LEVELS, value='Medium', key=None):
        row = [item('check', R(d[cb]), text=text, on=on, key=key or group or text, action='state')]
        if text in ('Backlight Correction', 'Color Restoration'):
            row[0]['action'] = 'autoexposure'      # re-run auto exposure on the preview
        if tri is not None:
            row.insert(0, item('disclosure', R(d[tri]), on=False, toggles=group))
        sub = None
        if lab is not None:
            sub = [item('label', R(d[lab]), text=level_label),
                   item('popup', R(d[pop]), text=value, menu=menu, key=(key or group) + 'Level')]
            if group == 'backlight':
                sub[1]['action'] = 'autoexposure'
        return row, sub

    um, um_l = feature(8, 9, 10, 11, 'Unsharp Mask', 'unsharp', False, value='Medium')
    # one place, two uses (CAdvancedDialog::InitMenu / UpdateMenu): Descreening with its Screen
    # Ruling for Reflective, Grain Reduction with its Level for Film; one open/closed state
    gr, gr_l = feature(12, 13, 14, 15, 'Grain Reduction', 'descreen', False, key='grain')
    ds, ds_l = feature(12, 13, 14, 15, 'Descreening', 'descreen', False, key='descreen',
                       level_label='Screen Ruling:', value='General',
                       menu=['General', 'Newspaper (85 lpi)', 'Magazine (133 lpi)', 'Fine Prints (175 lpi)'])
    cr, _ = feature(None, 16, None, None, 'Color Restoration', None, False)
    bl, bl_l = feature(17, 18, 19, 20, 'Backlight Correction', 'backlight', False)
    du, du_l = feature(21, 22, 23, 24, 'Dust Removal', 'dust', False)
    ice, ice_l = feature(25, 26, 27, 28, paths.STR('ui', 10002, 59), 'ice', True, level_label='Mode:',
                         menu=[paths.STR('ui', 10019, 5), paths.STR('ui', 10019, 6)],
                         value=paths.STR('ui', 10019, 6))
    # Black & White (CAdvancedDialog::ShowAdjustmentItem case 6 -> ShowBWAdjustment): the colour
    # adjustments go and DITL 10205's Threshold row (items 0x57-0x59, 0..255, default 110) takes
    # their place, right under the tool buttons
    e = ditl(10205)
    th = [item('label', [20, 70, 104, 85], text='Threshold:'),
          item('slider', [110, 69, 290, 86], key='threshold', min=0, max=255, value=110),
          item('edit', [304, 70, 339, 85], key='thresholdValue', text='110')]
    rows = [
        (0, None, header(d, 'Adjustments')),
        (27, None, tools),
        (66, None, th, 'bw'),
        (66, None, um, 'color'), (93, 'unsharp', um_l, 'color'),
        (120, None, gr, 'film+color'), (147, 'descreen', gr_l, 'film+color'),
        (120, None, ds, 'reflective+color'), (147, 'descreen', ds_l, 'reflective+color'),
        (174, None, cr, 'color'),
        (201, None, bl, 'color'), (228, 'backlight', bl_l, 'color'),
        (255, None, du, 'color'), (282, 'dust', du_l, 'color'),
        # GetDigitalICEAvailability: the V550 has DIGITAL ICE for film only
        (309, None, ice, 'film+color'), (336, 'ice', ice_l, 'film+color'),
    ]
    return build_section('Adjustments', d, rows, 360)


def chrome():
    """Window items outside the scrolling panel (absolute positions)."""
    items = [item('image', [0, 0, 380, 48], image='pict_10000.png')]
    o = ditl(10206)
    items += [item('label', R(o[1]), text='Mode:'),
              item('popup', R(o[2]), text='Professional Mode', key='mode', action='mode',
                   menu=['Full Auto Mode', 'Home Mode', 'Office Mode', 'Professional Mode'])]
    s = ditl(10201)
    dx, dy = 10, 52
    sh = lambda it: [it['rect'][0] + dx, it['rect'][1] + dy, it['rect'][2] + dx, it['rect'][3] + dy]
    items += [item('label', sh(s[0]), text='Settings', bold=True), item('separator', sh(s[1])),
              item('label', sh(s[2]), text='Name:'), item('edit', sh(s[3]), text='Current Setting', key='settingsName'),
              item('popup', sh(s[4]), text='', menu=['Current Setting'], key='settingsMenu', fills='settingsName',
                   action='settings:pick'),
              item('button', sh(s[5]), text='Save', action='settings:save'),
              item('button', sh(s[6]), text='Delete', action='settings:delete')]
    m = ditl(10200)
    bottom = [item('button', R(m[1]), text='Preview', icon=10008, key='previewButton'),
              item('button', R(m[2]), text='Scan', icon=10004),
              item('bevel', R(m[6]), icon=10037, action='filesave', tip=paths.STR('ui', 10000, 8)),

              item('button', R(m[3]), text='Help'),
              item('button', R(m[4]), text='Configuration...'),
              item('button', R(m[5]), text='Close')]
    for b in bottom:
        b['pin'] = 'bottom'      # stays at the bottom when the window is resized
        act = {'Preview': 'preview', 'Scan': 'scan', 'Close': 'close', 'Configuration...': 'configuration', 'Help': 'help'}
        if b.get('text') in act: b['action'] = act[b['text']]
    return items + bottom


layout = dict(
    window=dict(width=380, height=560, title='EPSON Scan'),
    items=chrome(),
    scroll=dict(rect=[10, 132, 369, 442], contentWidth=342, sectionGap=6,
                sections=[original(), destination(), adjustments()]),
)
json.dump(layout, open(f'{OUT}/professional.json', 'w'), indent=1)
full = sum(r['height'] for s in layout['scroll']['sections'] for r in s['rows']) + 6 * 4
print('sections:', [(s['title'], len(s['rows'])) for s in layout['scroll']['sections']], 'all-open height', full)
