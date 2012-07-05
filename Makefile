CC=gcc
LIBS=-lX11 -lXrandr `pkg-config --libs cairo` -lconfuse
CFLAGS=`pkg-config --cflags cairo`

all: xrandrbg

xrandrbg: xrandrbg.o images.o
	$(CC) $(CFLAGS) -o xrandrbg xrandrbg.o images.o $(LIBS)

xrandrbg.o: xrandrbg.c
	$(CC) $(CFLAGS) -c -o xrandrbg.o xrandrbg.c

images.o: images.c
	$(CC) $(CFLAGS) -c -o images.o images.c

clean:
	rm xrandrbg *.o
