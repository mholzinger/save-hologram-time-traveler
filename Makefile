# Time Traveler RE + 3DS port. Run from this directory.
# Setup: python3 -m venv .venv && .venv/bin/pip install -r requirements.txt (see README.md)

# Machine-specific settings (DS_HOST): local.mk, not in git. Ghidra needs none (ghidra-run).
-include local.mk

# Every tool runs isolated (-I) in the repo venv when there is one.
PY        ?= $(if $(wildcard .venv/bin/python),.venv/bin/python,python3) -I
ROM_ZIP   ?= ../timetrv.zip
ROM       := rom/tt061891.bin
ROM_SHA1  := 7c5003b6d3df1e472db45abd725e7d3d43f0dfb4

.PHONY: help patch patcher-exe test format format-check art log release run game doctor encode check-media rom verify disasm tables check-labels pack stage 3ds-syntax ghidra-import ghidra-labels ghidra-decomp ghidra-gui 3ds deploy deploy-app clean

help:
	@echo "make rom            unzip $(ROM_ZIP) into rom/ and verify SHA1"
	@echo "make disasm         Capstone pass -> build/capstone/{listing.asm,funcs.txt,io.txt}"
	@echo "make tables         regenerate data/*.json|csv from ROM"
	@echo "make check-labels   verify Ghidra label addresses are function starts"
	@echo "make ghidra-import  headless import + analysis + labels (build/ghidra/)"
	@echo "make ghidra-labels  re-apply re/ghidra/scripts/TimeTravelerLabels.java"
	@echo "make ghidra-decomp  dump decompiled C -> build/ghidra/decomp/"
	@echo "make ghidra-gui     open Ghidra"
	@echo "make 3ds            build port/3ds (needs devkitPro 3ds-dev)"
	@echo "make 3ds-syntax     syntax-check port/3ds sources without 3ds-dev"
	@echo "make stage          SD card files -> build/sdmc/ (scene graph + encoded clips)"
	@echo "make deploy         build, stage, FTP changed files to the 3DS (ftpd; DS_HOST=ip:port)"
	@echo "make deploy-app     build and FTP only the .3dsx"
	@echo "make run            build and launch the .3dsx on the 3DS over 3dslink (Homebrew Launcher, Y)"
	@echo "make format         reformat C (clang-format) and Python (ruff); make format-check only checks"
	@echo "make test           sim --check, C/Python parity, ROM differential tests (no 3DS needed)"
	@echo "make game           the whole game from rom/timetrv.chd + ../timetrv.zip: checks, encode, stage, one-file .3dsx"
	@echo "make doctor         check the tools and inputs make game needs"
	@echo "make encode         encode every clip and segment from the CHD into build/enc_ld (skipped when up to date)"
	@echo "make art            cut and convert the local art (docs/ART.md; never committed or shipped)"
	@echo "make log            fetch the last game's play log from the 3DS (ftpd) to build/log.txt and show it"
	@echo "make patch          the patcher as players run it: $(ROM_ZIP) + rom/timetrv.chd -> out/timetrv3ds.3dsx"
	@echo "make release        one-file test build: build/release/timetrv3ds.3dsx with all data in RomFS"

# The program ROM: rom/tt061891.bin, unzipped from ../timetrv.zip when it is not already there.
$(ROM):
	@[ -f $(ROM_ZIP) ] || { echo "missing $(ROM) and $(ROM_ZIP) (the program ROM, MAME set timetrv)"; exit 1; }
	mkdir -p rom
	unzip -o -j $(ROM_ZIP) tt061891.bin -d rom
	touch $(ROM)

rom: $(ROM) verify

verify: $(ROM)
	@echo "$(ROM_SHA1)  $(ROM)" | shasum -a 1 -c -

disasm: $(ROM)
	$(PY) re/capstone/trace.py $(ROM) build/capstone

tables: $(ROM)
	$(PY) tools/extract_tables.py $(ROM) data
build/led_messages.json: $(ROM) tools/extract_tables.py
	$(PY) tools/extract_tables.py $(ROM) data

check-labels:
	$(PY) tools/check_labels.py

ghidra-import: $(ROM)
	re/ghidra/ghidra.sh import
ghidra-labels:
	re/ghidra/ghidra.sh labels
ghidra-decomp:
	re/ghidra/ghidra.sh decomp
ghidra-gui:
	re/ghidra/ghidra.sh gui

pack: build/led_messages.json
	$(PY) tools/pack_game.py data build/timetrv.ttg

