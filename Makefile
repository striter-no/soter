all:
	gcc -g  -o ./bin/main ./code/main.c -Icode/p2p/include # -fsanitize=address,undefined
	gcc -g  -o ./bin/sser ./code/stateserver.c -Icode/p2p/include # -fsanitize=address,undefined
setup:
	mkdir -p bin
run:
	./bin/main
sser:
	./bin/sser
clean:
	rm -rf ./bin/*
