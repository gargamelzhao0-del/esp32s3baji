"""Compile the actual effect compositor and compare complete and segmented RGB565 paints."""
from pathlib import Path
import os
import subprocess
import tempfile

tests = Path(__file__).resolve().parent
components = tests.parents[1]
damage = components / "baji_scene_damage"
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / ("effects.exe" if os.name == "nt" else "effects")
    subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I", tests.parent, "-I", components/"baji_graphics/include", "-I", damage/"include",
                    tests/"effects_test.c", tests.parent/"pet_effects.c", damage/"baji_scene_damage.c",
                    "-o", exe], check=True)
    subprocess.run([exe], check=True)
