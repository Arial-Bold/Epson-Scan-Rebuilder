"""Opens Epson's installer: the DMG (macOS mounts it with hdiutil), the flat package inside it (a xar
archive) and the package's Payload (gzip'd cpio). Plain Python, no third-party modules."""
import gzip
import io
import os
import shutil
import struct
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zlib

PKG_NAME = 'EPSON Scan.pkg'


class InstallerError(Exception):
    pass


def mount_dmg(dmg):
    """Mounts the DMG read-only; returns (mount point, detach function). Linux test runs use 7z."""
    if shutil.which('hdiutil'):
        mnt = tempfile.mkdtemp(prefix='epsondmg-')
        r = subprocess.run(['hdiutil', 'attach', '-nobrowse', '-readonly', '-noautoopen', '-mountpoint', mnt, dmg],
                           capture_output=True, text=True)
        if r.returncode:
            raise InstallerError('macOS could not open the disk image: ' + (r.stderr.strip() or r.stdout.strip()))
        return mnt, lambda: subprocess.run(['hdiutil', 'detach', '-force', mnt], capture_output=True)
    if shutil.which('7z'):
        out = tempfile.mkdtemp(prefix='epsondmg-')
        subprocess.run(['7z', 'x', '-y', '-o' + out, dmg], capture_output=True)
        for root, dirs, files in os.walk(out):
            if PKG_NAME in files or PKG_NAME in dirs:
                return root, lambda: shutil.rmtree(out, ignore_errors=True)
        shutil.rmtree(out, ignore_errors=True)
        raise InstallerError('this disk image has no Epson Scan package')
    raise InstallerError('no tool to open disk images (hdiutil)')


def xar_files(path):
    """Reads a xar archive: {name path: (offset, length, encoding)} plus the open file."""
    f = open(path, 'rb')
    head = f.read(28)
    if head[:4] != b'xar!':
        raise InstallerError('the package inside the disk image is not a flat package')
    hsize, _ver, toc_c, toc_u, _ck = struct.unpack('>HHQQI', head[4:28])
    f.seek(hsize)
    toc = zlib.decompress(f.read(toc_c))
    heap = hsize + toc_c
    root = ET.fromstring(toc)
    out = {}

    def walk(node, prefix):
        for fe in node.findall('file'):
            name = prefix + fe.findtext('name')
            data = fe.find('data')
            if data is not None:
                enc = data.find('encoding').get('style')
                out[name] = (heap + int(data.findtext('offset')), int(data.findtext('length')), enc)
            walk(fe, name + '/')
    walk(root.find('toc'), '')
    return f, out


def xar_read(f, entry):
    off, length, enc = entry
    f.seek(off)
    raw = f.read(length)
    if 'gzip' in enc:
        return zlib.decompress(raw)
    if 'bzip2' in enc:
        import bz2
        return bz2.decompress(raw)
    return raw


def cpio_extract(stream, dest):
    """Unpacks an "odc" (070707) cpio archive into dest; returns the file count."""
    n = 0
    while True:
        hdr = stream.read(76)
        if len(hdr) < 76 or hdr[:6] != b'070707':
            raise InstallerError('the package payload is damaged')
        mode = int(hdr[18:24], 8)
        namesize = int(hdr[59:65], 8)
        filesize = int(hdr[65:76], 8)
        name = stream.read(namesize)[:-1].decode('utf-8', 'replace')
        if name == 'TRAILER!!!':
            return n
        data = stream.read(filesize)
        rel = os.path.normpath(name).lstrip('/')
        if rel.startswith('..'):
            raise InstallerError('unsafe path in the package payload')
        path = os.path.join(dest, rel)
        kind = mode & 0o170000
        if kind == 0o040000:
            os.makedirs(path, exist_ok=True)
        elif kind == 0o120000:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            if not os.path.lexists(path):
                os.symlink(data.decode(), path)
        elif kind == 0o100000:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'wb') as o:
                o.write(data)
            n += 1


def open_installer(src, dest):
    """src: the DMG (or the .pkg, or an already unpacked payload folder). Unpacks the payload into dest."""
    if os.path.isdir(src) and os.path.isdir(os.path.join(src, 'Library')):
        return src
    detach = None
    pkg = src
    if not src.lower().endswith('.pkg'):
        mnt, detach = mount_dmg(src)
        pkg = os.path.join(mnt, PKG_NAME)
    try:
        if not os.path.isfile(pkg):
            raise InstallerError('this disk image has no Epson Scan package')
        f, files = xar_files(pkg)
        payload = next((v for k, v in files.items() if k.endswith('Payload')), None)
        if not payload:
            raise InstallerError('the Epson Scan package has no payload')
        data = xar_read(f, payload)
        f.close()
        if data[:2] == b'\x1f\x8b':
            data = gzip.decompress(data)
        os.makedirs(dest, exist_ok=True)
        cpio_extract(io.BytesIO(data), dest)
    finally:
        if detach:
            detach()
    return dest
