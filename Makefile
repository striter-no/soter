CC = gcc
CFLAGS = -g
IFLAGS = -Icode/p2p/include

# CFLAGS += -fsanitize=address,undefined

LDFLAGS = 
LDLIBS = -lsodium

all:
	$(CC) $(CFLAGS) -o ./bin/main ./code/main.c $(LDFLAGS) $(LDLIBS) $(IFLAGS)
	$(CC) $(CFLAGS) -o ./bin/sser ./code/stateserver.c $(LDFLAGS) $(LDLIBS) $(IFLAGS)
setup:
	mkdir -p bin

	wget -O libsodium.tar https://download.libsodium.org/libsodium/releases/libsodium-1.0.21-stable.tar.gz
	tar -xf ./libsodium.tar
	cd ./libsodium-stable
	./configure
	make check && make
	sudo make install

	cd ../../
	rm -rf ./libsodium-stable
	rm -rf ./libsodium.tar
run:
	./bin/main
sser:
	./bin/sser
clean:
	rm -rf ./bin/*
