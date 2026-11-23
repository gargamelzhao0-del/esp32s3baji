"""Exercise the production animation, touch recognizer, dirty redraws and RGB565 painter."""
import argparse
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
COMPONENT = HERE.parent
GRAPHICS = COMPONENT.parent / "baji_graphics" / "include"
CLIP_BASES = (0, 8, 20, 30, 38, 46)


def item_frame(pose):
    if pose >= 54:
        return 6 + pose - 54, 0
    clip = max(i for i, start in enumerate(CLIP_BASES) if pose >= start)
    return clip, pose - CLIP_BASES[clip]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, default=COMPONENT / "assets/scene.dsk")
    parser.add_argument("--goldens", type=Path, default=COMPONENT / "authoring/web.crc")
    options = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        flags = ["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-I" + str(GRAPHICS), "-I" + str(COMPONENT / "include"),
                 str(COMPONENT / "deepseek_review.c"), str(COMPONENT / "deepseek_canvas.c")]
        checker = Path(tmp) / "check.exe"
        renderer = Path(tmp) / "render.exe"
        controls = Path(tmp) / "controls.exe"
        subprocess.run(flags + [str(HERE / "review_test.c"), "-o", str(checker)], check=True)
        subprocess.run([str(checker), str(options.pack)], check=True)
        subprocess.run(flags + ["-I" + str(COMPONENT),
                       "-I" + str(COMPONENT.parent / "baji_deepseek_hd" / "include"),
                       str(COMPONENT / "deepseek_controls.c"), str(HERE / "controls_test.c"),
                       "-o", str(controls)], check=True)
        subprocess.run([str(controls)], check=True)
        # Borrow the board/runtime header stubs already exercised by their
        # host suites. HD allocation and DMA failures are injected by the test.
        headers = Path(tmp) / "headers"
        for relative in ("esp_err.h", "esp_timer.h", "freertos/FreeRTOS.h", "freertos/semphr.h"):
            dest = headers / relative
            dest.parent.mkdir(exist_ok=True, parents=True)
            dest.write_text('#include "sim.h"\n', encoding="ascii")
        repo_tests = COMPONENT.parents[1] / "tests"
        presenter = Path(tmp) / "presenter.exe"
        includes = [headers, repo_tests / "runtime", repo_tests / "display", COMPONENT,
                    *(COMPONENT.parent / c / "include" for c in
                      ("baji_board", "baji_peripherals", "baji_runtime", "baji_scene_damage", "baji_deepseek_hd",
                       "baji_agent_port", "baji_core", "baji_agent_ui"))]
        subprocess.run(flags + ["-DBAJI_DEEPSEEK_EXTERNAL_ASSET", "-DESP_ERR_NOT_SUPPORTED=0x106",
                       *["-I" + str(path) for path in includes],
                       str(COMPONENT / "deepseek_app.c"), str(COMPONENT / "deepseek_controls.c"),
                       str(COMPONENT.parent / "baji_scene_damage/baji_scene_damage.c"),
                       str(COMPONENT.parent / "baji_agent_ui/agent_ui.c"),
                       str(HERE / "presenter_test.c"), "-o", str(presenter)], check=True)
        subprocess.run([str(presenter), str(options.pack)], check=True)
        subprocess.run(flags + [str(HERE / "render.c"), "-o", str(renderer)], check=True)

        requests, expected, seen = [], [], set()
        for line in options.goldens.read_text(encoding="ascii").splitlines():
            phase, view, pose, bubble, crc = line.split()
            key = tuple(map(int, (phase, view, pose, bubble)))
            if key in seen:
                raise ValueError(f"duplicate browser sample {line}")
            seen.add(key)
            item, frame = item_frame(key[2])
            requests.append(f"{item} {frame} {phase} {view} {bubble}")
            expected.append(crc.lower())
        steps = max(k[3] for k in seen) + 1
        if seen != {(p, v, f, b) for p in range(4) for v in range(2)
                    for f in range(66) for b in range(steps)}:
            raise ValueError("incomplete browser samples")
        run = subprocess.run([str(renderer), str(options.pack)], input="\n".join(requests),
                             capture_output=True, text=True, check=True)
        actual = run.stdout.splitlines()
        if len(actual) != len(expected):
            raise RuntimeError(f"{len(actual)} rendered frames for {len(expected)} references")
        for key, src, dst in zip(requests, expected, actual):
            if src != dst:
                raise AssertionError(f"{key}: browser {src}, device painter {dst}")
        print(f"{len(actual)} full-panel RGB565 browser frames matched")


if __name__ == "__main__":
    main()
