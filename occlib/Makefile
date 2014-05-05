CFLAGS=-Wall -I../occdriver
#CFLAGS+=-DRX_DUMP_PATH="/tmp/occ.dump" -DTX_DUMP_PATH="/tmp/occ_tx.dump"
LDFLAGS=
SRCS=occlib.c
SIMSRCS=occlib_sim.c
HDRS=occlib.h occlib_hw.h
LIBNAME=occ
OBJS=$(SRCS:.c=.o)
SIMOBJS=$(SIMSRCS:.c=.o)

.PHONY: all debug simulator common clean doc

all: CFLAGS+=-O2
all: common

simulator: OBJS=$(SIMOBJS)
simulator: debug

debug: CFLAGS+=-ggdb -g
debug: common

common: lib$(LIBNAME).a

occlib.o: occlib.c $(HDRS)

# This is tricky. Rules will be compiled before evaluated. When evaluated,
# OBJS might be different based on the simulator rule which makes this rule
# work.
lib$(LIBNAME).a: $(OBJS) $(SIMOBJS)
	$(AR) rcs $@ $(OBJS)

clean:
	rm -fR $(OBJS) lib$(LIBNAME).a doc/

doc: $(HDRS) occlib.doxygen
	doxygen occlib.doxygen
