TOP=../..

OCCLIB_DIR=/home/kvodopivec/work/SNS/git/detectors/occlib

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Split build into static library and app for unit tests
# to pick-up functionlity from library.

# The C/C++ bitfield ordering in structs is platform AND compiler dependant
# 
USR_CXXFLAGS += -DBITFIELD_LSB_FIRST
USR_CXXFLAGS += -std=c++0x
CXXFLAGS = -g -ggdb $(USR_CXXFLAGS)

# Product
PROD_NAME = ned
PROD_IOC = $(PROD_NAME)

USR_INCLUDES = -I$(OCCLIB_DIR)
occ_DIR = $(OCCLIB_DIR)

DBD += $(PROD_NAME).dbd
$(PROD_NAME)_DBD += nedSupport.dbd
DBD += nedSupport.dbd

PROD_LIBS += ned
PROD_LIBS += asyn
PROD_LIBS += occ
PROD_LIBS += $(EPICS_BASE_IOC_LIBS)

PROD_SRCS  += ned_registerRecordDeviceDriver.cpp
PROD_SRCS  += main.cpp

# Static library
LIBRARY = ned
SHARED_LIBRARIES = NO

# Headers used by unit-tests
INC += CircularBuffer.h

ned_SRCS  += OccPortDriver.cpp
ned_SRCS  += DmaCircularBuffer.cpp
ned_SRCS  += CircularBuffer.cpp
ned_SRCS  += DmaCopier.cpp
ned_SRCS  += DasPacket.cpp
ned_SRCS  += DasPacketList.cpp
ned_SRCS  += BasePlugin.cpp
ned_SRCS  += AdaraPlugin.cpp
ned_SRCS  += BaseDispatcher.cpp
ned_SRCS  += OccDispatcher.cpp

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
