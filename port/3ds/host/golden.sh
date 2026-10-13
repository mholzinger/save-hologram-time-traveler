#!/bin/sh
# Host regression tests with saved expected outputs (port/3ds/host/expected/). Every test prints text (or,
# for the LED panel, a picture whose SHA-256 is kept), and the run passes when all of it matches.
#   ./golden.sh          check (make -C port/3ds/host golden; part of make test)
#   ./golden.sh update   rewrite expected/ after an intended change; review the diff before committing
# The outputs hold scene ids, clip names and hashes, no media and no ROM text.
set -eu
cd "$(dirname "$0")"
TTG=../../../build/timetrv.ttg
OUT=../../../build/host/golden
EXP=expected
mode=${1:-check}
PY=${PY:-python3 -I}
rm -rf "$OUT" ../../../build/host/sdmc
mkdir -p "$OUT" ../../../build/host/sdmc/timetrv
# The front end's SD card: the game only (no saved settings or scores), and the clips' frame counts.
cp "$TTG" ../../../build/host/sdmc/timetrv/
$PY ../../../tools/check_media.py --manifest ../../../build/host/media_frames.txt

# The front end with scripted presses (test_frontend.c: <ms>:<key>[:<hold ms>]); 12 ms decode, 6 ms blit.
n=0
while IFS= read -r script; do
    n=$((n + 1))
    # shellcheck disable=SC2086
    ./test_frontend 12 6 $script > "$OUT/frontend_$n.txt" 2>&1
done <<'EOF'
2000:right
2000:right 30000:left 30400:b
3000:start:400 3150:y:200
2000:left
2000:right 9000:up 20000:a 40000:down
EOF

# Two-player alternation, no input, two seeds.
for seed in 1 2; do ./test_2p "$TTG" "$seed" > "$OUT/2p_seed$seed.txt"; done

# LED messages (ledmsg.h ids): attract prompt, status, Hellgate offer, game over, death; 6 s each. Their
# text is the ROM's wording, which stays out of git, so only its SHA-256 is kept; the full output is in
# build/host/ledmsg_<id>.txt to look at.
for id in 0x0C 0x0E 0x17 0x3D 0x8D; do
    ./test_ledmsg "$TTG" "$id" 6 "" > "../../../build/host/ledmsg_$id.txt"
    shasum -a 256 < "../../../build/host/ledmsg_$id.txt" > "$OUT/ledmsg_$id.sha256"
done

# The LED panel glyphs.
./test_led "$OUT/led.ppm" > /dev/null
shasum -a 256 < "$OUT/led.ppm" > "$OUT/led.sha256"
rm "$OUT/led.ppm"

if [ "$mode" = update ]; then
    rm -rf "$EXP"
    cp -R "$OUT" "$EXP"
    echo "golden: expected/ rewritten ($(ls "$EXP" | wc -l | tr -d ' ') files)"
    exit 0
fi
if diff -r "$EXP" "$OUT" > "$OUT.diff"; then
    echo "golden OK: $(ls "$EXP" | wc -l | tr -d ' ') host outputs match"
else
    echo "golden FAILED: diff -r port/3ds/host/$EXP build/host/golden"
    head -20 "$OUT.diff"
    exit 1
fi
