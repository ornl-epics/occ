TOP=../..

OCCLIB_DIR=$(TOP)/../occlib

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
# The temporary DWord alignment workaround in FPGA is to set bit 31 in the
# packet header length.
#USR_CXXFLAGS += -DDWORD_PADDING_WORKAROUND
CXXFLAGS = -Wall -g -ggdb $(USR_CXXFLAGS)

# Product
PROD_NAME = ned
PROD_IOC = $(PROD_NAME)

USR_INCLUDES = -I$(OCCLIB_DIR)
occ_DIR = $(OCCLIB_DIR)

DBD += $(PROD_NAME).dbd
$(PROD_NAME)_DBD += nedSupport.dbd
$(PROD_NAME)_DBD += asSupport.dbd
DBD += nedSupport.dbd

PROD_LIBS += ned
PROD_LIBS += asyn
PROD_LIBS += occ
PROD_LIBS += autosave
PROD_LIBS += stream
PROD_LIBS += $(EPICS_BASE_IOC_LIBS)

PROD_SRCS  += ned_registerRecordDeviceDriver.cpp
PROD_SRCS  += main.cpp

# Static library
LIBRARY = ned
SHARED_LIBRARIES = NO

# Headers used by unit-tests
INC += CircularBuffer.h

$(PROD_NAME)_SRCS  += Timer.cpp
$(PROD_NAME)_SRCS  += OccPortDriver.cpp
$(PROD_NAME)_SRCS  += DmaCircularBuffer.cpp
$(PROD_NAME)_SRCS  += CircularBuffer.cpp
$(PROD_NAME)_SRCS  += DmaCopier.cpp
$(PROD_NAME)_SRCS  += DasPacket.cpp
$(PROD_NAME)_SRCS  += DasPacketList.cpp
$(PROD_NAME)_SRCS  += BasePlugin.cpp
$(PROD_NAME)_SRCS  += BaseSocketPlugin.cpp
$(PROD_NAME)_SRCS  += ProxyPlugin.cpp
$(PROD_NAME)_SRCS  += AdaraPlugin.cpp
$(PROD_NAME)_SRCS  += CmdDispatcher.cpp
$(PROD_NAME)_SRCS  += BaseModulePlugin.cpp
$(PROD_NAME)_SRCS  += DspPlugin.cpp
$(PROD_NAME)_SRCS  += RocPlugin.cpp
$(PROD_NAME)_SRCS  += RocPlugin_v51.cpp
$(PROD_NAME)_SRCS  += RocPlugin_v52.cpp
$(PROD_NAME)_SRCS  += FemPlugin.cpp
$(PROD_NAME)_SRCS  += DiscoverPlugin.cpp
$(PROD_NAME)_SRCS  += DumpPlugin.cpp
$(PROD_NAME)_SRCS  += StatPlugin.cpp
$(PROD_NAME)_SRCS  += RtdlPlugin.cpp

$(PROD_NAME)_DOXS  += DspPlugin.dox
$(PROD_NAME)_DOXS  += RocPlugin.dox
$(PROD_NAME)_DOXS  += RocPlugin_v51.dox
$(PROD_NAME)_DOXS  += RocPlugin_v52.dox
$(PROD_NAME)_DOXS  += FemPlugin.dox

$(PROD_NAME)_TMPL  += DspPlugin.template
$(PROD_NAME)_TMPL  += RocPlugin.template
$(PROD_NAME)_TMPL  += FemPlugin.template

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

%.dox: %.cpp
	./tools/export2doxy.pl -input_file=$^ > $@

doc: Doxyfile $($(PROD_NAME)_DOXS)
	doxygen >/dev/null

%.template: %.cpp
	./tools/export2epics.pl -name_prefix=BL99b:xxxx: -input_file=$^ > ../Db/$@

dbtemplates: $(ned_TMPL)

clean::
	rm -f $($(PROD_NAME)_DOXS)
