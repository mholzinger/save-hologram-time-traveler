# Host builds of the engine and front end (no devkitPro needed). Run from this directory or via `make test`.
ROOT := ../../..
OUT := $(ROOT)/build/host
CC ?= cc
PY ?= python3 -I
CFLAGS := -std=c99 -O2 -Wall -Wextra -Werror

test_engine: test_engine.c ../source/engine.c ../source/engine.h
	$(CC) $(CFLAGS) -o $@ test_engine.c ../source/engine.c

TTG_INPUTS := $(addprefix $(ROOT)/data/,game.json clips.json sounds.json time_map.json) \
              $(ROOT)/build/led_messages.json $(ROOT)/tools/pack_game.py $(ROOT)/tools/sim.py
$(ROOT)/build/timetrv.ttg: $(TTG_INPUTS)
	cd $(ROOT) && $(PY) tools/pack_game.py data build/timetrv.ttg

# C engine and tools/sim.py must agree game for game (same RNG, same policy).
parity: test_engine $(ROOT)/build/timetrv.ttg
	@mkdir -p $(OUT)
	@for p in perfect none random; do \
	  ./test_engine $(ROOT)/build/timetrv.ttg $$p 300 > $(OUT)/parity_c_$$p.txt && \
	  (cd $(ROOT) && $(PY) tools/sim.py --auto 300 --rng xorshift --trace --policy $$p) > $(OUT)/parity_py_$$p.txt && \
	  diff $(OUT)/parity_c_$$p.txt $(OUT)/parity_py_$$p.txt > /dev/null || { echo "parity FAILED ($$p): diff $(OUT)/parity_c_$$p.txt $(OUT)/parity_py_$$p.txt"; exit 1; }; \
	done
	@echo "parity OK: 900 games identical"

test_2p: test_2p.c ../source/engine.c ../source/engine.h
	$(CC) $(CFLAGS) -pthread -o $@ test_2p.c ../source/engine.c

test_ledmsg: test_ledmsg.c ../source/ledmsg.c ../source/ledmsg.h ../source/engine.c
	$(CC) $(CFLAGS) -o $@ test_ledmsg.c ../source/ledmsg.c ../source/engine.c

test_led: test_led.c ../source/led.c ../source/led.h fake/3ds.h
	$(CC) -std=c99 -O2 -Wall -Wextra -Ifake -o $@ test_led.c ../source/led.c

# The LED panel rendered on the host, for checking the 14-segment glyphs: build/host/led.png
led: test_led
	@mkdir -p $(OUT)
	./test_led $(OUT)/led.ppm && ffmpeg -v error -y -i $(OUT)/led.ppm -vf scale=640:-1 $(OUT)/led.png

clean:
	rm -f test_engine test_led test_2p test_ledmsg test_frontend
	rm -rf test_frontend.dSYM $(OUT)

# Every host test against its saved output (golden.sh; `make golden-update` after an intended change).
HOST_TESTS := test_frontend test_2p test_ledmsg test_led
golden: $(HOST_TESTS) $(ROOT)/build/timetrv.ttg
	PY="$(PY)" ./golden.sh
golden-update: $(HOST_TESTS) $(ROOT)/build/timetrv.ttg
	PY="$(PY)" ./golden.sh update

.PHONY: parity led clean golden golden-update

# The real front end on the host with simulated time and scripted buttons (input path checks).
FE_SOURCES := $(wildcard ../source/fe_*.c) ../source/engine.c ../source/led.c ../source/ledmsg.c ../source/cfg.c
test_frontend: test_frontend.c fake_fe/3ds.h ../source/fe.h $(FE_SOURCES)
	$(CC) -std=gnu11 -O0 -g -Wall -Wno-unused-parameter -DTT_HOST -Ifake_fe -o $@ test_frontend.c $(FE_SOURCES)
