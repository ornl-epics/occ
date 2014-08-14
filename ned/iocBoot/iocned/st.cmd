#!../../bin/linux-x86_64/ned

< envPaths

epicsEnvSet("IOCNAME", "bl99-iocned")

# StreamDevice
epicsEnvSet("STREAM_PROTOCOL_PATH", "../../protocol/")

## Register all support components
dbLoadDatabase("../../dbd/ned.dbd",0,0)
ned_registerRecordDeviceDriver(pdbbase) 

# Autosave
epicsEnvSet SAVE_DIR /home/controls/var/$(IOCNAME)
system("install -m 777 -d $(SAVE_DIR)")
save_restoreSet_Debug(0)
save_restoreSet_status_prefix("BL99:IOCNAME:")
set_requestfile_path("$(SAVE_DIR)")
set_savefile_path("$(SAVE_DIR)")
save_restoreSet_NumSeqFiles(3)
save_restoreSet_SeqPeriodInSeconds(600)
###set_pass0_restoreFile("$(IOCNAME).sav")
###set_pass0_restoreFile("$(IOCNAME)_pass0.sav")
set_pass1_restoreFile("$(IOCNAME).sav")

## Load record instances
epicsEnvSet("PREFIX", "BL99:")
epicsEnvSet("OCC1",   "/dev/snsocb1")
#asynSetTraceIOMask("$(OCC1)",0,255)
OccConfigure("$(OCC1)", 40000000)
dbLoadRecords("../../db/Occ.template","P=$(PREFIX)Det:occ1:,PORT=$(OCC1)")

CmdDispatcherConfigure("cmd", "$(OCC1)")
dbLoadRecords("../../db/CmdPlugin.template","P=$(PREFIX)Det:cmd:,PORT=cmd")

AdaraPluginConfigure("Adara1", "$(OCC1)", 1, 2)
dbLoadRecords("../../db/AdaraPlugin.template","P=$(PREFIX)Det:adara1:,PORT=Adara1")
dbLoadRecords("../../db/BaseSocketPlugin.template","P=$(PREFIX)Det:adara1:,PORT=Adara1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX)Det:adara1:,PORT=Adara1")

ProxyPluginConfigure("proxy1", "$(OCC1)")
dbLoadRecords("../../db/BaseSocketPlugin.template","P=$(PREFIX)Det:proxy1:,PORT=proxy1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX)Det:proxy1:,PORT=proxy1")

#DspPluginConfigure("Dsp1", "$(OCC1)", "0x15FA76DF")
DspPluginConfigure("Dsp1", "cmd", "21.250.118.223", 1)
dbLoadRecords("../../db/DspPlugin.template","P=$(PREFIX)Det:dsp1:,PORT=Dsp1")
dbLoadRecords("../../db/BaseModulePlugin.template","P=$(PREFIX)Det:dsp1:,PORT=Dsp1")

DiscoverPluginConfigure("Disc", "$(OCC1)")
dbLoadRecords("../../db/DiscoverPlugin.template","P=$(PREFIX)Det:disc:,PORT=Disc")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX)Det:disc:,PORT=Disc")

RocPluginConfigure("roc1", "cmd", "20.39.216.73", "v52", 0)
dbLoadRecords("../../db/ROCHV.db","P=$(PREFIX)Det:HV1,G=$(PREFIX)Det:HVG,PORT=roc1")
dbLoadRecords("../../db/RocPlugin_v52.template","P=$(PREFIX)Det:roc1:,PORT=roc1")
dbLoadRecords("../../db/BaseModulePlugin.template","P=$(PREFIX)Det:roc1:,PORT=roc1")

DumpPluginConfigure("dump", "$(OCC1)", 0)
dbLoadRecords("../../db/DumpPlugin.template","P=$(PREFIX)Det:dump:,PORT=dump")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX)Det:dump:,PORT=dump")

StatPluginConfigure("stat", "$(OCC1)", 0)
dbLoadRecords("../../db/StatPlugin.template","P=$(PREFIX)Det:stat:,PORT=stat")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX)Det:stat:,PORT=stat")

RtdlPluginConfigure("rtdl", "$(OCC1)", 0)
dbLoadRecords("../../db/RtdlPlugin.template","P=$(PREFIX)Det:rtdl:,PORT=rtdl")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX)Det:rtdl:,PORT=rtdl")

FemPluginConfigure("fem1", "cmd", "0x603B0817", "10.0/5.0", 1)
dbLoadRecords("../../db/FemPlugin.template","P=$(PREFIX)Det:fem1:,PORT=fem1")
dbLoadRecords("../../db/BaseModulePlugin.template","P=$(PREFIX)Det:fem1:,PORT=fem1")

iocInit()

# Create request file and start periodic 'save'
makeAutosaveFileFromDbInfo("$(SAVE_DIR)/$(IOCNAME).req", "autosaveFields")
###makeAutosaveFileFromDbInfo("$(SAVE_DIR)/$(IOCNAME)_pass0.req", "autosaveFields_pass0")
create_monitor_set("$(IOCNAME).req", 30)
###create_monitor_set("$(IOCNAME)_pass0.req", 30)

# Display status
save_restoreShow(10)

# Fanout record for init in HVROC.db instead of PINI mechanism
epicsThreadSleep 1 
dbpf $(PREFIX)Det:HV1:InitProc.PROC 1
