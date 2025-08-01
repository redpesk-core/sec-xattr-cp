.PHONY: all

all: sec-xattr-restore sec-xattr-extract

CFLAGS += -g # -O3 -Wl,--strip-all # -static

sec-xattr-extract: sec-xattr-extract.c sec-xattr-cp.h
	$(CC) $(CFLAGS) -o $@ $<

sec-xattr-restore: sec-xattr-restore.c sec-xattr-cp.h
	$(CC) $(CFLAGS) -o $@ $<
