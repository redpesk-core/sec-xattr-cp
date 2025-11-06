.PHONY: all install clean

all: sec-xattr-restore sec-xattr-extract sec-xattr-debug

prefix ?= /usr/local
exec_prefix ?= $(prefix)
bindir ?= $(exec_prefix)/bin
INSTALL ?= install

sec-xattr-extract: sec-xattr-extract.o common.o common.h
	$(CC) $(CFLAGS) -o $@ sec-xattr-extract.o common.o

sec-xattr-restore: sec-xattr-restore.o common.o common.h
	$(CC) $(CFLAGS) -o $@ sec-xattr-restore.o common.o

sec-xattr-debug: sec-xattr-debug.o common.o common.h
	$(CC) $(CFLAGS) -o $@ sec-xattr-debug.o common.o



clean:
	rm $(ALL) *.o
install: sec-xattr-restore sec-xattr-extract
	$(INSTALL) -D -t $(DESTDIR)$(bindir) sec-xattr-extract sec-xattr-restore
