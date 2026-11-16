"""Decoders for classic Mac 'cicn' colour icons, 'icl8' icons and PICT v2 pictures to RGBA, and a PNG
writer. Plain Python (no numpy / PIL), so it runs with the Python that comes with Apple's tools."""
import struct
import zlib


class Image:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = bytearray(w * h * 4)          # RGBA

    def set(self, x, y, rgba):
        o = (y * self.w + x) * 4
        self.px[o:o + 4] = bytes(rgba)


def write_png(img, path):
    raw = bytearray()
    stride = img.w * 4
    for y in range(img.h):
        raw.append(0)
        raw += img.px[y * stride:(y + 1) * stride]

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', img.w, img.h, 8, 6, 0, 0, 0))
    data += chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(data)


def _unpackbits(data, o, row_bytes, rows, chunk=1):
    """PackBits rows as used in PICT (each row prefixed by a byte or word count)."""
    out = bytearray()
    for _ in range(rows):
        if row_bytes > 250:
            n = struct.unpack('>H', data[o:o + 2])[0]; o += 2
        else:
            n = data[o]; o += 1
        end = o + n
        row = bytearray()
        while o < end:
            c = data[o]; o += 1
            if c < 128:
                cnt = (c + 1) * chunk
                row += data[o:o + cnt]; o += cnt
            elif c > 128:
                cnt = 257 - c
                row += data[o:o + chunk] * cnt; o += chunk
        out += bytes(row[:row_bytes]).ljust(row_bytes, b'\0')
        o = end
    return bytes(out), o


def _ctab(data, o):
    seed, flags, size = struct.unpack('>IHH', data[o:o + 8]); o += 8
    tab = {}
    for k in range(size + 1):
        v, r, g, b = struct.unpack('>4H', data[o:o + 8]); o += 8
        tab[k if flags & 0x8000 else v] = (r >> 8, g >> 8, b >> 8)
    return tab, o


