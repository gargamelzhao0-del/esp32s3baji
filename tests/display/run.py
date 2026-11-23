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
    (build / "esp_log.h").write_text(
        '#include <stdio.h>\n#define ESP_LOGI(tag, ...) ((void)(tag), (void)sizeof(printf(__VA_ARGS__)))\n'
        '#define ESP_LOGW ESP_LOGI\n#define esp_err_to_name(e) "host error"\n', encoding="ascii")
    (build / "esp_timer.h").write_text(
        '#include <stdint.h>\n#ifdef BAJI_HOST_VSYNC\nint64_t esp_timer_get_time(void);\n#else\n'
        'static inline int64_t esp_timer_get_time(void) { return 0; }\n#endif\n', encoding="ascii")
    compiler = [
        os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(build), "-I", str(HERE), "-I", str(COMPONENTS.parent / "main"),
        "-I", str(COMPONENTS / "baji_board_profile/include"),
        "-I", str(COMPONENTS / "baji_display/include"),
        "-I", str(COMPONENTS / "baji_board/include"), "-I", str(COMPONENTS / "baji_peripherals/include"),
        "-I", str(COMPONENTS / "baji_scene_damage/include"),
        "-I", str(COMPONENTS / "baji_pet/include"),
    ]
    peripherals = COMPONENTS / "baji_peripherals"
    for name, sources in {
        "scene_damage_test": [COMPONENTS / "baji_scene_damage/baji_scene_damage.c", HERE / "scene_damage_test.c"],
        "display_test": [
            COMPONENTS / "baji_display/baji_display.c", COMPONENTS / "baji_board/baji_board.c",
            HERE / "display_test.c", HERE / "round_paint_test.c",
        ],
        "touch_init_test": [peripherals / "cst9217.c", HERE / "touch_init_test.c"],
        "vsync_test": [COMPONENTS / "baji_vsync/baji_vsync.c", HERE / "vsync_test.c"],
        "pet_player_test": [COMPONENTS.parent / "main/pet_player.c", COMPONENTS / "baji_scene_damage/baji_scene_damage.c",
                            HERE / "pet_player_test.c"],
        "board_check_paint_test": [COMPONENTS.parent / "main/board_check.c", HERE / "board_check_paint_test.c"],
        "peripheral_decode_test": [peripherals / "test/test_peripheral_decode.c"],
    }.items():
        executable = build / (name + (".exe" if os.name == "nt" else ""))
        options = ["-DBAJI_HOST_VSYNC", "-I", str(COMPONENTS / "baji_vsync/include")] if name == "vsync_test" else []
        subprocess.run([*compiler, *options, "-I", str(peripherals), *map(str, sources), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
