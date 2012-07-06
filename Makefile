include config.mk

SRC = xrandrbg.c images.c
HEADERS = images.h
DISTFILES = Makefile config.mk LICENSE README TODO
OBJ = ${SRC:.c=.o}

all: xrandrbg

.c.o:
	@${CC} -c $< ${CFLAGS}

${OBJ}: config.mk ${HEADERS}

xrandrbg: xrandrbg.o images.o
	@$(CC) -o $@ xrandrbg.o images.o $(LDFLAGS)

clean:
	@rm -f xrandrbg ${OBJ} xrandrbg-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p xrandrbg-${VERSION}
	@cp ${DISTFILES} ${SRC} ${HEADERS} xrandrbg-${VERSION}
	@tar -cf xrandrbg-${VERSION}.tar xrandrbg-${VERSION}
	@gzip xrandrbg-${VERSION}.tar
	@rm -rf xrandrbg-${VERSION}

install: all
	@echo installing executables to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f xrandrbg ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/xrandrbg
	#man pages

uninstall:
	@rm -f ${DESTDIR}${PREFIX}/bin/xrandrbg

.PHONY: all clean dist install uninstall