# Every check that needs no 3DS: the scene rules (sim --check), C engine = Python model (900 games),
# and the real ROM against the rules (romdiff: no input, perfect play).
test: pack
	$(PY) tools/sim.py --check
	$(MAKE) -C port/3ds/host parity golden PY="$(if $(wildcard .venv/bin/python),$(CURDIR)/.venv/bin/python,python3) -I"
	@mkdir -p build/host
	@for mode in "--seconds 900" "--perfect --seconds 1800"; do \
	  $(PY) tools/romdiff.py $$mode > build/host/romdiff.txt; st=$$?; \
	  echo "romdiff $$mode:"; grep agree build/host/romdiff.txt; \
	  [ $$st = 0 ] || { echo "romdiff FAILED: see build/host/romdiff.txt"; exit 1; }; \
	done

# Clip encodes: build/enc_ld from the laserdisc CHD (tools/encode_ld.py, default), or ENC=build/enc
# for the older DVD encodes (tools/encode_clip.py).
ENC ?= build/enc_ld

# SD card layout for the 3DS build: copy build/sdmc/* to the card root.
stage: pack
	mkdir -p build/sdmc/timetrv/clips
	cp build/timetrv.ttg build/sdmc/timetrv/
	mkdir -p build/sdmc/3ds
	test -f port/3ds/timetrv3ds.3dsx && cp port/3ds/timetrv3ds.3dsx build/sdmc/3ds/ || echo "(no .3dsx yet: run make 3ds)"
	rm -f build/sdmc/timetrv/clips/*
	if [ "$(ENC)" = build/enc_ld ]; then $(PY) tools/check_media.py $(ENC); fi
	cp $(ENC)/*.h264 $(ENC)/*.wav build/sdmc/timetrv/clips/
	@ls build/sdmc/timetrv/clips | wc -l | xargs echo "clip files staged:"
	rm -rf build/sdmc/timetrv/art
	if [ -d build/art/sdmc/timetrv/art ]; then cp -R build/art/sdmc/timetrv/art build/sdmc/timetrv/; echo "art staged"; fi

# One-file build for testing (docs/TASKS.md Phase 5): the .3dsx with everything make stage puts in
# build/sdmc/timetrv/ (game data, clips, art) packed in as RomFS; the app reads romfs:/timetrv when it
# finds the game there (port/3ds/source/data.c). Copy the one file to /3ds/ on the card. It contains
# the laserdisc footage and any local art: for your own testing, never committed or shared.
release: 3ds stage
	rm -rf build/release && mkdir -p build/release/romfs
	cp -R build/sdmc/timetrv build/release/romfs/
	rm -f port/3ds/timetrv3ds.3dsx
	$(MAKE) -C port/3ds ROMFS=../../build/release/romfs
	mv port/3ds/timetrv3ds.3dsx build/release/timetrv3ds.3dsx
	$(MAKE) -C port/3ds
	rm -rf build/release/romfs
	@ls -lh build/release/timetrv3ds.3dsx

# ---- The whole game from the user's own inputs (nothing copyrighted is in git) ----
# rom/timetrv.chd: the laserdisc (header SHA1 as MAME's timetrv.cpp:337); ../timetrv.zip: the program ROM.
CHD_SHA1 := 8abb5e6aa58ab49477ef89f507264d35454f99d3

doctor:
	@ok=1; \
	for t in chdman ffmpeg; do command -v $$t >/dev/null || { echo "missing: $$t (brew install rom-tools ffmpeg)"; ok=0; }; done; \
	ffmpeg -hide_banner -encoders 2>/dev/null | grep -q libx264 || { echo "ffmpeg has no libx264 encoder"; ok=0; }; \
	[ -x .venv/bin/python ] || { echo "no .venv: python3 -m venv .venv && .venv/bin/pip install -r requirements.txt"; ok=0; }; \
	[ -x .venv/bin/python ] && { .venv/bin/python -c "import numpy" 2>/dev/null || { echo "venv lacks numpy: .venv/bin/pip install -r requirements.txt"; ok=0; }; }; \
	[ -n "$(DEVKITARM)" ] || { echo "DEVKITARM unset: install devkitPro 3ds-dev"; ok=0; }; \
	[ -f rom/timetrv.chd ] || { echo "missing rom/timetrv.chd (the laserdisc, MAME set timetrv)"; ok=0; }; \
	[ -f rom/tt061891.bin ] || [ -f ../timetrv.zip ] || { echo "missing ../timetrv.zip (the program ROM)"; ok=0; }; \
	if [ -f rom/timetrv.chd ] && command -v chdman >/dev/null; then \
	  sha=$$(chdman info -i rom/timetrv.chd | awk '/^SHA1:/ {print $$2}'); \
	  [ "$$sha" = "$(CHD_SHA1)" ] || { echo "rom/timetrv.chd SHA1 $$sha, expected $(CHD_SHA1)"; ok=0; }; fi; \
	[ $$ok = 1 ] && echo "doctor: all set" || exit 1

# Every clip and named segment the game opens; only what is not encoded yet (rm -rf build/enc_ld to redo).
encode:
	$(PY) tools/encode_ld.py --all --extra --missing --out build/enc_ld

check-media:
	$(PY) tools/check_media.py

# The patcher as a player runs it (docs/PATCHER.md): the ROM zip and the CHD in, out/timetrv3ds.3dsx out.
patch: 3ds
	$(PY) tools/patcher.py --rom $(ROM_ZIP) --chd rom/timetrv.chd --out out

# The one-file patcher for this OS in dist/ (needs a Python with Tk and requirements-patcher.txt; the
# GitHub release workflow builds all three OSes).
patcher-exe: 3ds
	rm -rf build/patcher-app && mkdir -p build/patcher-app
	cp port/3ds/timetrv3ds.3dsx port/3ds/timetrv3ds.elf build/patcher-app/
	$(PY) tools/build_patcher.py build/patcher-app

game: doctor rom tables encode 3ds stage release
	@echo "game: build/release/timetrv3ds.3dsx (copy to /3ds/ on the SD card)"

# Code style (.clang-format, pyproject.toml): reformat, or only check (CI-style, no changes).
C_SOURCES := $(wildcard port/3ds/source/*.c port/3ds/source/*.h port/3ds/host/*.c port/3ds/host/fake*/*.h)
format:
	.venv/bin/clang-format -i $(C_SOURCES)
	.venv/bin/ruff format tools re
	.venv/bin/ruff check --fix tools re
