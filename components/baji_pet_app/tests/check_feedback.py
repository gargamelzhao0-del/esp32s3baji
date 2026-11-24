"""Exercise the production async feedback worker and the generated score on the host."""
from pathlib import Path
import os
import subprocess
import tempfile

tests = Path(__file__).resolve().parent
app = tests.parent
components = app.parent
wireless = components.parent / "tests/wireless"
subprocess.run(["node", app / "authoring/bake_tones.cjs", "--check"], check=True)
with tempfile.TemporaryDirectory() as directory:
    build = Path(directory)
    for name in ("esp_err.h", "esp_log.h", "nvs.h", "nvs_flash.h", "freertos/FreeRTOS.h",
                 "freertos/queue.h", "freertos/task.h", "freertos/semphr.h"):
        header = build / name
        header.parent.mkdir(exist_ok=True)
        header.write_text('#include "feedback_shim.h"\n')
    (build / "feedback_shim.h").write_text('''#pragma once
#include "lifecycle_shim.h"
#define ESP_LOGI(...) discard_log(__VA_ARGS__)
typedef struct queue *QueueHandle_t;
QueueHandle_t xQueueCreate(int,size_t);
void xQueueOverwrite(QueueHandle_t,const void *);
void vQueueDelete(QueueHandle_t);
int xQueueReceive(QueueHandle_t,void *,uint32_t);
esp_err_t nvs_get_u16(nvs_handle_t,const char *,uint16_t *);
esp_err_t nvs_set_u16(nvs_handle_t,const char *,uint16_t);
''')
    (build / "waves.S").write_text('''.text
.globl _binary_pet_tones_pcm_start
.globl _binary_pet_tones_pcm_end
_binary_pet_tones_pcm_start:
.incbin "pet_tones.pcm"
_binary_pet_tones_pcm_end:
''')
    exe = build / ("feedback.exe" if os.name == "nt" else "feedback")
    include = [app, wireless, build, *(components / name / "include" for name in
                                       ("baji_peripherals", "baji_graphics", "baji_board"))]
    subprocess.run([os.environ.get("CC", "gcc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O2", "-pthread",
                    *(f"-I{p}" for p in include), tests/"feedback_test.c", wireless/"sdk_shim.c",
                    app/"pet_feedback.c", build/"waves.S", "-o", exe], cwd=app/"assets", check=True)
    subprocess.run([exe], check=True, timeout=12)
