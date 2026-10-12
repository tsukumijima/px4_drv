#!/usr/bin/env python3
"""対象の C 関数を現行ソースから抽出し、時計と USB/GPIO を模擬して実行する。"""

import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile


HERE = Path(__file__).resolve().parent
DRIVER = HERE.parent


def extract(path, names):
    source = path.read_text()
    functions = []
    for name in names:
        # このドライバのトップレベル関数は、閉じ括弧を行頭へ置く書式。
        pattern = (r"^[\w *]+\b" + re.escape(name)
                   + r"\([^;{]*\)\n\{.*?^\}")
        matches = re.findall(pattern, source, flags=re.MULTILINE | re.DOTALL)
        if len(matches) != 1:
            raise RuntimeError(f"{path}: {name}: expected one definition")
        functions.append(matches[0])
    return "\n\n".join(functions) + "\n"


with tempfile.TemporaryDirectory(prefix="px4-card-regression-") as temporary:
    build = Path(temporary)
    for filename, names, output in [
        ("itedtv_bus.c", ["itedtv_bus_set_ctrl_deadline",
                          "itedtv_bus_clear_ctrl_deadline",
                          "itedtv_usb_ctrl_expired",
                          "itedtv_usb_ctrl_timeout", "itedtv_usb_ctrl_tx",
                          "itedtv_usb_ctrl_rx"], "bus.inc"),
        ("it930x.c", ["it930x_calc_checksum", "it930x_ctrl_msg"], "ctrl.inc"),
        ("pxmlt_device.c", ["pxmlt_backend_set_power", "pxmlt_card_power",
                           "pxmlt_card_close"], "power.inc"),
    ]:
        source = DRIVER / filename
        # プラットフォーム分岐は Linux 側だけを抽出する。
        text = source.read_text()
        if filename == "itedtv_bus.c":
            start = text.index("#ifdef __linux__\nvoid itedtv_bus_set_ctrl_deadline")
            otherwise = text.index("#else", start)
            end = text.index("#endif", otherwise)
            text = text[:otherwise] + text[end:]
            source = build / filename
            source.write_text(text)
        (build / output).write_text(extract(source, names))

    executable = build / "card_deadline_test"
    command = shlex.split(os.environ.get("CC", "cc"))
    command += shlex.split(os.environ.get("CFLAGS", "-O2 -g"))
    command += ["-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                "-I", str(build), str(HERE / "card_deadline_test.c"),
                "-o", str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable), *sys.argv[1:]], check=True)
