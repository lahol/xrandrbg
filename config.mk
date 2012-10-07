VERSION = 0.1

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

INCS = `imlib2-config --cflags`
LIBS = -lX11 -lXrandr `imlib2-config --libs` -lconfuse -lev

CFLAGS=-Wall -pedantic -std=gnu99 ${INCS} -D_GNU_SOURCE -DVERSION=\"${VERSION}\" -Os
LDFLAGS = -s ${LIBS}

CC = gcc
