OCCLIB=../../lib
CPPFLAGS=-Wall -I$(OCCLIB) -std=c++11
LDFLAGS=-L$(OCCLIB) -locc -lrt -Wl,-rpath,$(OCCLIB)
SRCS=proxy.cpp
BIN=occ_proxy

HDRS=
OBJS=$(SRCS:.cpp=.o)

.PHONY: all debug common clean doc

all: CPPFLAGS+=-O2 -DNDEBUG
all: $(BIN)

debug: CPPFLAGS+=-ggdb -g -O0 -DTRACE
debug: $(BIN)

$(BIN): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJS) $(BIN)
