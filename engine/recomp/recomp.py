"""
General static recompiler for Epson Scan's 32-bit x86 plugins.

    python3 recomp.py BIN PREFIX OUT_DIR

Reads OUT_DIR/syms.txt and OUT_DIR/imports.txt (from machosyms.py) and writes
OUT_DIR/<prefix>_gen.c. Every function from the first Epson routine on
becomes `void <PREFIX>_<addr>(void)`; runtime hooks are <prefix>_import,
<prefix>_dispatch, <prefix>_fatal and the header <prefix>_rt.h.

    python3 recomp.py DIGITAL_ICE_I386 > ice_film_gen.c

Every reachable function becomes `void F_<addr>(void)`. Registers live in C
locals inside each function and are synced to the global CPU state around
calls. Flags are C locals computed eagerly; the C compiler removes the ones
nobody reads. SSE scalar math maps 1:1 onto C float/double (compile with
-ffp-contract=off so no multiply-adds get fused).
"""
import sys
import capstone
from capstone import x86

BIN, PFX, HERE = sys.argv[1], sys.argv[2], sys.argv[3]
pfx = PFX.lower()
data = open(BIN, 'rb').read()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

syms = {int(a, 16): n for a, n in (l.split() for l in open(f'{HERE}/syms.txt'))}
byname = {n: a for a, n in syms.items()}
imports = {int(l.split()[0], 16): l.split()[1] for l in open(f'{HERE}/imports.txt') if not l.rstrip().endswith('DATA')}

THUNK_BX = byname.get('___i686.get_pc_thunk.bx', -1)
THUNK_CX = byname.get('___i686.get_pc_thunk.cx', -1)
import struct as _st
def _sections(d):
    n = _st.unpack('<I', d[16:20])[0]; off = 28; out = {}
    for _ in range(n):
        cmd, sz = _st.unpack('<II', d[off:off + 8])
        if cmd == 1:
            for j in range(_st.unpack('<I', d[off + 48:off + 52])[0]):
                sec = d[off + 56 + 68 * j: off + 56 + 68 * (j + 1)]
                a, size = _st.unpack('<II', sec[32:40])
                out[sec[:16].rstrip(b'\0').decode()] = (a, a + size)
        off += sz
    return out
_S = _sections(data)
TEXT_LO = _S['__text'][0]
TEXT_HI = _S.get('__textcoal_nt', _S['__text'])[1]
_CODE = [_S['__text']] + ([_S['__textcoal_nt']] if '__textcoal_nt' in _S else [])
def in_code(a): return any(lo <= a < hi for lo, hi in _CODE)
IMPORT_LO, IMPORT_HI = _S['__jump_table']

TRACE = bool(__import__('os').environ.get('ICE_TRACE'))
REG32 = ['eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp', 'esp']
SUB16 = {'ax': 'eax', 'bx': 'ebx', 'cx': 'ecx', 'dx': 'edx', 'si': 'esi', 'di': 'edi', 'bp': 'ebp', 'sp': 'esp'}
SUB8L = {'al': 'eax', 'bl': 'ebx', 'cl': 'ecx', 'dl': 'edx'}
SUB8H = {'ah': 'eax', 'bh': 'ebx', 'ch': 'ecx', 'dh': 'edx'}
MASK = {1: '0xFFu', 2: '0xFFFFu', 4: '0xFFFFFFFFu'}
MSB = {1: 7, 2: 15, 4: 31}


def u32(a):
    return int.from_bytes(data[a:a + 4], 'little')


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


class Unsupported(Exception):
    pass


# --------------------------------------------------------------------------
# Discovery
# --------------------------------------------------------------------------

def decode(a):
    ins = next(md.disasm(data[a:a + 16], a), None)
    if ins is None:
        raise Unsupported(f'cannot decode at {a:#x}')
    return ins


NORETURN = {'__Unwind_Resume', '___cxa_throw', '_abort', '___cxa_rethrow', '__ZSt9terminatev'}

def walk(entry):
    """Recursive-descent disassembly of one function."""
    seen, targets, calls, tables = {}, set(), set(), {}
    ebx_const = None
    work = [entry]
    while work:
        a = work.pop()
        while a not in seen:
            ins = decode(a)
            seen[a] = ins
            m, nxt = ins.mnemonic, a + ins.size
            if m == 'call' and ins.operands[0].type == x86.X86_OP_IMM and ins.operands[0].imm == THUNK_BX:
                ebx_const = nxt
            if m == 'call' and ins.operands[0].type == x86.X86_OP_IMM and ins.operands[0].imm == nxt:
                # inline PIC idiom "call $+5; pop reg": just pushes its own address
                if decode(nxt).mnemonic == 'pop' and decode(nxt).op_str == 'ebx':
                    ebx_const = nxt
                a = nxt
                continue
            if m in ('ret', 'hlt', 'ud2'):
                break
            if m == 'jmp':
                op = ins.operands[0]
                if op.type == x86.X86_OP_IMM:
                    t = op.imm
                    if TEXT_LO <= t < TEXT_HI and (t in syms and t != entry):
                        break          # tail call to another function
                    if IMPORT_LO <= t < IMPORT_HI:
                        break          # tail call into a library function
                    targets.add(t)
                    a = t
                    continue
                # PIC jump table: cmp idx,N ; ja ; mov r,[ebx+idx*4+D] ; add r,ebx ; jmp r
                try:
                    tbl = find_table(seen, a, ebx_const)
                except Unsupported:
                    tables[a] = None        # indirect tail call (e.g. through a vtable)
                    break
                tables[a] = tbl
                for t in tbl:
                    targets.add(t)
                    work.append(t)
                break
            if m.startswith('j'):
                t = ins.operands[0].imm
                targets.add(t)
                work.append(t)
                a = nxt
                continue
            if m == 'call':
                op = ins.operands[0]
                if op.type == x86.X86_OP_IMM and TEXT_LO <= op.imm < TEXT_HI and op.imm not in (THUNK_BX, THUNK_CX):
                    calls.add(op.imm)
                # calls that never return (C++ unwinding, abort): the bytes after them aren't code
                if op.type == x86.X86_OP_IMM and imports.get(op.imm) in NORETURN:
                    break
            if not in_code(nxt):
                break
            a = nxt
    tails = set()
    for ins in seen.values():
        if ins.mnemonic == 'jmp' and ins.operands[0].type == x86.X86_OP_IMM:
            t = ins.operands[0].imm
            if t in syms and t != entry:
                tails.add(t)
    return seen, targets, calls | tails, tables, ebx_const


