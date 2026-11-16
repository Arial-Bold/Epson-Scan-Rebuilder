"""List the symbols and import stubs of a 32-bit Mach-O bundle.
   python3 machosyms.py BIN syms.txt imports.txt"""
import struct, sys
d = open(sys.argv[1], 'rb').read()
ncmds = struct.unpack('<I', d[16:20])[0]
off = 28; sects = []; symtab = dysym = None
for _ in range(ncmds):
    cmd, sz = struct.unpack('<II', d[off:off + 8])
    if cmd == 1:
        nsect = struct.unpack('<I', d[off + 48:off + 52])[0]
        for j in range(nsect):
            s = d[off + 56 + 68 * j: off + 56 + 68 * (j + 1)]
            name = s[:16].rstrip(b'\0').decode(); seg = s[16:32].rstrip(b'\0').decode()
            addr, size, fo, al, reloff, nreloc, flags, r1, r2 = struct.unpack('<9I', s[32:68])
            sects.append(dict(seg=seg, name=name, addr=addr, size=size, flags=flags, r1=r1, r2=r2))
    elif cmd == 2: symtab = struct.unpack('<4I', d[off + 8:off + 24])
    elif cmd == 0xb: dysym = struct.unpack('<18I', d[off + 8:off + 80])
    off += sz
symoff, nsyms, stroff, strsize = symtab
names = []
syms = {}
for i in range(nsyms):
    strx, typ, sect, desc, val = struct.unpack('<IBBhI', d[symoff + 12 * i: symoff + 12 * i + 12])
    n = d[stroff + strx:d.index(b'\0', stroff + strx)].decode('latin1')
    names.append(n)
    if (typ & 0x0e) == 0x0e and not (typ & 0xe0) and n:   # N_SECT, not a stab
        syms.setdefault(val, n)
with open(sys.argv[2], 'w') as f:
    for a in sorted(syms): f.write(f'{a:08x} {syms[a]}\n')
indirectsymoff, nindirect = dysym[12], dysym[13]
ind = struct.unpack(f'<{nindirect}I', d[indirectsymoff:indirectsymoff + 4 * nindirect])
with open(sys.argv[3], 'w') as f:
    for s in sects:
        typ = s['flags'] & 0xff
        if typ == 8:   # S_SYMBOL_STUBS (jump table)
            n = s['size'] // s['r2']
            for k in range(n):
                idx = ind[s['r1'] + k]
                f.write(f"{s['addr'] + k * s['r2']:x} {names[idx] if idx < len(names) else '?'}\n")
        elif typ in (6, 7):   # non-lazy / lazy pointers (data imports)
            for k in range(s['size'] // 4):
                idx = ind[s['r1'] + k]
                f.write(f"{s['addr'] + 4 * k:x} {names[idx] if idx < len(names) else '?'} DATA\n")
for s in sects: print(f"{s['seg']},{s['name']} {s['addr']:#x}+{s['size']:#x} flags={s['flags']:#x}")
