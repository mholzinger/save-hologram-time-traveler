#!/bin/bash
# Push build/sdmc/ to the 3DS SD card over FTP (ftpd running on the console).
# Only files whose MD5 changed since the last successful deploy to this SD card
# are sent. Each card carries an id in timetrv/.card-id; the record of what it
# holds lives in build/.deploy-<card id>.md5. A card without an id (new or
# swapped) gets an id and everything.
#
#   tools/deploy.sh            upload changed files
#   tools/deploy.sh --app      upload only 3ds/timetrv3ds.3dsx
#   tools/deploy.sh --all      ignore the record, upload everything
#   tools/deploy.sh -n         dry run: list what would be sent
#   tools/deploy.sh --mark     record current build/sdmc as deployed, send nothing
#
# Host: DS_HOST=ip:port (the Makefile passes it from local.mk or the command line).

set -u
cd "$(dirname "$0")/.." || exit 1

HOST=${DS_HOST:?set DS_HOST=ip:port of the 3DS running ftpd (local.mk or make deploy DS_HOST=...)}
SRC=build/sdmc
APP=3ds/timetrv3ds.3dsx

mode=changed; dry=0
for a in "$@"; do
  case $a in
    --app)  mode=app ;;
    --all)  mode=all ;;
    --mark) mode=mark ;;
    -n)     dry=1 ;;
    *) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
  esac
done

[ -d "$SRC" ] || { echo "no $SRC: run make stage"; exit 1; }

curl -s --disable-epsv --max-time 5 "ftp://$HOST/" > /dev/null || {
  echo "can't reach ftp://$HOST/ : is ftpd open on the 3DS? (set DS_HOST=ip:port if the address changed)"; exit 1; }

# Which card is in the console: its id file, or a new id if it has none.
CARD_ID=timetrv/.card-id
card=$(curl -s --disable-epsv --max-time 10 "ftp://$HOST/$CARD_ID" | tr -dc 'A-Za-z0-9-')
if [ -z "$card" ]; then
  card=$(uuidgen | tr 'A-Z' 'a-z')
  echo "SD card has no id: new card $card, sending everything"
  if [ $dry = 0 ]; then
    idf=$(mktemp); echo "$card" > "$idf"
    curl -s --disable-epsv --ftp-create-dirs --max-time 30 -T "$idf" "ftp://$HOST/$CARD_ID" || {
      rm -f "$idf"; echo "could not write $CARD_ID to the card"; exit 1; }
    rm -f "$idf"
  fi
fi
MANIFEST=build/.deploy-$card.md5
touch "$MANIFEST"

# "hash path" for every staged file, paths relative to the card root
cur=$(mktemp); todo=$(mktemp); failed=$(mktemp)
trap 'rm -f "$cur" "$todo" "$failed"' EXIT
(cd "$SRC" && find . -type f ! -name '.*' | sed 's|^\./||' | sort | xargs md5 -r) > "$cur"

if [ $mode = mark ]; then
  cp "$cur" "$MANIFEST"; echo "recorded $(wc -l < "$cur" | tr -d ' ') files as deployed to card $card"; exit 0
fi

case $mode in
  app)     grep " $APP\$" "$cur" > "$todo" ;;
  all)     cp "$cur" "$todo" ;;
  changed) sort "$MANIFEST" | comm -23 <(sort "$cur") - > "$todo" ;;
esac

n=$(wc -l < "$todo" | tr -d ' ')
if [ "$n" = 0 ]; then echo "up to date (card $card at $HOST)"; exit 0; fi
bytes=$(cut -d' ' -f2- "$todo" | (cd "$SRC" && xargs stat -f %z) | awk '{s+=$1} END {printf "%.1f MB", s/1048576}')
echo "$n file(s), $bytes -> ftp://$HOST/"

if [ $dry = 1 ]; then cut -d' ' -f2- "$todo"; exit 0; fi

i=0; start=$SECONDS
while read -r hash path; do
  i=$((i+1)); ok=0
  for try in 1 2 3; do
    curl -s --disable-epsv --ftp-create-dirs --max-time 120 -T "$SRC/$path" "ftp://$HOST/$path" && { ok=1; break; }
    sleep 1
  done
  if [ $ok = 1 ]; then
    echo "$hash $path" >> "$MANIFEST"
  else
    echo "$hash $path" >> "$failed"; echo "FAIL $path"
  fi
  if [ "$n" -le 10 ]; then echo "  $path"; elif [ $((i % 25)) = 0 ] || [ $i = "$n" ]; then echo "  $i/$n"; fi
done < "$todo"

# Rewrite the record: everything staged, minus what failed to send. (--app sent only the app; the
# loop already recorded it.)
[ $mode = app ] || sort "$failed" | comm -23 <(sort "$cur") - > "$MANIFEST"

nf=$(wc -l < "$failed" | tr -d ' ')
echo "done in $((SECONDS-start))s, $((n-nf)) sent, $nf failed"
[ "$nf" = 0 ]
