CPPFLAGS=-Wall -I../occlib/
LDFLAGS=-L../occlib -locc -lpthread -lrt
SRCS=occ_loopback.cpp occ_rawio.cpp occ_dump.cpp
HDRS=
BINS=$(SRCS:.cpp=)
OBJS=$(SRCS:.cpp=.o)

.PHONY: all debug common clean doc

all: CPPFLAGS+=-O2 -DNDEBUG
all: $(BINS)

debug: CPPFLAGS+=-ggdb -g -DTRACE
debug: $(BINS)

$(BINS): occ_% : occ_%.o
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJS) $(BINS)
