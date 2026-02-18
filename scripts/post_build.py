Import("env")

import itertools
import json
import os
import pathlib
import shutil


def merge_firmware_assets(source, target, env):
    """
    Merges firmware + assets into a single bin file
    """
    firmware_name = os.path.basename(env.subst("$PROGNAME")) + ".bin"
    build_dir = pathlib.Path(env.subst("$BUILD_DIR"))
    firmware_path = build_dir / firmware_name
    assets_path = pathlib.Path("lib/EpdFont/builtinFonts/assets.bin")

    with open(firmware_path, "rb") as firmware_file, open(assets_path, "rb") as assets_file:
        firmware_data = firmware_file.read()
        assets_data = assets_file.read()
        merged_data = firmware_data + assets_data
        with open(firmware_path, "wb") as merged_file:
            merged_file.write(merged_data)
        
        # Print stats
        print(f"Merged firmware and assets into {firmware_path}")
        print(f"Firmware size: {len(firmware_data)} bytes")
        print(f"Assets size: {len(assets_data)} bytes")
        print(f"Total size: {len(merged_data)} bytes")
        MAX_SIZE = 0xEE0000
        DISPLAY_SPACES = 20
        if len(merged_data) > MAX_SIZE:
            print(f"WARNING: Merged firmware exceeds maximum size of {MAX_SIZE} bytes!")
        else:
            percentage = len(merged_data) / MAX_SIZE * 100
            print(f"Size usage: [{'=' * int(len(merged_data) / MAX_SIZE * DISPLAY_SPACES):<{DISPLAY_SPACES}}]  {len(merged_data)} of {MAX_SIZE} bytes ({percentage:.2f}%)")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_firmware_assets)