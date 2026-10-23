#!/bin/bash
# Copies the files of an itelx installation tree (what the ODE installer
# installs: the filesystem.squashfs of its ISO, unpacked, or the "install"
# folder of an itelx build) into customizers/factory/itelx-VERSION, the
# files recustomize puts back. /boot/cmdline is left out: the installer
# writes it for each computer.
#
#   bash customizers/tools/get-factory.sh <install tree> <version>
set -euo pipefail
SRC=$1
VER=$2
HERE="$(cd "$(dirname "$0")/.." && pwd)"
D="$HERE/factory/itelx-$VER"
[ -f "$SRC/rootfs/system/terminal.xvr" ] && [ -f "$SRC/esp/boot/vmlinuz" ] || { echo "$SRC is not an itelx installation tree" >&2; exit 1; }
rm -rf "$D"
mkdir -p "$D/esp/boot"
cp -a "$SRC/rootfs" "$D/rootfs"
rm -f "$D/rootfs/boot/cmdline"
cp "$SRC/esp/boot/vmlinuz" "$SRC/esp/boot/initrd.img" "$D/esp/boot/"
head -1 "$SRC/manifest" > "$D/FROM.txt" 2>/dev/null || true
(cd "$D" && find rootfs -type d | LC_ALL=C sort > DIRS.txt && find rootfs esp -type f | LC_ALL=C sort | xargs sha256sum > SHA256SUMS)
echo "$D: $(wc -l < "$D/SHA256SUMS") files, $(wc -l < "$D/DIRS.txt") folders"
