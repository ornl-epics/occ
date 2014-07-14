CFLAGS=-Wall -I../occdriver -fPIC
#CFLAGS+=-DRX_DUMP_PATH="/tmp/occ.dump" -DTX_DUMP_PATH="/tmp/occ_tx.dump"
LDFLAGS=-shared -Wl,-soname,lib$(LIBNAME).so
SRCS=occlib.c i2c.c
SIMSRCS=occlib_sim.c
HDRS=occlib.h occlib_hw.h
LIBNAME=occ
LIBSIMNAME=occ_sim
OBJS=$(SRCS:.c=.o)
SIMOBJS=$(SIMSRCS:.c=.o)

.PHONY: all debug simulator common clean doc

all: CFLAGS+=-O2
all: shared

debug: CFLAGS+=-ggdb -g
debug: shared

static: lib$(LIBNAME).a lib$(LIBSIMNAME).a

shared: lib$(LIBNAME).so lib$(LIBSIMNAME).so

occlib.o: occlib.c $(HDRS)

# This is tricky. Rules will be compiled before evaluated. When evaluated,
# OBJS might be different based on the simulator rule which makes this rule
# work.
lib$(LIBNAME).a: $(OBJS)
	$(AR) rcs $@ $(OBJS)
lib$(LIBNAME).so: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

lib$(LIBSIMNAME).a: $(SIMOBJS)
	$(AR) rcs $@ $(OBJS)
lib$(LIBSIMNAME).so: $(SIMOBJS)
	$(CC) $(LDFLAGS) -o $@ $(SIMOBJS)

clean:
	rm -fR $(OBJS) $(SIMOBJS) lib*.so lib*.a doc/

doc: $(HDRS) occlib.doxygen
	doxygen occlib.doxygen