def find_table(seen, jmp_addr, ebx_const):
    addrs = sorted(x for x in seen if x < jmp_addr)[-8:]
    ins = [seen[x] for x in addrs]
    n = disp = None
    lea_disp = None
    for i in ins:
        if i.mnemonic == 'cmp' and i.operands[1].type == x86.X86_OP_IMM:
            n = i.operands[1].imm
        if i.mnemonic == 'mov' and i.operands[1].type == x86.X86_OP_MEM and i.operands[1].mem.scale == 4:
            disp = i.operands[1].mem.disp
        # variant: shl idx,2 ; lea r,[ebx+D] ; mov r,[idx+r] ; add r,ebx ; jmp r
        if i.mnemonic == 'lea' and i.operands[1].mem.base != 0 and i.reg_name(i.operands[1].mem.base) == 'ebx':
            lea_disp = i.operands[1].mem.disp
    if disp is None and lea_disp is not None and any(i.mnemonic == 'shl' for i in ins):
        disp = lea_disp
    if n is None or disp is None or ebx_const is None:
        raise Unsupported(f'unrecognised jump table at {jmp_addr:#x}')
    base = ebx_const + disp
    return [(ebx_const + s32(u32(base + 4 * k))) & 0xFFFFFFFF for k in range(n + 1)]


# --------------------------------------------------------------------------
# Operands
# --------------------------------------------------------------------------

class Ctx:
    def __init__(self, ins):
        self.ins = ins

    def rname(self, r):
        return self.ins.reg_name(r)


def addr_expr(c, op):
    m = op.mem
    parts = []
    if m.segment != 0:
        raise Unsupported('segment override')
    if m.base != 0:
        parts.append(c.rname(m.base))
    if m.index != 0:
        idx = c.rname(m.index)
        parts.append(f'{idx}*{m.scale}u' if m.scale != 1 else idx)
    if m.disp:
        parts.append(f'0x{m.disp & 0xFFFFFFFF:X}u')
    return '(uint32_t)(' + (' + '.join(parts) if parts else '0u') + ')'


def rd(c, op, size=None):
    size = size or op.size
    if op.type == x86.X86_OP_IMM:
        v = op.imm & ((1 << (8 * size)) - 1) if size <= 4 else op.imm
        return f'0x{v:X}u'
    if op.type == x86.X86_OP_REG:
        r = c.rname(op.reg)
        if r in REG32:
            return r
        if r in SUB16:
            return f'({SUB16[r]} & 0xFFFFu)'
        if r in SUB8L:
            return f'({SUB8L[r]} & 0xFFu)'
        if r in SUB8H:
            return f'(({SUB8H[r]} >> 8) & 0xFFu)'
        raise Unsupported(f'read reg {r}')
    if op.type == x86.X86_OP_MEM:
        a = addr_expr(c, op)
        return {1: f'R8({a})', 2: f'R16({a})', 4: f'R32({a})', 8: f'R64({a})'}[size]
    raise Unsupported('operand')


def wr(c, op, val, size=None):
    size = size or op.size
    if op.type == x86.X86_OP_REG:
        r = c.rname(op.reg)
        if r in REG32:
            return f'{r} = (uint32_t)({val});'
        if r in SUB16:
            R = SUB16[r]
            return f'{R} = ({R} & 0xFFFF0000u) | ((uint32_t)({val}) & 0xFFFFu);'
        if r in SUB8L:
            R = SUB8L[r]
            return f'{R} = ({R} & 0xFFFFFF00u) | ((uint32_t)({val}) & 0xFFu);'
        if r in SUB8H:
            R = SUB8H[r]
            return f'{R} = ({R} & 0xFFFF00FFu) | (((uint32_t)({val}) & 0xFFu) << 8);'
        raise Unsupported(f'write reg {r}')
    if op.type == x86.X86_OP_MEM:
        a = addr_expr(c, op)
        return {1: f'W8({a}, {val});', 2: f'W16({a}, {val});', 4: f'W32({a}, {val});', 8: f'W64({a}, {val});'}[size]
    raise Unsupported('write operand')


def is_xmm(c, op):
    return op.type == x86.X86_OP_REG and c.rname(op.reg).startswith('xmm')


def xreg(c, op):
    return c.rname(op.reg)


def xf(c, op):          # scalar float operand
    if is_xmm(c, op):
        return f'XF({xreg(c, op)})'
    return f'BF(R32({addr_expr(c, op)}))'


