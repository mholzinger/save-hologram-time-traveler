#!/usr/bin/env bash
# Headless Ghidra driver for the Time Traveler ROM.
#
#   re/ghidra/ghidra.sh import   # create project, import ROM, auto-analyze, apply labels
#   re/ghidra/ghidra.sh labels   # re-apply labels to existing project (after editing the script)
#   re/ghidra/ghidra.sh decomp   # export decompiled C to build/ghidra/decomp/
#   re/ghidra/ghidra.sh gui      # open the Ghidra GUI
#
# Ghidra is the shared install, run through ghidra-run (see ~/tools/ghidra/AGENTS.md). The
# version comes from build/ghidra/.ghidra-version (the project's pin), so nothing has to be
# set here; `ghidra-run --which build/ghidra` shows it.
#
# Env overrides:
#   GHIDRA_VERSION      e.g. ghidra_12.2_PUBLIC: run another install (a writable run on the
#                       pinned project is refused: project DBs upgrade one way)
#   TT_BASEADDR         default c000:0000 (real-mode seg:off). Verified accepted by
#                       BinaryLoader in 11.2.1 (2026-10-06). 0xC0000 is the fallback.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
# make does not always see the login PATH, so fall back to the install's own bin/
HEADLESS="$(command -v ghidra-run || echo "$HOME/tools/ghidra/bin/ghidra-run")"
PROJ_DIR="$ROOT/build/ghidra"
PROJ_NAME="TimeTraveler"
ROM="$ROOT/rom/tt061891.bin"
SCRIPTS="$HERE/scripts"
BASE="${TT_BASEADDR:-c000:0000}"
LANG_ID="x86:LE:16:Real Mode"

[ -x "$HEADLESS" ] || { echo "ghidra-run not found at $HEADLESS (shared install: ~/tools/ghidra)"; exit 1; }
mkdir -p "$PROJ_DIR"

case "${1:-}" in
  import)
    [ -f "$ROM" ] || { echo "missing $ROM -- run: make rom"; exit 1; }
    "$HEADLESS" "$PROJ_DIR" "$PROJ_NAME" \
      -import "$ROM" -overwrite \
      -loader BinaryLoader \
      -loader-baseAddr "$BASE" \
      -processor "$LANG_ID" \
      -scriptPath "$SCRIPTS" \
      -preScript TimeTravelerLabels.java \
      -postScript TimeTravelerLabels.java -postScript TimeTravelerModules.java
    ;;
  labels)
    "$HEADLESS" "$PROJ_DIR" "$PROJ_NAME" -process tt061891.bin -noanalysis \
      -scriptPath "$SCRIPTS" -postScript TimeTravelerLabels.java -postScript TimeTravelerModules.java
    ;;
  decomp)
    "$HEADLESS" "$PROJ_DIR" "$PROJ_NAME" -process tt061891.bin -noanalysis -readOnly \
      -scriptPath "$SCRIPTS" -postScript ExportDecomp.java "$ROOT/build/ghidra/decomp"
    ;;
  gui)
    exec "$(dirname "$HEADLESS")/ghidra" "$PROJ_DIR/$PROJ_NAME.gpr"
    ;;
  *)
    sed -n '2,12p' "$0"; exit 2 ;;
esac
