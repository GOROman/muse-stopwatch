#!/usr/bin/env python3
"""Fetch a pinned SDK and apply this project's small, auditable overlay."""
from pathlib import Path
import argparse
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SDK_REV = "7e7123e2815d3e7e3c0f2ca330f576290ae6a6a9"
SDK_URL = "https://github.com/facebookincubator/muse-gadget-sdk.git"


def replace_once(path, old, new):
    text = path.read_text()
    if text.count(old) != 1:
        raise RuntimeError(f"Pinned SDK integration anchor changed: {path.name}")
    path.write_text(text.replace(old, new, 1))


def prepare(source, destination):
    # Never reset or overwrite an existing working tree or user config.
    if destination.exists():
        raise RuntimeError(f"Destination exists: {destination}; choose a new --destination")
    if source:
        actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        if actual != SDK_REV:
            raise RuntimeError(f"SDK revision mismatch: {actual}")
        # Export committed files only; ignore local changes and credentials.
        archive = subprocess.Popen(["git", "-C", str(source), "archive", SDK_REV], stdout=subprocess.PIPE)
        destination.mkdir(parents=True)
        subprocess.run(["tar", "-x", "-C", str(destination)], stdin=archive.stdout, check=True)
        archive.stdout.close()
        if archive.wait():
            raise RuntimeError("SDK export failed")
    else:
        subprocess.run(["git", "clone", SDK_URL, str(destination)], check=True)
        subprocess.run(["git", "-C", str(destination), "checkout", "--detach", SDK_REV], check=True)
    shutil.copytree(ROOT / "overlay", destination, dirs_exist_ok=True)
    main = destination / "esp32/main"
    replace_once(main / "CMakeLists.txt", '"led_status.c"', '"stopwatch_board.c" "focus_core.c" "focus.c"')
    replace_once(main / "CMakeLists.txt", "    nvs_flash", "    esp_timer\n    nvs_flash")
    replace_once(main / "app.c", '#include "app.h"', '#include "app.h"\n#include "focus.h"')
    replace_once(main / "app.c", '    if (strcmp(command, "device.list_vms") == 0) {',
        '    if (strncmp(command, "focus.", 6) == 0) return focus_command(command, params);\n'
        '    if (strcmp(command, "device.list_vms") == 0) {')
    replace_once(main / "noise_control.cpp", "    cJSON *commands = cJSON_CreateObject();",
        '    cJSON *commands = cJSON_CreateObject();\n#include "focus_commands.inc"')
    print(f"Prepared pinned Muse SDK: {destination / 'esp32'}")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, help="existing official SDK clone (offline export)")
    p.add_argument("--destination", type=Path, default=ROOT / ".build-sdk")
    args = p.parse_args()
    prepare(args.source, args.destination)
