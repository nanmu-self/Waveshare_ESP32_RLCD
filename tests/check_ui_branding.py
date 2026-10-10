# Copyright (c) 2026 黑沐. MIT License.
import subprocess
import sys
import tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="syna-ui-") as temporary:
    output=Path(temporary)/"test"
    # 排除 LVGL 的 SDL 驱动源文件：无头测试自绘 flush，不需要链接 SDL2。
    sources=[s for s in (root/"simulator/vendor/lvgl-9.5.0/src").rglob("*.c")
             if "sdl" not in s.name.lower()]
    assets=list((root/"simulator/src/assets").glob("*.c"))
    # LVGL 全量源码超出 Windows 命令行长度上限，改走 clang 响应文件。
    # clang 会把反斜杠当转义符，路径统一用正斜杠。
    arguments=["-w","-O0","-DLV_CONF_INCLUDE_SIMPLE","-DLV_LVGL_H_INCLUDE_SIMPLE",
        "-Iinclude","-Isimulator","-Isimulator/vendor/lvgl-9.5.0","-Isimulator/src",
        "tests/ui_branding.c","simulator/src/ui/ui.c","simulator/src/ui/lunar.cc",
        *map(str,sources),*map(str,assets)]
    arguments=[a.replace("\\","/") for a in arguments]
    response=Path(temporary)/"sources.rsp"
    response.write_text("\n".join(arguments),encoding="utf-8")
    subprocess.run(["clang",f"@{response}","-o",str(output)],cwd=root,check=True)
    subprocess.run([str(output),str(Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(temporary)/"about.ppm")],cwd=root,check=True)
