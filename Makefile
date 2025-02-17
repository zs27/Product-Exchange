CC=gcc
CFLAGS=-Wall -Werror -Wvla -O0 -std=c11 -g -fsanitize=address,leak
LDFLAGS=-lm
BINARIES=pe_exchange pe_trader

all: $(BINARIES)

.PHONY: clean
clean:
	rm -f $(BINARIES)


tests:
	echo tests_coming

run_tests:
	echo tests_coming

.PHONY: tests run_tests