format-check:
	.venv/bin/clang-format --dry-run --Werror $(C_SOURCES)
	.venv/bin/ruff format --check tools re
	.venv/bin/ruff check tools re

# Local art (docs/ART.md): docs/reference/art/cut_art.py crops the sources into build/art/, then
# tools/art_convert.py makes the screens the port loads, in build/art/sdmc/timetrv/art/. All ignored.
art:
	@test -f docs/reference/art/cut_art.py || { echo "no docs/reference/art/cut_art.py: see docs/ART.md"; exit 1; }
	$(PY) docs/reference/art/cut_art.py
	$(PY) tools/art_convert.py

# Syntax-check the 3DS sources against pinned libctru headers (no 3ds-dev install needed).
3ds-syntax:
	test -d build/third_party/libctru || git clone -q https://github.com/devkitPro/libctru.git build/third_party/libctru
	git -C build/third_party/libctru checkout -q 9b55eda
	for f in port/3ds/source/*.c; do $(DEVKITARM)/bin/arm-none-eabi-gcc -fsyntax-only -std=gnu11 -Wall -Wextra \
	  -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -D__3DS__ \
	  -Ibuild/third_party/libctru/libctru/include -Iport/3ds/source $$f || exit 1; done
	@echo "3DS sources: syntax OK"

3ds:
	$(MAKE) -C port/3ds

# Over Wi-Fi to ftpd on the console; only files changed on that SD card are sent (tools/deploy.sh;
# the card carries an id, so a swapped card gets everything).
# The 3DS running ftpd, ip:port: set it once in local.mk (DS_HOST := 192.168.1.20:5000), or per run
# (make deploy DS_HOST=...).
export DS_HOST

# The last game's play log (port/3ds/source/frontend.c plog_line), fetched from the 3DS over ftpd.
log:
	@curl -s --disable-epsv --max-time 10 -o build/log.txt "ftp://$(DS_HOST)/timetrv/log.txt" || { echo "can't reach ftp://$(DS_HOST)/ : is ftpd open?"; exit 1; }
	@cat build/log.txt

# Code-only changes: send the .3dsx straight to the Homebrew Launcher's netloader (press Y on the 3DS;
# ftpd must be closed, one app runs at a time) and start it. Runs from memory: the card's copy is
# updated by the next make deploy. Don't port-scan 17491 first: the netloader takes one connection and
# an empty one makes it fail ("namelen:11").
run: 3ds
	$(DEVKITPRO)/tools/bin/3dslink -r 10 -a $(firstword $(subst :, ,$(DS_HOST))) port/3ds/timetrv3ds.3dsx

deploy: 3ds stage
	tools/deploy.sh

deploy-app: 3ds
	mkdir -p build/sdmc/3ds
	cp port/3ds/timetrv3ds.3dsx build/sdmc/3ds/
	tools/deploy.sh --app

clean:
	rm -rf build/capstone build/ghidra/decomp
	$(MAKE) -C port/3ds clean || true