def xd(c, op):          # scalar double operand
    if is_xmm(c, op):
        return f'XD({xreg(c, op)})'
    return f'BD(R64({addr_expr(c, op)}))'


def x128(c, op):
    if is_xmm(c, op):
        return xreg(c, op)
    return f'R128({addr_expr(c, op)})'


def flags_logic(r, size):
    m = MASK[size]
    return (f'zf = (({r}) & {m}) == 0; sf = (({r}) >> {MSB[size]}) & 1; pf = PARITY({r}); '
            f'cf = 0; of = 0;')


COND = {
    'je': 'zf', 'jne': '!zf', 'jb': 'cf', 'jae': '!cf', 'jbe': '(cf | zf)', 'ja': '(!cf && !zf)',
    'jl': '(sf != of)', 'jge': '(sf == of)', 'jle': '(zf || sf != of)', 'jg': '(!zf && sf == of)',
    'js': 'sf', 'jns': '!sf', 'jp': 'pf', 'jnp': '!pf', 'jo': 'of', 'jno': '!of',
}


def cond_of(m):
    for p in ('set', 'cmov'):
        if m.startswith(p):
            return COND['j' + m[len(p):]]
    return COND[m]


# --------------------------------------------------------------------------
# Instruction translation
# --------------------------------------------------------------------------

