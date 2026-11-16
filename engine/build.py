#!/usr/bin/env python3
"""Scanner App Builder engine: turns Epson's own installer into the native EPSON Scan app.

    build.py check  INSTALLER.dmg            is this the right installer?
    build.py build  INSTALLER.dmg OUT_DIR    make "EPSON Scan.app" in OUT_DIR

Nothing from Epson comes with the builder: the recompiled engines, the windows, artwork, help,
data tables and messages are all made here, on this Mac, from the installer the user downloaded.

Progress goes to stdout one line at a time, for the builder window:
    STEP <n> <title>          a stage starts (1..5)
    PART <i> <total> <title>  a part of stage 3 finished
    LOG <text>                details
    CHECK ok|wrong <reason>   the answer to "check"
    DONE <path>               the app is built
    FAIL <message>            something went wrong
"""
import concurrent.futures
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import traceback
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)                       # the builder's resources: engine/, app/, vendor/
sys.path.insert(0, HERE)
import extract  # noqa: E402
import macgfx  # noqa: E402
import macres  # noqa: E402

STEPS = ['Opening the installer', "Reading Epson's files", "Converting Epson's code for Apple Silicon",
         'Copying the windows, artwork and help', 'Putting the app together']


def say(*parts):
    print(' '.join(str(p) for p in parts), flush=True)


def manifest():
    return json.load(open(os.path.join(HERE, 'installer.json')))


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def verify(payload):
    """the installer's files must be exactly the ones the recipes were made from"""
    for rel, want in manifest()['hashes'].items():
        p = os.path.join(payload, rel)
        if not os.path.isfile(p):
            return 'this installer has no ' + os.path.basename(rel)
        if sha256(p) != want:
            return os.path.basename(rel) + ' is a different version'
    return None


def thin_i386(src, dst):
    d = open(src, 'rb').read()
    if d[:4] == b'\xce\xfa\xed\xfe':                 # already a thin i386 file
        open(dst, 'wb').write(d)
        return
    magic, n = struct.unpack('>II', d[:8])
    if magic != 0xcafebabe:
        raise RuntimeError(os.path.basename(src) + ' is not a universal binary')
    for i in range(n):
        ct, _cs, off, size, _al = struct.unpack('>5I', d[8 + 20 * i:28 + 20 * i])
        if ct == 7:
            open(dst, 'wb').write(d[off:off + size])
            return
    raise RuntimeError(os.path.basename(src) + ' has no 32-bit Intel code')


def python_env(work):
    """capstone (the disassembler the converter uses): the copy that comes with the builder"""
    env = dict(os.environ)
    if os.environ.get('BUILDER_SYSTEM_CAPSTONE'):        # development runs on other systems
        return env
    # always the builder's own capstone: the generated C depends on its exact disassembly text
    lib = os.path.join(work, 'pylib')
    if not os.path.isdir(os.path.join(lib, 'capstone')):
        wheels = [w for w in os.listdir(os.path.join(ROOT, 'vendor')) if w.startswith('capstone') and w.endswith('.whl')]
        if not wheels:
            raise RuntimeError('the builder is missing its capstone package')
        zipfile.ZipFile(os.path.join(ROOT, 'vendor', wheels[0])).extractall(lib)
    env['PYTHONPATH'] = lib + os.pathsep + env.get('PYTHONPATH', '')
    return env


