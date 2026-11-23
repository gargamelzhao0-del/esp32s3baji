"""Verify the production painter and its submitted rectangles against Chrome RGB565 pixels."""
import ctypes as c
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import zlib

HERE=Path(__file__).resolve().parent
ROOT=HERE.parent
SIDE=466
class State(c.Structure):
    _fields_=[("clip",c.c_int),("frame",c.c_uint8),("gx",c.c_int8),("gy",c.c_int8)]
class Rect(c.Structure):
    _fields_=[(name,c.c_uint16) for name in ["x","y","width","height"]]
ptr=c.c_void_p
szp=c.POINTER(c.c_size_t)
inflate_fn=c.CFUNCTYPE(c.c_int,ptr,szp,ptr,szp)
checksum_fn=c.CFUNCTYPE(c.c_uint32,c.c_uint32,ptr,c.c_size_t)

@inflate_fn
def inflate(src,length,dest,capacity):
    try:
        d=zlib.decompressobj()
        raw=d.decompress(c.string_at(src,length[0]))
        if not d.eof or len(raw)>capacity[0]:
            return -1
        length[0]-=len(d.unused_data)
        capacity[0]=len(raw)
        c.memmove(dest,raw,len(raw))
        return 0
    except zlib.error:
        return -1

@checksum_fn
def crc(seed,buf,length):
    return zlib.crc32(c.string_at(buf,length),seed)

def pack(magic, raw):
    compressed = zlib.compress(raw, 1)
    return struct.pack("<4sIII", magic, len(raw), len(compressed), zlib.crc32(raw)) + compressed


