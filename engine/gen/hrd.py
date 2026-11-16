"""Parse Epson Scan's V550 hardware data file (EPSON Scan ES00EB HRD), following
CFileIoHRD::ReadParmTable in the original Device Control plugin."""
import struct, sys, json

def parse(path, start=0x541):
    d = open(path, 'rb').read()
    o = start
    def i32():
        nonlocal o
        v = struct.unpack_from('<i', d, o)[0]; o += 4; return v
    def raw(n):
        nonlocal o
        b = d[o:o + n]; o += n; return b
    def tables(ntab, recsize, fmt):
        out = []
        for t in range(ntab):
            n = i32()
            blob = raw(recsize * n)
            recs = [struct.unpack_from('<' + fmt, blob, k * recsize) for k in range(n)]
            out.append(recs)
        return out
    H = {}
    H['profile'] = [[v / 10000 for v in struct.unpack('<9i', raw(36))] for _ in range(3)]
    H['red_profile'] = [[v / 10000 for v in struct.unpack('<9i', raw(36))] for _ in range(3)]
    H['usm'] = tables(5, 16, '4I')            # per type: (max_res, strength*10000, radius, clipping)
    H['clip_ctl'] = tables(1, 4, 'I')
    H['clip_rev'] = tables(1, 4, 'I')
    H['clip_rev_parm'] = tables(1, 8, '4H')
    H['descreening'] = tables(4, 24, '6I')
    H['grain'] = tables(3, 8, '2I')
    H['color_smoothing'] = tables(1, 20, '5I')
    H['dust'] = tables(3, 32, '8I')
    H['resolution'] = tables(26, 8, '2I')
    H['option_max_res'] = tables(1, 4, 'I')
    H['focus_range'] = tables(1, 10, 'hII')
    H['sharpness'] = tables(1, 8, 'if')
    H['usm_noise'] = tables(2, 8, '4H')
    H['usm_blur'] = tables(2, 2, 'H')
    H['wait_time'] = tables(1, 12, '3I')
    H['draft_res'] = tables(26, 8, '2I')
    H['dice_res'] = tables(4, 8, '2I')
    H['dice_draft_res'] = tables(4, 8, '2I')
    return H, o, len(d)

if __name__ == '__main__':
    H, end, size = parse(sys.argv[1])
    print('consumed', hex(end), 'of', hex(size))
    for k in ('usm', 'usm_noise', 'usm_blur', 'grain', 'dust', 'descreening', 'color_smoothing', 'dice_res'):
        print(k, json.dumps(H[k]))
