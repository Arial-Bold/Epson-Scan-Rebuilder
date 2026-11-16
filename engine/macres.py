"""Reads classic Mac resource files (the data-fork .rsrc files in Epson's bundles). Plain Python."""
import struct


class Resource:
    def __init__(self, rid, name, data):
        self.id, self.name, self.data = rid, name, data


class ResourceFile:
    def __init__(self, path):
        d = open(path, 'rb').read()
        doff, moff, _dlen, _mlen = struct.unpack('>4I', d[:16])
        tl = moff + struct.unpack('>H', d[moff + 24:moff + 26])[0]
        nl = moff + struct.unpack('>H', d[moff + 26:moff + 28])[0]
        ntypes = (struct.unpack('>h', d[tl:tl + 2])[0] + 1) & 0xFFFF
        self.types = {}
        for i in range(ntypes):
            e = tl + 2 + i * 8
            rtype = d[e:e + 4]
            count = struct.unpack('>H', d[e + 4:e + 6])[0] + 1
            refs = tl + struct.unpack('>H', d[e + 6:e + 8])[0]
            items = {}
            for k in range(count):
                r = refs + k * 12
                rid, noff = struct.unpack('>hh', d[r:r + 4])
                off = struct.unpack('>I', d[r + 4:r + 8])[0] & 0xFFFFFF
                p = doff + off
                ln = struct.unpack('>I', d[p:p + 4])[0]
                name = None
                if noff != -1:
                    q = nl + noff
                    name = d[q + 1:q + 1 + d[q]].decode('mac_roman')
                items[rid] = Resource(rid, name, d[p + 4:p + 4 + ln])
            self.types[rtype] = items

    def __contains__(self, t):
        return t in self.types

    def __getitem__(self, t):
        return self.types[t]

    def keys(self):
        return self.types.keys()


def open_rsrc(path):
    return ResourceFile(path)
