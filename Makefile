all: bin/ns-spawn bin/nshd bin/nsh bin/nsexec

bin/%: src/%.c | bin
	gcc -Wall -Werror -o "$@" "$<" -lutil

bin:
	mkdir bin

clean:
	rm -rf bin/*
