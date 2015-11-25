CPPFLAGS=-Wall -std=c++0x
LDFLAGS=-lrt
SRCS=replay.cpp
BIN=occ_replay

HDRS=
OBJS=$(SRCS:.cpp=.o)

.PHONY: all debug clean

all: CPPFLAGS+=-O2 -DNDEBUG
all: $(BIN)

debug: CPPFLAGS+=-ggdb -g -DTRACE
debug: $(BIN)

$(BIN): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJS) $(BIN)
