CFLAGS=-Wall
LDFLAGS=
SRCS=occlib.c
HDRS=occlib.h occlib_hw.h
LIBNAME=occ
OBJS=$(SRCS:.c=.o)

.PHONY: all debug common clean doc

all: CFLAGS+=-O2 -DNDEBUG
all: common

debug: CFLAGS+=-ggdb -g
debug: common

common: lib$(LIBNAME).a

occlib.o: occlib.c $(HDRS)

lib$(LIBNAME).a: $(OBJS)
	$(AR) rcs $@ $^

clean:
	rm -fR $(OBJS) lib$(LIBNAME).a doc/

doc: $(HDRS) occlib.doxygen
	doxygen occlib.doxygen
