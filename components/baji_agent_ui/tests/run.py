"""Exercise the display projection and GPT presenter with the production damage planner."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
COMP = HERE.parents[1]
ROOT = COMP.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview", nargs=3, metavar=("INPUT_RGB565", "OUTPUT_RGB565", "MODEL"),
                        help="composite the production overlay onto a physical 466x466 wire-order frame")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        build = Path(directory)
        for name in ("esp_err.h", "esp_timer.h", "freertos/FreeRTOS.h", "freertos/semphr.h"):
            path = build / name
            path.parent.mkdir(exist_ok=True, parents=True)
            path.write_text('#include "sim.h"\n', encoding="ascii")
        components = ("baji_core", "baji_graphics", "baji_agent_ui", "baji_agent_port", "baji_scene_damage",
                      "baji_board", "baji_pet", "baji_pet_app", "baji_runtime", "baji_peripherals")
        flags = [os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                 "-I" + str(build), "-I" + str(ROOT / "tests/runtime"), "-I" + str(ROOT / "tests/display"),
                 *["-I" + str(COMP / module / "include") for module in components]]
        shared = [COMP / "baji_agent_ui/agent_ui.c", COMP / "baji_scene_damage/baji_scene_damage.c"]
        for src, extras, arguments in (
            ("ui_test.c", [COMP / "baji_core/baji_state.c"], args.preview or []),
            ("gpt_connected_test.c", [COMP / "baji_pet_app/pet_app.c", COMP / "baji_pet_app/pet_player.c"], []),
        ):
            out = build / (Path(src).stem + (".exe" if os.name == "nt" else ""))
            subprocess.run([*flags, *map(str, shared + extras + [HERE / src]), "-o", str(out)], check=True)
            subprocess.run([str(out), *arguments], check=True)


if __name__ == "__main__":
    main()
