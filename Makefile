CC=gcc
LIBS=-lX11 -lXrandr `pkg-config --libs cairo`
CFLAGS=`pkg-config --cflags cairo`

all: xrandrbg

xrandrbg: xrandrbg.c
	$(CC) $(CFLAGS) -o xrandrbg xrandrbg.c $(LIBS)

clean:
	rm xrandrbg
