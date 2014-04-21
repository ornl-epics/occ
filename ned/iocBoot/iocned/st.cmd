#!../../bin/linux-x86_64/ned

< ../../iocBoot/iocned/envPaths

## Register all support components
dbLoadDatabase("../../dbd/ned.dbd",0,0)
ned_registerRecordDeviceDriver(pdbbase) 


## Load record instances
epicsEnvSet("PREFIX", "SNS:")
epicsEnvSet("PORT",   "/dev/snsocb0")
#asynSetTraceIOMask("$(PORT)",0,255)
dbLoadRecords("../../db/ned.template","P=$(PREFIX),R=ocb1:,PORT=$(PORT),ADDR=0,TIMEOUT=1")
nedConfigure("$(PORT)", 0, 4000000)

#CmdPluginConfigure("Test", "$(PORT)")
AdaraPluginConfigure("Adara1", "$(PORT)")
dbLoadRecords("../../db/AdaraPlugin.template","P=$(PREFIX),R=ocb1:,PORT=Adara1,ADDR=0,TIMEOUT=1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=ocb1:,PORT=Adara1,ADDR=0,TIMEOUT=1")

#DspPluginConfigure("Dsp1", "$(PORT)", "0x15FACB2D")
DspPluginConfigure("Dsp1", "$(PORT)", "21.250.203.45")
dbLoadRecords("../../db/DspPlugin.template","P=$(PREFIX),R=dsp1:,PORT=Dsp1,ADDR=0,TIMEOUT=1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=dsp1:,PORT=Dsp1,ADDR=0,TIMEOUT=1")

DiscoverPluginConfigure("Disc", "$(PORT)")
dbLoadRecords("../../db/DiscoverPlugin.template","P=$(PREFIX),R=disc:,PORT=Disc,ADDR=0,TIMEOUT=1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=disc:,PORT=Disc,ADDR=0,TIMEOUT=1")

iocInit()
