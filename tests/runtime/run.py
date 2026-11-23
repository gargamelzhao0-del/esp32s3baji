"""Run the production scheduler with a deterministic panel, touch IRQ and clock."""
import os
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
components = here.parents[1] / "components"
with tempfile.TemporaryDirectory() as directory:
    build = Path(directory)
    for header in ("esp_err.h", "esp_log.h", "esp_timer.h", "freertos/FreeRTOS.h",
                   "freertos/semphr.h", "freertos/task.h"):
        src = build / header
        src.parent.mkdir(exist_ok=True)
        src.write_text('#include "sim.h"\n', encoding="ascii")
    includes = [here, here.parent / "display", build,
                *(components / name / "include" for name in
                  ("baji_board", "baji_graphics", "baji_peripherals", "baji_runtime", "baji_vsync"))]
    exe = build / ("runtime.exe" if os.name == "nt" else "runtime")
    subprocess.run([os.environ.get("CC", "gcc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    *[f"-I{path}" for path in includes], str(components / "baji_runtime/baji_runtime.c"),
                    str(here / "runtime_test.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