def convert_module(m, payload, work, core, env):
    """one Epson engine: thin it to 32-bit Intel, read its symbols, translate it to C"""
    d = os.path.join(work, 'recomp', m['module'])
    os.makedirs(d, exist_ok=True)
    binp = os.path.join(d, m['binary'])
    thin_i386(os.path.join(payload, m['source']), binp)
    rdir = os.path.join(HERE, 'recomp')
    py = sys.executable
    r = subprocess.run([py, os.path.join(rdir, 'machosyms.py'), binp, os.path.join(d, 'syms.txt'),
                        os.path.join(d, 'imports.txt')], capture_output=True, text=True, env=env)
    if r.returncode:
        raise RuntimeError('symbols of %s: %s' % (m['binary'], r.stderr[-400:]))
    out = os.path.join(core, m['output'])
    gen = m['generator']
    if gen == 'recomp.py':
        e = dict(env, ROOTS=','.join(m['roots']))
        cmd = [py, os.path.join(rdir, 'recomp.py'), m['binary'], m['prefix'], '.']
    else:
        shutil.copy(os.path.join(rdir, gen), d)
        if gen == 'ice_recomp.py':
            # this generator was written for the code section's symbols and two-column imports
            end = int(m['code_end'], 16)
            syms = [l.split() for l in open(os.path.join(d, 'syms.txt'))]
            with open(os.path.join(d, 'syms.txt'), 'w') as f:
                f.write('00000000 ___dso_handle\n')
                f.writelines('%s %s\n' % (a, n) for a, n in syms if int(a, 16) < end)
            imps = [l.split() for l in open(os.path.join(d, 'imports.txt'))]
            with open(os.path.join(d, 'imports.txt'), 'w') as f:
                f.writelines('%s %s\n' % (x[0], x[1]) for x in imps if len(x) == 2)
            with open(os.path.join(d, 'covered.txt'), 'w') as f:
                f.write('\n'.join(m['roots_by_name']) + '\n')
        e = env
        cmd = [py, os.path.join(d, gen), m['binary']]
    with open(out + '.part', 'w') as f:
        r = subprocess.run(cmd, cwd=d, stdout=f, stderr=subprocess.PIPE, text=True, env=e)
    if r.returncode:
        raise RuntimeError('converting %s: %s' % (m['binary'], r.stderr[-600:]))
    if m.get('renames'):
        txt = open(out + '.part').read()
        for a in m['renames']:
            old = 'void %s_%s(void) {' % (m['prefix'], a)
            if txt.count(old) != 1:
                raise RuntimeError('%s: function %s not found once' % (m['output'], a))
            txt = txt.replace(old, 'void %s_%s_orig(void) {' % (m['prefix'], a))
        open(out + '.part', 'w').write(txt)
    os.replace(out + '.part', out)
    return m['title']


def make_resources(payload, res, env):
    man = manifest()
    os.makedirs(os.path.join(res, 'art'), exist_ok=True)
    for name, (rel, how) in man['copies'].items():
        src = os.path.join(payload, rel)
        if how == 'thin':
            thin_i386(src, os.path.join(res, name))
        else:
            shutil.copyfile(src, os.path.join(res, name))
    hdst = os.path.join(res, 'Help')
    if os.path.isdir(hdst):
        shutil.rmtree(hdst)
    shutil.copytree(os.path.join(payload, man['help']), hdst, ignore=shutil.ignore_patterns('._*', '.DS_Store'))
    # window layouts (the original dialog resources -> JSON)
    lay = os.path.join(HERE, 'layouts')
    e = dict(env, ES_PAYLOAD=payload, ES_OUT=res)
    for script in ('export_layout2', 'export_preview', 'export_save', 'export_ts', 'export_config', 'export_modes',
                   'export_tc', 'export_ia', 'export_cp', 'export_ha'):
        r = subprocess.run([sys.executable, os.path.join(lay, script + '.py')], cwd=lay, capture_output=True,
                           text=True, env=e)
        if r.returncode:
            raise RuntimeError('window layout %s: %s' % (script, r.stderr[-400:]))
    # artwork (cicn / PICT / icl8 -> PNG)
    files = {k: macres.open_rsrc(os.path.join(payload, v)) for k, v in man['art_files'].items()}
    for name, (tag, rtype, rid) in man['art'].items():
        rf = files[tag]
        data = rf[rtype.encode()][rid].data
        if rtype == 'cicn':
            img = macgfx.cicn(data)
        elif rtype == 'PICT':
            img = macgfx.pict(data)
        else:
            mask = rf[b'ICN#'][rid].data if b'ICN#' in rf and rid in rf[b'ICN#'] else None
            img = macgfx.icl8(data, mask)
        macgfx.write_png(img, os.path.join(res, 'art', name))


