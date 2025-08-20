.PHONY: all install

all: sec-xattr-restore sec-xattr-extract sec-xattr-debug

prefix ?= /usr/local
exec_prefix ?= $(prefix)
bindir ?= $(exec_prefix)/bin
INSTALL ?= install

sec-xattr-extract: sec-xattr-extract.c sec-xattr-cp.h
	$(CC) $(CFLAGS) -o $@ $<

sec-xattr-restore: sec-xattr-restore.c sec-xattr-cp.h
	$(CC) $(CFLAGS) -o $@ $<

sec-xattr-debug: sec-xattr-debug.c sec-xattr-cp.h
	$(CC) $(CFLAGS) -o $@ $<

install: sec-xattr-restore sec-xattr-extract
	$(INSTALL) -D -t $(DESTDIR)$(bindir) sec-xattr-extract sec-xattr-restore
