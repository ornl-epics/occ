#!../../bin/linux-x86_64/ned

< ../../iocBoot/iocned/envPaths

## Register all support components
dbLoadDatabase("../../dbd/ned.dbd",0,0)
ned_registerRecordDeviceDriver(pdbbase) 


## Load record instances
epicsEnvSet("PREFIX", "SNS:")
epicsEnvSet("PORT",   "/dev/snsocb1")
#asynSetTraceIOMask("$(PORT)",0,255)
dbLoadRecords("../../db/ned.template","P=$(PREFIX),R=ocb1:,PORT=$(PORT),ADDR=0,TIMEOUT=1")
nedConfigure("$(PORT)", 0, 4000000)
#nedConfigure("$(PORT)", 0, 0)

#CmdPluginConfigure("Test", "$(PORT)")
AdaraPluginConfigure("Adara1", "$(PORT)")
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

FemPluginConfigure("fem1", "$(PORT)", "0x603B0817", "10/5.2", 1)
dbLoadRecords("../../db/FemPlugin.template","P=$(PREFIX),R=fem1:,PORT=fem1,ADDR=0,TIMEOUT=1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=fem1:,PORT=fem1,ADDR=0,TIMEOUT=1")

iocInit()
