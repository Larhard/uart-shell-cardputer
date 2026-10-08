# Post-build: produce a single image flashable at 0x0 (web flashers, Launcher "merged" installs).
# The plain app image (firmware.bin) is what Launcher installs from SD/OTA.
Import("env")
import os


def merge_bin(source, target, env):
    build = env.subst("$BUILD_DIR")
    platform = env.PioPlatform()
    esptool = os.path.join(platform.get_package_dir("tool-esptoolpy"), "esptool.py")
    boot_app0 = os.path.join(platform.get_package_dir("framework-arduinoespressif32"),
                             "tools", "partitions", "boot_app0.bin")
    sections = [("0x0", os.path.join(build, "bootloader.bin")),
                ("0x8000", os.path.join(build, "partitions.bin")),
                ("0xe000", boot_app0),
                ("0x10000", os.path.join(build, "firmware.bin"))]
    out = os.path.join(build, "uart_shell_merged.bin")
    args = " ".join(f'{a} "{p}"' for a, p in sections)
    env.Execute(f'"$PYTHONEXE" "{esptool}" --chip esp32s3 merge_bin -o "{out}" '
                f'--flash_mode dio --flash_size 8MB {args}')


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)
