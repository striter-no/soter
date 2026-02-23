CC = gcc
CFLAGS = -g
IFLAGS = -Icode/p2p/include

# CFLAGS += -fsanitize=address,undefined

LDFLAGS = 
LDLIBS = -lsodium

all:
	$(CC) $(CFLAGS) -o ./bin/main ./code/main.c $(LDFLAGS) $(LDLIBS) $(IFLAGS)
	$(CC) $(CFLAGS) -o ./bin/example ./code/example.c $(LDFLAGS) $(LDLIBS) $(IFLAGS)
	$(CC) $(CFLAGS) -o ./bin/sser ./code/stateserver.c $(LDFLAGS) $(LDLIBS) $(IFLAGS)
setup:
	mkdir -p bin
	chmod +x ./setup
	./setup
run:
	./bin/main
sser:
	./bin/sser
clean:
	rm -rf ./bin/*
