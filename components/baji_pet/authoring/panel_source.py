"""Freeze the full-body GPT atlas cells shared by both device playback paths."""
import argparse
from contextlib import contextmanager
from io import BytesIO
import json
from pathlib import Path
import tempfile
import zipfile

from PIL import Image

ARCHIVE = Path(__file__).with_name("panel-reference.zip")
SCRIPTS = ("clips.js", "gesture-clips.js", "nap-prop.js", "face-fx.js", "display.js")


def metadata(web):
    clips = {}
    for script in SCRIPTS[:2]:
        source = (web / script).read_text(encoding="utf8")
        clips.update(json.JSONDecoder().raw_decode(source[source.index("{"):])[0])
    return clips


def freeze(web, archive):
    clips = metadata(web)
    bounds = {}

    def include(name, width, height):
        w, h = bounds.get(name, (0, 0))
        bounds[name] = max(w, width), max(h, height)

    for action, entry in clips.items():
        for clip in (entry, *entry.get("scenes", {}).values()):
            w, h = clip.get("tileW", 138), clip.get("tileH", 132)
            end = clip.get("offset", 0) + clip["frames"]
            # The second bank of a sleeping pose contains its foreground hands.
            include(clip.get("atlas", action), (16 if "cushion" in clip else 8)*w, ((end+7)//8)*h)
            if clip.get("gear"):
                include(clips[clip["gear"]]["gearAtlas"], w, h)

    with zipfile.ZipFile(archive, "w") as out:
        def put(name, data):
            info = zipfile.ZipInfo(name)
            info.compress_type = zipfile.ZIP_DEFLATED
            out.writestr(info, data, compresslevel=9)

        for script in (*SCRIPTS, "licenses/CrossPet.md", "licenses/Fusion-Pixel-OFL.txt"):
            put(script, (web/script).read_bytes().replace(b"\r\n", b"\n"))
        put("CREDITS.txt", (Path(__file__).resolve().parents[1]/"assets/CREDITS.txt").read_bytes().replace(b"\r\n", b"\n"))
        for name, (w, h) in sorted(bounds.items()):
            with Image.open(web/"assets"/(name+".png")) as atlas:
                if w > atlas.width or h > atlas.height:
                    raise ValueError(f"{name}: required {w}x{h} exceeds {atlas.size}")
                buf = BytesIO()
                atlas.crop((0, 0, w, h)).save(buf, format="PNG")
                put(f"assets/{name}.png", buf.getvalue())


@contextmanager
def panel_source():
    with tempfile.TemporaryDirectory() as tmp, zipfile.ZipFile(ARCHIVE) as archive:
        archive.extractall(tmp)
        yield Path(tmp)


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("web", type=Path)
    cli.add_argument("--output", type=Path, default=ARCHIVE)
    args = cli.parse_args()
    freeze(args.web, args.output)
