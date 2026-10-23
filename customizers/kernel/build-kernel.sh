#!/bin/bash
# Builds the two kernels the customizer apps carry, from the itelx 1.4
# sources (the itelxOS64bit folder of ifos_subsystem):
#
#   net   "itelx 1.4 net"   File saver: itelx 1.4 with the network
#   data  "itelx 1.4 data"  data_place: the same, black screen, white text
#
# Each is itelx 1.4 with the files of src/ added (the network stack and the
# drivers: e1000, e1000e 82574L, RTL8139, virtio-net) and itelx-1.4-net.patch
# applied (six small changes: start the network, its system calls, sockets
# closed with their process, the colours, the version name). The results go
# to out/<name>/ (vmlinuz, itelx.elf, SHA256SUMS); `make customizers` puts
# them into file_saver.xvr and data_place.xvr.
#
#   bash customizers/kernel/build-kernel.sh /path/to/itelxOS64bit
#
# Needs what itelx needs: gcc, ld, objcopy, make, python3, patch.
set -euo pipefail
ITELX=${1:?usage: build-kernel.sh /path/to/itelxOS64bit}
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK=${CZK_WORK:-/tmp/itelx-czk}
grep -q '^itelx 1.4$' "$ITELX/VERSION" || { echo "$ITELX is not the itelx 1.4 source" >&2; exit 1; }
for v in net data; do
    S="$WORK/$v/src"
    rm -rf "$WORK/$v"
    mkdir -p "$S"
    # the parts of itelx a kernel is built from (make cannot take a path with spaces)
    for d in Makefile VERSION LICENSE LICENSES abi boot kernel itfs tools xvr third_party/unicode; do
        mkdir -p "$S/$(dirname "$d")"
        cp -a "$ITELX/$d" "$S/$d"
    done
    cp -a "$HERE/src/." "$S/"
    (cd "$S" && patch -p1 --quiet < "$HERE/itelx-1.4-net.patch")
    {
        echo "/* which customizer kernel this is (customizers/kernel/build-kernel.sh) */"
        if [ "$v" = data ]; then
            echo '#define ITELX_EDITION " data"'
            echo '#define ITX_THEME_MONO 1'
        else
            echo '#define ITELX_EDITION " net"'
        fi
    } > "$S/kernel/include/edition.h"
    make -s -C "$S" B="$WORK/$v/build" ITELX_SRC="$S" "$WORK/$v/build/vmlinuz" "$WORK/$v/build/itelx.elf"
    mkdir -p "$HERE/out/$v"
    cp "$WORK/$v/build/vmlinuz" "$WORK/$v/build/itelx.elf" "$HERE/out/$v/"
    (cd "$HERE/out/$v" && sha256sum vmlinuz itelx.elf > SHA256SUMS)
    echo "out/$v: $(stat -c %s "$HERE/out/$v/vmlinuz") bytes (vmlinuz)"
done
