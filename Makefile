all:
	gcc -g -o ./bin/main ./code/main.c -Icode/p2p/include -Lcode/p2p/lib
	gcc -g -o ./bin/sser ./code/stateserver.c -Icode/p2p/include -Lcode/p2p/lib
setup:
	mkdir -p bin
run:
	./bin/main
sser:
	./bin/sser
clean:
	rm -rf ./bin/*
