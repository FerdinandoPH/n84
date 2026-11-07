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
# send a TI-BASIC program (Disp "HELLO N84") and run it from the PRGM menu
python3 tools/mk_8xp.py $OUT/hello.8xp HELLO 'DE 2A 48 45 4C 4C 4F 29 4E 38 34 2A'
$HOST "$ROM" 9 -o $OUT/send.ppm 1:ON 2.5:CLEAR 3:send=$OUT/hello.8xp 4:PRGM 4.5:ENTER 5:ENTER 2>&1 | grep -q "hello.8xp: OK" \
    || { echo "FAIL: sending a program to the TI-84+"; exit 1; }
echo "send (TI-84+): OK"
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
