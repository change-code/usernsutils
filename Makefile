C_SRCS= $(wildcard src/*.c)

all: bin/userns

bin/userns: $(C_SRCS) src/global.h Makefile | bin
	gcc -std=c99 -s -Os -Wall -Wextra -Werror -D _GNU_SOURCE -o "$@" $(C_SRCS) -lutil

bin:
	mkdir bin

clean:
	rm -rf bin/*