def make_tables(payload, app, work):
    man = manifest()
    gen = os.path.join(HERE, 'gen')
    sys.path.insert(0, gen)
    import gen_tables
    import strings_gen
    pdflib = os.path.join(work, 'PDFLib.i386')
    thin_i386(os.path.join(payload, man['copies']['PDFLib_i386'][0]), pdflib)
    gen_tables.hrd_header(os.path.join(payload, man['hrd']), os.path.join(app, 'src', 'core', 'es_hrd_tables.h'))
    gen_tables.pdf_header(pdflib, os.path.join(app, 'src', 'core', 'es_pdf_tables.h'))
    strings_gen.generate(payload, os.path.join(gen, 'strings.json'), os.path.join(app, 'src', 'es_strings.h'))


def cmd_check(src):
    work = tempfile.mkdtemp(prefix='scannerbuilder-')
    try:
        payload = extract.open_installer(src, os.path.join(work, 'payload'))
        why = verify(payload)
        say('CHECK', 'wrong ' + why if why else 'ok')
    except extract.InstallerError as e:
        say('CHECK', 'wrong ' + str(e))
    finally:
        shutil.rmtree(work, ignore_errors=True)


def cmd_build(src, out_dir, compile_app=True):
    work = tempfile.mkdtemp(prefix='scannerbuilder-')
    try:
        say('STEP', 1, STEPS[0])
        payload = extract.open_installer(src, os.path.join(work, 'payload'))
        why = verify(payload)
        if why:
            say('FAIL', "That isn't the right installer: " + why)
            return 2
        say('STEP', 2, STEPS[1])
        app = os.path.join(work, 'EpsonScanApp')
        shutil.copytree(os.path.join(ROOT, 'app'), app)
        env = python_env(work)
        make_tables(payload, app, work)
        say('STEP', 3, STEPS[2])
        mods = json.load(open(os.path.join(HERE, 'recipes.json')))['modules']
        core = os.path.join(app, 'src', 'core')
        done = 0
        jobs = max(1, min(len(mods), (os.cpu_count() or 2)))
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            futs = [pool.submit(convert_module, m, payload, work, core, env) for m in mods]
            for f in concurrent.futures.as_completed(futs):
                title = f.result()
                done += 1
                say('PART', done, len(mods), 'Converted ' + title)
        say('STEP', 4, STEPS[3])
        make_resources(payload, os.path.join(app, 'Resources'), env)
        say('STEP', 5, STEPS[4])
        if not compile_app:
            dst = os.path.join(out_dir, 'EpsonScanApp')
            if os.path.exists(dst):
                shutil.rmtree(dst)
            shutil.move(app, dst)
            say('DONE', dst)
            return 0
        p = subprocess.Popen(['sh', 'build_app.sh'], cwd=app, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True)
        tail = []
        for line in p.stdout:
            line = line.rstrip()
            tail = (tail + [line])[-30:]
            say('LOG', line)
        if p.wait():
            say('FAIL', 'The compiler stopped:\n' + '\n'.join(tail[-12:]))
            return 1
        built = os.path.join(app, 'build', 'EPSON Scan.app')
        os.makedirs(out_dir, exist_ok=True)
        dst = os.path.join(out_dir, 'EPSON Scan.app')
        if os.path.exists(dst):
            shutil.rmtree(dst)
        shutil.move(built, dst)
        say('DONE', dst)
        return 0
    except extract.InstallerError as e:
        say('FAIL', "That isn't the right installer: " + str(e))
        return 2
    except Exception as e:
        for line in traceback.format_exc().splitlines():
            say('LOG', line)
        say('FAIL', str(e))
        return 1
    finally:
        if not os.environ.get('BUILDER_KEEP_WORK'):
            shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == 'check':
        cmd_check(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] in ('build', 'sources'):
        sys.exit(cmd_build(sys.argv[2], sys.argv[3], compile_app=sys.argv[1] == 'build'))
    else:
        print(__doc__)
        sys.exit(64)