def translate(ins, fn):
    """Returns C statements for one instruction (string)."""
    c = Ctx(ins)
    m = ins.mnemonic
    ops = ins.operands
    nxt = ins.address + ins.size
    o = ops

    if m in ('nop', 'cld'):
        return ''
    if m == 'mov':
        return wr(c, o[0], rd(c, o[1], o[0].size))
    if m == 'movzx':
        return wr(c, o[0], rd(c, o[1]))
    if m == 'movsx':
        s = o[1].size
        cast = {1: 'int8_t', 2: 'int16_t'}[s]
        return wr(c, o[0], f'(uint32_t)(int32_t)({cast})({rd(c, o[1])})')
    if m == 'lea':
        return wr(c, o[0], addr_expr(c, o[1]))
    if m == 'push':
        return f'{{ uint32_t _v = {rd(c, o[0], 4)}; esp -= 4; W32(esp, _v); }}'
    if m == 'pop':
        return f'{{ uint32_t _v = R32(esp); esp += 4; {wr(c, o[0], "_v")} }}'
    if m == 'leave':
        return 'esp = ebp; ebp = R32(esp); esp += 4;'
    if m in ('inc', 'dec'):
        sz = o[0].size; msk = MASK[sz]; a = rd(c, o[0])
        if m == 'inc':
            core = f'uint32_t _a = {a}, _r = (_a + 1u) & {msk}; of = _r == ((1u << {MSB[sz]}) & {msk});'
        else:
            core = f'uint32_t _a = {a}, _r = (_a - 1u) & {msk}; of = _a == ((1u << {MSB[sz]}) & {msk});'
        return '{ ' + core + f' zf = _r == 0; sf = (_r >> {MSB[sz]}) & 1; pf = PARITY(_r); ' + wr(c, o[0], '_r') + ' }'
    if m == 'neg':
        sz = o[0].size; msk = MASK[sz]
        return ('{ uint32_t _a = ' + rd(c, o[0]) + f', _r = (0u - _a) & {msk}; cf = _a != 0; '
                f'of = _a == ((1u << {MSB[sz]}) & {msk}); zf = _r == 0; sf = (_r >> {MSB[sz]}) & 1; pf = PARITY(_r); '
                + wr(c, o[0], '_r') + ' }')
    if m == 'bswap':
        return '{ uint32_t _v = ' + rd(c, o[0]) + '; ' + wr(c, o[0], '__builtin_bswap32(_v)') + ' }'
    if m == 'not':
        return wr(c, o[0], f'(~{rd(c, o[0])}) & {MASK[o[0].size]}')
    if m == 'xchg':
        a, b = rd(c, o[0]), rd(c, o[1])
        return '{ uint32_t _a = ' + a + ', _b = ' + b + '; ' + wr(c, o[0], '_b') + ' ' + wr(c, o[1], '_a') + ' }'
    if m in ('adc', 'sbb'):
        sz = o[0].size; msk = MASK[sz]
        a, b = rd(c, o[0]), rd(c, o[1], sz)
        if m == 'adc':
            core = (f'uint64_t _a = {a}, _b = {b}, _w = _a + _b + cf; uint32_t _r = (uint32_t)_w & {msk}; '
                    f'cf = (_w >> {8 * sz}) & 1; of = (((_a ^ _r) & (_b ^ _r)) >> {MSB[sz]}) & 1;')
        else:
            core = (f'uint64_t _a = {a}, _b = {b}; uint32_t _r = (uint32_t)(_a - _b - cf) & {msk}; '
                    f'cf = _a < _b + cf; of = (((_a ^ _b) & (_a ^ _r)) >> {MSB[sz]}) & 1;')
        core += f' zf = _r == 0; sf = (_r >> {MSB[sz]}) & 1; pf = PARITY(_r); ' + wr(c, o[0], '_r')
        return '{ ' + core + ' }'
    if m == 'mul':
        if o[0].size != 4:
            raise Unsupported('mul r/m8/16')
        return ('{ uint64_t _p = (uint64_t)eax * (uint64_t)(' + rd(c, o[0]) + '); eax = (uint32_t)_p; '
                'edx = (uint32_t)(_p >> 32); cf = of = edx != 0; }')
    if m == 'div':
        if o[0].size != 4:
            raise Unsupported('div r/m8/16')
        return ('{ uint64_t _n = ((uint64_t)edx << 32) | eax; uint32_t _d = ' + rd(c, o[0]) + '; '
                'eax = (uint32_t)(_n / _d); edx = (uint32_t)(_n % _d); }')
    if m in ('shrd', 'shld'):
        a, b = rd(c, o[0]), rd(c, o[1])
        cnt = rd(c, o[2], 1)
        if m == 'shrd':
            body = '_r = (_a >> _n) | (_b << (32 - _n)); cf = (_a >> (_n - 1)) & 1;'
        else:
            body = '_r = (_a << _n) | (_b >> (32 - _n)); cf = (_a >> (32 - _n)) & 1;'
        return ('{ uint32_t _a = ' + a + ', _b = ' + b + f', _n = ({cnt}) & 31u, _r; if (_n) {{ ' + body +
                ' of = 0; zf = _r == 0; sf = _r >> 31; pf = PARITY(_r); ' + wr(c, o[0], '_r') + ' } }')
    if m == 'rep stosb':
        return 'while (ecx) { W8(edi, eax); edi += 1; ecx--; }'
    if m == 'repne scasb':      # strlen / memchr idiom: stop when AL is found or ECX runs out
        return ('{ uint8_t _al = (uint8_t)eax; while (ecx) { uint8_t _m = R8(edi); edi += 1; ecx--; '
                'uint8_t _r = (uint8_t)(_al - _m); zf = _r == 0; sf = _r >> 7; cf = _al < _m; pf = PARITY(_r); '
                'of = ((_al ^ _m) & (_al ^ _r) & 0x80) != 0; if (zf) break; } }')
    if m == 'repe cmpsb':       # memcmp idiom: stop at the first difference or when ECX runs out
        return ('{ while (ecx) { uint8_t _a = R8(esi), _b = R8(edi); esi += 1; edi += 1; ecx--; '
                'uint8_t _r = (uint8_t)(_a - _b); zf = _r == 0; sf = _r >> 7; cf = _a < _b; pf = PARITY(_r); '
                'of = ((_a ^ _b) & (_a ^ _r) & 0x80) != 0; if (!zf) break; } }')
    if m == 'rep movsb':
        return 'while (ecx) { W8(edi, R8(esi)); edi += 1; esi += 1; ecx--; }'
    if m == 'rep movsd':
        return 'while (ecx) { W32(edi, R32(esi)); edi += 4; esi += 4; ecx--; }'
    if m in ('minsd', 'maxsd'):
        d = xreg(c, o[0])
        op_ = '<' if m == 'minsd' else '>'
        return f'{{ double _a = XD({d}), _b = {xd(c, o[1])}; {d} = XSETD({d}, _a {op_} _b ? _a : _b); }}'
    if m in ('cmplesd', 'cmpltsd', 'cmpeqsd', 'cmpnltsd', 'cmpnlesd'):
        d = xreg(c, o[0])
        expr = {'cmplesd': '_a <= _b', 'cmpltsd': '_a < _b', 'cmpeqsd': '_a == _b',
                'cmpnltsd': '!(_a < _b)', 'cmpnlesd': '!(_a <= _b)'}[m]
        return f'{{ double _a = XD({d}), _b = {xd(c, o[1])}; {d}.lo = ({expr}) ? ~0ull : 0ull; }}'
    if m == 'movd':
        if is_xmm(c, o[0]):
            return f'{xreg(c, o[0])} = (X){{ (uint64_t)({rd(c, o[1], 4)}), 0 }};'
        return wr(c, o[0], f'(uint32_t){xreg(c, o[1])}.lo')
    if m == 'movq' and is_xmm(c, o[0]):
        if is_xmm(c, o[1]):
            return f'{xreg(c, o[0])} = (X){{ {xreg(c, o[1])}.lo, 0 }};'
        return f'{xreg(c, o[0])} = (X){{ R64({addr_expr(c, o[1])}), 0 }};'
    if m == 'movq':
        return f'W64({addr_expr(c, o[0])}, {xreg(c, o[1])}.lo);'
    if m == 'punpckldq':
        d, sv = xreg(c, o[0]), x128(c, o[1])
        return (f'{{ X _s = {sv}; uint64_t _d0 = {d}.lo & 0xFFFFFFFFull, _d1 = {d}.lo >> 32, '
                f'_s0 = _s.lo & 0xFFFFFFFFull, _s1 = _s.lo >> 32; {d}.lo = _d0 | (_s0 << 32); {d}.hi = _d1 | (_s1 << 32); }}')
    if m == 'unpcklpd':
        d, sv = xreg(c, o[0]), x128(c, o[1])
        return f'{{ X _s = {sv}; {d}.hi = _s.lo; }}'
    if m == 'psllq':
        d = xreg(c, o[0]); n = o[1].imm
        if n > 63:
            return f'{d}.lo = 0; {d}.hi = 0;'
        return f'{d}.lo <<= {n}; {d}.hi <<= {n};'
    if m == 'psrlq':
        d = xreg(c, o[0]); n = o[1].imm
        if n > 63:
            return f'{d}.lo = 0; {d}.hi = 0;'
        return f'{d}.lo >>= {n}; {d}.hi >>= {n};'
    if m in ('addpd', 'subpd', 'mulpd', 'divpd'):
        d, sv = xreg(c, o[0]), x128(c, o[1])
        op_ = {'addpd': '+', 'subpd': '-', 'mulpd': '*', 'divpd': '/'}[m]
        return (f'{{ X _s = {sv}; {d}.lo = DB(BD({d}.lo) {op_} BD(_s.lo)); '
                f'{d}.hi = DB(BD({d}.hi) {op_} BD(_s.hi)); }}')
    if m == 'movhpd':
        if is_xmm(c, o[0]):
            return f'{xreg(c, o[0])}.hi = R64({addr_expr(c, o[1])});'
        return f'W64({addr_expr(c, o[0])}, {xreg(c, o[1])}.hi);'
    if m == 'movlpd':
        if is_xmm(c, o[0]):
            return f'{xreg(c, o[0])}.lo = R64({addr_expr(c, o[1])});'
        return f'W64({addr_expr(c, o[0])}, {xreg(c, o[1])}.lo);'
    if m == 'cvtdq2pd':
        d, sv = xreg(c, o[0]), x128(c, o[1])
        return (f'{{ X _s = {sv}; {d}.lo = DB((double)(int32_t)(uint32_t)_s.lo); '
                f'{d}.hi = DB((double)(int32_t)(uint32_t)(_s.lo >> 32)); }}')
    if m == 'cvttpd2dq':
        d, sv = xreg(c, o[0]), x128(c, o[1])
        return (f'{{ X _s = {sv}; {d}.lo = (uint64_t)(uint32_t)SSE_CVTT_D(BD(_s.lo)) | '
                f'((uint64_t)(uint32_t)SSE_CVTT_D(BD(_s.hi)) << 32); {d}.hi = 0; }}')
    if m == 'cvtsi2sd' or m == 'cvtsi2ss':
        pass
    if m == 'cwde':
        return 'eax = (uint32_t)(int32_t)(int16_t)(eax & 0xFFFFu);'
    if m == 'cdq':
        return 'edx = (uint32_t)((int32_t)eax >> 31);'

    if m in ('add', 'sub', 'cmp'):
        s = o[0].size
        a, b = rd(c, o[0]), rd(c, o[1], s)
        msk = MASK[s]
        if m == 'add':
            core = (f'uint32_t _a = {a}, _b = {b}, _r = (_a + _b) & {msk}; '
                    f'cf = _r < _a; of = (((_a ^ _r) & (_b ^ _r)) >> {MSB[s]}) & 1;')
        else:
            core = (f'uint32_t _a = {a}, _b = {b}, _r = (_a - _b) & {msk}; '
                    f'cf = _a < _b; of = (((_a ^ _b) & (_a ^ _r)) >> {MSB[s]}) & 1;')
        core += f' zf = _r == 0; sf = (_r >> {MSB[s]}) & 1; pf = PARITY(_r);'
        if m != 'cmp':
            core += ' ' + wr(c, o[0], '_r')
        return '{ ' + core + ' }'
    if m in ('and', 'or', 'xor', 'test'):
        s = o[0].size
        op_ = {'and': '&', 'or': '|', 'xor': '^', 'test': '&'}[m]
        if m == 'xor' and o[0].type == x86.X86_OP_REG and o[1].type == x86.X86_OP_REG and o[0].reg == o[1].reg:
            return wr(c, o[0], '0') + ' zf = 1; sf = 0; pf = 1; cf = 0; of = 0;'
        core = f'uint32_t _r = ({rd(c, o[0])} {op_} {rd(c, o[1], s)}) & {MASK[s]}; ' + flags_logic('_r', s)
        if m != 'test':
            core += ' ' + wr(c, o[0], '_r')
        return '{ ' + core + ' }'
    if m in ('shl', 'sar', 'shr'):
        s = o[0].size
        if len(o) == 1:
            cnt = '1u'
        else:
            cnt = rd(c, o[1], 1)
        a = rd(c, o[0])
        if m == 'shl':
            body = (f'_r = (_a << _n) & {MASK[s]}; cf = (_a >> ({8 * s} - _n)) & 1; '
                    f'of = ((_r >> {MSB[s]}) & 1) ^ cf;')
        elif m == 'shr':
            body = f'_r = _a >> _n; cf = (_a >> (_n - 1)) & 1; of = (_a >> {MSB[s]}) & 1;'
        else:
            st = {1: 'int8_t', 2: 'int16_t', 4: 'int32_t'}[s]
            body = (f'_r = (uint32_t)((int32_t)({st})_a >> _n) & {MASK[s]}; '
                    f'cf = ((int32_t)({st})_a >> (_n - 1)) & 1; of = 0;')
        return ('{ uint32_t _a = ' + a + f', _n = ({cnt}) & 31u, _r; if (_n) {{ ' + body +
                f' zf = _r == 0; sf = (_r >> {MSB[s]}) & 1; pf = PARITY(_r); ' + wr(c, o[0], '_r') + ' } }')
    if m == 'imul':
        if len(o) == 1:
            if o[0].size != 4:
                raise Unsupported('imul r/m8/16')
            return ('{ int64_t _p = (int64_t)(int32_t)eax * (int64_t)(int32_t)(' + rd(c, o[0]) + '); '
                    'eax = (uint32_t)_p; edx = (uint32_t)((uint64_t)_p >> 32); '
                    'cf = of = (_p != (int64_t)(int32_t)eax); }')
        if len(o) == 2:
            a, b, dst = rd(c, o[0]), rd(c, o[1]), o[0]
        else:
            a, b, dst = rd(c, o[1]), rd(c, o[2], 4), o[0]
        return ('{ int64_t _p = (int64_t)(int32_t)(' + a + ') * (int64_t)(int32_t)(' + b + '); '
                'uint32_t _r = (uint32_t)_p; cf = of = (_p != (int64_t)(int32_t)_r); '
                'zf = _r == 0; sf = _r >> 31; pf = PARITY(_r); ' + wr(c, dst, '_r') + ' }')
    if m == 'idiv':
        return ('{ int64_t _n = (int64_t)(((uint64_t)edx << 32) | eax); int32_t _d = (int32_t)(' +
                rd(c, o[0]) + '); eax = (uint32_t)(int32_t)(_n / _d); edx = (uint32_t)(int32_t)(_n % _d); }')
    if m.startswith('set'):
        return wr(c, o[0], f'({cond_of(m)}) ? 1u : 0u')
    if m.startswith('cmov'):
        return f'if ({cond_of(m)}) {{ {wr(c, o[0], rd(c, o[1]))} }}'
    if m == 'rep stosd':
        return 'while (ecx) { W32(edi, eax); edi += 4; ecx--; }'
    if m == 'stosd':
        return 'W32(edi, eax); edi += 4;'
    if m == 'stosb':
        return 'W8(edi, eax); edi += 1;'
    if m == 'lodsb':
        return 'eax = (eax & 0xFFFFFF00u) | R8(esi); esi += 1;'
    if m == 'lodsd':
        return 'eax = R32(esi); esi += 4;'
    if m == 'clc':
        return 'cf = 0;'
    if m == 'stc':
        return 'cf = 1;'

    # ---- SSE ----
    if m == 'movss':
        if is_xmm(c, o[0]) and is_xmm(c, o[1]):
            return f'{xreg(c, o[0])} = XSETF({xreg(c, o[0])}, XF({xreg(c, o[1])}));'
        if is_xmm(c, o[0]):
            return f'{xreg(c, o[0])} = XLOADF({xf(c, o[1])});'
        return f'W32({addr_expr(c, o[0])}, (uint32_t){xreg(c, o[1])}.lo);'
    if m == 'movsd':
        if is_xmm(c, o[0]) and is_xmm(c, o[1]):
            return f'{xreg(c, o[0])}.lo = {xreg(c, o[1])}.lo;'
        if is_xmm(c, o[0]):
            return f'{xreg(c, o[0])} = (X){{ R64({addr_expr(c, o[1])}), 0 }};'
        return f'W64({addr_expr(c, o[0])}, {xreg(c, o[1])}.lo);'
    if m in ('movaps', 'movapd', 'movups', 'movdqa'):
        if is_xmm(c, o[0]):
            return f'{xreg(c, o[0])} = {x128(c, o[1])};'
        return f'W128({addr_expr(c, o[0])}, {xreg(c, o[1])});'
    if m in ('xorps', 'pxor', 'xorpd', 'andps', 'andpd', 'orps', 'orpd', 'andnps', 'andnpd'):
        d, s = xreg(c, o[0]), x128(c, o[1])
        if m.startswith('andn'):
            return f'{{ X _s = {s}; {d}.lo = ~{d}.lo & _s.lo; {d}.hi = ~{d}.hi & _s.hi; }}'
        op_ = '^' if 'xor' in m else ('&' if m.startswith('and') else '|')
        return f'{{ X _s = {s}; {d}.lo {op_}= _s.lo; {d}.hi {op_}= _s.hi; }}'
    if m in ('addss', 'subss', 'mulss', 'divss'):
        op_ = {'addss': '+', 'subss': '-', 'mulss': '*', 'divss': '/'}[m]
        d = xreg(c, o[0])
        return f'{d} = XSETF({d}, XF({d}) {op_} {xf(c, o[1])});'
    if m in ('addsd', 'subsd', 'mulsd', 'divsd'):
        op_ = {'addsd': '+', 'subsd': '-', 'mulsd': '*', 'divsd': '/'}[m]
        d = xreg(c, o[0])
        return f'{d} = XSETD({d}, XD({d}) {op_} {xd(c, o[1])});'
    if m == 'sqrtsd':
        d = xreg(c, o[0])
        return f'{d} = XSETD({d}, sqrt({xd(c, o[1])}));'
    if m == 'sqrtss':
        d = xreg(c, o[0])
        return f'{d} = XSETF({d}, sqrtf({xf(c, o[1])}));'
    if m in ('minss', 'maxss'):
        d = xreg(c, o[0])
        f = 'SSE_MINSS' if m == 'minss' else 'SSE_MAXSS'
        return f'{d} = XSETF({d}, {f}(XF({d}), {xf(c, o[1])}));'
    if m in ('ucomiss', 'comiss', 'ucomisd', 'comisd'):
        g = xf if m.endswith('ss') else xd
        a, b = g(c, o[0]), g(c, o[1])
        t = 'float' if m.endswith('ss') else 'double'
        return (f'{{ {t} _a = {a}, _b = {b}; if (_a != _a || _b != _b) {{ zf = pf = cf = 1; }} '
                f'else {{ zf = _a == _b; cf = _a < _b; pf = 0; }} of = sf = 0; }}')
    if m in ('cmpless', 'cmpnltss', 'cmpltss', 'cmpeqss', 'cmpnless'):
        d = xreg(c, o[0])
        expr = {'cmpless': '_a <= _b', 'cmpltss': '_a < _b', 'cmpeqss': '_a == _b',
                'cmpnltss': '!(_a < _b)', 'cmpnless': '!(_a <= _b)'}[m]
        return (f'{{ float _a = XF({d}), _b = {xf(c, o[1])}; '
                f'{d}.lo = ({d}.lo & 0xFFFFFFFF00000000ull) | (({expr}) ? 0xFFFFFFFFull : 0); }}')
    if m == 'cvtss2sd':
        d = xreg(c, o[0])
        return f'{d} = XSETD({d}, (double){xf(c, o[1])});'
    if m == 'cvtsd2ss':
        d = xreg(c, o[0])
        return f'{d} = XSETF({d}, (float){xd(c, o[1])});'
    if m == 'cvtsi2ss':
        d = xreg(c, o[0])
        return f'{d} = XSETF({d}, (float)(int32_t)({rd(c, o[1], 4)}));'
    if m == 'cvtsi2sd':
        d = xreg(c, o[0])
        return f'{d} = XSETD({d}, (double)(int32_t)({rd(c, o[1], 4)}));'
    if m == 'cvttss2si':
        return wr(c, o[0], f'(uint32_t)SSE_CVTT_F({xf(c, o[1])})')
    if m == 'cvttsd2si':
        return wr(c, o[0], f'(uint32_t)SSE_CVTT_D({xd(c, o[1])})')

    # ---- x87 (only used to pass float/double return values) ----
    if m == 'fld':
        op = o[0]
        if op.type == x86.X86_OP_MEM:
            a = addr_expr(c, op)
            if op.size == 4:
                return f'FPUSH((double)BF(R32({a})));'
            if op.size == 8:
                return f'FPUSH(BD(R64({a})));'
            raise Unsupported('fld size')
        i = int(c.rname(op.reg)[3])
        return f'FPUSH(FST({i}));'
    if m == 'fild':
        a = addr_expr(c, o[0])
        cast, rdn = {2: ('int16_t', 'R16'), 4: ('int32_t', 'R32'), 8: ('int64_t', 'R64')}[o[0].size]
        return f'FPUSH((double)({cast}){rdn}({a}));'
    if m in ('fistp', 'fist'):
        a = addr_expr(c, o[0])
        sz = o[0].size
        cv = {2: 'FIST16', 4: 'FIST32', 8: 'FIST64'}[sz]
        w = {2: 'W16', 4: 'W32', 8: 'W64'}[sz]
        v = 'FPOP()' if m == 'fistp' else 'FST(0)'
        return f'{w}({a}, {cv}({v}));'
    if m == 'fldcw':
        return f'G_FCW = (uint16_t)R16({addr_expr(c, o[0])});'
    if m in ('fnstcw', 'fstcw'):
        return f'W16({addr_expr(c, o[0])}, G_FCW);'
    if m in ('wait', 'fwait'):
        return ';'
    if m == 'fstp':
        op = o[0]
        if op.type == x86.X86_OP_MEM:
            a = addr_expr(c, op)
            if op.size == 4:
                return f'W32({a}, FB((float)FPOP()));'
            if op.size == 8:
                return f'W64({a}, DB(FPOP()));'
            raise Unsupported('fstp size')
        i = int(c.rname(op.reg)[3])
        return f'{{ double _v = FST(0); FSETST({i}, _v); FPOP(); }}'

    raise Unsupported(f'{m} {ins.op_str}')


