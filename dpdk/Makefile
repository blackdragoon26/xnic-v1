PKGCONF ?= pkg-config
CC ?= cc
CFLAGS += -O2 -g -Wall -Wextra -Wformat=2 -Wshadow -Werror
CFLAGS += $(shell $(PKGCONF) --cflags libdpdk)
LDLIBS += $(shell $(PKGCONF) --libs libdpdk)

.PHONY: all clean
all: xnic-forwarder

xnic-forwarder: xnic_forwarder.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	$(RM) xnic-forwarder
