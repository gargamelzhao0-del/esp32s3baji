"""Exercise every web drawing on the target C decoder, including partial updates."""
import ctypes as c
import importlib.util
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import zlib

import numpy as np

HERE=Path(__file__).resolve().parent
COMPONENT=HERE.parent
COMPONENTS=COMPONENT.parent
SHARED=COMPONENTS/"baji_pet/tests"
spec=importlib.util.spec_from_file_location("pet_test_host",SHARED/"run.py")
host=importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)
ptr=c.c_void_p
Rect=host.Rect
SIDE=466


def compile_decoder(build):
    for h in ("esp_heap_caps.h","esp_rom_crc.h","miniz.h"):
        (build/h).write_text('#include "host.h"\n')
    (build/"esp_err.h").write_text('#pragma once\n#include "host.h"\n'
        'enum {ESP_ERR_NOT_FOUND=10, ESP_ERR_INVALID_CRC};\n')
    (build/"esp_partition.h").write_text('#include "esp_err.h"\n'
        'enum {ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY};\n'
        'typedef struct {size_t size;} esp_partition_t;\n'
        'const esp_partition_t *esp_partition_find_first(int,int,const char *);\n'
        'esp_err_t esp_partition_read(const esp_partition_t *,size_t,void *,size_t);\n')
    path=build/("clips.dll" if os.name=="nt" else "clips.so")
    subprocess.run([os.environ.get("CC","gcc"),"-std=c11","-Wall","-Wextra","-Werror","-O2",
        "-DESP_PLATFORM","-fPIC","-shared","-I",str(build),"-I",str(SHARED),
        "-I",str(COMPONENT/"include"),"-I",str(COMPONENTS/"baji_graphics/include"),
        str(COMPONENT/"pet_clips.c"),str(HERE/"host_partition.c"),"-lm","-o",str(path)],check=True)
    dll=c.CDLL(str(path))
    dll.mount_bank.argtypes=[ptr,c.c_size_t,host.inflate_fn,host.checksum_fn]
    dll.pet_clips_open.argtypes=[c.POINTER(ptr)]
    dll.pet_clips_open_data.argtypes=[c.POINTER(ptr),ptr,c.c_size_t]
    dll.pet_clips_lookup.argtypes=[ptr,c.c_char_p,c.c_char_p]
    dll.pet_clips_lookup.restype=ptr
    dll.pet_clip_frames.argtypes=dll.pet_clip_fps.argtypes=[ptr]
    dll.pet_clips_select.argtypes=[ptr,ptr,c.c_uint,c.POINTER(Rect)]
    dll.pet_clips_paint.argtypes=[c.POINTER(Rect),ptr,ptr]
    dll.pet_clips_close.argtypes=[ptr]
    dll.pet_clip_pillow.argtypes=[ptr,c.c_uint,c.POINTER(Rect)]
    dll.pet_clip_pillow.restype=c.c_bool
    dll.pet_clip_pillow_contains.argtypes=[ptr,ptr,c.c_uint,c.c_int,c.c_int]
    dll.pet_clip_pillow_contains.restype=c.c_bool
    return dll


def exercise(lib):
    data=(COMPONENT/"assets/clips.bpc").read_bytes()
    golden=(HERE/"chrome.crc").read_bytes()
    magic,total,crc,nclip,nframe,_,_,_=struct.unpack_from("<4s7I",data)
    assert magic==b"BPC1" and len(data)==total and zlib.crc32(data[32:])==crc
    assert len(golden)==4*nframe
    out=ptr()
    assert lib.pet_clips_open_data(c.byref(out),data,len(data)-1)!=0 and not out.value
    damaged=data[:-1]+bytes([data[-1]^1])
    lib.mount_bank(damaged,len(data),host.inflate,host.crc)
    assert lib.pet_clips_open(c.byref(out))!=0 and not out.value
    old_projection=data[:28]+bytes(4)+data[32:]
    lib.mount_bank(old_projection,len(data),host.inflate,host.crc)
    assert lib.pet_clips_open(c.byref(out))!=0 and not out.value
    lib.mount_bank(data,len(data),host.inflate,host.crc)
    assert lib.pet_clips_open(c.byref(out))==0 and out.value
    entries=[]
    for i in range(nclip):
        name,scene,first,count,fps,_=struct.unpack_from("<24s16sIHBB",data,32+48*i)
        name=name.rstrip(b"\0")
        scene=scene.rstrip(b"\0")
        clip=lib.pet_clips_lookup(out,name,scene)
        assert clip and lib.pet_clip_frames(clip)==count and lib.pet_clip_fps(clip)==fps
        entries.append((name,scene,clip,first,count))
    assert not lib.pet_clips_lookup(out,b"missing",None)
    assert lib.pet_clips_lookup(out,b"doze",b"missing")==lib.pet_clips_lookup(out,b"doze",None)
    screen=np.full((SIDE,SIDE),0xaaaa,dtype="u2")
    buffer=np.zeros_like(screen)
    def check(entry,i):
        name,scene,clip,first,_=entry
        r=Rect()
        assert lib.pet_clips_select(out,clip,i,c.byref(r))==0,(name,scene,i)
        if r.width:
            assert r.height and r.x+r.width<=SIDE and r.y+r.height<=SIDE
            lib.pet_clips_paint(c.byref(r),buffer.ctypes.data,out)
            screen[r.y:r.y+r.height,r.x:r.x+r.width]=buffer.ravel()[:r.width*r.height].reshape(r.height,r.width)
        actual=zlib.crc32(screen)
        expected,=struct.unpack_from("<I",golden,4*(first+i))
        assert actual==expected,(name,scene,i,hex(actual),hex(expected))
    for entry in entries:
        for i in range(entry[4]):
            check(entry,i)
    rng=random.Random(466)
    for _ in range(180):
        e=rng.choice(entries)
        f=rng.randrange(e[4])
        check(e,f)
        check(e,f)
    # Check the production streamed pillow mask at every native cell. Grip
    # and transparent regions leave holes within the visible fabric bounds.
    clip=lib.pet_clips_lookup(out,b"doze",None)
    r=Rect()
    assert not lib.pet_clip_pillow(clip,0,c.byref(r))
    frame=22
    assert lib.pet_clip_pillow(clip,frame,c.byref(r))
    inside=[lib.pet_clip_pillow_contains(out,clip,frame,x,y)
            for y in range(r.y,r.y+r.height,2) for x in range(r.x,r.x+r.width,2)]
    assert 50<sum(inside)<len(inside)-50 and not lib.pet_clip_pillow_contains(out,clip,frame,0,0)
    lib.pet_clips_close(out)
    print(f"{nclip} entries / {nframe} sequential frames and 180 random seeks match independent Chrome RGB565; pillow and invalid pack checks passed.")


if __name__=="__main__":
    with tempfile.TemporaryDirectory() as folder:
        lib=compile_decoder(Path(folder))
        try:
            exercise(lib)
        finally:
            if os.name=="nt":
                import _ctypes
                _ctypes.FreeLibrary(lib._handle)
