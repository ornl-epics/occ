CFLAGS=-Wall -I../driver -fPIC
#CFLAGS+=-DRX_DUMP_PATH="/tmp/occ.dump"
#CFLAGS+=-DTX_DUMP_PATH="/tmp/occ_tx.dump"
# Uncomment the next line to support larger packets,
# default is 8192 bytes. DSP-T max packetsize is ~3600 bytes but
# DSP before v6.3 allows configurable max packet size.
#CFLAGS+=-DROLLOVER_BUFFER_SIZE=65536
LDFLAGS=-shared -Wl,-soname,lib$(LIBNAME).so
SRCS=occlib.c i2c.c occlib_drv.c occlib_sock.c
HDRS=occlib.h occlib_hw.h occlib_drv.h occlib_sock.h
LIBNAME=occ
OBJS=$(SRCS:.c=.o)

.PHONY: all debug clean doc shared static

all: CFLAGS+=-O2
all: shared

debug: CFLAGS+=-ggdb -g
debug: shared

static: lib$(LIBNAME).a

shared: lib$(LIBNAME).so

%.o: %.cpp $(HDRS)
	$(CC) -c $(CFLAGS) $< -o $@

lib$(LIBNAME).so: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

clean:
	rm -fR $(OBJS) $(PIPEOBJS) $(SOCKOBJS) lib$(LIBNAME).so lib$(LIBNAME).a doc/

doc: $(HDRS) occlib.doxygen
	doxygen occlib.doxygen
