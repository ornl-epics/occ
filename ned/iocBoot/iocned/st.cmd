#!../../bin/linux-x86_64/ned

< ../../iocBoot/iocned/envPaths

## Register all support components
dbLoadDatabase("../../dbd/ned.dbd",0,0)
ned_registerRecordDeviceDriver(pdbbase) 


## Load record instances
epicsEnvSet("PREFIX", "SNS:")
epicsEnvSet("PORT",   "OCC1")
#asynSetTraceIOMask("$(PORT)",0,255)
dbLoadRecords("../../db/ned.template","P=$(PREFIX),R=ocb1:,PORT=$(PORT),ADDR=0,TIMEOUT=1")
nedConfigure("$(PORT)", 0, 1000000)

AdaraPluginConfigure("Adara1", "$(PORT)")
dbLoadRecords("../../db/AdaraPlugin.template","P=$(PREFIX),R=ocb1:,PORT=Adara1,ADDR=0,TIMEOUT=1")
dbLoadRecords("../../db/BasePlugin.template","P=$(PREFIX),R=ocb1:,PORT=Adara1,ADDR=0,TIMEOUT=1")

dbLoadRecords("$(ASYN)/db/asynRecord.db","P=$(PREFIX):,R=ocb1,PORT=$(PORT),ADDR=0,OMAX=80,IMAX=80")

iocInit()
