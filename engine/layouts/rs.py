import struct
import paths
L = paths.rsrc(paths.UI_LOCALIZED); U = paths.rsrc(paths.UI_RSRC)
def pstr(b,o): n=b[o]; return b[o+1:o+1+n].decode('mac_roman'), o+1+n
def res(t, i):
    for f in (L, U):
        if t in f and i in f[t]: return f[t][i]
    return None
def dlog(i):
    d=res(b'DLOG',i).data
    t,l,b,r=struct.unpack('>4h',d[:8]); proc=struct.unpack('>h',d[8:10])[0]; items=struct.unpack('>h',d[18:20])[0]
    title,_=pstr(d,20)
    return dict(rect=(l,t,r,b), proc=proc, items=items, title=title)
def cntl(cid):
    r=res(b'CNTL',cid)
    if r is None: return None
    c=r.data; ct,cl,cb,cr=struct.unpack('>4h',c[:8]); val,vis,mx,mn,proc=struct.unpack('>hhhhh',c[8:18]); ref=struct.unpack('>I',c[18:22])[0]; ttl,_=pstr(c,22)
    return dict(id=cid, rect=(cl,ct,cr,cb), value=val, max=mx, min=mn, proc=proc, refcon=ref, title=ttl)
def ditl(i):
    di=res(b'DITL',i).data
    n=struct.unpack('>h',di[:2])[0]+1; o=2; items=[]
    for k in range(n):
        rt,rl,rb,rr=struct.unpack('>4h',di[o+4:o+12]); typ=di[o+12]; ln=di[o+13]; data=di[o+14:o+14+ln]
        o+=14+ln+(ln&1)
        it=dict(n=k+1, type=typ&0x7f, disabled=bool(typ&0x80), rect=(rl,rt,rr,rb))
        if typ&0x7f==7: it['cntl']=cntl(struct.unpack('>h',data)[0])
        elif typ&0x7f in (32,64): it['res']=struct.unpack('>h',data)[0]
        else: it['text']=data.decode('mac_roman')
        items.append(it)
    return items
