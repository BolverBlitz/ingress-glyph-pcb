Import("env")

import csv
from pathlib import Path


project_dir = Path(env.subst("$PROJECT_DIR"))
partition_table = project_dir / env.GetProjectOption("board_build.partitions")
firmware_image = Path(env.subst("$BUILD_DIR")) / "firmware.bin"


def app_partition_size():
    with partition_table.open(encoding="utf-8", newline="") as table:
        for row in csv.reader(table):
            if row and row[0].strip() == "app0":
                return int(row[4].strip(), 0)
    raise RuntimeError("No app0 partition found in " + str(partition_table))


def check_firmware_size(target, source, env):
    image_size = firmware_image.stat().st_size
    limit = app_partition_size()
    if image_size > limit:
        raise RuntimeError(
            f"Firmware image is {image_size} bytes, larger than the OTA "
            f"partition ({limit} bytes). Refusing this build/upload."
        )
    print(f"Firmware image fits OTA partition: {image_size}/{limit} bytes")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_firmware_size)
env.AddPreAction("upload", check_firmware_size)
