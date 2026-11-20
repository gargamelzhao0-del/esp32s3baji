"""Replay local pet motions in Chromium and export per-frame geometry and raster measurements."""
import argparse
import base64
import csv
from pathlib import Path

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
COLUMNS = ("action", "frame", "max_stretch", "flipped", "over_1_5", "source_x", "source_y",
           "opaque_pixels", "isolated_holes", "changed_rgba_pixels")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="Directory for frames.csv and keyframes")
    parser.add_argument("--browser", help="Path to an installed Chrome or Chromium executable")
    parser.add_argument("--case", action="append", dest="cases", help="Run only these motion names")
    parser.add_argument("--check", action="store_true", help="Fail on lost face, flipped mesh or broken firm contact")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    failures = []
    with sync_playwright() as runner:
        browser = runner.chromium.launch(executable_path=args.browser, headless=True)
        page = browser.new_page()
        page.goto((ROOT / "design/badge-prototype.html").as_uri())
        page.wait_for_function("dragon.canvas.dataset.ready==='true'")
        page.evaluate("dragon.manual=true;dragon.refresh()")
        page.add_script_tag(path=str(ROOT / "tools/pet-motion-samples.js"))
        names = page.evaluate("motionCases.map(entry=>entry[0])")
        if args.cases and set(args.cases) - set(names):
            parser.error("Available cases: " + ", ".join(names))
        with (args.out / "frames.csv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(COLUMNS)
            for name in args.cases or names:
                result = page.evaluate("name=>auditPetMotion(name)", name)
                failures.extend((name, frame, reason) for frame, reason in result["failures"])
                for row in result["results"]:
                    writer.writerow([name, *row])
                for index, png in result["pictures"]:
                    (args.out / f"{name}-{index}.png").write_bytes(base64.b64decode(png))
                print(name, len(result["results"]), flush=True)
        browser.close()
    if failures:
        print(f"{len(failures)} frame checks failed. First frames: {failures[:8]}")
        if args.check:
            raise SystemExit(1)


if __name__ == "__main__":
    main()
