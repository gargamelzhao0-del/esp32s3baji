"""Build the offline sleeve, palm and clean-plate atlas from the credited CrossPet poses.

Requires Pillow, NumPy and SciPy. All contours are in original portrait pixels.
"""
import base64
from io import BytesIO
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import binary_dilation, distance_transform_edt, map_coordinates

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "assets/ai-girls/state-poses/gpt"
LEFT_ARM = [(202,366),(212,368),(220,380),(223,391),(220,404),(216,414),(207,425),
            (201,436),(199,449),(187,453),(172,454),(157,448),(152,439),(154,419),
            (164,410),(166,405),(173,396),(181,387),(191,376)]
RIGHT_ARM = [(334,367),(345,371),(355,384),(366,399),(376,406),(389,405),(399,418),
             (402,431),(399,444),(389,452),(372,454),(357,449),(349,443),(345,430),
             (334,421),(327,414),(323,398),(323,384)]
OPEN_HAND = [(195,388),(190,378),(187,371),(177,373),(168,373),(158,368),(153,364),
             (152,361),(153,358),(156,357),(162,359),(167,357),(176,351),(171,349),
             (168,346),(168,343),(169,341),(172,340),(179,343),(185,343),(191,345),
             (196,350),(200,352),(203,359),(204,370),(202,378)]
FIST = [(157,430),(159,420),(164,413),(172,409),(182,408),(190,411),(197,419),
        (199,428),(197,437),(191,446),(181,451),(172,451),(164,447),(158,440)]
CUFF = [(204,367),(210,369),(217,376),(221,384),(220,396),(216,409),(205,428),
        (197,438),(198,423),(191,413),(181,408),(168,409),(161,416),(162,408),
        (171,398),(179,392),(180,385),(187,381),(193,379),(197,374)]


def matte(points, size):
    canvas = Image.new("L", (size[0]*4, size[1]*4))
    ImageDraw.Draw(canvas).polygon([(x*4, y*4) for x, y in points], fill=255)
    return canvas.resize(size, Image.Resampling.LANCZOS)


def cutout(image, points, box):
    crop = image.crop(box)
    crop.putalpha(matte(points, image.size).crop(box))
    return crop


def exposed_silk(idle, happy):
    # Register the two waist flowers, then sample the exposed skirt folds outside them.
    y, x = np.mgrid[:idle.height, :idle.width]
    donor_x = x + 35 - (x-218)/99 + np.where(x < 272, -18, 18)
    donor_y = y + 8 + 2*(x-218)/99
    rgb = np.asarray(happy).astype(float)
    cloth = np.stack([map_coordinates(rgb[:,:,k], [donor_y, donor_x], order=1, mode="nearest")
                      for k in range(3)], axis=2)
    outlines = np.maximum(matte(LEFT_ARM, idle.size), matte(RIGHT_ARM, idle.size))
    mask = binary_dilation(outlines > 60, iterations=2)
    delta = np.asarray(idle)[:,:,:3].astype(float) - cloth
    nearest = distance_transform_edt(mask, return_distances=False, return_indices=True)
    delta[mask] = delta[tuple(nearest[:,mask])]
    ys, xs = np.where(mask)
    # Harmonic boundary correction preserves the donor's ink gradients.
    for _ in range(1400):
        delta[ys,xs] = .25*(delta[ys-1,xs] + delta[ys+1,xs] + delta[ys,xs-1] + delta[ys,xs+1])
    patch = Image.fromarray(np.clip(delta + cloth, 0, 255).astype("uint8"))
    patch.putalpha(Image.fromarray((mask*255).astype("uint8")))
    return patch.crop((145,360,408,458))


def main():
    idle = Image.open(ART / "idle.webp").convert("RGBA")
    happy = Image.open(ART / "happy.webp").convert("RGBA")
    atlas = Image.new("RGBA", (263,175))
    atlas.paste(exposed_silk(idle, happy), (0,0))
    atlas.paste(cutout(idle, FIST, (154,405,202,454)), (0,103))
    atlas.paste(cutout(happy, OPEN_HAND, (151,339,206,390)), (55,103))
    atlas.paste(cutout(idle, CUFF, (158,365,224,440)), (120,100))
    compressed = BytesIO()
    atlas.save(compressed, format="WEBP", lossless=True, method=6)
    encoded = base64.b64encode(compressed.getvalue()).decode("ascii")
    script = ("/* Registered sleeve patches and hand cutouts from the credited CrossPet idle and happy poses. */\n"
              f'window.GPT_ARM_ART="data:image/webp;base64,{encoded}";\n')
    (ROOT / "design/gpt-pet/arms-art.js").write_text(script, encoding="ascii")


if __name__ == "__main__":
    main()
