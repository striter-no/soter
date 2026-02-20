setup:
	mkdir -p bin

all:
	gcc -g -o ./bin/main ./code/main.c -Icode/p2p/include -Lcode/p2p/lib
run:
	./bin/main
clean:
	rm -rf ./bin/*
