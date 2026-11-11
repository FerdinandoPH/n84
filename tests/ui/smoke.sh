#!/bin/sh
# Drives the real Nspire frontend (built for the host as build/host/n84-ui)
# through the launcher, a CE session, the in-game menu and back, using the
# script mode of the host platform. ROMs are copied: sessions write to them.
set -e
D=${TMPDIR:-/tmp}/n84-ui-test.$$
mkdir -p "$D/shots"
trap 'rm -rf "$D"' EXIT
[ -f roms/ti84ce.rom ] || { echo "no CE ROM, skipping"; exit 0; }
cp roms/ti84ce.rom "$D/CE.rom.tns"
[ -f roms/ti84se.rom ] && cp roms/ti84se.rom "$D/SE.rom.tns"
{
    echo "wait 10"; echo "shot $D/shots/launcher.ppm"
    echo "tap ENTER"; echo "wait 20"; echo "shot $D/shots/welcome.ppm"     # first run: menu hint
    echo "tap ENTER"; echo "wait 420"; echo "tap QUESEXCL"; echo "wait 10"
    for i in 1 2 3 4; do echo "tap DOWN"; echo "wait 6"; done           # to "Key guide"
    echo "tap ENTER"; echo "wait 10"; echo "shot $D/shots/keys.ppm"; echo "tap ESC"; echo "wait 10"
    for i in 1 2 3 4 5; do echo "tap DOWN"; echo "wait 6"; done         # to "Exit to launcher"
    echo "tap ENTER"; echo "wait 40"; echo "shot $D/shots/back.ppm"; echo "end"
} > "$D/script.txt"
N84_DIR="$D" N84_SCRIPT="$D/script.txt" build/host/n84-ui
[ -f "$D/CE.sav.tns" ] || { echo "FAIL: no state saved on exit to launcher"; exit 1; }
cmp -s "$D/shots/launcher.ppm" "$D/shots/back.ppm" && { echo "FAIL: launcher did not show the saved state"; exit 1; }
grep -q "welcome_done=1" "$D/n84.cfg.tns" || { echo "FAIL: first-run hint not recorded"; exit 1; }
[ -n "$N84_UI_SHOTS" ] && cp "$D"/shots/*.ppm "$N84_UI_SHOTS"/
echo "UI smoke OK"
