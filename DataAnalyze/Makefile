CPPFLAGS=-Wall -I../occlib/ -std=c++0x
LDFLAGS=
SRCS=sms2csv.cpp occ2csv.cpp
HDRS=
BINS=$(SRCS:.cpp=)
OBJS=$(SRCS:.cpp=.o)

.PHONY: all debug common clean doc

all: CPPFLAGS+=-O2 -DNDEBUG
all: $(BINS)

debug: CPPFLAGS+=-ggdb -g -DTRACE
debug: $(BINS)

$(BINS): %: %.o
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJS) $(BINS)
