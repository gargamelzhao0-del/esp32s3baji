"""Replay physical touch reports against the firmware state machine and shipped GPT clip masks."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
COMPONENTS = HERE.parents[1]


def main():
    with tempfile.TemporaryDirectory() as directory:
        build = Path(directory)
        (build / "esp_err.h").write_text(
            "#pragma once\ntypedef int esp_err_t;\n"
            "enum {ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_RESPONSE, ESP_ERR_NO_MEM, ESP_ERR_NOT_FOUND};\n",
            encoding="ascii",
        )
        (build / "baji_agent_feed.h").write_text("#include <stdbool.h>\ntypedef struct host_feed baji_agent_feed_t;\n", encoding="ascii")
        libraries = ["-lz"]
        env = os.environ.copy()
        if os.name == "nt":
            git = shutil.which("git")
            dll = Path(git).resolve().parents[1] / "mingw64/bin/zlib1.dll" if git else None
            if dll and dll.is_file():
                (build / "zlib.h").write_text(
                    "typedef unsigned long uLongf;\n#define Z_OK 0\n"
                    "unsigned long crc32(unsigned long,const unsigned char*,unsigned);\n"
                    "int uncompress(unsigned char*,uLongf*,const unsigned char*,unsigned long);\n",
                    encoding="ascii",
                )
                libraries = [str(dll)]
                env["PATH"] = str(dll.parent) + os.pathsep + env["PATH"]
        modules = ["baji_runtime", "baji_agent_ui", "baji_pet_clips", "baji_graphics", "baji_scene_damage", "baji_peripherals"]
        includes = [build, HERE.parent, *(COMPONENTS / module / "include" for module in modules)]
        out = build / ("interactions.exe" if os.name == "nt" else "interactions")
        subprocess.run([os.environ.get("CC", "gcc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        *["-I" + str(path) for path in includes], str(HERE.parent / "pet_interactions.c"),
                        str(COMPONENTS / "baji_pet_clips/pet_clips.c"), str(HERE / "interactions.c"),
                        *libraries, "-lm", "-o", str(out)], check=True)
        subprocess.run([str(out), str(COMPONENTS / "baji_pet_clips/assets/clips.bpc")], check=True, env=env)


if __name__ == "__main__":
    main()
