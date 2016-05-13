CFLAGS=-Wall -I../driver -fPIC
#CFLAGS+=-DRX_DUMP_PATH="/tmp/occ.dump"
#CFLAGS+=-DTX_DUMP_PATH="/tmp/occ_tx.dump"
LDFLAGS=-shared -Wl,-soname,lib$(LIBNAME).so
SRCS=occlib.c i2c.c
PIPESRCS=occlib_pipe.c
SOCKSRCS=occlib_socket.c
HDRS=occlib.h occlib_hw.h
LIBHWNAME=occ_hw
LIBNAME=occ
LIBPIPENAME=occ_pipe
OBJS=$(SRCS:.c=.o)
PIPEOBJS=$(PIPESRCS:.c=.o)
SOCKOBJS=$(SOCKSRCS:.c=.o)
LIBSOCKNAME=occ_socket

.PHONY: all debug simulator common clean doc

all: CFLAGS+=-O2
all: shared

debug: CFLAGS+=-ggdb -g
debug: shared

static: lib$(LIBHWNAME).a lib$(LIBPIPENAME).a lib$(LIBSOCKNAME).a

shared: lib$(LIBHWNAME).so lib$(LIBPIPENAME).so lib$(LIBSOCKNAME).so

occlib.o: occlib.c $(HDRS)

# This is tricky. Rules will be compiled before evaluated. When evaluated,
# OBJS might be different based on the simulator rule which makes this rule
# work.
lib$(LIBHWNAME).a: $(OBJS)
	$(AR) rcs $@ $(OBJS)
lib$(LIBHWNAME).so: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	ln -sf lib$(LIBHWNAME).so lib$(LIBNAME).so

lib$(LIBPIPENAME).a: $(PIPEOBJS)
	$(AR) rcs $@ $(PIPEOBJS)
lib$(LIBPIPENAME).so: $(PIPEOBJS)
	$(CC) $(LDFLAGS) -o $@ $(PIPEOBJS)

lib$(LIBSOCKNAME).a: $(SOCKOBJS)
	$(AR) rcs $@ $(SOCKOBJS)
lib$(LIBSOCKNAME).so: $(SOCKOBJS)
	$(CC) $(LDFLAGS) -o $@ $(SOCKOBJS)

clean:
	rm -fR $(OBJS) $(PIPEOBJS) $(SOCKOBJS) lib*.so lib*.a doc/

doc: $(HDRS) occlib.doxygen
	doxygen occlib.doxygen

hw: all
	ln -sf lib$(LIBHWNAME).so lib$(LIBNAME).so

pipe: all
	ln -sf lib$(LIBPIPENAME).so lib$(LIBNAME).so

socket: all
	ln -sf lib$(LIBSOCKNAME).so lib$(LIBNAME).so
