OCCLIB=$(abspath $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/../../lib)
CPPFLAGS=-Wall -I$(OCCLIB) -std=c++0x -DBITFIELD_LSB_FIRST
LDFLAGS=-L$(OCCLIB) -locc -Wl,-rpath,$(OCCLIB) -lcurses -lrt
SRCS=$(wildcard *.cpp)
HDRS=
BIN=occdiag
OBJS=$(SRCS:.cpp=.o)

.PHONY: all debug common clean doc

all: CPPFLAGS+=-O2 -DNDEBUG
all: $(BIN)

debug: CPPFLAGS+=-ggdb -g -DTRACE
debug: $(BIN)

$(BIN): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJS) $(BIN)
