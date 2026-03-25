.PHONY: all install clean

CFLAGS += -Wall

prefix ?= /usr/local
exec_prefix ?= $(prefix)
bindir ?= $(exec_prefix)/bin
INSTALL ?= install

ALL = sec-xattr-restore sec-xattr-extract sec-xattr-debug sec-xattr-build

all: $(ALL)

sec-xattr-extract: sec-xattr-extract.o common.o common.h
	$(CC) $(CFLAGS) -o $@ sec-xattr-extract.o common.o

sec-xattr-restore: sec-xattr-restore.o common.o common.h
	$(CC) $(CFLAGS) -o $@ sec-xattr-restore.o common.o

sec-xattr-debug: sec-xattr-debug.o common.o common.h
	$(CC) $(CFLAGS) -o $@ sec-xattr-debug.o common.o

sec-xattr-build: sec-xattr-build.o common.o common.h
	$(CC) $(CFLAGS) -o $@ sec-xattr-build.o common.o

install: sec-xattr-restore sec-xattr-extract sec-xattr-build
	$(INSTALL) -D -t $(DESTDIR)$(bindir) $^

clean:
	rm $(ALL) *.o
