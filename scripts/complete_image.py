"""Create a standalone ESP32 flash image after every successful program build."""

import os
import shutil
import subprocess

Import("env")


def merge_complete_image(source, target, env):
    output = env.subst("$BUILD_DIR/receiver-complete.bin")
    esptool = os.path.join(
        env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py"
    )
    command = [
        env.subst("$PYTHONEXE"),
        esptool,
        "--chip", env.BoardConfig().get("build.mcu", "esp32"),
        "merge_bin",
        "--target-offset", "0x0",
        "--output", output,
    ]
    # Use PlatformIO's own image list and application offset so the combined
    # image stays consistent with its normal upload and partition configuration.
    images = [
        (env.subst(str(offset)), env.subst(path))
        for offset, path in env.get("FLASH_EXTRA_IMAGES", [])
    ]
    images.append((
        env.subst("$ESP32_APP_OFFSET"),
        env.subst("$BUILD_DIR/${PROGNAME}.bin"),
    ))
    # Include framework-provided boot_app0.bin in the portable build output.
    # Keep PlatformIO's original paths for merging and normal upload.
    for offset, path in images:
        if os.path.basename(path) == "boot_app0.bin":
            destination = env.subst("$BUILD_DIR/boot_app0.bin")
            if os.path.normcase(os.path.abspath(path)) != os.path.normcase(
                os.path.abspath(destination)
            ):
                shutil.copyfile(path, destination)
    for offset, path in images:
        command.extend([offset, path])
    print("Creating receiver-complete.bin (flash at 0x0; replaces NVS settings)")
    subprocess.check_call(command)

    manifest = env.subst("$BUILD_DIR/flash-addresses.txt")
    lines = [
        "ESP32 receiver - flash addresses",
        "Generated from the current PlatformIO build configuration.",
        "",
        "Choose ONE of the following alternatives; do not flash both.",
        "",
        "1. Complete image (bootloader + partitions + boot_app0 + firmware):",
        "0x0000  receiver-complete.bin",
        "WARNING: This image clears NVS settings, presets and station lists.",
        "",
        "2. Individual images (retains NVS with the current partition layout):",
    ]
    for offset, path in images:
        lines.append("0x{:04X}  {}".format(int(offset, 0), os.path.basename(path)))
    lines.extend([
        "",
        "All listed files are included in this build output directory.",
        "Do not use a full-chip erase if you want to retain NVS.",
        "",
    ])
    with open(manifest, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))
    print("Created flash-addresses.txt")


# The alias must run even when firmware.bin is already up to date, including
# when the user removed only the combined image since the previous build.
env.AlwaysBuild(env.Alias("buildprog"))
env.AddPostAction("buildprog", merge_complete_image)
