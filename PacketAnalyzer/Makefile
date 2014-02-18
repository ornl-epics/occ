CPPFLAGS=-Wall -I../occlib/
LDFLAGS=-L../occlib -locc -lcurses
SRCS=$(wildcard *.cpp)
HDRS=
BIN=PacketAnalyzer
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