# --------------------------------------------------------------------------
# Function emission
# --------------------------------------------------------------------------

LOCALS = ('uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp; X xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7; '
          'uint32_t zf = 0, sf = 0, cf = 0, of = 0, pf = 0; (void)zf; (void)sf; (void)cf; (void)of; (void)pf;')
LOAD = ('eax = G.eax; ebx = G.ebx; ecx = G.ecx; edx = G.edx; esi = G.esi; edi = G.edi; ebp = G.ebp; esp = G.esp; '
        'xmm0 = G.xmm[0]; xmm1 = G.xmm[1]; xmm2 = G.xmm[2]; xmm3 = G.xmm[3]; '
        'xmm4 = G.xmm[4]; xmm5 = G.xmm[5]; xmm6 = G.xmm[6]; xmm7 = G.xmm[7];')
STORE = ('G.eax = eax; G.ebx = ebx; G.ecx = ecx; G.edx = edx; G.esi = esi; G.edi = edi; G.ebp = ebp; G.esp = esp; '
         'G.xmm[0] = xmm0; G.xmm[1] = xmm1; G.xmm[2] = xmm2; G.xmm[3] = xmm3; '
         'G.xmm[4] = xmm4; G.xmm[5] = xmm5; G.xmm[6] = xmm6; G.xmm[7] = xmm7;')


