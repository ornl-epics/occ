OCCLIB=../../lib
CPPFLAGS=-Wall -I$(OCCLIB) -std=c++0x
LDFLAGS=-L$(OCCLIB) -locc -Wl,-rpath,$(OCCLIB) -lboost_iostreams
SRCS=flashprog.cpp
BIN=occ_flash

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
