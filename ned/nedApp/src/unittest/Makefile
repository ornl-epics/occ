TOP=../../..

include $(TOP)/configure/CONFIG

PROD_LIBS += Com
PROD_LIBS += asyn
PROD_LIBS += ned

CXXFLAGS = -g -ggdb

# Tests for the parameters
TESTPROD_HOST += testCircularBuffer
testCircularBuffer_SRCS += testCircularBuffer.cpp
testCircularBuffer_INCLUDES += -I../..
TESTS += testCircularBuffer

TESTSCRIPTS_HOST += $(TESTS:%=%.t)

include $(TOP)/configure/RULES

test:
	sh $(EPICS_HOST_ARCH)/
