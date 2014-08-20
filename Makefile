C_SRCS=			\
    src/userns.c	\
    src/util.c		\
    src/spawn.c		\
    src/attach.c	\
    src/listen.c	\
    src/connect.c	\
    src/socketd.c       \
    src/proxy.c         \


all: bin/userns


bin/userns: $(C_SRCS) src/global.h Makefile | bin
	gcc -std=c99 -s -Os -Wall -Werror -D _GNU_SOURCE -o "$@" $(C_SRCS) -lutil


bin:
	mkdir bin


clean:
	rm -rf bin/*
