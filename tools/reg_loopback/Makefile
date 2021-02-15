OCCLIB=$(abspath ../../lib)
CPPFLAGS=-Wall -I$(OCCLIB) -std=c++0x -Wno-packed-bitfield-compat
LDFLAGS=-L$(OCCLIB) -locc -lrt -Wl,-rpath,$(OCCLIB) -pthread
SRCS=reg_loop.cpp
BIN=reg_loop
WRITESRCS=write.cpp
WRITEBIN=write
READSRCS=read.cpp
READBIN=read

HDRS=
OBJS=$(SRCS:.cpp=.o)
WRITEOBJS=$(WRITESRCS:.cpp=.o)
READOBJS=$(READSRCS:.cpp=.o)

.PHONY: all debug common clean doc

all: CPPFLAGS+=-O2 -DNDEBUG
all: $(BIN) $(WRITEBIN) $(READBIN)

debug: CPPFLAGS+=-ggdb -g -DTRACE
debug: $(BIN) $(WRITEBIN) $(READBIN)

$(BIN): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(WRITEBIN): $(WRITEOBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(READBIN): $(READOBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJS) $(WRITEOBJS) $(READOBJS) $(BIN) $(WRITEBIN) $(READBIN)
