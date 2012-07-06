VERSION = 0.1

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

INCS = `pkg-config --cflags cairo`
LIBS = -lX11 -lXrandr `pkg-config --libs cairo` -lconfuse

CFLAGS=-Wall -pedantic -std=gnu99 ${INCS} -D_GNU_SOURCE -DVERSION=\"${VERSION}\" -Os
LDFLAGS = -s ${LIBS}

CC = gcc
