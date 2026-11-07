#!/bin/sh
# Smoke test with a real TI-84+ (SE) ROM: boot, type 2+3*7 ENTER, graph sin(X).
# The ROM is not part of the repository: pass its path (default roms/ti84se.rom).
# Screenshots go to build/smoke/; compare them by eye after core changes, and
# the script checks that the result is deterministic and the screen is not blank.
set -e
ROM=${1:-roms/ti84se.rom}
HOST=build/host/n84-host
OUT=build/smoke
[ -f "$ROM" ] || { echo "no ROM at $ROM, skipping"; exit 0; }
mkdir -p $OUT
$HOST "$ROM" 6 -t -o $OUT/calc.ppm 1:ON 2.6:CLEAR 3:2 3.3:ADD 3.6:3 3.9:MUL 4.2:7 4.5:ENTER
$HOST "$ROM" 6 -o $OUT/calc2.ppm 1:ON 2.6:CLEAR 3:2 3.3:ADD 3.6:3 3.9:MUL 4.2:7 4.5:ENTER 2>/dev/null
cmp -s $OUT/calc.ppm $OUT/calc2.ppm || { echo "FAIL: not deterministic"; exit 1; }
$HOST "$ROM" 12 -t -o $OUT/graph.ppm 1:ON 2:CLEAR 2.5:YEQU 3:SIN 3.3:XTTN 3.6:RPAREN 4:ZOOM 4.5:7
for f in calc graph; do
    # P6 header is 15 bytes for 320x240; count dark pixels in the LCD area
    n=$(tail -c +16 $OUT/$f.ppm | od -An -v -tu1 -w3 | awk '$1 < 100 {n++} END {print n+0}')
    [ "$n" -gt 21504 ] || { echo "FAIL: $f screen is blank"; exit 1; }
    echo "$f: $((n - 21504)) dark LCD pixels"
done
echo "ROM smoke OK (screenshots in $OUT)"
