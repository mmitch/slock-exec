# slock - simple screen locker
# See LICENSE file for copyright and license details.

include config.mk

SRC = slock.c ${COMPATSRC}
OBJ = ${SRC:.c=.o}

# !!!!
# Look out: we drop privileges to mitch:mitch and as such, we don't
# want to be called by others.  So install to /home/mitch/bin and
# ensure that nobody can call the binary (/home/mitch/bin is 0700)
# !!!!
BINDIR = /home/mitch/bin
MANDIR = ${DESTDIR}${MANPREFIX}/man1

all: options slock

options:
	@echo slock build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk arg.h util.h

config.h:
	@echo creating $@ from config.def.h
	@cp config.def.h $@

slock: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f slock ${OBJ} slock-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p slock-${VERSION}
	@cp -R LICENSE Makefile README slock.1 config.mk \
		${SRC} explicit_bzero.c config.def.h arg.h util.h slock-${VERSION}
	@tar -cf slock-${VERSION}.tar slock-${VERSION}
	@gzip slock-${VERSION}.tar
	@rm -rf slock-${VERSION}

install: all
	@echo ensure that ${BINDIR} is ours
	@chmod 700 ${BINDIR}
	@echo installing executable file to ${BINDIR}
	@mkdir -p ${BINDIR}
	@cp -f slock ${BINDIR}
	@chmod 755 ${BINDIR}/slock
	@chmod u+s ${BINDIR}/slock
	@echo installing manual page to ${MANDIR}
	@mkdir -p ${MANDIR}
	@sed "s/VERSION/${VERSION}/g" <slock.1 >${MANDIR}/slock.1
	@chmod 644 ${MANDIR}/slock.1

uninstall:
	@echo removing executable file from ${BINDIR}
	@rm -f ${BINDIR}/slock
	@echo removing manual page from ${MANDIR}
	@rm -f ${MANDIR}/slock.1

.PHONY: all options clean dist install uninstall
