CC ?= gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -g -I../../src/include
SRC = test_main.c test_reload_core.c test_holster_core.c test_holster_layout.c test_reload_loadsoninsert.c test_reload_rackslap.c test_reload_shotgun.c test_reload_crossbow.c ../../src/game/vr_reload_core.c ../../src/game/vr_holster_core.c
vrtests: $(SRC) ../../src/include/game/vr_reload_core.h ../../src/include/game/vr_holster_core.h vrtest.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm
test: vrtests
	./vrtests
clean:
	rm -f vrtests
.PHONY: test clean
