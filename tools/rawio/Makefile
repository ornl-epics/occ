OCCLIB=../../lib
CPPFLAGS=-Wall -I$(OCCLIB) -std=c++0x
LDFLAGS=-L$(OCCLIB) -locc -Wl,-rpath,$(OCCLIB)
SRCS=rawio.cpp
BIN=occ_rawio

HDRS=
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