def row_formats(asset):
    """Exercise real pixels through both index widths and an invalid bank."""
    assert asset[:4] == b"BPT2"
    raw = zlib.decompress(asset[16:])
    tiles, rows, patches, rectangles, idle, pat, _, _ = struct.unpack_from("<4I4H", raw)
    poses = idle + pat
    start = (24 + poses * (48 + 59*59*2) + 3) & ~3
    start += poses*15*8 + patches*4 + rectangles*8
    bank, = struct.unpack_from("<I", raw, start)
    after = start + 4 + tiles*8*2
    wide = bytearray(tiles*8*4)
    for i, (row,) in enumerate(struct.iter_unpack("<H", raw[start+4:after])):
        struct.pack_into("<I", wide, 4*i, row + (bank if i//8 >= 32768 else 0))
    old = pack(b"BPT1", raw[:start] + wide + raw[after:])
    bad = pack(b"BPT2", raw[:start] + struct.pack("<I", rows+1) + raw[start+4:])
    return old, bad


def run(build):
    for header in ["esp_err.h","esp_heap_caps.h","esp_rom_crc.h","miniz.h"]:
        (build/header).write_text('#include "host.h"\n')
    source=build/"callbacks.c"
    source.write_text('#include "host.h"\ninflate_fn inflate_host; checksum_fn checksum_host;\n'
                      'void set_callbacks(inflate_fn d,checksum_fn c){inflate_host=d;checksum_host=c;}\n')
    binary=build/("pet.dll" if os.name=="nt" else "pet.so")
    subprocess.run([os.environ.get("CC","gcc"),"-std=c11","-Wall","-Wextra","-Werror","-O2",
                    "-fPIC","-shared","-DBAJI_PET_EXTERNAL_ASSET","-I",str(build),"-I",str(HERE),
                    "-I",str(ROOT/"include"),"-I",str(ROOT.parent/"baji_graphics/include"),
                    str(ROOT/"baji_pet.c"),str(ROOT/"baji_pet_asset.c"),str(source),"-o",str(binary)],check=True)
    lib=c.CDLL(str(binary))
    lib.set_callbacks.argtypes=[inflate_fn,checksum_fn]
    lib.set_callbacks(inflate,crc)
    lib.baji_pet_open_data.argtypes=[c.POINTER(ptr),ptr,c.c_size_t]
    lib.baji_pet_select.argtypes=[ptr,State]
    lib.baji_pet_paint.argtypes=[c.POINTER(Rect),ptr,ptr]
    lib.baji_pet_damage.argtypes=[ptr,c.POINTER(State),c.POINTER(State),c.POINTER(Rect),c.c_size_t]
    lib.baji_pet_damage.restype=c.c_size_t
    lib.baji_pet_forehead.argtypes=[ptr,State,c.c_uint16,c.c_uint16]
    lib.baji_pet_forehead.restype=c.c_bool
    lib.baji_pet_close.argtypes=[ptr]
    asset=(ROOT/"assets/scene.bpz").read_bytes()
    legacy, bad_bank = row_formats(asset)
    empty = ptr()
    assert lib.baji_pet_open_data(c.byref(empty),bad_bank,len(bad_bank)) and not empty.value
    for payload in (asset, legacy):
        check_pixels(lib, payload)
    # Explicit unload lets Windows remove the temporary DLL.
    if os.name=="nt":
        import _ctypes
        _ctypes.FreeLibrary(lib._handle)


def check_pixels(lib, asset):
    golden=[v[0] for v in struct.iter_unpack("<I",(HERE/"web.crc").read_bytes())]
    p=ptr()
    for bad in [asset[:15],asset[:-1],asset[:12]+bytes(4)+asset[16:]]:
        assert lib.baji_pet_open_data(c.byref(p),bad,len(bad)) and not p.value
    assert lib.baji_pet_open_data(c.byref(p),asset,len(asset))==0
    assert lib.baji_pet_forehead(p,State(0,0),220,185)
    assert not lib.baji_pet_forehead(p,State(0,0),20,30)
    screen=c.create_string_buffer(SIDE*SIDE*2)
    band=c.create_string_buffer(SIDE*SIDE*2)
    full=Rect(0,0,SIDE,SIDE)
    rects=(Rect*40)()
    prev=None
    def step(state,limit):
        nonlocal prev
        g=(max(-1,min(1,state.gy))+1)*5+max(-2,min(2,state.gx))+2
        pose=(state.frame%30+43) if state.clip==1 else state.frame%43
        target=golden[pose*15+g]
        n=lib.baji_pet_damage(p,c.byref(prev) if prev is not None else None,
                              c.byref(state),rects,limit)
        assert n<=limit
        lib.baji_pet_select(p,state)
        for r in rects[:n]:
            assert 0<r.width<=SIDE-r.x and 0<r.height<=SIDE-r.y
            assert (r.x|r.y|r.width|r.height)%2==0
            lib.baji_pet_paint(c.byref(r),band,p)
            for row in range(r.height):
                c.memmove(c.addressof(screen)+2*((r.y+row)*SIDE+r.x),
                          c.addressof(band)+2*row*r.width,2*r.width)
        assert zlib.crc32(screen)==target,(state.clip,state.frame,g,limit,n)
        lib.baji_pet_paint(c.byref(full),band,p)
        assert zlib.crc32(band)==target
        prev=state
        return n

    for clip,count in [(0,43),(1,30)]:
        for f in range(count):
            for g in range(15):
                step(State(clip,f,g%5-2,g//5-1),40)
    # All ordinary entry and release poses, plus clock skips, simultaneous
    # face/body changes and restricted region capacities.
    rng=random.Random(372)
    for pose in range(73):
        for target_clip in (0,1):
            step(State(int(pose>=43),pose if pose<43 else pose-43,2,-1),16)
            step(State(target_clip,0,-2,1),16)
    for i in range(220):
        step(State(rng.randrange(2),rng.randrange(256),rng.randrange(-4,5),rng.randrange(-3,4)),
             [1,2,8,32][i%4])
    assert step(prev,1)==0
    step(State(1,12,-2,-1),16)
    assert step(State(1,12,2,1),16)==0
    # Public painter also accepts single pixels and unaligned crops.
    for _ in range(50):
        x,y=rng.randrange(SIDE),rng.randrange(SIDE)
        w,h=rng.randrange(1,SIDE-x+1),rng.randrange(1,SIDE-y+1)
        lib.baji_pet_paint(c.byref(Rect(x,y,w,h)),band,p)
        for row in range(h):
            assert c.string_at(c.addressof(band)+row*w*2,w*2)==c.string_at(
                c.addressof(screen)+((y+row)*SIDE+x)*2,w*2)
    lib.baji_pet_close(p)
    print(asset[:4].decode(), "1095 web RGB565 states, 146 clip entries/returns, 220 arbitrary jumps and 50 odd crops passed.")

if __name__=="__main__":
    with tempfile.TemporaryDirectory() as temp:
        run(Path(temp))
