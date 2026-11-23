"""Encode per-pose RGB565 changes at the 184x184 LCD pixel grid."""
import struct
import zlib
import numpy as np
from bake import changes, cost, eye_bounds, rect

COUNT = 262
GRID = 184


def diff(a, b):
    flat = b.ravel()
    ids = np.flatnonzero(a.ravel() != flat)
    return np.column_stack((ids,flat[ids])).astype("<u2").tobytes()


def split_plan(mask):
    """Partition the change mask along gaps in both axes within 16 bus windows."""
    # Price native windows on pairs of physical pixels so every edge is even.
    reduced = mask.reshape(233,2,233,2).any(axis=(1,3))
    def box(region):
        x,y,r,b=region
        yy,xx=np.nonzero(reduced[y:b,x:r])
        if len(xx)==0:
            return None
        return (x+int(xx.min()),y+int(yy.min()),x+int(xx.max())+1,y+int(yy.max())+1)
    def score(region):
        x,y,r,b=region
        return 4*(r-x)*(b-y)+500
    def best_split(region):
        x,y,r,b=region
        best=(0,None)
        for vertical in (False,True):
            cells=reduced[y:b,x:r]
            if vertical: cells=cells.T
            active=cells.any(axis=1)
            idx=np.flatnonzero(active)
            if len(idx)<2: continue
            first=np.argmax(cells,axis=1)[idx]
            last=cells.shape[1]-np.argmax(cells[:,::-1],axis=1)[idx]
            left=np.minimum.accumulate(first)
            right=np.maximum.accumulate(last)
            lo=np.minimum.accumulate(first[::-1])[::-1]
            hi=np.maximum.accumulate(last[::-1])[::-1]
            area=((idx[:-1]+1)*(right[:-1]-left[:-1]) +
                  (cells.shape[0]-idx[1:])*(hi[1:]-lo[1:]))
            # Price both new windows on the physical grid.
            k=int(np.argmin(area))
            gain=score(region)-int(area[k])*4-1000
            if gain>best[0]:
                cut=int(idx[k])+1
                pair=([(x,y,x+cut,b),(x+cut,y,r,b)] if vertical else
                      [(x,y,r,y+cut),(x,y+cut,r,b)])
                best=gain,[box(v) for v in pair]
        return best
    outer=box((0,0,233,233))
    if outer is None: return []
    live={outer:best_split(outer)}
    while len(live)<16:
        item=max(live,key=lambda r:live[r][0])
        gain,pair=live[item]
        if not gain:break
        del live[item]
        for child in pair: live[child]=best_split(child)
    return [tuple(2*v for v in r) for r in live]


def transition_masks(frames):
    # Gesture entry can interrupt any sample. Run-length encode the bit mask
    # of differing native cells. The destination keyframe provides the colours.
    ranges, data = [], bytearray()
    for frame in frames:
        for target in (frames[0],frames[172]):
            bits=np.packbits((frame!=target).ravel(),bitorder="little")
            changed=np.flatnonzero(bits)
            start=len(data)
            if len(changed):
                for group in np.split(changed,np.flatnonzero(np.diff(changed)>2)+1):
                    first,last=int(group[0]),int(group[-1])+1
                    data.extend(struct.pack("<HH",first,last-first))
                    data.extend(bits[first:last].tobytes())
            ranges.append(struct.pack("<IHH",start,len(data)-start,0))
    return b"".join(ranges)+data


def encode(grid, quads, heads):
    axis = ((2*np.arange(466)+1)*368-1)//932
    # Two adjacent 368 px samples encode one logical pixel and its mesh.
    physical = axis//2
    keys, diffs, gaze_data, rects = [],bytearray(),bytearray(),bytearray()
    key_ids, ranges, gazes, plans, eyes = [],[],[],[],[]
    base = [grid(i,7) for i in range(COUNT)]
    def add_change(table, data, stride):
        start=len(table)//stride
        table.extend(data)
        return struct.pack("<IHH",start,len(data)//stride,0)
    for i,frame in enumerate(base):
        index=i if i<172 else i-172
        if index%24==0:
            key_ids.append(len(keys))
            keys.append(frame.astype("<u2").tobytes())
        else:
            key_ids.append(65535)
        prev = (171 if i==0 else 261 if i==172 else i-1)
        ranges.append(add_change(diffs,diff(base[prev],frame),4))
        eye_mask = np.zeros((GRID,GRID),dtype=bool)
        for g in range(15):
            image=grid(i,g)
            eye_mask |= frame!=image
            gazes.append(add_change(gaze_data,diff(frame,image),4))
        eyes.append(b"".join(rect(r) for r in eye_bounds(eye_mask[physical[:,None],physical])))
        for a,b in ((base[prev],frame),(frame,base[0]),(frame,base[172])):
            # Comparing quad IDs deliberately includes the whole native pixel.
            mask=(a!=b)[physical[:,None],physical]
            areas=min((changes(mask,np.zeros_like(mask)),split_plan(mask)),key=lambda a:sum(map(cost,a)))
            plans.append(add_change(rects,b"".join(rect(r) for r in areas),8))
    meta=struct.pack("<5I",len(quads)//8,len(diffs)//4,len(gaze_data)//4,len(rects)//8,len(keys))
    raw = (meta + np.asarray(heads,dtype="<i2").tobytes() + np.array(key_ids,dtype="<u2").tobytes() +
           b"".join(ranges) + b"".join(gazes) + b"".join(plans) + b"".join(eyes) + quads +
           b"".join(keys)+diffs+gaze_data+rects)
    raw += transition_masks(base)
    packed=zlib.compress(raw,9)
    print(f"{len(raw)} decoded bytes, {len(packed)} compressed, {len(quads)//8} LCD quads",flush=True)
    return struct.pack("<4sIII",b"BPH5",len(raw),len(packed),zlib.crc32(raw))+packed