def _indexed(pix, row_bytes, w, h, depth, tab):
    img = Image(w, h)
    per = 8 // depth if depth < 8 else 1
    mask = (1 << depth) - 1
    for y in range(h):
        row = pix[y * row_bytes:(y + 1) * row_bytes]
        for x in range(w):
            if depth == 8:
                i = row[x]
            else:
                i = (row[x // per] >> (8 - depth * (x % per + 1))) & mask
            r, g, b = tab.get(i, (0, 0, 0))
            img.set(x, y, (r, g, b, 255))
    return img


def cicn(data):
    rb = struct.unpack('>H', data[4:6])[0] & 0x3FFF
    top, left, bottom, right = struct.unpack('>4h', data[6:14])
    pixel_size = struct.unpack('>H', data[32:34])[0]
    w, h = right - left, bottom - top
    mrb = struct.unpack('>H', data[54:56])[0]
    brb = struct.unpack('>H', data[68:70])[0]
    o = 50 + 14 + 14 + 4
    mask = data[o:o + mrb * h]; o += mrb * h
    o += brb * h
    tab, o = _ctab(data, o)
    pix = data[o:o + rb * h]
    img = _indexed(pix, rb, w, h, pixel_size, tab)
    for y in range(h):
        for x in range(w):
            img.px[(y * w + x) * 4 + 3] = 255 if (mask[y * mrb + x // 8] >> (7 - x % 8)) & 1 else 0
    return img


def mac_palette8():
    """The Mac OS standard 8-bit colour table (clut 8): a 6x6x6 cube, then red, green, blue and
    gray ramps without the cube's levels, ending in black."""
    tab = {}
    i = 0
    lv = [0xFF, 0xCC, 0x99, 0x66, 0x33, 0x00]
    for r in lv:
        for g in lv:
            for b in lv:
                tab[i] = (r, g, b); i += 1
    i -= 1                              # the cube's last entry (black) moves to the end
    ramp = [0xEE, 0xDD, 0xBB, 0xAA, 0x88, 0x77, 0x55, 0x44, 0x22, 0x11]
    for comp in range(4):
        for v in ramp:
            tab[i] = (v, 0, 0) if comp == 0 else (0, v, 0) if comp == 1 else (0, 0, v) if comp == 2 else (v, v, v)
            i += 1
    tab[255] = (0, 0, 0)
    return tab


def icl8(data, mask_bits=None):
    """32 x 32 icon in the standard 8-bit palette; mask_bits: the ICN# resource (icon + mask)"""
    tab = mac_palette8()
    img = _indexed(data[:1024], 32, 32, 32, 8, tab)
    if mask_bits and len(mask_bits) >= 256:
        m = mask_bits[128:256]
        for y in range(32):
            for x in range(32):
                img.px[(y * 32 + x) * 4 + 3] = 255 if (m[y * 4 + x // 8] >> (7 - x % 8)) & 1 else 0
    return img


def _tiff_rgb(t):
    """Big-endian, uncompressed, 8-bit RGB(A) TIFF -> Image (None for anything else)"""
    try:
        ifd = struct.unpack('>I', t[4:8])[0]
        n = struct.unpack('>H', t[ifd:ifd + 2])[0]
        tags = {}
        for i in range(n):
            e = ifd + 2 + i * 12
            tag, typ, cnt = struct.unpack('>HHI', t[e:e + 8])
            if typ not in (3, 4):
                continue
            if typ == 3 and cnt == 1:
                val = struct.unpack('>H', t[e + 8:e + 10])[0]
            elif typ == 4 and cnt == 1:
                val = struct.unpack('>I', t[e + 8:e + 12])[0]
            else:
                off = struct.unpack('>I', t[e + 8:e + 12])[0]
                size = 2 if typ == 3 else 4
                vals = [struct.unpack('>H' if size == 2 else '>I', t[off + k * size:off + (k + 1) * size])[0]
                        for k in range(cnt)] if cnt * size > 4 else \
                       [struct.unpack('>H' if size == 2 else '>I', t[e + 8 + k * size:e + 8 + (k + 1) * size])[0]
                        for k in range(cnt)]
                val = vals
            tags[tag] = val
        if tags.get(259, 1) != 1:
            return None
        w, h, spp = tags[256], tags[257], tags.get(277, 3)
        offs = tags[273] if isinstance(tags[273], list) else [tags[273]]
        raw = b''.join(t[x:] for x in offs[:1])
        img = Image(w, h)
        for y in range(h):
            for x in range(w):
                q = (y * w + x) * spp
                img.set(x, y, (raw[q], raw[q + 1], raw[q + 2], 255))
        return img
    except Exception:
        return None


def pict(data):
    """Minimal PICT v2 renderer: the bitmap opcodes used by Epson's artwork."""
    top, left, bottom, right = struct.unpack('>4h', data[2:10])
    W, H = right - left, bottom - top
    canvas = Image(W, H)
    o = 10
    if data[o:o + 4] != b'\x00\x11\x02\xff':
        raise ValueError('not a PICT v2')
    o += 4
    while o < len(data) - 1:
        if o & 1:
            o += 1
        op = struct.unpack('>H', data[o:o + 2])[0]; o += 2
        if op == 0x00FF:
            break
        if op in (0x0000, 0x001E):
            continue
        if op == 0x0C00:
            o += 24; continue
        if op == 0x0001:
            n = struct.unpack('>H', data[o:o + 2])[0]; o += n; continue
        if op == 0x00A0:
            o += 2; continue
        if op == 0x00A1:
            o += 2; n = struct.unpack('>H', data[o:o + 2])[0]; o += 2 + n; continue
        if op in (0x001A, 0x001B, 0x001F):
            o += 6; continue
        if op in (0x0098, 0x009A, 0x0099, 0x009B):
            direct = op in (0x009A, 0x009B)
            if direct:
                o += 4
            rb = struct.unpack('>H', data[o:o + 2])[0] & 0x3FFF
            bt, bl, bb, br = struct.unpack('>4h', data[o + 2:o + 10])
            pack_type = struct.unpack('>H', data[o + 12:o + 14])[0]
            pixel_size = struct.unpack('>H', data[o + 28:o + 30])[0]
            cmp_count = struct.unpack('>H', data[o + 30:o + 32])[0]
            o += 46
            tab = None
            if not direct:
                tab, o = _ctab(data, o)
            st, sl, sb, sr = struct.unpack('>4h', data[o:o + 8]); o += 8
            dt, dl, db, dr = struct.unpack('>4h', data[o:o + 8]); o += 8
            o += 2
            if op in (0x0099, 0x009B):
                n = struct.unpack('>H', data[o:o + 2])[0]; o += n
            w, h = br - bl, bb - bt
            img = Image(w, h)
            if direct and pixel_size == 32:
                if pack_type == 4 or (pack_type == 0 and rb >= 8):
                    raw, o = _unpackbits(data, o, w * cmp_count, h)
                    for y in range(h):
                        base = y * w * cmp_count
                        for x in range(w):
                            r = raw[base + (cmp_count - 3) * w + x]
                            g = raw[base + (cmp_count - 2) * w + x]
                            b = raw[base + (cmp_count - 1) * w + x]
                            img.set(x, y, (r, g, b, 255))
                else:
                    raw = data[o:o + rb * h]; o += rb * h
                    for y in range(h):
                        for x in range(w):
                            q = y * rb + x * 4
                            img.set(x, y, (raw[q + 1], raw[q + 2], raw[q + 3], 255))
            elif direct and pixel_size == 16:
                raw, o = _unpackbits(data, o, rb, h, chunk=2)
                for y in range(h):
                    for x in range(w):
                        v = struct.unpack('>H', raw[y * rb + x * 2:y * rb + x * 2 + 2])[0]
                        r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
                        img.set(x, y, (r * 255 // 31, g * 255 // 31, b * 255 // 31, 255))
            else:
                if rb < 8:
                    raw = data[o:o + rb * h]; o += rb * h
                else:
                    raw, o = _unpackbits(data, o, rb, h)
                img = _indexed(raw, rb, w, h, pixel_size if pixel_size else 1,
                               tab or {0: (255, 255, 255), 1: (0, 0, 0)})
            y0, x0 = dt - top, dl - left
            hh, ww = min(h, H - y0), min(w, W - x0)
            for yy in range(hh):
                sy = st - bt + yy
                src = img.px[(sy * w + (sl - bl)) * 4:(sy * w + (sl - bl) + ww) * 4]
                d0 = ((y0 + yy) * W + x0) * 4
                canvas.px[d0:d0 + ww * 4] = src
            continue
        SKIP = {0x0003: 2, 0x0004: 1, 0x0005: 2, 0x0007: 4, 0x0008: 2, 0x0009: 8, 0x000A: 8, 0x000B: 4,
                0x000C: 4, 0x000D: 2, 0x000E: 4, 0x000F: 4, 0x0010: 8, 0x0020: 8, 0x0021: 4, 0x0022: 6,
                0x0023: 2, 0x0030: 8, 0x0031: 8, 0x0032: 8, 0x0033: 8, 0x0034: 8, 0x0038: 0, 0x0039: 0,
                0x003A: 0, 0x003B: 0, 0x003C: 0, 0x0040: 8, 0x0041: 8, 0x0042: 8, 0x0043: 8, 0x0044: 8,
                0x0048: 0, 0x0049: 0, 0x004A: 0, 0x004B: 0, 0x004C: 0, 0x001C: 0}
        if op in SKIP:
            o += SKIP[op]; continue
        if op == 0x0028:
            o += 4; n = data[o]; o += 1 + n; continue
        if op in (0x0029, 0x002A):
            o += 1; n = data[o]; o += 1 + n; continue
        if op == 0x002B:
            o += 2; n = data[o]; o += 1 + n; continue
        if op in (0x002E, 0x002C):
            n = struct.unpack('>H', data[o:o + 2])[0]; o += 2 + n; continue
        if op == 0x8200:      # QuickTime picture: Epson's colour swatches hold an uncompressed TIFF
            n = struct.unpack('>I', data[o:o + 4])[0]
            body = data[o + 4:o + 4 + n]
            o += 4 + n
            t = body.find(b'MM\x00\x2a')
            if t >= 0:
                tif = _tiff_rgb(body[t:])
                if tif:
                    for y in range(min(tif.h, H)):
                        ww = min(tif.w, W)
                        canvas.px[y * W * 4:(y * W + ww) * 4] = tif.px[y * tif.w * 4:(y * tif.w + ww) * 4]
            continue
        raise ValueError(f'unsupported PICT opcode {op:#06x} at {o - 2}')
    return canvas
