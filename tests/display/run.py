"""Run board, QSPI and peripheral checks on a host with Python 3 and gcc or clang."""
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
COMPONENTS = HERE.parents[1] / "components"

with tempfile.TemporaryDirectory(prefix="baji-display-test-") as directory:
    build = Path(directory)
    # The driver includes the normal IDF header names. Forward only these
    # headers to our tiny SPI recorder; no generated file goes in the checkout.
    for header in ("esp_err.h", "esp_memory_utils.h", "esp_heap_caps.h", "driver/gpio.h", "driver/spi_master.h",
                   "freertos/FreeRTOS.h", "freertos/task.h", "esp_flash.h", "esp_psram.h"):
        target = build / header
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#include "spi_recorder.h"\n', encoding="ascii")
    for header in ("driver/i2c_master.h", "esp_attr.h", "freertos/semphr.h"):
        (build / header).write_text('#include "touch_recorder.h"\n', encoding="ascii")
    compiler = [
        os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(build), "-I", str(HERE),
        "-I", str(COMPONENTS / "baji_board_profile/include"),
        "-I", str(COMPONENTS / "baji_display/include"),
        "-I", str(COMPONENTS / "baji_board/include"), "-I", str(COMPONENTS / "baji_peripherals/include"),
    ]
    peripherals = COMPONENTS / "baji_peripherals"
    for name, sources in {
        "display_test": [
            COMPONENTS / "baji_display/baji_display.c", COMPONENTS / "baji_board/baji_board.c",
            HERE / "display_test.c",
        ],
        "touch_init_test": [peripherals / "cst9217.c", HERE / "touch_init_test.c"],
        "peripheral_decode_test": [peripherals / "test/test_peripheral_decode.c"],
    }.items():
        executable = build / (name + (".exe" if os.name == "nt" else ""))
        subprocess.run([*compiler, "-I", str(peripherals), *map(str, sources), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
