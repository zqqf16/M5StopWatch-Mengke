#!/bin/bash
set -e
cd "$(dirname "$0")"
. ~/esp/esp-idf-5.5/export.sh >/dev/null 2>&1
idf.py -B build -DSDKCONFIG=build/sdkconfig build
mkdir -p release
(cd build && python -m esptool --chip esp32s3 merge_bin --fill-flash-size 16MB -o ../release/mengke-stopwatch.bin @flash_args)
ls -la release/