def emit_function(entry, info, out):
    seen, targets, calls, tables, _ = info
    name = syms.get(entry, f'sub_{entry:x}')
    out.append(f'/* {name} */')
    out.append(f'void {PFX}_{entry:x}(void) {{')
    out.append('  ' + LOCALS)
    out.append('  ' + LOAD)
    addrs = sorted(seen)
    labels = set(targets)
    for idx, a in enumerate(addrs):
        ins = seen[a]
        if a in labels:
            out.append(f' L_{a:x}:;')
        m = ins.mnemonic
        nxt = a + ins.size
        line = f'  /* {a:05x}: {m} {ins.op_str} */ ' + (f'TRACE(0x{a:x}); ' if TRACE else '')
        if m == 'ret':
            imm = ins.operands[0].imm if ins.operands else 0
            out.append(line + f'esp += {4 + imm}; {STORE} return;')
            continue
        if m in ('hlt', 'ud2'):
            out.append(line + f'{pfx}_fatal("hlt", 0x{a:x});')
            continue
        if m == 'call':
            op = ins.operands[0]
            if op.type == x86.X86_OP_IMM:
                t = op.imm
                if t == nxt:     # "call $+5; pop reg": push the address, the pop takes it
                    out.append(line + f'esp -= 4; W32(esp, 0x{nxt:X}u);')
                elif t == THUNK_BX:
                    out.append(line + f'ebx = 0x{nxt:X}u;')
                elif t == THUNK_CX:
                    out.append(line + f'ecx = 0x{nxt:X}u;')
                elif IMPORT_LO <= t < IMPORT_HI:
                    # the host function returns without popping; drop the return address here
                    out.append(line + f'esp -= 4; W32(esp, 0x{nxt:X}u); {STORE} {pfx}_import(0x{t:X}u); {LOAD} '
                               f'esp += 4; /* {imports.get(t, "?")} */')
                else:
                    out.append(line + f'esp -= 4; W32(esp, 0x{nxt:X}u); {STORE} {PFX}_{t:x}(); {LOAD}')
            else:
                tgt = rd(Ctx(ins), op, 4)
                out.append(line + f'{{ uint32_t _t = {tgt}; esp -= 4; W32(esp, 0x{nxt:X}u); {STORE} '
                                  f'{pfx}_dispatch(_t); {LOAD} }}')
        elif m == 'jmp':
            op = ins.operands[0]
            if op.type == x86.X86_OP_IMM:
                t = op.imm
                if t in seen:
                    out.append(line + f'goto L_{t:x};')
                elif IMPORT_LO <= t < IMPORT_HI:   # tail call into libc: run it, then return for it
                    out.append(line + f'{STORE} {pfx}_import(0x{t:X}u); {LOAD} esp += 4; {STORE} return; '
                               f'/* {imports.get(t, "?")} */')
                else:   # tail call
                    out.append(line + f'{STORE} {PFX}_{t:x}(); return;')
            else:
                tgt = rd(Ctx(ins), op, 4)
                if tables.get(a) is None:
                    out.append(line + f'{{ uint32_t _t = {tgt}; {STORE} {pfx}_dispatch(_t); return; }}')
                    continue
                cases = ' '.join(f'case 0x{t:X}u: goto L_{t:x};' for t in sorted(set(tables[a])))
                out.append(line + f'switch ({tgt}) {{ {cases} default: {pfx}_fatal("jump table", 0x{a:x}); }}')
        elif m.startswith('j'):
            t = ins.operands[0].imm
            out.append(line + f'if ({COND[m]}) goto L_{t:x};')
        else:
            out.append(line + translate(ins, entry))
        # explicit fallthrough when the next instruction isn't adjacent
        if m != 'jmp' and (idx + 1 >= len(addrs) or addrs[idx + 1] != nxt):
            if nxt in seen:
                out.append(f'  goto L_{nxt:x};')
                labels.add(nxt)
    out.append('}')
    out.append('')
    return labels


