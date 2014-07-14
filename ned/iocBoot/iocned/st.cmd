#!../../bin/linux-x86_64/ned

< ../../iocBoot/iocned/envPaths

epicsEnvSet IOCNAME bl99-iocned

## Register all support components
dbLoadDatabase("../../dbd/ned.dbd",0,0)
ned_registerRecordDeviceDriver(pdbbase) 

# Autosave
epicsEnvSet SAVE_DIR /home/controls/var/$(IOCNAME)
system("install -m 777 -d $(SAVE_DIR)")
save_restoreSet_Debug(0)
save_restoreSet_status_prefix("BL7:IOCNAME:")
set_requestfile_path("$(SAVE_DIR)")
set_savefile_path("$(SAVE_DIR)")
save_restoreSet_NumSeqFiles(3)
save_restoreSet_SeqPeriodInSeconds(600)
###set_pass0_restoreFile("$(IOCNAME).sav")
###set_pass0_restoreFile("$(IOCNAME)_pass0.sav")
set_pass1_restoreFile("$(IOCNAME).sav")

## Load record instances
epicsEnvSet("PREFIX", "SNS:")
epicsEnvSet("PORT",   "/dev/snsocb1")
#asynSetTraceIOMask("$(PORT)",0,255)
dbLoadRecords("../../db/ned.template","P=$(PREFIX),R=ocb1:,PORT=$(PORT),ADDR=0,TIMEOUT=1")
nedConfigure("$(PORT)", 0, 4000000)
#nedConfigure("$(PORT)", 0, 0)

CmdDispatcherConfigure("cmd", "$(PORT)")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=cmd:,PORT=cmd,ADDR=0,TIMEOUT=1")

#CmdPluginConfigure("Test", "$(PORT)")
AdaraPluginConfigure("Adara1", "$(PORT)", 1, 0, 4)
dbLoadRecords("../../db/BaseSocketPlugin.template","P=$(PREFIX),R=adara1:,PORT=Adara1,ADDR=0,TIMEOUT=1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=adara1:,PORT=Adara1,ADDR=0,TIMEOUT=1")

ProxyPluginConfigure("proxy1", "$(PORT)")
dbLoadRecords("../../db/BaseSocketPlugin.template","P=$(PREFIX),R=proxy1:,PORT=proxy1,ADDR=0,TIMEOUT=1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=proxy1:,PORT=proxy1,ADDR=0,TIMEOUT=1")

#DspPluginConfigure("Dsp1", "$(PORT)", "0x15FA76DF")
DspPluginConfigure("Dsp1", "$(PORT)", "21.250.118.223", 1)
dbLoadRecords("../../db/DspPlugin.template","P=$(PREFIX),R=dsp1:,PORT=Dsp1,ADDR=0,TIMEOUT=1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=dsp1:,PORT=Dsp1,ADDR=0,TIMEOUT=1")

DiscoverPluginConfigure("Disc", "$(PORT)")
dbLoadRecords("../../db/DiscoverPlugin.template","P=$(PREFIX),R=disc:,PORT=Disc,ADDR=0,TIMEOUT=1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=disc:,PORT=Disc,ADDR=0,TIMEOUT=1")

RocPluginConfigure("roc1", "$(PORT)", "0x1427D924", "5.2/5.2", 1)
dbLoadRecords("../../db/RocPlugin.template","P=$(PREFIX),R=roc1:,PORT=roc1,ADDR=0,TIMEOUT=1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=roc1:,PORT=roc1,ADDR=0,TIMEOUT=1")

DumpPluginConfigure("dump", "$(PORT)", 0)
dbLoadRecords("../../db/DumpPlugin.template","P=$(PREFIX),R=dump:,PORT=dump,ADDR=0,TIMEOUT=1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=dump:,PORT=dump,ADDR=0,TIMEOUT=1")

StatPluginConfigure("stat", "$(PORT)", 0)
dbLoadRecords("../../db/StatPlugin.template","P=$(PREFIX),R=stat:,PORT=stat,ADDR=0,TIMEOUT=1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=stat:,PORT=stat,ADDR=0,TIMEOUT=1")

RtdlPluginConfigure("rtdl", "$(PORT)", 0)
dbLoadRecords("../../db/RtdlPlugin.template","P=$(PREFIX),R=rtdl:,PORT=rtdl,ADDR=0,TIMEOUT=1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=rtdl:,PORT=rtdl,ADDR=0,TIMEOUT=1")

FemPluginConfigure("fem1", "$(PORT)", "0x603B0817", "10.0/5.0", 1)
dbLoadRecords("../../db/FemPlugin.template","P=$(PREFIX),R=fem1:,PORT=fem1,ADDR=0,TIMEOUT=1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=fem1:,PORT=fem1,ADDR=0,TIMEOUT=1")

iocInit()

# Create request file and start periodic 'save'
makeAutosaveFileFromDbInfo("$(SAVE_DIR)/$(IOCNAME).req", "autosaveFields")
###makeAutosaveFileFromDbInfo("$(SAVE_DIR)/$(IOCNAME)_pass0.req", "autosaveFields_pass0")
create_monitor_set("$(IOCNAME).req", 30)
###create_monitor_set("$(IOCNAME)_pass0.req", 30)

# Display status
save_restoreShow(10)
