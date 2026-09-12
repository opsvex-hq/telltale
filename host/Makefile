# Host test build for the telltale portable core.
#
# No CMake, no package manager, no network: any machine with a C compiler can
# run the suite. `make` builds and runs it; `make asan` re-runs it under the
# address and undefined-behaviour sanitizers, which is where buffer arithmetic
# bugs actually surface.

CC      ?= gcc
CSTD    ?= -std=c11
WARN     = -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
           -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith
INCLUDE  = -I../portable/include -I.
SRC      = ../portable/src/telltale_core.c test_core.c
BUILD   ?= build

.PHONY: all test asan clean

all: test

test: $(BUILD)/test_core
	@$(BUILD)/test_core

$(BUILD)/test_core: $(SRC) ../portable/include/telltale_core.h tinytest.h
	@mkdir -p $(BUILD)
	$(CC) $(CSTD) $(WARN) -O2 $(INCLUDE) $(SRC) -o $@ -lm

asan: $(SRC)
	@mkdir -p $(BUILD)
	$(CC) $(CSTD) $(WARN) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		$(INCLUDE) $(SRC) -o $(BUILD)/test_core_asan -lm
	@$(BUILD)/test_core_asan

clean:
	@rm -rf $(BUILD)
