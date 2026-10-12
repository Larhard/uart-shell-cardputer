# Post-build:
#  - produce a single image flashable at 0x0 (web flashers, esptool)
#  - copy release files into dist/, named with the version from src/main.cpp
# The plain app image (firmware.bin) is what Launcher installs from SD/OTA.
Import("env")
import os
import re
import shutil


def app_version(project):
    with open(os.path.join(project, "src", "main.cpp"), encoding="utf-8") as f:
        m = re.search(r'#define\s+APP_VERSION\s+"([^"]+)"', f.read())
    return m.group(1) if m else "dev"


def merge_bin(source, target, env):
    build = env.subst("$BUILD_DIR")
    project = env.subst("$PROJECT_DIR")
    platform = env.PioPlatform()
    boot_app0 =os.path.join(platform.get_package_dir("framework-arduinoespressif32"),
                             "tools", "partitions", "boot_app0.bin")
    sections = [("0x0", os.path.join(build, "bootloader.bin")),
                ("0x8000", os.path.join(build, "partitions.bin")),
                ("0xe000", boot_app0),
                ("0x10000", os.path.join(build, "firmware.bin"))]
    merged = os.path.join(build, "uart_shell_merged.bin")
    args = " ".join(f'{a} "{p}"' for a, p in sections)
    # pioarduino installs esptool (v5) as a module in the build's Python environment.
    if env.Execute(f'"$PYTHONEXE" -m esptool --chip esp32s3 merge-bin -o "{merged}" '
                   f'--flash-mode dio --flash-size 8MB {args}'):
        raise RuntimeError("esptool merge-bin failed")

    dist = os.path.join(project, "dist")
    os.makedirs(dist, exist_ok=True)
    name = f"UART_Shell_v{app_version(project)}_CardputerADV"
    shutil.copyfile(os.path.join(build, "firmware.bin"), os.path.join(dist, name + ".bin"))
    shutil.copyfile(merged, os.path.join(dist, name + "_merged.bin"))
    print(f"dist: {name}.bin (Launcher/SD), {name}_merged.bin (flash at 0x0)")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)
