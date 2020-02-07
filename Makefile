CFLAGS = -O -std=c++11
CC = g++
LIBS=-pthread

all: proxy


proxy: httpProxy.o
	$(CC) $(CFLAGS) -o proxy httpProxy.o $(LIBS)

httpProxy.o: httpProxy.cpp
	$(CC) $(CFLAGS) -c httpProxy.cpp


clean:
	-rm  *.o proxy