def main():
    glue = {'dyld_stub_binding_helper', '__dyld_func_lookup', '_cxa_atexit_check_2', '_cxa_atexit_check_1',
            '_check_cxa_atexit', '_get_globals', '_add_routine', '_run_routines', '_cxa_atexit_wrapper',
            '_atexit_common', '___cxa_atexit', '_atexit', '___i686.get_pc_thunk.bx', '___i686.get_pc_thunk.cx'}
    todo = [a for a, n in syms.items() if in_code(a) and not n.endswith('.eh') and n not in glue]
    import os
    if os.environ.get('ROOTS'):      # translate only what these functions reach
        todo = [int(x, 16) for x in os.environ['ROOTS'].split(',')]
    if os.environ.get('EXTRA'):      # more roots: switch-table targets reached only through jmp reg
        todo += [int(x, 16) for x in os.environ['EXTRA'].split(',')]
    funcs = {}
    while todo:
        f = todo.pop()
        if f in funcs or not in_code(f) or f in (THUNK_BX, THUNK_CX):
            continue
        funcs[f] = walk(f)
        todo.extend(funcs[f][2])
    out = [f'/* Generated by recomp.py from {BIN.rsplit("/", 1)[-1]} (Epson Scan 3.9.4, V550). */',
           f'#include "{pfx}_rt.h"', '']
    for f in sorted(funcs):
        out.append(f'void {PFX}_{f:x}(void);')
    out.append('')
    # first pass determines extra fallthrough labels, so emit twice
    for f in sorted(funcs):
        body = []
        extra = emit_function(f, funcs[f], body)
        seen, targets, *_ = funcs[f]
        if extra - targets:
            funcs[f] = (seen, targets | extra) + funcs[f][2:]
            body = []
            emit_function(f, funcs[f], body)
        out.extend(body)
    out.append(f'void {pfx}_dispatch_translated(uint32_t t, int *found) {{')
    out.append('  *found = 1;')
    out.append('  switch (t) {')
    for f in sorted(funcs):
        out.append(f'    case 0x{f:X}u: {PFX}_{f:x}(); return;')
    out.append('  }')
    out.append('  *found = 0;')
    out.append('}')
    print('\n'.join(out))
    print(f'/* {len(funcs)} functions */', file=sys.stderr)
    for a in sorted(funcs):
        n = syms.get(a, '')
        if n.startswith('_') and not n.startswith('__'):
            print(f'/* {n} = {PFX}_{a:x} */', file=sys.stderr)


if __name__ == '__main__':
    main()
