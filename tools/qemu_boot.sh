#!/bin/bash
# Boot release bin in the patched QEMU (headless), log to /tmp/q.log. Usage: tools/qemu_boot.sh [--reset]
Q=/workspace/m5stopwatch-qemu-oss
pkill -f qemu-system-xtensa; sleep 1
cd $Q && (./run.sh --headless "$@" --fw /workspace/mengke-fw/release/mengke-stopwatch.bin > /tmp/q.log 2>&1 &)
for i in $(seq 1 60); do grep -aq "Launcher\] on open" /tmp/q.log 2>/dev/null && break; sleep 1; done
grep -a "Launcher\] on open" /tmp/q.log && echo BOOT_OK
