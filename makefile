CC=g++
CFLAGS=-Wall -g -std=c++11 -lpthread

storemake:
	$(CC) -o store store.cpp $(CFLAGS)
