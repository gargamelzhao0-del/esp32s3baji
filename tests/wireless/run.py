"""Compile and exercise the production BAS1 endpoint and real Python sender on the host."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
COMPONENTS = HERE.parents[1] / "components"


def check_lifecycle(build):
    headers = build / "radio"
    headers.mkdir()
    for header in ("esp_err.h", "esp_event.h", "esp_log.h", "esp_netif.h", "nvs_flash.h",
                   "nvs.h", "bootloader_random.h", "esp_random.h", "esp_mac.h",
                   "freertos/FreeRTOS.h", "freertos/semphr.h", "freertos/task.h",
                   "driver/usb_serial_jtag.h", "driver/usb_serial_jtag_vfs.h"):
        path = headers / header
        path.parent.mkdir(exist_ok=True)
        path.write_text('#include "lifecycle_shim.h"\n', encoding="ascii")
    components = ("baji_agent", "baji_agent_port", "baji_link", "baji_core", "baji_wifi",
                  "baji_ble", "baji_connectivity")
    coordinator = COMPONENTS / "baji_connectivity"
    includes = [HERE, headers, coordinator, *(COMPONENTS / c / "include" for c in components)]
    exe = headers / ("lifecycle.exe" if os.name == "nt" else "lifecycle")
    sources = [HERE / "lifecycle.c", HERE / "sdk_shim.c", coordinator / "settings.c",
               coordinator / "connectivity.c", coordinator / "usb_setup.c"]
    subprocess.run([os.environ.get("CC", "gcc"), "-std=c11", "-O2", "-pthread", "-Wall", "-Wextra",
                    "-Werror", *(f"-I{p}" for p in includes), *map(str, sources), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    for header in ("sdkconfig.h", "pet_app.h", "baji_deepseek_app.h"):
        (headers / header).write_text('#include "app_shim.h"\n', encoding="ascii")
    for character in ("deepseek", "gpt"):
        exe = headers / (character + (".exe" if os.name == "nt" else ""))
        subprocess.run([os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-DBAJI_WIRELESS", *(["-DBAJI_GPT_PET"] if character == "gpt" else []),
                        *(f"-I{p}" for p in includes), str(HERE.parents[1] / "main/app_main.c"),
                        str(HERE / "app_fallback.c"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


def main():
    with tempfile.TemporaryDirectory() as directory:
        build = Path(directory)
        for header in ("esp_err.h", "esp_timer.h", "esp_random.h", "freertos/FreeRTOS.h",
                       "freertos/semphr.h", "mbedtls/md.h", "mbedtls/constant_time.h"):
            path = build / header
            path.parent.mkdir(exist_ok=True)
            path.write_text('#include "sdk_shim.h"\n', encoding="ascii")
        library = build / ("agent.dll" if os.name == "nt" else "libagent.so")
        includes = [HERE, build, *(COMPONENTS / name / "include" for name in
                    ("baji_agent", "baji_agent_port", "baji_link", "baji_core"))]
        sources = [HERE / "sdk_shim.c", COMPONENTS / "baji_agent/agent_sync.c",
                   COMPONENTS / "baji_agent/agent_wire.c", COMPONENTS / "baji_core/baji_state.c"]
        subprocess.run([os.environ.get("CC", "gcc"), "-std=c11", "-O2", "-shared", "-pthread",
                        *(["-fPIC"] if os.name != "nt" else []), "-Wall", "-Wextra", "-Werror",
                        *(f"-I{p}" for p in includes), *map(str, sources), "-o", str(library)], check=True)
        # Windows keeps a loaded DLL locked until its process exits.
        subprocess.run([sys.executable, str(HERE / "roundtrip.py"), str(library)], check=True)
        check_lifecycle(build)


if __name__ == "__main__":
    main()
