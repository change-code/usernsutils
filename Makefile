all: bin/ns-spawn

bin/%: src/%.c | bin
	gcc -o "$@" "$<"

bin:
	mkdir bin

clean:
	rm -rf bin/*
