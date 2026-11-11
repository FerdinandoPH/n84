#!/bin/sh
# Smoke test with real ROMs: boot, type 2+3*7 ENTER, graph sin(X), send and run
# a program. The ROMs are not part of the repository; they are taken from
# roms/ (TI-84 Plus SE, TI-83 Plus, TI-84 Plus CE) or passed as arguments:
#     rom_smoke.sh [Z80_ROM] [CE_ROM]
# Screenshots go to build/smoke/; compare them by eye after core changes, and
# the script checks that the result is deterministic and the screen is not blank.
set -e
HOST=build/host/n84-host
OUT=build/smoke
mkdir -p $OUT
python3 tools/mk_8xp.py $OUT/hello.8xp HELLO 'DE 2A 48 45 4C 4C 4F 29 4E 38 34 2A'

z80_smoke() {   # ROM NAME
    ROM=$1; N=$2
    $HOST "$ROM" 6 -t -o $OUT/${N}_calc.ppm 1:ON 2.6:CLEAR 3:2 3.3:ADD 3.6:3 3.9:MUL 4.2:7 4.5:ENTER
    $HOST "$ROM" 6 -o $OUT/${N}_calc2.ppm 1:ON 2.6:CLEAR 3:2 3.3:ADD 3.6:3 3.9:MUL 4.2:7 4.5:ENTER 2>/dev/null
    cmp -s $OUT/${N}_calc.ppm $OUT/${N}_calc2.ppm || { echo "FAIL: $N not deterministic"; exit 1; }
    $HOST "$ROM" 12 -t -o $OUT/${N}_graph.ppm 1:ON 2:CLEAR 2.5:YEQU 3:SIN 3.3:XTTN 3.6:RPAREN 4:ZOOM 4.5:7
    for f in ${N}_calc ${N}_graph; do
        # P6 header is 15 bytes for 320x240; count dark pixels in the LCD area
        n=$(tail -c +16 $OUT/$f.ppm | od -An -v -tu1 -w3 | awk '$1 < 100 {n++} END {print n+0}')
        [ "$n" -gt 21504 ] || { echo "FAIL: $f screen is blank"; exit 1; }
        echo "$f: $((n - 21504)) dark LCD pixels"
    done
    # send a TI-BASIC program (Disp "HELLO N84") and run it from the PRGM menu
    $HOST "$ROM" 9 -o $OUT/${N}_send.ppm 1:ON 2.5:CLEAR 3:send=$OUT/hello.8xp 4:PRGM 4.5:ENTER 5:ENTER 2>&1 | grep -q "hello.8xp: OK" \
        || { echo "FAIL: sending a program to the $N"; exit 1; }
    echo "send ($N): OK"
}

if [ -n "$1" ]; then
    z80_smoke "$1" z80
else
    found=
    [ -f roms/ti84se.rom ] && { z80_smoke roms/ti84se.rom ti84se; found=1; }
    [ -f roms/ti83plus.rom ] && { z80_smoke roms/ti83plus.rom ti83plus; found=1; }
    [ -n "$found" ] || echo "no Z80 ROM in roms/, skipping"
fi
echo "ROM smoke OK (screenshots in $OUT)"

# TI-84 Plus CE
CEROM=${2:-roms/ti84ce.rom}
CEHOST=build/host/n84ce-host
if [ -f "$CEROM" ] && [ -x "$CEHOST" ]; then
    $CEHOST "$CEROM" 9 -t -o $OUT/ce_calc.ppm 5:CLEAR 5.5:2 5.8:ADD 6.1:3 6.4:MUL 6.7:7 7:ENTER 2>&1 >/dev/null | tail -1
    $CEHOST "$CEROM" 14 -t -o $OUT/ce_graph.ppm 5:CLEAR 5.5:YEQU 6.2:SIN 6.6:XTTN 7:RPAREN 7.5:ZOOM 8.2:7 2>&1 >/dev/null | tail -1
    for f in ce_calc ce_graph; do
        n=$(tail -c +16 $OUT/$f.ppm | od -An -v -tu1 -w3 | awk '$1 < 100 {n++} END {print n+0}')
        [ "$n" -gt 500 ] || { echo "FAIL: $f screen is blank"; exit 1; }
        echo "$f: $n dark pixels"
    done
    $CEHOST "$CEROM" 13 -o $OUT/ce_send.ppm 5:CLEAR 6:send=$OUT/hello.8xp 7:PRGM 2>&1 >/dev/null | grep -q "hello.8xp: OK" \
        || { echo "FAIL: sending a program to the CE"; exit 1; }
    echo "send (CE): OK"
    echo "CE smoke OK"
fi
